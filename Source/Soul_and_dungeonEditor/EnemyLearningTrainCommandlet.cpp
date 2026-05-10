#include "EnemyLearningTrainCommandlet.h"

#include "AIController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EnemyLearningInteractor.h"
#include "EnemyLearningTrainingEnvironment.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsPPOTrainer.h"
#include "LMStudioPlayerDriver.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MyAIController.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
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

static FString ResolveProjectOutputPath(const FString& OutputPath)
{
	return FPaths::IsRelative(OutputPath) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OutputPath) : OutputPath;
}

static bool SaveNetworkAsset(ULearningAgentsNeuralNetwork* Network, const FString& AssetPath)
{
	if (!Network || AssetPath.IsEmpty())
	{
		return false;
	}

	FString PackageName = AssetPath;
	FString AssetName;
	if (!PackageName.Split(TEXT("."), &PackageName, &AssetName))
	{
		AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return false;
	}

	Network->Rename(*AssetName, Package, REN_DontCreateRedirectors | REN_NonTransactional);
	Network->SetFlags(RF_Public | RF_Standalone);
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Network);

	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, Network, *Filename, SaveArgs);
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

static void RegisterLearningPythonPathsForCommandlet()
{
	const FString ExperimentalPluginsPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Experimental")));

	TArray<FString> PluginPythonPaths;
	PluginPythonPaths.Add(FPaths::Combine(ExperimentalPluginsPath, TEXT("LearningAgents/Content/Python")));
	PluginPythonPaths.Add(FPaths::Combine(ExperimentalPluginsPath, TEXT("NNERuntimeBasicCpu/Content/Python")));

	for (FString& PluginPythonPath : PluginPythonPaths)
	{
		PluginPythonPath = FPaths::ConvertRelativePathToFull(PluginPythonPath);
	}

	const FString ExistingPythonPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PYTHONPATH"));
	TArray<FString> PythonPathEntries = PluginPythonPaths;
	if (!ExistingPythonPath.IsEmpty())
	{
		PythonPathEntries.Add(ExistingPythonPath);
	}
	FPlatformMisc::SetEnvironmentVar(TEXT("PYTHONPATH"), *FString::Join(PythonPathEntries, TEXT(":")));

	TArray<FString> SitePackagePaths;
	SitePackagePaths.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("PipInstall") / TEXT("Lib") / TEXT("site-packages")));
	SitePackagePaths.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("PipInstall") / TEXT("lib") / TEXT("python3.11") / TEXT("site-packages")));

	for (const FString& SitePackagePath : SitePackagePaths)
	{
		IFileManager::Get().MakeDirectory(*SitePackagePath, true);
		FFileHelper::SaveStringArrayToFile(
			PluginPythonPaths,
			*(SitePackagePath / TEXT("learning_agents.pth")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
}

UEnemyLearningTrainCommandlet::UEnemyLearningTrainCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UEnemyLearningTrainCommandlet::Main(const FString& Params)
{
	const FString MapPath = GetParamValue(Params, TEXT("Map="), TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"));
	const FString OutputPolicyPath = GetParamValue(Params, TEXT("OutputPolicy="), TEXT("/Game/AI/Learning/NN_EnemySteering"));
	const int32 Steps = FMath::Max(1, GetIntParamValue(Params, TEXT("Steps="), 500));
	const int32 MaxEpisodeSteps = FMath::Max(1, GetIntParamValue(Params, TEXT("MaxEpisodeSteps="), 1200));
	const int32 TrainingIterations = FMath::Max(1, GetIntParamValue(Params, TEXT("Iterations="), FMath::Max(1, Steps / 512)));
	const bool bUseLMStudioPlayer = GetBoolParamValue(Params, TEXT("UseLMStudioPlayer="), false);
	const FString LMStudioEndpoint = GetParamValue(Params, TEXT("LMStudioEndpoint="), TEXT("http://localhost:1234/v1/chat/completions"));
	const FString LMStudioModel = GetParamValue(Params, TEXT("LMStudioModel="), TEXT(""));
	const float LMStudioTimeout = FMath::Max(0.5f, GetFloatParamValue(Params, TEXT("LMStudioTimeout="), 10.0f));
	const FString SummaryPath = ResolveProjectOutputPath(GetParamValue(Params, TEXT("SummaryPath="), FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EnemyLearning"), TEXT("TrainingSummary.json"))));
	const int32 ParallelCount = FMath::Max(1, GetIntParamValue(Params, TEXT("ParallelAgents="), 1));
	const int32 Seed = GetIntParamValue(Params, TEXT("Seed="), 1234);
	const float FixedDeltaSeconds = 1.0f / 60.0f;

	RegisterLearningPythonPathsForCommandlet();

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: loading map %s"), *MapPath);
	if (!UEditorLoadingAndSavingUtils::LoadMap(MapPath))
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: failed to load map %s"), *MapPath);
		return 1;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: no editor world after map load"));
		return 1;
	}

	TArray<AMyAIController*> ParallelControllers;
	TArray<TUniquePtr<FLMStudioPlayerDriver>> ParallelDrivers;

	FLMStudioPlayerDriverSettings PlayerDriverSettings;
	PlayerDriverSettings.bUseLMStudio = bUseLMStudioPlayer;
	PlayerDriverSettings.Endpoint = LMStudioEndpoint;
	PlayerDriverSettings.Model = LMStudioModel;
	PlayerDriverSettings.TimeoutSeconds = LMStudioTimeout;
	PlayerDriverSettings.Seed = Seed;

	for (int32 i = 0; i < ParallelCount; ++i)
	{
		const FVector Offset(i * 2500.0f, 0.0f, 0.0f);
		const FVector PlayerLoc = FVector(0.0f, 0.0f, 100.0f) + Offset;
		const FVector EnemyLoc = FVector(700.0f, 0.0f, 100.0f) + Offset;

		FActorSpawnParameters PlayerSpawnParams;
		PlayerSpawnParams.Name = FName(*FString::Printf(TEXT("TrainingPlayer_%d"), i));
		APawn* CurrentPlayerPawn = World->SpawnActor<APawn>(APawn::StaticClass(), PlayerLoc, FRotator::ZeroRotator, PlayerSpawnParams);

		ACharacter* EnemyCharacter = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), EnemyLoc, FRotator::ZeroRotator);
		AMyAIController* CurrentEnemyController = World->SpawnActor<AMyAIController>(AMyAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		
		if (CurrentEnemyController && EnemyCharacter && CurrentPlayerPawn)
		{
			CurrentEnemyController->Possess(EnemyCharacter);
			CurrentEnemyController->SetLearningTrainingPlayer(CurrentPlayerPawn);
			ParallelControllers.Add(CurrentEnemyController);

			TUniquePtr<FLMStudioPlayerDriver> Driver = MakeUnique<FLMStudioPlayerDriver>();
			Driver->Configure(PlayerDriverSettings);
			Driver->Reset(
				CurrentPlayerPawn,
				CurrentEnemyController,
				bUseLMStudioPlayer ? ELMStudioPlayerBehavior::LMStudioEvasive : ELMStudioPlayerBehavior::DeterministicEvasive,
				CurrentPlayerPawn->GetActorLocation());
			ParallelDrivers.Add(MoveTemp(Driver));
		}
	}

	if (ParallelControllers.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: failed to spawn any parallel training instances"));
		return 1;
	}

	if (IConsoleVariable* ModeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sd.EnemyNavigation.Mode")))
	{
		ModeCVar->Set(2, ECVF_SetByCode);
	}

	AActor* ManagerOwner = World->SpawnActor<AActor>();
	ULearningAgentsManager* Manager = NewObject<ULearningAgentsManager>(ManagerOwner, TEXT("EnemyLearningAgentsManager"));
	Manager->SetMaxAgentNum(ParallelCount);
	Manager->RegisterComponent();

	ULearningAgentsManager* ManagerRef = Manager;
	ULearningAgentsInteractor* Interactor = ULearningAgentsInteractor::MakeInteractor(ManagerRef, UEnemyLearningInteractor::StaticClass(), TEXT("EnemyLearningInteractor"));
	ULearningAgentsTrainingEnvironment* TrainingEnvironmentBase = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(ManagerRef, UEnemyLearningTrainingEnvironment::StaticClass(), TEXT("EnemyLearningTrainingEnvironment"));
	UEnemyLearningTrainingEnvironment* TrainingEnvironment = CastChecked<UEnemyLearningTrainingEnvironment>(TrainingEnvironmentBase);
	TrainingEnvironment->Configure(MaxEpisodeSteps, 150.0f);

	for (int32 i = 0; i < ParallelControllers.Num(); ++i)
	{
		const int32 AgentId = Manager->AddAgent(ParallelControllers[i]);
		UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: Registered Parallel Agent %d with ID %d"), i, AgentId);
	}

	ULearningAgentsPolicy* Policy = ULearningAgentsPolicy::MakePolicy(ManagerRef, Interactor, ULearningAgentsPolicy::StaticClass(), TEXT("EnemySteeringPolicy"));
	ULearningAgentsPolicy* PolicyRef = Policy;
	ULearningAgentsCritic* Critic = ULearningAgentsCritic::MakeCritic(ManagerRef, Interactor, PolicyRef, ULearningAgentsCritic::StaticClass(), TEXT("EnemySteeringCritic"));

	FLearningAgentsTrainerProcessSettings ProcessSettings;
	ProcessSettings.TaskName = TEXT("EnemyLearningTrain");
	FLearningAgentsSharedMemoryCommunicatorSettings SharedMemorySettings;
	SharedMemorySettings.Timeout = 30.0f;
	const FLearningAgentsCommunicator Communicator = ULearningAgentsCommunicatorLibrary::MakeSharedMemoryTrainingProcess(ProcessSettings, SharedMemorySettings);
	if (!Communicator.Trainer.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: failed to create Learning Agents training communicator"));
		return 1;
	}

	ULearningAgentsTrainingEnvironment* TrainingEnvironmentRef = TrainingEnvironment;
	ULearningAgentsCritic* CriticRef = Critic;
	ULearningAgentsPPOTrainer* Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(
		ManagerRef,
		Interactor,
		TrainingEnvironmentRef,
		PolicyRef,
		CriticRef,
		Communicator,
		ULearningAgentsPPOTrainer::StaticClass(),
		TEXT("EnemyPPOTrainer"));

	FLearningAgentsPPOTrainingSettings TrainingSettings;
	TrainingSettings.NumberOfIterations = TrainingIterations;
	TrainingSettings.Device = ELearningAgentsTrainingDevice::CPU;
	TrainingSettings.bSaveSnapshots = true;
	TrainingSettings.IterationsPerSnapshot = FMath::Max(1, TrainingIterations);

	FLearningAgentsTrainingGameSettings GameSettings;
	GameSettings.bUseFixedTimeStep = true;
	GameSettings.FixedTimeStepFrequency = 60.0f;

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: running %d steps (%d PPO iterations) with %d parallel agents"), Steps, TrainingIterations, ParallelCount);
	for (int32 StepIndex = 0; StepIndex < Steps; ++StepIndex)
	{
		for (TUniquePtr<FLMStudioPlayerDriver>& Driver : ParallelDrivers)
		{
			Driver->Tick(World, FixedDeltaSeconds, World->GetTimeSeconds());
		}
		
		Trainer->RunTraining(TrainingSettings, GameSettings, StepIndex == 0, true);
		TickWorld(World, FixedDeltaSeconds);

		if (Trainer->HasTrainingFailed())
		{
			UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: PPO trainer reported communication failure at step %d"), StepIndex);
			Trainer->EndTraining();
			return 1;
		}
	}

	Trainer->EndTraining();

	const bool bSavedPolicy = SaveNetworkAsset(Policy->GetPolicyNetworkAsset(), OutputPolicyPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SummaryPath), true);

	FLMStudioPlayerDriverMetrics TotalMetrics;
	for (const TUniquePtr<FLMStudioPlayerDriver>& Driver : ParallelDrivers)
	{
		const FLMStudioPlayerDriverMetrics& m = Driver->GetMetrics();
		TotalMetrics.DecisionRequests += m.DecisionRequests;
		TotalMetrics.DecisionResponses += m.DecisionResponses;
		TotalMetrics.DecisionTimeouts += m.DecisionTimeouts;
		TotalMetrics.FallbackDecisions += m.FallbackDecisions;
		TotalMetrics.InvalidResponses += m.InvalidResponses;
	}

	const float AverageReward = TrainingEnvironment->GetRewardSampleCount() > 0
		? TrainingEnvironment->GetRewardSum() / static_cast<float>(TrainingEnvironment->GetRewardSampleCount())
		: 0.0f;

	const FString Summary = FString::Printf(
		TEXT("{\n  \"steps\": %d,\n  \"ppo_iterations\": %d,\n  \"parallel_agents\": %d,\n  \"average_reward\": %.6f,\n  \"completed_episodes\": %d,\n  \"truncated_episodes\": %d,\n  \"stuck_episodes\": %d,\n  \"policy_path\": \"%s\",\n  \"policy_saved\": %s,\n  \"lmstudio_enabled\": %s,\n  \"lm_decision_requests\": %d,\n  \"lm_decision_responses\": %d,\n  \"lm_decision_timeouts\": %d,\n  \"lm_fallback_decisions\": %d,\n  \"lm_invalid_responses\": %d\n}\n"),
		Steps,
		TrainingIterations,
		ParallelCount,
		AverageReward,
		TrainingEnvironment->GetCompletedEpisodeCount(),
		TrainingEnvironment->GetTruncatedEpisodeCount(),
		TrainingEnvironment->GetStuckEpisodeCount(),
		*OutputPolicyPath,
		bSavedPolicy ? TEXT("true") : TEXT("false"),
		bUseLMStudioPlayer ? TEXT("true") : TEXT("false"),
		TotalMetrics.DecisionRequests,
		TotalMetrics.DecisionResponses,
		TotalMetrics.DecisionTimeouts,
		TotalMetrics.FallbackDecisions,
		TotalMetrics.InvalidResponses);
	FFileHelper::SaveStringToFile(Summary, *SummaryPath);

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: wrote summary %s"), *SummaryPath);
	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: policy save %s at %s"), bSavedPolicy ? TEXT("succeeded") : TEXT("failed"), *OutputPolicyPath);

	return bSavedPolicy ? 0 : 1;
}
