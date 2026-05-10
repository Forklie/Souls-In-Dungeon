#include "EnemyLearningEvaluateCommandlet.h"

#include "Editor.h"
#include "EnemyLearningInteractor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Json.h"
#include "Kismet/GameplayStatics.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LMStudioPlayerDriver.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MyAIController.h"

namespace
{
struct FEnemyEvalMetrics
{
	int32 Episodes = 0;
	int32 Successes = 0;
	int32 StuckEpisodes = 0;
	int32 AStarFallbacks = 0;
	float TotalChaseTime = 0.0f;
	float TotalDistance = 0.0f;
	int32 DistanceSamples = 0;
	FLMStudioPlayerDriverMetrics LMStudioMetrics;
};

static FString GetParamValue(const FString& Params, const TCHAR* Name, const FString& DefaultValue)
{
	FString Value;
	return FParse::Value(*Params, Name, Value) ? Value : DefaultValue;
}

static int32 GetIntParamValue(const FString& Params, const TCHAR* Name, int32 DefaultValue)
{
	int32 Value = DefaultValue;
	FParse::Value(*Params, Name, Value);
	return Value;
}

static float GetFloatParamValue(const FString& Params, const TCHAR* Name, float DefaultValue)
{
	float Value = DefaultValue;
	FParse::Value(*Params, Name, Value);
	return Value;
}

static bool GetBoolParamValue(const FString& Params, const TCHAR* Name, bool DefaultValue)
{
	FString ValueString;
	if (FParse::Value(*Params, Name, ValueString))
	{
		bool ParsedValue = DefaultValue;
		LexTryParseString(ParsedValue, *ValueString);
		return ParsedValue;
	}

	FString SwitchName(Name);
	SwitchName.RemoveFromEnd(TEXT("="));
	bool Value = DefaultValue;
	FParse::Bool(*Params, *SwitchName, Value);
	return Value;
}

static void TickWorld(UWorld* World, float DeltaSeconds)
{
	if (!World)
	{
		return;
	}

	World->Tick(LEVELTICK_All, DeltaSeconds);
	if (GEngine)
	{
		GEngine->Tick(DeltaSeconds, false);
	}
}

static APawn* FindOrSpawnPlayer(UWorld* World)
{
	if (APawn* ExistingPlayer = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		return ExistingPlayer;
	}

	return World ? World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator) : nullptr;
}

static AMyAIController* FindOrSpawnEnemy(UWorld* World)
{
	for (TActorIterator<AMyAIController> It(World); It; ++It)
	{
		if (It->GetPawn())
		{
			return *It;
		}
	}

	if (!World)
	{
		return nullptr;
	}

	ACharacter* EnemyCharacter = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(700.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
	AMyAIController* EnemyController = World->SpawnActor<AMyAIController>(AMyAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (EnemyController && EnemyCharacter)
	{
		EnemyController->Possess(EnemyCharacter);
	}
	return EnemyController;
}

static void SetActorTransform(AActor* Actor, const FVector& Location, const FRotator& Rotation)
{
	if (Actor)
	{
		Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

static ELMStudioPlayerBehavior ParseBehavior(const FString& BehaviorName)
{
	if (BehaviorName.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
	{
		return ELMStudioPlayerBehavior::Static;
	}
	if (BehaviorName.Equals(TEXT("Moving"), ESearchCase::IgnoreCase))
	{
		return ELMStudioPlayerBehavior::Moving;
	}
	if (BehaviorName.Equals(TEXT("LMStudio"), ESearchCase::IgnoreCase) || BehaviorName.Equals(TEXT("LMStudioEvasive"), ESearchCase::IgnoreCase))
	{
		return ELMStudioPlayerBehavior::LMStudioEvasive;
	}
	return ELMStudioPlayerBehavior::DeterministicEvasive;
}

static FString JsonEscape(const FString& Value)
{
	FString Output = Value.Replace(TEXT("\\"), TEXT("\\\\"));
	Output = Output.Replace(TEXT("\""), TEXT("\\\""));
	return Output;
}

static FString ResolveProjectOutputPath(const FString& OutputPath)
{
	FString AbsPath = FPaths::IsRelative(OutputPath) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OutputPath) : OutputPath;
	FPaths::CollapseRelativeDirectories(AbsPath);
	return AbsPath;
}

static bool WriteEvalJson(const FString& OutputPath, const FString& PolicyPath, const FString& BehaviorName, bool bUsedPolicy, const FEnemyEvalMetrics& Metrics)
{
	const float AverageChaseTime = Metrics.Episodes > 0 ? Metrics.TotalChaseTime / static_cast<float>(Metrics.Episodes) : 0.0f;
	const float SuccessRate = Metrics.Episodes > 0 ? static_cast<float>(Metrics.Successes) / static_cast<float>(Metrics.Episodes) : 0.0f;
	const float AverageDistance = Metrics.DistanceSamples > 0 ? Metrics.TotalDistance / static_cast<float>(Metrics.DistanceSamples) : 0.0f;
	const FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"policy_path\": \"%s\",\n")
		TEXT("  \"used_policy\": %s,\n")
		TEXT("  \"behavior\": \"%s\",\n")
		TEXT("  \"episodes\": %d,\n")
		TEXT("  \"successes\": %d,\n")
		TEXT("  \"success_rate\": %.6f,\n")
		TEXT("  \"average_chase_time\": %.6f,\n")
		TEXT("  \"stuck_episodes\": %d,\n")
		TEXT("  \"astar_fallbacks\": %d,\n")
		TEXT("  \"average_distance\": %.6f,\n")
		TEXT("  \"lm_decision_requests\": %d,\n")
		TEXT("  \"lm_decision_responses\": %d,\n")
		TEXT("  \"lm_decision_timeouts\": %d,\n")
		TEXT("  \"lm_fallback_decisions\": %d,\n")
		TEXT("  \"lm_invalid_responses\": %d\n")
		TEXT("}\n"),
		*JsonEscape(PolicyPath),
		bUsedPolicy ? TEXT("true") : TEXT("false"),
		*JsonEscape(BehaviorName),
		Metrics.Episodes,
		Metrics.Successes,
		SuccessRate,
		AverageChaseTime,
		Metrics.StuckEpisodes,
		Metrics.AStarFallbacks,
		AverageDistance,
		Metrics.LMStudioMetrics.DecisionRequests,
		Metrics.LMStudioMetrics.DecisionResponses,
		Metrics.LMStudioMetrics.DecisionTimeouts,
		Metrics.LMStudioMetrics.FallbackDecisions,
		Metrics.LMStudioMetrics.InvalidResponses);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	return FFileHelper::SaveStringToFile(Json, *OutputPath);
}
}

UEnemyLearningEvaluateCommandlet::UEnemyLearningEvaluateCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UEnemyLearningEvaluateCommandlet::Main(const FString& Params)
{
	const FString MapPath = GetParamValue(Params, TEXT("Map="), TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"));
	const FString PolicyPath = GetParamValue(Params, TEXT("Policy="), TEXT(""));
	const FString OutputPath = ResolveProjectOutputPath(GetParamValue(Params, TEXT("Output="), FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EnemyLearning"), TEXT("EvalSummary.json"))));
	const FString BehaviorName = GetParamValue(Params, TEXT("Behavior="), TEXT("DeterministicEvasive"));
	const int32 Episodes = FMath::Max(1, GetIntParamValue(Params, TEXT("Episodes="), 5));
	const int32 EpisodeSteps = FMath::Max(1, GetIntParamValue(Params, TEXT("EpisodeSteps="), 600));
	const int32 Seed = GetIntParamValue(Params, TEXT("Seed="), 1234);
	const bool bUseLMStudio = GetBoolParamValue(Params, TEXT("UseLMStudioPlayer="), false);
	const FString LMEndpoint = GetParamValue(Params, TEXT("LMStudioEndpoint="), TEXT("http://localhost:1234/v1/chat/completions"));
	const FString LMModel = GetParamValue(Params, TEXT("LMStudioModel="), TEXT(""));
	const float LMStudioTimeout = FMath::Max(0.5f, GetFloatParamValue(Params, TEXT("LMStudioTimeout="), 10.0f));
	const float FixedDeltaSeconds = 1.0f / 60.0f;
	const float AttackRange = 150.0f;

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningEvaluate: loading map %s"), *MapPath);
	if (!UEditorLoadingAndSavingUtils::LoadMap(MapPath))
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningEvaluate: failed to load map %s"), *MapPath);
		return 1;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	APawn* PlayerPawn = FindOrSpawnPlayer(World);
	AMyAIController* EnemyController = FindOrSpawnEnemy(World);
	APawn* EnemyPawn = EnemyController ? EnemyController->GetPawn() : nullptr;
	if (!World || !PlayerPawn || !EnemyController || !EnemyPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningEvaluate: missing world, player, or enemy"));
		return 1;
	}

	EnemyController->SetLearningTrainingPlayer(PlayerPawn);
	ULearningAgentsNeuralNetwork* PolicyAsset = PolicyPath.IsEmpty() ? nullptr : LoadObject<ULearningAgentsNeuralNetwork>(nullptr, *PolicyPath);
	const bool bUsePolicy = PolicyAsset != nullptr;
	if (IConsoleVariable* ModeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sd.EnemyNavigation.Mode")))
	{
		ModeCVar->Set(bUsePolicy ? 2 : 1, ECVF_SetByCode);
	}

	AActor* ManagerOwner = World->SpawnActor<AActor>();
	ULearningAgentsManager* Manager = NewObject<ULearningAgentsManager>(ManagerOwner);
	Manager->SetMaxAgentNum(1);
	Manager->RegisterComponent();
	Manager->RemoveAllAgents();
	
	ULearningAgentsManager* ManagerRef = Manager;
	ULearningAgentsInteractor* Interactor = ULearningAgentsInteractor::MakeInteractor(ManagerRef, UEnemyLearningInteractor::StaticClass(), TEXT("EnemyLearningEvalInteractor"));
	
	const int32 AgentId = Manager->AddAgent(EnemyController);
	UE_LOG(LogTemp, Display, TEXT("EnemyLearningEvaluate: Registered Agent (Ptr: %p) → Learning ID %d"), EnemyController, AgentId);
	
	if (AgentId < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningEvaluate: Failed to register agent!"));
		return 1;
	}

	ULearningAgentsPolicy* Policy = nullptr;
	if (bUsePolicy)
	{
		Policy = ULearningAgentsPolicy::MakePolicy(
			ManagerRef,
			Interactor,
			ULearningAgentsPolicy::StaticClass(),
			TEXT("EnemyEvalPolicy"),
			nullptr,
			PolicyAsset,
			nullptr,
			true,
			false,
			true);
	}

	FLMStudioPlayerDriverSettings DriverSettings;
	DriverSettings.bUseLMStudio = bUseLMStudio;
	DriverSettings.Endpoint = LMEndpoint;
	DriverSettings.Model = LMModel;
	DriverSettings.TimeoutSeconds = LMStudioTimeout;
	DriverSettings.Seed = Seed;
	FLMStudioPlayerDriver PlayerDriver;
	PlayerDriver.Configure(DriverSettings);

	FEnemyEvalMetrics Metrics;
	const FVector EnemyStart(700.0f, 0.0f, 100.0f);
	const FVector PlayerStart(0.0f, 0.0f, 100.0f);
	const FRotator StartRotation = FRotator::ZeroRotator;
	const ELMStudioPlayerBehavior Behavior = ParseBehavior(BehaviorName);

	for (int32 EpisodeIndex = 0; EpisodeIndex < Episodes; ++EpisodeIndex)
	{
		const FVector EnemyEpisodeStart = EnemyStart + FVector(0.0f, EpisodeIndex * 60.0f, 0.0f);
		const FVector PlayerEpisodeStart = PlayerStart + FVector(0.0f, -EpisodeIndex * 45.0f, 0.0f);
		SetActorTransform(EnemyPawn, EnemyEpisodeStart, StartRotation);
		SetActorTransform(PlayerPawn, PlayerEpisodeStart, StartRotation);
		EnemyController->SetLearningTrainingPlayer(PlayerPawn);
		Manager->ResetAgents({0});
		PlayerDriver.Reset(PlayerPawn, EnemyController, Behavior, PlayerEpisodeStart);

		const int32 StartFallbacks = EnemyController->GetAStarFallbackCount();
		bool bSucceeded = false;
		bool bStuck = false;
		int32 StepIndex = 0;
		for (; StepIndex < EpisodeSteps; ++StepIndex)
		{
			const float CurrentTime = World->GetTimeSeconds();
			PlayerDriver.Tick(World, FixedDeltaSeconds, CurrentTime);
			if (Policy)
			{
				Policy->RunInference(0.0f);
			}
			TickWorld(World, FixedDeltaSeconds);

			FEnemyLearningObservation Observation;
			EnemyController->GetEnemyLearningObservation(Observation);
			const float DirectDistanceToPlayer = FVector::Dist2D(EnemyPawn->GetActorLocation(), PlayerPawn->GetActorLocation());
			const float DistanceToPlayer = Observation.DistanceToPlayer > 0.0f ? Observation.DistanceToPlayer : DirectDistanceToPlayer;
			Metrics.TotalDistance += DirectDistanceToPlayer;
			Metrics.DistanceSamples++;

			if (DistanceToPlayer <= AttackRange)
			{
				bSucceeded = true;
				break;
			}
			if (Observation.StuckSeconds >= 4.0f)
			{
				bStuck = true;
				break;
			}
		}

		Metrics.Episodes++;
		Metrics.TotalChaseTime += static_cast<float>(StepIndex + 1) * FixedDeltaSeconds;
		Metrics.AStarFallbacks += FMath::Max(0, EnemyController->GetAStarFallbackCount() - StartFallbacks);
		if (bSucceeded)
		{
			Metrics.Successes++;
		}
		if (bStuck)
		{
			Metrics.StuckEpisodes++;
		}
	}

	Metrics.LMStudioMetrics = PlayerDriver.GetMetrics();
	const bool bWrote = WriteEvalJson(OutputPath, PolicyPath, BehaviorName, bUsePolicy, Metrics);
	UE_LOG(LogTemp, Display, TEXT("EnemyLearningEvaluate: wrote %s"), *OutputPath);
	return bWrote ? 0 : 1;
}
