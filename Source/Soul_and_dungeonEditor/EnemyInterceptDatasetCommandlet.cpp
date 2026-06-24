#include "EnemyInterceptDatasetCommandlet.h"

#include "AIController.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "LMStudioAutoPlayerDriver.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "MyAIController.h"
#include "SecondarySearchSolver.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TrainingAutoPlayerComponent.h"
#include "EnemyInterceptCsvWriter.h"

namespace
{
enum class EEnemyInterceptTrainingScenarioType : uint8
{
	OpenField,
	SideCross,
	Retreat,
	DiagonalRetreat,
	Circle,
	ZigZagRetreat,
	Mixed
};

struct FEnemyInterceptDatasetArgs
{
	FString MapName = TEXT("/Game/ThirdPerson/Lvl_ThirdPerson");
	int32 Episodes = 500;
	FString OutputPath = TEXT("Saved/EnemyLearning/InterceptDataset/intercept_samples.csv");
	bool bUseLMStudioPlayer = false;
	FString LMStudioUrl = TEXT("http://localhost:1234/v1");
	FString LMStudioModel = TEXT("google/gemma-3-270m");
	float LMStudioTimeoutSeconds = 1.0f;
	int32 Seed = 1234;
	float EvaluationWindowSeconds = 6.0f;
	float FixedDeltaSeconds = 1.0f / 30.0f;
	float MinStartDistance = 650.0f;
	float MaxStartDistance = 1400.0f;
	bool bForcePlayerMoving = false;
	float PlayerSpeedScaleMin = 0.75f;
	float PlayerSpeedScaleMax = 1.25f;
	EEnemyInterceptTrainingScenarioType ScenarioType = EEnemyInterceptTrainingScenarioType::OpenField;
	bool bUseProgressScore = false;
	bool bEvaluateLearnedPolicy = false;
	FString LearnedPolicyPath = TEXT("Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json");
	FString PolicyEvalReportPath = TEXT("Saved/EnemyLearning/Reports/enemy_intercept_policy_eval_report.json");
	bool bClearOutput = false;
	int32 EnemyInterceptRuntimeMode = -1;
	bool bVisualDebug = false;
	bool bCompareWithCurrent = false;
};

struct FEnemyInterceptTrainingScenario
{
	int32 ScenarioId = 0;
	int32 Seed = 0;
	FTransform EnemyStartTransform = FTransform::Identity;
	FTransform PlayerStartTransform = FTransform::Identity;
	TArray<FTrainingPlayerActionDecision> PlayerActionPlan;
	FString PlayerMovementSource = TEXT("ScriptedRandom");
	FString PlayerActionSummary;
	EEnemyInterceptTrainingScenarioType ScenarioType = EEnemyInterceptTrainingScenarioType::OpenField;
};

struct FEnemyInterceptModeEvaluationResult
{
	EEnemyInterceptMode Mode = EEnemyInterceptMode::CurrentLocation;
	bool bReachedAttackRange = false;
	float TimeToAttackRange = 0.0f;
	float StartDistanceToPlayer = 0.0f;
	float FinalDistanceToPlayer = 0.0f;
	float DistanceReduction = 0.0f;
	int32 InvalidTargetCount = 0;
	int32 RepathCount = 0;
	int32 FallbackCount = 0;
	int32 PathFailureCount = 0;
	float Score = 0.0f;
};

struct FEnemyInterceptPolicyEvalAccumulator
{
	int32 TotalScenarios = 0;
	int32 LearnedCorrect = 0;
	int32 DeterministicCorrect = 0;
	int32 Predict035Correct = 0;
	TArray<int32> OracleModeCounts;
	TArray<int32> LearnedModeCounts;
	TArray<int32> DeterministicModeCounts;
	TArray<int32> Predict035ModeCounts;
	TArray<int32> LearnedConfusionMatrix;
	TArray<int32> DeterministicConfusionMatrix;
	double OracleBestScoreSum = 0.0;
	double LearnedChosenScoreSum = 0.0;
	double DeterministicChosenScoreSum = 0.0;
	double Predict035ScoreSum = 0.0;
	double LearnedScoreGapFromOracleSum = 0.0;
	double DeterministicScoreGapFromOracleSum = 0.0;
	double Predict035ScoreGapFromOracleSum = 0.0;
	int32 LearnedInvalidTargetCount = 0;
	int32 LearnedPathFailureCount = 0;
	int32 LearnedFallbackCount = 0;

	FEnemyInterceptPolicyEvalAccumulator()
	{
		OracleModeCounts.Init(0, 5);
		LearnedModeCounts.Init(0, 5);
		DeterministicModeCounts.Init(0, 5);
		Predict035ModeCounts.Init(0, 5);
		LearnedConfusionMatrix.Init(0, 25);
		DeterministicConfusionMatrix.Init(0, 25);
	}
};

static bool ParseBoolArg(const FString& Params, const TCHAR* Name, bool DefaultValue)
{
	FString Value;
	const FString ValueMatch = FString::Printf(TEXT("%s="), Name);
	if (!FParse::Value(*Params, *ValueMatch, Value))
	{
		return FParse::Param(*Params, Name) ? true : DefaultValue;
	}

	Value = Value.TrimStartAndEnd().ToLower();
	return Value == TEXT("1") || Value == TEXT("true") || Value == TEXT("yes") || Value == TEXT("on");
}

static const TCHAR* ScenarioTypeToString(EEnemyInterceptTrainingScenarioType ScenarioType)
{
	switch (ScenarioType)
	{
	case EEnemyInterceptTrainingScenarioType::OpenField:
		return TEXT("OpenField");
	case EEnemyInterceptTrainingScenarioType::SideCross:
		return TEXT("SideCross");
	case EEnemyInterceptTrainingScenarioType::Retreat:
		return TEXT("Retreat");
	case EEnemyInterceptTrainingScenarioType::DiagonalRetreat:
		return TEXT("DiagonalRetreat");
	case EEnemyInterceptTrainingScenarioType::Circle:
		return TEXT("Circle");
	case EEnemyInterceptTrainingScenarioType::ZigZagRetreat:
		return TEXT("ZigZagRetreat");
	case EEnemyInterceptTrainingScenarioType::Mixed:
		return TEXT("Mixed");
	default:
		return TEXT("OpenField");
	}
}

static EEnemyInterceptTrainingScenarioType ParseScenarioType(const FString& Value)
{
	const FString Normalized = Value.TrimStartAndEnd();
	if (Normalized.Equals(TEXT("SideCross"), ESearchCase::IgnoreCase))
	{
		return EEnemyInterceptTrainingScenarioType::SideCross;
	}
	if (Normalized.Equals(TEXT("Retreat"), ESearchCase::IgnoreCase))
	{
		return EEnemyInterceptTrainingScenarioType::Retreat;
	}
	if (Normalized.Equals(TEXT("DiagonalRetreat"), ESearchCase::IgnoreCase))
	{
		return EEnemyInterceptTrainingScenarioType::DiagonalRetreat;
	}
	if (Normalized.Equals(TEXT("Circle"), ESearchCase::IgnoreCase))
	{
		return EEnemyInterceptTrainingScenarioType::Circle;
	}
	if (Normalized.Equals(TEXT("ZigZag"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("ZigZagRetreat"), ESearchCase::IgnoreCase))
	{
		return EEnemyInterceptTrainingScenarioType::ZigZagRetreat;
	}
	if (Normalized.Equals(TEXT("Mixed"), ESearchCase::IgnoreCase))
	{
		return EEnemyInterceptTrainingScenarioType::Mixed;
	}
	return EEnemyInterceptTrainingScenarioType::OpenField;
}

static int32 ParseEnemyInterceptRuntimeMode(const FString& Value)
{
	const FString Normalized = Value.TrimStartAndEnd();
	if (Normalized.IsNumeric())
	{
		return FMath::Clamp(FCString::Atoi(*Normalized), -1, 7);
	}
	if (Normalized.Equals(TEXT("Off"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("Off_CurrentLocationOnly"), ESearchCase::IgnoreCase))
	{
		return 0;
	}
	if (Normalized.Equals(TEXT("Deterministic"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("DeterministicPrediction"), ESearchCase::IgnoreCase))
	{
		return 1;
	}
	if (Normalized.Equals(TEXT("Learned"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("LearnedPrediction"), ESearchCase::IgnoreCase))
	{
		return 2;
	}
	if (Normalized.Equals(TEXT("ForceCurrentLocation"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("CurrentLocation"), ESearchCase::IgnoreCase))
	{
		return 3;
	}
	if (Normalized.Equals(TEXT("ForcePredict035"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("Predict035"), ESearchCase::IgnoreCase))
	{
		return 4;
	}
	if (Normalized.Equals(TEXT("ForcePredict075"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("Predict075"), ESearchCase::IgnoreCase))
	{
		return 5;
	}
	if (Normalized.Equals(TEXT("ForcePredict125"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("Predict125"), ESearchCase::IgnoreCase))
	{
		return 6;
	}
	if (Normalized.Equals(TEXT("ForcePredict175"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("Predict175"), ESearchCase::IgnoreCase))
	{
		return 7;
	}
	return -1;
}

static const TCHAR* RuntimeModeToString(int32 RuntimeMode)
{
	switch (RuntimeMode)
	{
	case -1:
		return TEXT("LegacyCVars");
	case 0:
		return TEXT("Off_CurrentLocationOnly");
	case 1:
		return TEXT("DeterministicPrediction");
	case 2:
		return TEXT("LearnedPrediction");
	case 3:
		return TEXT("ForceCurrentLocation");
	case 4:
		return TEXT("ForcePredict035");
	case 5:
		return TEXT("ForcePredict075");
	case 6:
		return TEXT("ForcePredict125");
	case 7:
		return TEXT("ForcePredict175");
	default:
		return TEXT("Invalid");
	}
}

static EEnemyInterceptTrainingScenarioType ResolveScenarioType(EEnemyInterceptTrainingScenarioType RequestedType, FRandomStream& ScenarioStream)
{
	if (RequestedType != EEnemyInterceptTrainingScenarioType::Mixed)
	{
		return RequestedType;
	}

	const int32 Roll = ScenarioStream.RandRange(0, 4);
	switch (Roll)
	{
	case 0:
		return EEnemyInterceptTrainingScenarioType::SideCross;
	case 1:
		return EEnemyInterceptTrainingScenarioType::Retreat;
	case 2:
		return EEnemyInterceptTrainingScenarioType::DiagonalRetreat;
	case 3:
		return EEnemyInterceptTrainingScenarioType::Circle;
	case 4:
	default:
		return EEnemyInterceptTrainingScenarioType::ZigZagRetreat;
	}
}

static FEnemyInterceptDatasetArgs ParseArgs(const FString& Params)
{
	FEnemyInterceptDatasetArgs Args;
	FParse::Value(*Params, TEXT("Map="), Args.MapName);
	FParse::Value(*Params, TEXT("Episodes="), Args.Episodes);
	FParse::Value(*Params, TEXT("Output="), Args.OutputPath);
	FParse::Value(*Params, TEXT("Seed="), Args.Seed);
	FParse::Value(*Params, TEXT("LMStudioUrl="), Args.LMStudioUrl);
	FParse::Value(*Params, TEXT("LMStudioModel="), Args.LMStudioModel);
	FParse::Value(*Params, TEXT("Model="), Args.LMStudioModel);
	FParse::Value(*Params, TEXT("LMStudioTimeoutSeconds="), Args.LMStudioTimeoutSeconds);
	FParse::Value(*Params, TEXT("EvaluationWindowSeconds="), Args.EvaluationWindowSeconds);
	FParse::Value(*Params, TEXT("FixedDeltaSeconds="), Args.FixedDeltaSeconds);
	FParse::Value(*Params, TEXT("MinStartDistance="), Args.MinStartDistance);
	FParse::Value(*Params, TEXT("MaxStartDistance="), Args.MaxStartDistance);
	FParse::Value(*Params, TEXT("PlayerSpeedScaleMin="), Args.PlayerSpeedScaleMin);
	FParse::Value(*Params, TEXT("PlayerSpeedScaleMax="), Args.PlayerSpeedScaleMax);
	FParse::Value(*Params, TEXT("LearnedPolicyPath="), Args.LearnedPolicyPath);
	FParse::Value(*Params, TEXT("PolicyEvalReport="), Args.PolicyEvalReportPath);
	FString RuntimeModeName;
	if (FParse::Value(*Params, TEXT("EnemyInterceptMode="), RuntimeModeName))
	{
		Args.EnemyInterceptRuntimeMode = ParseEnemyInterceptRuntimeMode(RuntimeModeName);
	}
	FString ScenarioTypeName;
	if (FParse::Value(*Params, TEXT("ScenarioType="), ScenarioTypeName))
	{
		Args.ScenarioType = ParseScenarioType(ScenarioTypeName);
	}
	Args.bUseLMStudioPlayer = ParseBoolArg(Params, TEXT("UseLMStudioPlayer"), Args.bUseLMStudioPlayer);
	Args.bForcePlayerMoving = ParseBoolArg(Params, TEXT("ForcePlayerMoving"), Args.bForcePlayerMoving);
	Args.bUseProgressScore = ParseBoolArg(Params, TEXT("UseProgressScore"), Args.bUseProgressScore);
	Args.bEvaluateLearnedPolicy = ParseBoolArg(Params, TEXT("EvaluateLearnedPolicy"), Args.bEvaluateLearnedPolicy);
	Args.bClearOutput = ParseBoolArg(Params, TEXT("ClearOutput"), Args.bClearOutput);
	Args.bVisualDebug = ParseBoolArg(Params, TEXT("VisualDebug"), Args.bVisualDebug);
	Args.bCompareWithCurrent = ParseBoolArg(Params, TEXT("CompareWithCurrent"), Args.bCompareWithCurrent);
	Args.Episodes = FMath::Max(1, Args.Episodes);
	Args.LMStudioTimeoutSeconds = FMath::Max(0.1f, Args.LMStudioTimeoutSeconds);
	Args.EvaluationWindowSeconds = FMath::Max(0.1f, Args.EvaluationWindowSeconds);
	Args.FixedDeltaSeconds = FMath::Clamp(Args.FixedDeltaSeconds, 1.0f / 120.0f, 1.0f / 5.0f);
	Args.MinStartDistance = FMath::Max(100.0f, Args.MinStartDistance);
	Args.MaxStartDistance = FMath::Max(Args.MinStartDistance, Args.MaxStartDistance);
	Args.PlayerSpeedScaleMin = FMath::Max(0.01f, Args.PlayerSpeedScaleMin);
	Args.PlayerSpeedScaleMax = FMath::Max(Args.PlayerSpeedScaleMin, Args.PlayerSpeedScaleMax);
	return Args;
}

static int32 ModeIndex(EEnemyInterceptMode Mode)
{
	return static_cast<int32>(Mode);
}

static EEnemyInterceptMode ModeFromIndex(int32 Index)
{
	return static_cast<EEnemyInterceptMode>(FMath::Clamp(Index, 0, 4));
}

static const TCHAR* InterceptModeToString(EEnemyInterceptMode Mode)
{
	switch (Mode)
	{
	case EEnemyInterceptMode::CurrentLocation:
		return TEXT("CurrentLocation");
	case EEnemyInterceptMode::Predict035:
		return TEXT("Predict035");
	case EEnemyInterceptMode::Predict075:
		return TEXT("Predict075");
	case EEnemyInterceptMode::Predict125:
		return TEXT("Predict125");
	case EEnemyInterceptMode::Predict175:
		return TEXT("Predict175");
	default:
		return TEXT("Unknown");
	}
}

static FString ResolveMapFilename(const FString& MapName)
{
	if (MapName.StartsWith(TEXT("/Game/")))
	{
		return FPackageName::LongPackageNameToFilename(MapName, FPackageName::GetMapPackageExtension());
	}
	return MapName;
}

static AActor* FindTrainingMarker(UWorld* World, const FName& MarkerName)
{
	if (!World || MarkerName.IsNone())
	{
		return nullptr;
	}

	const FString MarkerString = MarkerName.ToString();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->ActorHasTag(MarkerName) ||
			Actor->GetName().Contains(MarkerString, ESearchCase::IgnoreCase))
		{
			return Actor;
		}

#if WITH_EDITOR
		if (Actor->GetActorLabel().Contains(MarkerString, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
#endif
	}

	return nullptr;
}

static FVector ResolveTrainingArenaOrigin(UWorld* World)
{
	if (AActor* EnemySpawn = FindTrainingMarker(World, TEXT("EnemyTrainingSpawn")))
	{
		return EnemySpawn->GetActorLocation();
	}

	if (AActor* ArenaBounds = FindTrainingMarker(World, TEXT("ArenaBounds")))
	{
		return ArenaBounds->GetActorLocation();
	}

	return FVector::ZeroVector;
}

static void SetConsoleInt(const TCHAR* Name, int32 Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByCode);
	}
}

static void SetConsoleString(const TCHAR* Name, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(*Value, ECVF_SetByCode);
	}
}

static UWorld* LoadTrainingWorld(const FString& MapName)
{
	const FString MapFilename = ResolveMapFilename(MapName);
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	if (World && !World->HasBegunPlay())
	{
		World->BeginPlay();
	}
	return World;
}

static AMyAIController* FindExistingEnemyController(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMyAIController> It(World); It; ++It)
	{
		if (It->GetPawn())
		{
			return *It;
		}
	}
	return nullptr;
}

static APawn* FindExistingTrainingPlayer(UWorld* World, APawn* EnemyPawn)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (Pawn && Pawn != EnemyPawn)
		{
			return Pawn;
		}
	}
	return nullptr;
}

static ACharacter* SpawnTrainingCharacter(UWorld* World, const FString& Name, const FTransform& Transform)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = MakeUniqueObjectName(World, ACharacter::StaticClass(), *Name);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World ? World->SpawnActor<ACharacter>(ACharacter::StaticClass(), Transform, SpawnParams) : nullptr;
	if (Character)
	{
		Character->SetActorEnableCollision(true);
		Character->PrimaryActorTick.SetTickFunctionEnable(true);
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->MaxWalkSpeed = 600.0f;
			MoveComp->StopMovementImmediately();
		}
	}
	return Character;
}

static bool ResolveActors(UWorld* World, APawn*& OutPlayerPawn, AMyAIController*& OutEnemyController)
{
	OutEnemyController = FindExistingEnemyController(World);
	APawn* EnemyPawn = OutEnemyController ? OutEnemyController->GetPawn() : nullptr;
	OutPlayerPawn = FindExistingTrainingPlayer(World, EnemyPawn);

	if (!OutPlayerPawn)
	{
		OutPlayerPawn = SpawnTrainingCharacter(World, TEXT("EnemyInterceptTrainingPlayer"), FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 220.0f)));
	}

	if (!OutEnemyController)
	{
		ACharacter* EnemyCharacter = SpawnTrainingCharacter(World, TEXT("EnemyInterceptTrainingEnemy"), FTransform(FRotator::ZeroRotator, FVector(900.0f, 0.0f, 220.0f)));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		OutEnemyController = World ? World->SpawnActor<AMyAIController>(AMyAIController::StaticClass(), EnemyCharacter ? EnemyCharacter->GetActorTransform() : FTransform::Identity, SpawnParams) : nullptr;
		if (OutEnemyController && EnemyCharacter)
		{
			OutEnemyController->Possess(EnemyCharacter);
		}
	}

	return OutPlayerPawn != nullptr && OutEnemyController != nullptr && OutEnemyController->GetPawn() != nullptr;
}

static void ResetPawnForScenario(APawn* Pawn, const FTransform& Transform)
{
	if (!Pawn)
	{
		return;
	}

	if (AController* Controller = Pawn->GetController())
	{
		Controller->StopMovement();
	}

	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}

	Pawn->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
}

static FString BuildActionSummary(const TArray<FTrainingPlayerActionDecision>& Plan)
{
	TArray<FString> Parts;
	Parts.Reserve(Plan.Num());
	for (const FTrainingPlayerActionDecision& Decision : Plan)
	{
		Parts.Add(FString::Printf(
			TEXT("%s:%.2fs:%s"),
			UTrainingAutoPlayerComponent::TrainingPlayerActionToString(Decision.Action),
			Decision.DurationSeconds,
			*Decision.Source));
	}
	return FString::Join(Parts, TEXT("|"));
}

static FString BuildMovementSource(const TArray<FTrainingPlayerActionDecision>& Plan)
{
	bool bHasLMStudio = false;
	bool bHasScriptedScenario = false;
	for (const FTrainingPlayerActionDecision& Decision : Plan)
	{
		if (Decision.Source.Equals(TEXT("LMStudio"), ESearchCase::IgnoreCase))
		{
			bHasLMStudio = true;
			break;
		}
		if (Decision.Source.Equals(TEXT("ScriptedScenario"), ESearchCase::IgnoreCase))
		{
			bHasScriptedScenario = true;
		}
	}
	if (bHasLMStudio)
	{
		return TEXT("MixedLMStudioScripted");
	}
	return bHasScriptedScenario ? TEXT("ScriptedScenario") : TEXT("ScriptedRandom");
}

static FTransform BuildPlayerTransform(const FVector& Origin, float AngleRadians, float Distance)
{
	const FVector Offset(FMath::Cos(AngleRadians) * Distance, FMath::Sin(AngleRadians) * Distance, 0.0f);
	const FVector Location = Origin + Offset + FVector(0.0f, 0.0f, 220.0f);
	const FRotator Rotation = (-Offset).Rotation();
	return FTransform(FRotator(0.0f, Rotation.Yaw, 0.0f), Location);
}

static FTransform BuildEnemyTransform(const FVector& Origin)
{
	return FTransform(FRotator::ZeroRotator, Origin + FVector(0.0f, 0.0f, 220.0f));
}

static void ProjectTransformToWalkable(UWorld* World, FTransform& Transform)
{
	if (!World)
	{
		return;
	}

	FSecondarySearchSettings Settings;
	Settings.ProjectionExtent = FVector(300.0f, 300.0f, 1000.0f);
	FVector ProjectedLocation = FVector::ZeroVector;
	if (FSecondarySearchSolver::ProjectPointToWalkable(World, Transform.GetLocation(), Settings, ProjectedLocation))
	{
		Transform.SetLocation(ProjectedLocation);
	}
}

static FVector SanitizePlanDirection(FVector Direction)
{
	Direction.Z = 0.0f;
	return Direction.GetSafeNormal2D();
}

static FVector GetSideDirection(const FVector& AwayDirection, bool bLeft)
{
	const FVector Left(-AwayDirection.Y, AwayDirection.X, 0.0f);
	return bLeft ? Left.GetSafeNormal2D() : (-Left).GetSafeNormal2D();
}

static FTrainingPlayerActionDecision MakeScenarioAction(
	ETrainingPlayerAction Action,
	float DurationSeconds,
	float SpeedScale,
	const FVector& Direction,
	const FString& Reason)
{
	FTrainingPlayerActionDecision Decision;
	Decision.Action = Action;
	Decision.DurationSeconds = DurationSeconds;
	Decision.SpeedScale = SpeedScale;
	Decision.Direction = SanitizePlanDirection(Direction);
	Decision.Source = TEXT("ScriptedScenario");
	Decision.Reason = Reason;
	return Decision;
}

static TArray<FTrainingPlayerActionDecision> GenerateScenarioTypeActionPlan(
	const FEnemyInterceptDatasetArgs& Args,
	EEnemyInterceptTrainingScenarioType ScenarioType,
	const FTransform& PlayerStartTransform,
	const FTransform& EnemyStartTransform,
	FRandomStream& ScenarioStream)
{
	TArray<FTrainingPlayerActionDecision> Plan;
	const float DurationSeconds = Args.EvaluationWindowSeconds;
	if (DurationSeconds < 0.05f)
	{
		return Plan;
	}

	const FVector PlayerLocation = PlayerStartTransform.GetLocation();
	const FVector EnemyLocation = EnemyStartTransform.GetLocation();
	const FVector AwayFromEnemy = SanitizePlanDirection(PlayerLocation - EnemyLocation);
	const bool bUseLeft = ScenarioStream.RandRange(0, 1) == 0;
	const FVector Side = GetSideDirection(AwayFromEnemy.IsNearlyZero() ? FVector::ForwardVector : AwayFromEnemy, bUseLeft);
	const float SpeedScale = ScenarioStream.FRandRange(Args.PlayerSpeedScaleMin, Args.PlayerSpeedScaleMax);

	switch (ScenarioType)
	{
	case EEnemyInterceptTrainingScenarioType::SideCross:
		Plan.Add(MakeScenarioAction(
			ETrainingPlayerAction::RunStraight,
			DurationSeconds,
			SpeedScale,
			Side,
			TEXT("side crossing movement")));
		break;
	case EEnemyInterceptTrainingScenarioType::Retreat:
		Plan.Add(MakeScenarioAction(
			ETrainingPlayerAction::RunAwayFromEnemy,
			DurationSeconds,
			SpeedScale,
			AwayFromEnemy,
			TEXT("retreat movement")));
		break;
	case EEnemyInterceptTrainingScenarioType::DiagonalRetreat:
		Plan.Add(MakeScenarioAction(
			ETrainingPlayerAction::RunStraight,
			DurationSeconds,
			SpeedScale,
			(AwayFromEnemy * 0.75f + Side * 0.55f).GetSafeNormal2D(),
			TEXT("diagonal retreat movement")));
		break;
	case EEnemyInterceptTrainingScenarioType::Circle:
		Plan.Add(MakeScenarioAction(
			ETrainingPlayerAction::CircleEnemy,
			DurationSeconds,
			SpeedScale,
			Side,
			TEXT("circle movement")));
		break;
	case EEnemyInterceptTrainingScenarioType::ZigZagRetreat:
		Plan.Add(MakeScenarioAction(
			ETrainingPlayerAction::ZigZag,
			DurationSeconds,
			SpeedScale,
			AwayFromEnemy,
			TEXT("zigzag retreat movement")));
		break;
	case EEnemyInterceptTrainingScenarioType::OpenField:
	case EEnemyInterceptTrainingScenarioType::Mixed:
	default:
		break;
	}

	return Plan;
}

static void EnsureMovingActionPlan(TArray<FTrainingPlayerActionDecision>& Plan)
{
	for (FTrainingPlayerActionDecision& Decision : Plan)
	{
		if (Decision.Action == ETrainingPlayerAction::StopAndTurn)
		{
			Decision.Action = ETrainingPlayerAction::RunAwayFromEnemy;
			Decision.Reason = TEXT("ForcePlayerMoving replaced StopAndTurn");
		}
	}
}

static TArray<FTrainingPlayerActionDecision> GeneratePlayerActionPlan(
	const FEnemyInterceptDatasetArgs& Args,
	EEnemyInterceptTrainingScenarioType ScenarioType,
	UTrainingAutoPlayerComponent* AutoPlayer,
	APawn* PlayerPawn,
	APawn* EnemyPawn,
	FRandomStream& ScenarioStream,
	const FLMStudioAutoPlayerDriver* LMDriver)
{
	if (ScenarioType != EEnemyInterceptTrainingScenarioType::OpenField)
	{
		TArray<FTrainingPlayerActionDecision> ScenarioPlan = GenerateScenarioTypeActionPlan(
			Args,
			ScenarioType,
			PlayerPawn ? PlayerPawn->GetActorTransform() : FTransform::Identity,
			EnemyPawn ? EnemyPawn->GetActorTransform() : FTransform::Identity,
			ScenarioStream);
		if (Args.bForcePlayerMoving)
		{
			EnsureMovingActionPlan(ScenarioPlan);
		}
		return ScenarioPlan;
	}

	TArray<FTrainingPlayerActionDecision> Plan;
	float AccumulatedSeconds = 0.0f;
	const float MinDuration = 0.5f;
	const float MaxDuration = 2.0f;
	const float SpeedMin = Args.PlayerSpeedScaleMin;
	const float SpeedMax = Args.PlayerSpeedScaleMax;
	const bool bLMAvailable = Args.bUseLMStudioPlayer && LMDriver && LMDriver->IsAvailable();

	while (AccumulatedSeconds < Args.EvaluationWindowSeconds)
	{
		FTrainingPlayerActionDecision Decision;
		bool bUsedLMStudio = false;
		const bool bTryLMStudio = bLMAvailable && ScenarioStream.FRand() < 0.30f;
		if (bTryLMStudio && AutoPlayer)
		{
			const FAutoPlayerMovementObservation Observation = AutoPlayer->BuildAutoPlayerObservation(PlayerPawn, EnemyPawn);
			bUsedLMStudio = LMDriver->RequestAction(Observation, Decision);
		}

		if (!bUsedLMStudio)
		{
			Decision = UTrainingAutoPlayerComponent::ChooseScriptedPlayerAction(ScenarioStream, MinDuration, MaxDuration, SpeedMin, SpeedMax);
			if (bTryLMStudio)
			{
				Decision.Source = TEXT("Fallback");
				Decision.Reason = TEXT("LM Studio unavailable or invalid; scripted fallback");
			}
		}

		Decision.SpeedScale = ScenarioStream.FRandRange(SpeedMin, SpeedMax);
		if (Decision.Direction.IsNearlyZero())
		{
			const float DirectionAngle = ScenarioStream.FRandRange(-PI, PI);
			Decision.Direction = FVector(FMath::Cos(DirectionAngle), FMath::Sin(DirectionAngle), 0.0f);
		}
		Decision.DurationSeconds = FMath::Min(Decision.DurationSeconds, Args.EvaluationWindowSeconds - AccumulatedSeconds);
		if (Decision.DurationSeconds < 0.05f)
		{
			break;
		}

		Plan.Add(Decision);
		AccumulatedSeconds += Decision.DurationSeconds;
	}

	if (Args.bForcePlayerMoving)
	{
		EnsureMovingActionPlan(Plan);
	}
	return Plan;
}

static FEnemyInterceptTrainingScenario BuildScenario(
	UWorld* World,
	const FEnemyInterceptDatasetArgs& Args,
	int32 ScenarioId,
	FRandomStream& MasterStream,
	UTrainingAutoPlayerComponent* AutoPlayer,
	APawn* PlayerPawn,
	APawn* EnemyPawn,
	const FLMStudioAutoPlayerDriver* LMDriver)
{
	FEnemyInterceptTrainingScenario Scenario;
	Scenario.ScenarioId = ScenarioId;
	Scenario.Seed = MasterStream.RandRange(1, MAX_int32 - 1);
	FRandomStream ScenarioStream(Scenario.Seed);
	Scenario.ScenarioType = ResolveScenarioType(Args.ScenarioType, ScenarioStream);

	const FVector Origin = ResolveTrainingArenaOrigin(World);
	const float Distance = ScenarioStream.FRandRange(Args.MinStartDistance, Args.MaxStartDistance);
	const float Angle = ScenarioStream.FRandRange(-PI, PI);
	Scenario.EnemyStartTransform = BuildEnemyTransform(Origin);
	Scenario.PlayerStartTransform = BuildPlayerTransform(Origin, Angle, Distance);
	ProjectTransformToWalkable(World, Scenario.EnemyStartTransform);
	ProjectTransformToWalkable(World, Scenario.PlayerStartTransform);

	ResetPawnForScenario(PlayerPawn, Scenario.PlayerStartTransform);
	ResetPawnForScenario(EnemyPawn, Scenario.EnemyStartTransform);
	Scenario.PlayerActionPlan = GeneratePlayerActionPlan(Args, Scenario.ScenarioType, AutoPlayer, PlayerPawn, EnemyPawn, ScenarioStream, LMDriver);
	Scenario.PlayerMovementSource = BuildMovementSource(Scenario.PlayerActionPlan);
	Scenario.PlayerActionSummary = FString::Printf(
		TEXT("%s;%s"),
		ScenarioTypeToString(Scenario.ScenarioType),
		*BuildActionSummary(Scenario.PlayerActionPlan));
	return Scenario;
}

static void TickTrainingWorld(UWorld* World, float DeltaSeconds)
{
	if (!World)
	{
		return;
	}

	World->Tick(LEVELTICK_All, DeltaSeconds);
}

static float ScoreInterceptMode(const FEnemyInterceptModeEvaluationResult& Result, bool bUseProgressScore)
{
	if (bUseProgressScore)
	{
		const float ProgressFraction = Result.StartDistanceToPlayer > 1.0f
			? Result.DistanceReduction / Result.StartDistanceToPlayer
			: 0.0f;
		float Score = Result.DistanceReduction * 5.0f + ProgressFraction * 500.0f - Result.FinalDistanceToPlayer * 0.05f;
		if (Result.bReachedAttackRange)
		{
			Score += 1000.0f - Result.TimeToAttackRange * 50.0f;
		}
		Score -= Result.InvalidTargetCount * 0.1f;
		Score -= Result.RepathCount * 0.01f;
		Score -= Result.FallbackCount * 0.05f;
		Score -= Result.PathFailureCount * 0.1f;
		return Score;
	}

	float Score = Result.bReachedAttackRange
		? 1000.0f - Result.TimeToAttackRange * 100.0f
		: -Result.FinalDistanceToPlayer;
	Score -= Result.InvalidTargetCount * 50.0f;
	Score -= Result.RepathCount * 5.0f;
	Score -= Result.FallbackCount * 25.0f;
	Score -= Result.PathFailureCount * 50.0f;
	return Score;
}

static FEnemyInterceptModeEvaluationResult EvaluateInterceptMode(
	UWorld* World,
	const FEnemyInterceptTrainingScenario& Scenario,
	EEnemyInterceptMode Mode,
	APawn* PlayerPawn,
	AMyAIController* EnemyController,
	UTrainingAutoPlayerComponent* AutoPlayer,
	float EvaluationWindowSeconds,
	float FixedDeltaSeconds,
	bool bUseProgressScore)
{
	FEnemyInterceptModeEvaluationResult Result;
	Result.Mode = Mode;

	APawn* EnemyPawn = EnemyController ? EnemyController->GetPawn() : nullptr;
	ResetPawnForScenario(PlayerPawn, Scenario.PlayerStartTransform);
	ResetPawnForScenario(EnemyPawn, Scenario.EnemyStartTransform);

	if (EnemyController)
	{
		EnemyController->SetTrainingTargetPlayer(PlayerPawn);
		EnemyController->SetTrainingInterceptOverride(Mode);
		EnemyController->ResetEnemyInterceptMetricsForTraining();
	}

	if (AutoPlayer)
	{
		AutoPlayer->SetActionPlan(Scenario.PlayerActionPlan);
		AutoPlayer->StartAutoMovement(PlayerPawn, EnemyPawn);
		AutoPlayer->SetComponentTickEnabled(false);
	}

	if (PlayerPawn && EnemyPawn)
	{
		Result.StartDistanceToPlayer = FVector::Dist2D(PlayerPawn->GetActorLocation(), EnemyPawn->GetActorLocation());
		Result.FinalDistanceToPlayer = Result.StartDistanceToPlayer;
	}

	const float AttackRange = EnemyController ? EnemyController->GetTrainingAttackRange() : 180.0f;
	float ElapsedSeconds = 0.0f;
	while (ElapsedSeconds < EvaluationWindowSeconds)
	{
		if (AutoPlayer)
		{
			AutoPlayer->TickAutoMovement(FixedDeltaSeconds);
		}
		if (EnemyController)
		{
			EnemyController->TickTrainingNavigationForCommandlet(FixedDeltaSeconds);
		}
		TickTrainingWorld(World, FixedDeltaSeconds);
		ElapsedSeconds += FixedDeltaSeconds;

		if (PlayerPawn && EnemyPawn)
		{
			const float Distance = FVector::Dist2D(PlayerPawn->GetActorLocation(), EnemyPawn->GetActorLocation());
			Result.FinalDistanceToPlayer = Distance;
			if (!Result.bReachedAttackRange && Distance <= AttackRange)
			{
				Result.bReachedAttackRange = true;
				Result.TimeToAttackRange = ElapsedSeconds;
			}
		}
	}

	if (AutoPlayer)
	{
		AutoPlayer->StopAutoMovement();
	}

	if (PlayerPawn && EnemyPawn)
	{
		Result.FinalDistanceToPlayer = FVector::Dist2D(PlayerPawn->GetActorLocation(), EnemyPawn->GetActorLocation());
	}
	Result.DistanceReduction = Result.StartDistanceToPlayer - Result.FinalDistanceToPlayer;

	if (EnemyController)
	{
		Result.InvalidTargetCount = EnemyController->GetInvalidInterceptTargetCount();
		Result.RepathCount = EnemyController->GetAStarReplanCount();
		Result.FallbackCount = EnemyController->GetAStarFallbackCount();
		Result.PathFailureCount = EnemyController->GetAStarPathFailureCount();
		EnemyController->ClearTrainingInterceptOverride();
	}

	Result.Score = ScoreInterceptMode(Result, bUseProgressScore);
	return Result;
}

static FEnemyInterceptCsvEvaluationColumns ToCsvColumns(const FEnemyInterceptModeEvaluationResult& Result)
{
	FEnemyInterceptCsvEvaluationColumns Columns;
	Columns.Score = Result.Score;
	Columns.TimeToAttackRange = Result.bReachedAttackRange ? Result.TimeToAttackRange : 0.0f;
	Columns.StartDistance = Result.StartDistanceToPlayer;
	Columns.FinalDistance = Result.FinalDistanceToPlayer;
	Columns.DistanceReduction = Result.DistanceReduction;
	Columns.InvalidTargetCount = Result.InvalidTargetCount;
	Columns.RepathCount = Result.RepathCount;
	Columns.FallbackCount = Result.FallbackCount;
	Columns.PathFailureCount = Result.PathFailureCount;
	return Columns;
}

static bool IsValidModeIndex(int32 Index)
{
	return Index >= 0 && Index < 5;
}

static double SafeRatio(int32 Numerator, int32 Denominator)
{
	return Denominator > 0
		? static_cast<double>(Numerator) / static_cast<double>(Denominator)
		: 0.0;
}

static double SafeAverage(double Sum, int32 Count)
{
	return Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
}

static double CalculateBalancedAccuracy(const TArray<int32>& ConfusionMatrix, const TArray<int32>& OracleModeCounts)
{
	double RecallSum = 0.0;
	int32 PresentClassCount = 0;
	for (int32 ClassIndex = 0; ClassIndex < 5; ++ClassIndex)
	{
		const int32 TrueCount = OracleModeCounts.IsValidIndex(ClassIndex) ? OracleModeCounts[ClassIndex] : 0;
		if (TrueCount <= 0)
		{
			continue;
		}

		const int32 CorrectIndex = ClassIndex * 5 + ClassIndex;
		const int32 CorrectCount = ConfusionMatrix.IsValidIndex(CorrectIndex) ? ConfusionMatrix[CorrectIndex] : 0;
		RecallSum += SafeRatio(CorrectCount, TrueCount);
		PresentClassCount++;
	}

	return PresentClassCount > 0 ? RecallSum / static_cast<double>(PresentClassCount) : 0.0;
}

static void AddConfusionEntry(TArray<int32>& ConfusionMatrix, int32 OracleModeIndex, int32 PredictedModeIndex)
{
	if (!IsValidModeIndex(OracleModeIndex) || !IsValidModeIndex(PredictedModeIndex))
	{
		return;
	}

	const int32 MatrixIndex = OracleModeIndex * 5 + PredictedModeIndex;
	if (ConfusionMatrix.IsValidIndex(MatrixIndex))
	{
		ConfusionMatrix[MatrixIndex]++;
	}
}

static void AddModeCount(TArray<int32>& Counts, int32 ModeValue)
{
	if (Counts.IsValidIndex(ModeValue))
	{
		Counts[ModeValue]++;
	}
}

static const FEnemyInterceptModeEvaluationResult& GetEvaluationResultForMode(
	const TArray<FEnemyInterceptModeEvaluationResult>& Results,
	EEnemyInterceptMode Mode)
{
	const int32 Index = ModeIndex(Mode);
	return Results.IsValidIndex(Index) ? Results[Index] : Results[0];
}

static void RecordPolicyEvaluationScenario(
	FEnemyInterceptPolicyEvalAccumulator& Eval,
	const FEnemyInterceptModeEvaluationResult& OracleResult,
	const TArray<FEnemyInterceptModeEvaluationResult>& Results,
	EEnemyInterceptMode LearnedMode,
	EEnemyInterceptMode DeterministicMode)
{
	constexpr EEnemyInterceptMode Predict035Mode = EEnemyInterceptMode::Predict035;
	const int32 OracleModeIndex = ModeIndex(OracleResult.Mode);
	const int32 LearnedModeIndex = ModeIndex(LearnedMode);
	const int32 DeterministicModeIndex = ModeIndex(DeterministicMode);
	const int32 Predict035ModeIndex = ModeIndex(Predict035Mode);

	Eval.TotalScenarios++;
	AddModeCount(Eval.OracleModeCounts, OracleModeIndex);
	AddModeCount(Eval.LearnedModeCounts, LearnedModeIndex);
	AddModeCount(Eval.DeterministicModeCounts, DeterministicModeIndex);
	AddModeCount(Eval.Predict035ModeCounts, Predict035ModeIndex);
	AddConfusionEntry(Eval.LearnedConfusionMatrix, OracleModeIndex, LearnedModeIndex);
	AddConfusionEntry(Eval.DeterministicConfusionMatrix, OracleModeIndex, DeterministicModeIndex);

	if (LearnedMode == OracleResult.Mode)
	{
		Eval.LearnedCorrect++;
	}
	if (DeterministicMode == OracleResult.Mode)
	{
		Eval.DeterministicCorrect++;
	}
	if (Predict035Mode == OracleResult.Mode)
	{
		Eval.Predict035Correct++;
	}

	const FEnemyInterceptModeEvaluationResult& LearnedResult = GetEvaluationResultForMode(Results, LearnedMode);
	const FEnemyInterceptModeEvaluationResult& DeterministicResult = GetEvaluationResultForMode(Results, DeterministicMode);
	const FEnemyInterceptModeEvaluationResult& Predict035Result = GetEvaluationResultForMode(Results, Predict035Mode);

	Eval.OracleBestScoreSum += OracleResult.Score;
	Eval.LearnedChosenScoreSum += LearnedResult.Score;
	Eval.DeterministicChosenScoreSum += DeterministicResult.Score;
	Eval.Predict035ScoreSum += Predict035Result.Score;
	Eval.LearnedScoreGapFromOracleSum += OracleResult.Score - LearnedResult.Score;
	Eval.DeterministicScoreGapFromOracleSum += OracleResult.Score - DeterministicResult.Score;
	Eval.Predict035ScoreGapFromOracleSum += OracleResult.Score - Predict035Result.Score;
	Eval.LearnedInvalidTargetCount += LearnedResult.InvalidTargetCount;
	Eval.LearnedPathFailureCount += LearnedResult.PathFailureCount;
	Eval.LearnedFallbackCount += LearnedResult.FallbackCount;
}

static TArray<TSharedPtr<FJsonValue>> MakeIntArray(const TArray<int32>& Values)
{
	TArray<TSharedPtr<FJsonValue>> JsonValues;
	JsonValues.Reserve(Values.Num());
	for (int32 Value : Values)
	{
		JsonValues.Add(MakeShared<FJsonValueNumber>(Value));
	}
	return JsonValues;
}

static TArray<TSharedPtr<FJsonValue>> MakeConfusionMatrixArray(const TArray<int32>& Matrix)
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 RowIndex = 0; RowIndex < 5; ++RowIndex)
	{
		TArray<TSharedPtr<FJsonValue>> Columns;
		for (int32 ColumnIndex = 0; ColumnIndex < 5; ++ColumnIndex)
		{
			const int32 MatrixIndex = RowIndex * 5 + ColumnIndex;
			Columns.Add(MakeShared<FJsonValueNumber>(Matrix.IsValidIndex(MatrixIndex) ? Matrix[MatrixIndex] : 0));
		}
		Rows.Add(MakeShared<FJsonValueArray>(Columns));
	}
	return Rows;
}

static TSharedPtr<FJsonObject> MakeModeCountObject(const TArray<int32>& Counts)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	for (int32 ModeValue = 0; ModeValue < 5; ++ModeValue)
	{
		Object->SetNumberField(InterceptModeToString(ModeFromIndex(ModeValue)), Counts.IsValidIndex(ModeValue) ? Counts[ModeValue] : 0);
	}
	return Object;
}

static FString ResolveProjectRelativeFilePath(const FString& Path)
{
	if (FPaths::IsRelative(Path))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
	return FPaths::ConvertRelativePathToFull(Path);
}

static FString HashProjectRelativeFile(const FString& Path)
{
	const FString FullPath = ResolveProjectRelativeFilePath(Path);
	if (!FPaths::FileExists(FullPath))
	{
		return TEXT("");
	}
	const FMD5Hash Hash = FMD5Hash::HashFile(*FullPath);
	if (!Hash.IsValid())
	{
		return TEXT("");
	}

	FString Hex;
	const uint8* Bytes = Hash.GetBytes();
	for (int32 ByteIndex = 0; ByteIndex < Hash.GetSize(); ++ByteIndex)
	{
		Hex += FString::Printf(TEXT("%02x"), Bytes[ByteIndex]);
	}
	return Hex;
}

static bool WritePolicyEvaluationReport(
	const FString& ReportPath,
	const FEnemyInterceptDatasetArgs& Args,
	const FEnemyInterceptPolicyEvalAccumulator& Eval,
	FString& OutError)
{
	if (Eval.TotalScenarios <= 0)
	{
		OutError = TEXT("no learned policy evaluation rows were recorded");
		return false;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("map"), Args.MapName);
	Root->SetNumberField(TEXT("episodes"), Args.Episodes);
	Root->SetStringField(TEXT("csv_output"), Args.OutputPath);
	Root->SetStringField(TEXT("learned_policy_path"), Args.LearnedPolicyPath);
	Root->SetStringField(TEXT("learned_policy_md5"), HashProjectRelativeFile(Args.LearnedPolicyPath));
	Root->SetNumberField(TEXT("runtime_mode"), Args.EnemyInterceptRuntimeMode);
	Root->SetStringField(TEXT("runtime_mode_name"), RuntimeModeToString(Args.EnemyInterceptRuntimeMode));
	Root->SetStringField(TEXT("scenario_type"), ScenarioTypeToString(Args.ScenarioType));
	Root->SetNumberField(TEXT("min_start_distance"), Args.MinStartDistance);
	Root->SetNumberField(TEXT("max_start_distance"), Args.MaxStartDistance);
	Root->SetBoolField(TEXT("use_progress_score"), Args.bUseProgressScore);
	Root->SetNumberField(TEXT("total_scenarios"), Eval.TotalScenarios);

	TArray<TSharedPtr<FJsonValue>> ModeLabels;
	for (int32 ModeValue = 0; ModeValue < 5; ++ModeValue)
	{
		TSharedPtr<FJsonObject> ModeObject = MakeShared<FJsonObject>();
		ModeObject->SetNumberField(TEXT("label"), ModeValue);
		ModeObject->SetStringField(TEXT("name"), InterceptModeToString(ModeFromIndex(ModeValue)));
		ModeLabels.Add(MakeShared<FJsonValueObject>(ModeObject));
	}
	Root->SetArrayField(TEXT("mode_labels"), ModeLabels);

	TSharedPtr<FJsonObject> Accuracy = MakeShared<FJsonObject>();
	Accuracy->SetNumberField(TEXT("learned_initial_choice_accuracy"), SafeRatio(Eval.LearnedCorrect, Eval.TotalScenarios));
	Accuracy->SetNumberField(TEXT("deterministic_initial_choice_accuracy"), SafeRatio(Eval.DeterministicCorrect, Eval.TotalScenarios));
	Accuracy->SetNumberField(TEXT("predict035_baseline_accuracy"), SafeRatio(Eval.Predict035Correct, Eval.TotalScenarios));
	Accuracy->SetNumberField(TEXT("learned_balanced_accuracy"), CalculateBalancedAccuracy(Eval.LearnedConfusionMatrix, Eval.OracleModeCounts));
	Accuracy->SetNumberField(TEXT("deterministic_balanced_accuracy"), CalculateBalancedAccuracy(Eval.DeterministicConfusionMatrix, Eval.OracleModeCounts));
	Root->SetObjectField(TEXT("accuracy"), Accuracy);

	TSharedPtr<FJsonObject> Distributions = MakeShared<FJsonObject>();
	Distributions->SetObjectField(TEXT("oracle_label_distribution"), MakeModeCountObject(Eval.OracleModeCounts));
	Distributions->SetObjectField(TEXT("learned_chosen_mode_distribution"), MakeModeCountObject(Eval.LearnedModeCounts));
	Distributions->SetObjectField(TEXT("deterministic_chosen_mode_distribution"), MakeModeCountObject(Eval.DeterministicModeCounts));
	Distributions->SetObjectField(TEXT("predict035_chosen_mode_distribution"), MakeModeCountObject(Eval.Predict035ModeCounts));
	Distributions->SetArrayField(TEXT("oracle_label_distribution_array"), MakeIntArray(Eval.OracleModeCounts));
	Distributions->SetArrayField(TEXT("learned_chosen_mode_distribution_array"), MakeIntArray(Eval.LearnedModeCounts));
	Distributions->SetArrayField(TEXT("deterministic_chosen_mode_distribution_array"), MakeIntArray(Eval.DeterministicModeCounts));
	Root->SetObjectField(TEXT("distributions"), Distributions);

	TSharedPtr<FJsonObject> Confusion = MakeShared<FJsonObject>();
	Confusion->SetStringField(TEXT("orientation"), TEXT("rows=oracle_label, columns=predicted_mode, labels=[0,1,2,3,4]"));
	Confusion->SetArrayField(TEXT("learned"), MakeConfusionMatrixArray(Eval.LearnedConfusionMatrix));
	Confusion->SetArrayField(TEXT("deterministic"), MakeConfusionMatrixArray(Eval.DeterministicConfusionMatrix));
	Root->SetObjectField(TEXT("confusion_matrices"), Confusion);

	TSharedPtr<FJsonObject> Scores = MakeShared<FJsonObject>();
	Scores->SetNumberField(TEXT("average_oracle_best_score"), SafeAverage(Eval.OracleBestScoreSum, Eval.TotalScenarios));
	Scores->SetNumberField(TEXT("average_learned_chosen_score"), SafeAverage(Eval.LearnedChosenScoreSum, Eval.TotalScenarios));
	Scores->SetNumberField(TEXT("average_deterministic_chosen_score"), SafeAverage(Eval.DeterministicChosenScoreSum, Eval.TotalScenarios));
	Scores->SetNumberField(TEXT("average_predict035_score"), SafeAverage(Eval.Predict035ScoreSum, Eval.TotalScenarios));
	Scores->SetNumberField(TEXT("average_learned_score_gap_from_oracle"), SafeAverage(Eval.LearnedScoreGapFromOracleSum, Eval.TotalScenarios));
	Scores->SetNumberField(TEXT("average_deterministic_score_gap_from_oracle"), SafeAverage(Eval.DeterministicScoreGapFromOracleSum, Eval.TotalScenarios));
	Scores->SetNumberField(TEXT("average_predict035_score_gap_from_oracle"), SafeAverage(Eval.Predict035ScoreGapFromOracleSum, Eval.TotalScenarios));
	Root->SetObjectField(TEXT("score_comparison"), Scores);

	TSharedPtr<FJsonObject> Safety = MakeShared<FJsonObject>();
	Safety->SetNumberField(TEXT("learned_chosen_invalid_target_count"), Eval.LearnedInvalidTargetCount);
	Safety->SetNumberField(TEXT("learned_chosen_path_failure_count"), Eval.LearnedPathFailureCount);
	Safety->SetNumberField(TEXT("learned_chosen_fallback_count"), Eval.LearnedFallbackCount);
	Safety->SetNumberField(TEXT("average_learned_chosen_invalid_target_count"), SafeAverage(Eval.LearnedInvalidTargetCount, Eval.TotalScenarios));
	Safety->SetNumberField(TEXT("average_learned_chosen_path_failure_count"), SafeAverage(Eval.LearnedPathFailureCount, Eval.TotalScenarios));
	Safety->SetNumberField(TEXT("average_learned_chosen_fallback_count"), SafeAverage(Eval.LearnedFallbackCount, Eval.TotalScenarios));
	Root->SetObjectField(TEXT("safety_metrics"), Safety);

	FString OutputJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OutError = TEXT("failed to serialize learned policy evaluation report");
		return false;
	}

	const FString FullReportPath = ResolveProjectRelativeFilePath(ReportPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullReportPath), true);
	if (!FFileHelper::SaveStringToFile(OutputJson, *FullReportPath))
	{
		OutError = FString::Printf(TEXT("failed to write learned policy evaluation report: %s"), *FullReportPath);
		return false;
	}

	OutError.Reset();
	return true;
}

static void LogPolicyEvaluationSummary(const FEnemyInterceptPolicyEvalAccumulator& Eval, const FString& ReportPath)
{
	if (Eval.TotalScenarios <= 0)
	{
		return;
	}

	const double LearnedAccuracy = SafeRatio(Eval.LearnedCorrect, Eval.TotalScenarios);
	const double DeterministicAccuracy = SafeRatio(Eval.DeterministicCorrect, Eval.TotalScenarios);
	const double Predict035Accuracy = SafeRatio(Eval.Predict035Correct, Eval.TotalScenarios);
	const double LearnedBalancedAccuracy = CalculateBalancedAccuracy(Eval.LearnedConfusionMatrix, Eval.OracleModeCounts);
	const double DeterministicBalancedAccuracy = CalculateBalancedAccuracy(Eval.DeterministicConfusionMatrix, Eval.OracleModeCounts);
	const double LearnedScoreGap = SafeAverage(Eval.LearnedScoreGapFromOracleSum, Eval.TotalScenarios);
	const double DeterministicScoreGap = SafeAverage(Eval.DeterministicScoreGapFromOracleSum, Eval.TotalScenarios);
	const double Predict035ScoreGap = SafeAverage(Eval.Predict035ScoreGapFromOracleSum, Eval.TotalScenarios);

	UE_LOG(LogTemp, Display,
		TEXT("EnemyInterceptPolicyEval: accuracy learned=%.2f%% deterministic=%.2f%% predict035=%.2f%% | balanced learned=%.2f%% deterministic=%.2f%%"),
		LearnedAccuracy * 100.0,
		DeterministicAccuracy * 100.0,
		Predict035Accuracy * 100.0,
		LearnedBalancedAccuracy * 100.0,
		DeterministicBalancedAccuracy * 100.0);
	UE_LOG(LogTemp, Display,
		TEXT("EnemyInterceptPolicyEval: avg score gap from oracle learned=%.2f deterministic=%.2f predict035=%.2f"),
		LearnedScoreGap,
		DeterministicScoreGap,
		Predict035ScoreGap);
	UE_LOG(LogTemp, Display,
		TEXT("EnemyInterceptPolicyEval: learned safety totals invalid_targets=%d path_failures=%d fallbacks=%d"),
		Eval.LearnedInvalidTargetCount,
		Eval.LearnedPathFailureCount,
		Eval.LearnedFallbackCount);
	UE_LOG(LogTemp, Display,
		TEXT("EnemyInterceptPolicyEval: report=%s"),
		*ReportPath);
}
}

UEnemyInterceptDatasetCommandlet::UEnemyInterceptDatasetCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UEnemyInterceptDatasetCommandlet::Main(const FString& Params)
{
	const FEnemyInterceptDatasetArgs Args = ParseArgs(Params);
	UE_LOG(LogTemp, Display,
		TEXT("EnemyInterceptDataset: map=%s episodes=%d output=%s seed=%d use_lmstudio=%s scenario_type=%s distance=%.1f..%.1f progress_score=%s evaluate_learned=%s runtime_mode=%s clear_output=%s"),
		*Args.MapName,
		Args.Episodes,
		*Args.OutputPath,
		Args.Seed,
		Args.bUseLMStudioPlayer ? TEXT("true") : TEXT("false"),
		ScenarioTypeToString(Args.ScenarioType),
		Args.MinStartDistance,
		Args.MaxStartDistance,
		Args.bUseProgressScore ? TEXT("true") : TEXT("false"),
		Args.bEvaluateLearnedPolicy ? TEXT("true") : TEXT("false"),
		RuntimeModeToString(Args.EnemyInterceptRuntimeMode),
		Args.bClearOutput ? TEXT("true") : TEXT("false"));

	UWorld* World = LoadTrainingWorld(Args.MapName);
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyInterceptDataset: failed to load map %s"), *Args.MapName);
		return 1;
	}

	APawn* PlayerPawn = nullptr;
	AMyAIController* EnemyController = nullptr;
	if (!ResolveActors(World, PlayerPawn, EnemyController))
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyInterceptDataset: failed to resolve training player and enemy controller"));
		return 1;
	}

	APawn* EnemyPawn = EnemyController->GetPawn();
	UTrainingAutoPlayerComponent* AutoPlayer = NewObject<UTrainingAutoPlayerComponent>(PlayerPawn, TEXT("TrainingAutoPlayerComponent"));
	PlayerPawn->AddInstanceComponent(AutoPlayer);
	AutoPlayer->RegisterComponent();

	FLMStudioAutoPlayerSettings LMSettings;
	LMSettings.BaseUrl = Args.LMStudioUrl;
	LMSettings.Model = Args.LMStudioModel;
	LMSettings.TimeoutSeconds = Args.LMStudioTimeoutSeconds;
	FLMStudioAutoPlayerDriver LMDriver(LMSettings);
	const FLMStudioAutoPlayerDriver* LMDriverPtr = Args.bUseLMStudioPlayer ? &LMDriver : nullptr;

	SetConsoleInt(TEXT("sd.EnemyIntercept.EnablePrediction"), 1);
	SetConsoleInt(TEXT("sd.EnemyIntercept.ForceMode"), -1);
	SetConsoleInt(TEXT("sd.EnemyIntercept.UseLearnedPolicy"), 0);
	SetConsoleInt(TEXT("sd.EnemyIntercept.Mode"), Args.EnemyInterceptRuntimeMode);
	SetConsoleInt(TEXT("sd.EnemyIntercept.VisualDebug"), Args.bVisualDebug ? 1 : 0);
	SetConsoleInt(TEXT("sd.EnemyIntercept.CompareWithCurrent"), Args.bCompareWithCurrent ? 1 : 0);
	if (Args.bEvaluateLearnedPolicy)
	{
		SetConsoleString(TEXT("sd.EnemyIntercept.LearnedPolicyPath"), Args.LearnedPolicyPath);
	}

	if (Args.bClearOutput)
	{
		const FString FullOutputPath = ResolveProjectRelativeFilePath(Args.OutputPath);
		IFileManager::Get().Delete(*FullOutputPath, false, true, true);
	}

	FEnemyInterceptCsvWriter CsvWriter(Args.OutputPath);
	FRandomStream MasterStream(Args.Seed);
	int32 RowsWritten = 0;
	FEnemyInterceptPolicyEvalAccumulator PolicyEval;

	for (int32 EpisodeIndex = 0; EpisodeIndex < Args.Episodes; ++EpisodeIndex)
	{
		const FEnemyInterceptTrainingScenario Scenario = BuildScenario(
			World,
			Args,
			EpisodeIndex,
			MasterStream,
			AutoPlayer,
			PlayerPawn,
			EnemyPawn,
			LMDriverPtr);

		ResetPawnForScenario(PlayerPawn, Scenario.PlayerStartTransform);
		ResetPawnForScenario(EnemyPawn, Scenario.EnemyStartTransform);
		EnemyController->SetTrainingTargetPlayer(PlayerPawn);
		EnemyController->ResetEnemyInterceptMetricsForTraining();
		const FEnemyInterceptObservation InitialObservation = EnemyController->BuildInterceptObservation(PlayerPawn);

		TArray<FEnemyInterceptModeEvaluationResult> Results;
		Results.SetNum(5);
		for (int32 ModeValue = 0; ModeValue <= 4; ++ModeValue)
		{
			const EEnemyInterceptMode Mode = ModeFromIndex(ModeValue);
			Results[ModeValue] = EvaluateInterceptMode(
				World,
				Scenario,
				Mode,
				PlayerPawn,
				EnemyController,
				AutoPlayer,
				Args.EvaluationWindowSeconds,
				Args.FixedDeltaSeconds,
				Args.bUseProgressScore);
		}

		FEnemyInterceptModeEvaluationResult BestResult = Results[0];
		for (const FEnemyInterceptModeEvaluationResult& Result : Results)
		{
			if (Result.Score > BestResult.Score)
			{
				BestResult = Result;
			}
		}

		if (Args.bEvaluateLearnedPolicy)
		{
			ResetPawnForScenario(PlayerPawn, Scenario.PlayerStartTransform);
			ResetPawnForScenario(EnemyPawn, Scenario.EnemyStartTransform);
			EnemyController->ClearTrainingInterceptOverride();
			EnemyController->SetTrainingTargetPlayer(PlayerPawn);
			EnemyController->ResetEnemyInterceptMetricsForTraining();

			SetConsoleInt(TEXT("sd.EnemyIntercept.UseLearnedPolicy"), 0);
			const FEnemyInterceptDecision DeterministicDecision = EnemyController->ChooseSmartNavigationGoal(PlayerPawn);

			SetConsoleInt(TEXT("sd.EnemyIntercept.UseLearnedPolicy"), 1);
			const FEnemyInterceptDecision LearnedDecision = EnemyController->ChooseSmartNavigationGoal(PlayerPawn);
			RecordPolicyEvaluationScenario(
				PolicyEval,
				BestResult,
				Results,
				LearnedDecision.Mode,
				DeterministicDecision.Mode);
			SetConsoleInt(TEXT("sd.EnemyIntercept.UseLearnedPolicy"), 0);
		}

		FEnemyInterceptCsvRow CsvRow;
		CsvRow.ScenarioId = Scenario.ScenarioId;
		CsvRow.Seed = Scenario.Seed;
		CsvRow.MapName = Args.MapName;
		CsvRow.PlayerMovementSource = Scenario.PlayerMovementSource;
		CsvRow.PlayerActionSummary = Scenario.PlayerActionSummary;
		CsvRow.Observation = InitialObservation;
		CsvRow.BestMode = BestResult.Mode;
		CsvRow.BestModeScore = BestResult.Score;
		CsvRow.CurrentLocation = ToCsvColumns(Results[ModeIndex(EEnemyInterceptMode::CurrentLocation)]);
		CsvRow.Predict035 = ToCsvColumns(Results[ModeIndex(EEnemyInterceptMode::Predict035)]);
		CsvRow.Predict075 = ToCsvColumns(Results[ModeIndex(EEnemyInterceptMode::Predict075)]);
		CsvRow.Predict125 = ToCsvColumns(Results[ModeIndex(EEnemyInterceptMode::Predict125)]);
		CsvRow.Predict175 = ToCsvColumns(Results[ModeIndex(EEnemyInterceptMode::Predict175)]);

		FString CsvError;
		if (!CsvWriter.WriteRow(CsvRow, CsvError))
		{
			UE_LOG(LogTemp, Error, TEXT("EnemyInterceptDataset: %s"), *CsvError);
			return 1;
		}

		RowsWritten++;
		UE_LOG(LogTemp, Display,
			TEXT("EnemyInterceptDataset: scenario=%d best_mode=%d best_score=%.2f source=%s"),
			Scenario.ScenarioId,
			static_cast<int32>(BestResult.Mode),
			BestResult.Score,
			*Scenario.PlayerMovementSource);
	}

	if (EnemyController)
	{
		EnemyController->ClearTrainingInterceptOverride();
		EnemyController->ClearTrainingTargetPlayer();
	}
	if (AutoPlayer)
	{
		AutoPlayer->StopAutoMovement();
	}
	SetConsoleInt(TEXT("sd.EnemyIntercept.UseLearnedPolicy"), 0);
	SetConsoleInt(TEXT("sd.EnemyIntercept.Mode"), -1);
	SetConsoleInt(TEXT("sd.EnemyIntercept.VisualDebug"), 0);
	SetConsoleInt(TEXT("sd.EnemyIntercept.CompareWithCurrent"), 0);

	UE_LOG(LogTemp, Display, TEXT("EnemyInterceptDataset: wrote %d rows to %s"), RowsWritten, *Args.OutputPath);
	if (Args.bEvaluateLearnedPolicy && PolicyEval.TotalScenarios > 0)
	{
		FString ReportError;
		if (!WritePolicyEvaluationReport(Args.PolicyEvalReportPath, Args, PolicyEval, ReportError))
		{
			UE_LOG(LogTemp, Warning, TEXT("EnemyInterceptPolicyEval: failed to write report: %s"), *ReportError);
		}
		LogPolicyEvaluationSummary(PolicyEval, Args.PolicyEvalReportPath);
	}
	return 0;
}
