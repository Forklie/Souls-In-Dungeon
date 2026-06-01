#include "MyAIController.h"

#include "Animation/AnimInstance.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "LevelManager.h"
#include "SecondarySearchVisualizerActor.h"
#include "Soul_and_dungeon.h"
#include "Soul_and_dungeonCharacter.h"


namespace
{
static TAutoConsoleVariable<int32> CVarEnemyNavigationMode(
	TEXT("sd.EnemyNavigation.Mode"),
	1,
	TEXT("Enemy navigation mode: 0=AStarOnly, 1=SmoothedAStar."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptEnablePrediction(
	TEXT("sd.EnemyIntercept.EnablePrediction"),
	0,
	TEXT("Enable enemy intercept target prediction before A* planning. 0=old current-location behavior, 1=prediction enabled."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptUseLearnedPolicy(
	TEXT("sd.EnemyIntercept.UseLearnedPolicy"),
	0,
	TEXT("Use the runtime learned intercept policy when prediction is enabled. 0=deterministic policy, 1=load JSON tree policy and fall back to deterministic on failure."),
	ECVF_Default);

static FString GEnemyInterceptLearnedPolicyPath = TEXT("Saved/EnemyLearning/Models/enemy_intercept_policy_runtime.json");
static FAutoConsoleVariableRef CVarEnemyInterceptLearnedPolicyPath(
	TEXT("sd.EnemyIntercept.LearnedPolicyPath"),
	GEnemyInterceptLearnedPolicyPath,
	TEXT("Project-relative or absolute path to a runtime-safe enemy intercept policy JSON file."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptLearnedPolicyDebug(
	TEXT("sd.EnemyIntercept.LearnedPolicyDebug"),
	0,
	TEXT("Log learned intercept policy load and fallback details. 0=off, 1=on."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptForceMode(
	TEXT("sd.EnemyIntercept.ForceMode"),
	-1,
	TEXT("Force intercept mode for testing when prediction is enabled: -1=auto, 0=CurrentLocation, 1=Predict035, 2=Predict075, 3=Predict125, 4=Predict175."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptRuntimeMode(
	TEXT("sd.EnemyIntercept.Mode"),
	-1,
	TEXT("Clear runtime intercept mode. -1=legacy CVars, 0=Off_CurrentLocationOnly, 1=DeterministicPrediction, 2=LearnedPrediction, 3=ForceCurrentLocation, 4=ForcePredict035, 5=ForcePredict075, 6=ForcePredict125, 7=ForcePredict175."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptVisualDebug(
	TEXT("sd.EnemyIntercept.VisualDebug"),
	0,
	TEXT("Draw enemy intercept target debug markers. 0=off, 1=on."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptCompareWithCurrent(
	TEXT("sd.EnemyIntercept.CompareWithCurrent"),
	0,
	TEXT("When visual debug is enabled, draw current-player target alongside the selected intercept target. 0=off, 1=on."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyInterceptDebug(
	TEXT("sd.EnemyIntercept.Debug"),
	0,
	TEXT("Log enemy intercept decisions. 0=off, 1=throttled decision logs."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarEnemyInterceptDebugLogInterval(
	TEXT("sd.EnemyIntercept.DebugLogInterval"),
	1.0f,
	TEXT("Minimum seconds between repeated enemy intercept debug logs."),
	ECVF_Default);

static FVector CatmullRomPoint(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
{
	const float T2 = T * T;
	const float T3 = T2 * T;
	return 0.5f * (
		(2.0f * P1) +
		(-P0 + P2) * T +
		(2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2 +
	(-P0 + 3.0f * P1 - 3.0f * P2 + P3) * T3);
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

static bool TryInterceptModeFromInt(int32 Value, EEnemyInterceptMode& OutMode)
{
	if (Value < 0 || Value > 4)
	{
		return false;
	}

	OutMode = static_cast<EEnemyInterceptMode>(Value);
	return true;
}

static const TCHAR* EnemyInterceptRuntimeModeToString(int32 RuntimeMode)
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

static bool TryResolveForcedModeFromRuntimeMode(int32 RuntimeMode, EEnemyInterceptMode& OutMode)
{
	if (RuntimeMode < 3 || RuntimeMode > 7)
	{
		return false;
	}

	OutMode = static_cast<EEnemyInterceptMode>(RuntimeMode - 3);
	return true;
}

static void CycleEnemyInterceptRuntimeModeConsole()
{
	const int32 CurrentMode = CVarEnemyInterceptRuntimeMode.GetValueOnGameThread();
	int32 NextMode = 0;
	switch (CurrentMode)
	{
	case 0:
		NextMode = 1;
		break;
	case 1:
		NextMode = 2;
		break;
	case 2:
		NextMode = 4;
		break;
	case 4:
		NextMode = 5;
		break;
	case 5:
		NextMode = 6;
		break;
	case 6:
		NextMode = 7;
		break;
	case 7:
		NextMode = 0;
		break;
	default:
		NextMode = 0;
		break;
	}

	if (IConsoleVariable* ModeVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("sd.EnemyIntercept.Mode")))
	{
		ModeVariable->Set(NextMode, ECVF_SetByConsole);
	}

	UE_LOG(LogSoul_and_dungeon, Display, TEXT("EnemyIntercept Mode: %s"), EnemyInterceptRuntimeModeToString(NextMode));
}

static FAutoConsoleCommand CmdEnemyInterceptCycleMode(
	TEXT("sd.EnemyIntercept.CycleMode"),
	TEXT("Cycle enemy intercept mode: Off -> Deterministic -> Learned -> Force035 -> Force075 -> Force125 -> Force175 -> Off."),
	FConsoleCommandDelegate::CreateStatic(&CycleEnemyInterceptRuntimeModeConsole));

static FString ResolveEnemyInterceptPolicyPath(const FString& ConfiguredPath)
{
	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ConfiguredPath);
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}
}



AMyAIController::AMyAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMyAIController::BeginPlay()
{
	Super::BeginPlay();
}





int32 AMyAIController::GetAStarFallbackCount() const
{
	return AStarFallbackCount;
}

int32 AMyAIController::GetInvalidInterceptTargetCount() const
{
	return InvalidInterceptTargetCount;
}

int32 AMyAIController::GetAStarReplanCount() const
{
	return AStarReplanCount;
}

int32 AMyAIController::GetAStarPathFailureCount() const
{
	return AStarPathFailureCount;
}

float AMyAIController::GetTrainingAttackRange() const
{
	return StopDistance + 30.0f;
}

void AMyAIController::ResetEnemyInterceptMetricsForTraining()
{
	AStarFallbackCount = 0;
	InvalidInterceptTargetCount = 0;
	AStarReplanCount = 0;
	AStarPathFailureCount = 0;
	LastPlayerMoveDirectionForIntercept = FVector::ZeroVector;
	LastPlayerDirectionChangeTime = -1000000.0f;
	LastInterceptDebugLogTime = -1000000.0f;
	LastLoggedInterceptMode = EEnemyInterceptMode::CurrentLocation;
	bHasLoggedInterceptMode = false;
	ResetAStarNavigation();
}

void AMyAIController::TickTrainingNavigationForCommandlet(float DeltaTime)
{
	if (!IsRunningCommandlet() || !IsValid(TrainingTargetPlayer))
	{
		return;
	}

	APawn* AI = GetPawn();
	APawn* Player = TrainingTargetPlayer.Get();
	if (!AI || !Player)
	{
		return;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	UpdateAStarNavigation(AI, Player, CurrentTime, DeltaTime);
}

FEnemyInterceptObservation AMyAIController::BuildInterceptObservation(APawn* PlayerPawn)
{
	FEnemyInterceptObservation Observation;

	APawn* EnemyPawn = GetPawn();
	if (!EnemyPawn || !PlayerPawn)
	{
		return Observation;
	}

	Observation.EnemyLocation = EnemyPawn->GetActorLocation();
	Observation.EnemyVelocity = EnemyPawn->GetVelocity();
	Observation.PlayerLocation = PlayerPawn->GetActorLocation();
	Observation.PlayerVelocity = PlayerPawn->GetVelocity();
	Observation.PlayerSpeed = Observation.PlayerVelocity.Size2D();
	Observation.EnemySpeed = Observation.EnemyVelocity.Size2D();
	Observation.DistanceToPlayer = FVector::Dist2D(Observation.EnemyLocation, Observation.PlayerLocation);
	Observation.ZDelta = Observation.PlayerLocation.Z - Observation.EnemyLocation.Z;

	const FVector EnemyDirectionFromPlayer = (Observation.EnemyLocation - Observation.PlayerLocation).GetSafeNormal2D();
	const FVector PlayerMoveDirection = Observation.PlayerVelocity.GetSafeNormal2D();
	Observation.DotPlayerMoveWithEnemyDirection = FVector::DotProduct(PlayerMoveDirection, EnemyDirectionFromPlayer);

	UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;
	if (!PlayerMoveDirection.IsNearlyZero())
	{
		if (LastPlayerMoveDirectionForIntercept.IsNearlyZero())
		{
			LastPlayerMoveDirectionForIntercept = PlayerMoveDirection;
			LastPlayerDirectionChangeTime = CurrentTime;
		}
		else
		{
			const float DirectionDot = FMath::Clamp(FVector::DotProduct(LastPlayerMoveDirectionForIntercept, PlayerMoveDirection), -1.0f, 1.0f);
			Observation.RecentPlayerTurnAmount = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
			if (Observation.RecentPlayerTurnAmount >= 15.0f)
			{
				LastPlayerMoveDirectionForIntercept = PlayerMoveDirection;
				LastPlayerDirectionChangeTime = CurrentTime;
			}
		}
	}

	Observation.TimeSinceLastPlayerDirectionChange = LastPlayerDirectionChangeTime < -999999.0f
		? 0.0f
		: FMath::Max(0.0f, CurrentTime - LastPlayerDirectionChangeTime);

	if (World)
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyInterceptLineOfSight), false);
		QueryParams.AddIgnoredActor(EnemyPawn);
		QueryParams.AddIgnoredActor(PlayerPawn);
		const FVector TraceStart = Observation.EnemyLocation + FVector(0.0f, 0.0f, 45.0f);
		const FVector TraceEnd = Observation.PlayerLocation + FVector(0.0f, 0.0f, 45.0f);
		Observation.bHasLineOfSight = !World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	}

	if (Observation.EnemyLocation.ContainsNaN())
	{
		Observation.EnemyLocation = FVector::ZeroVector;
	}
	if (Observation.EnemyVelocity.ContainsNaN())
	{
		Observation.EnemyVelocity = FVector::ZeroVector;
	}
	if (Observation.PlayerLocation.ContainsNaN())
	{
		Observation.PlayerLocation = FVector::ZeroVector;
	}
	if (Observation.PlayerVelocity.ContainsNaN())
	{
		Observation.PlayerVelocity = FVector::ZeroVector;
	}

	Observation.PlayerSpeed = FMath::IsFinite(Observation.PlayerSpeed) ? Observation.PlayerSpeed : 0.0f;
	Observation.EnemySpeed = FMath::IsFinite(Observation.EnemySpeed) ? Observation.EnemySpeed : 0.0f;
	Observation.DistanceToPlayer = FMath::IsFinite(Observation.DistanceToPlayer) ? Observation.DistanceToPlayer : 0.0f;
	Observation.ZDelta = FMath::IsFinite(Observation.ZDelta) ? Observation.ZDelta : 0.0f;
	Observation.DotPlayerMoveWithEnemyDirection = FMath::IsFinite(Observation.DotPlayerMoveWithEnemyDirection) ? Observation.DotPlayerMoveWithEnemyDirection : 0.0f;
	Observation.RecentPlayerTurnAmount = FMath::IsFinite(Observation.RecentPlayerTurnAmount) ? Observation.RecentPlayerTurnAmount : 0.0f;
	Observation.TimeSinceLastPlayerDirectionChange = FMath::IsFinite(Observation.TimeSinceLastPlayerDirectionChange) ? Observation.TimeSinceLastPlayerDirectionChange : 0.0f;

	return Observation;
}

EEnemyInterceptMode AMyAIController::ChooseDeterministicInterceptMode(const FEnemyInterceptObservation& Observation) const
{
	if (Observation.PlayerSpeed < 80.0f || Observation.DistanceToPlayer <= StopDistance * 1.5f)
	{
		return EEnemyInterceptMode::CurrentLocation;
	}

	if (!Observation.bHasLineOfSight)
	{
		return Observation.DistanceToPlayer > 900.0f
			? EEnemyInterceptMode::Predict075
			: EEnemyInterceptMode::Predict035;
	}

	if (Observation.DotPlayerMoveWithEnemyDirection > 0.35f)
	{
		return EEnemyInterceptMode::Predict035;
	}

	if (Observation.DistanceToPlayer > 1300.0f && Observation.PlayerSpeed > 450.0f)
	{
		return EEnemyInterceptMode::Predict175;
	}

	if (Observation.DistanceToPlayer > 900.0f && Observation.PlayerSpeed > 300.0f)
	{
		return EEnemyInterceptMode::Predict125;
	}

	if (Observation.DistanceToPlayer > 450.0f && Observation.PlayerSpeed > 250.0f)
	{
		return EEnemyInterceptMode::Predict075;
	}

	return EEnemyInterceptMode::Predict035;
}

float AMyAIController::GetPredictionTimeForMode(EEnemyInterceptMode Mode) const
{
	switch (Mode)
	{
	case EEnemyInterceptMode::Predict035:
		return 0.35f;
	case EEnemyInterceptMode::Predict075:
		return 0.75f;
	case EEnemyInterceptMode::Predict125:
		return 1.25f;
	case EEnemyInterceptMode::Predict175:
		return 1.75f;
	case EEnemyInterceptMode::CurrentLocation:
	default:
		return 0.0f;
	}
}

FVector AMyAIController::ComputeGoalForInterceptMode(APawn* PlayerPawn, EEnemyInterceptMode Mode) const
{
	if (!PlayerPawn)
	{
		return FVector::ZeroVector;
	}

	const float PredictionTime = GetPredictionTimeForMode(Mode);
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	if (PredictionTime <= 0.0f)
	{
		return PlayerLocation;
	}

	FVector PlayerVelocity = PlayerPawn->GetVelocity();
	PlayerVelocity.Z = 0.0f;
	if (PlayerVelocity.SizeSquared2D() < FMath::Square(1.0f))
	{
		return PlayerLocation;
	}

	FVector PredictedGoal = PlayerLocation + PlayerVelocity * PredictionTime;
	PredictedGoal.Z = PlayerLocation.Z;
	return PredictedGoal;
}

bool AMyAIController::TryValidateInterceptGoal(const FVector& CandidateGoal, FVector& OutValidatedGoal, FString& OutReason) const
{
	if (CandidateGoal.ContainsNaN() ||
		!FMath::IsFinite(CandidateGoal.X) ||
		!FMath::IsFinite(CandidateGoal.Y) ||
		!FMath::IsFinite(CandidateGoal.Z))
	{
		OutReason = TEXT("candidate is not finite");
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		OutReason = TEXT("world unavailable");
		return false;
	}

	FSecondarySearchSettings Settings = BuildSecondarySearchSettings();
	const float ProjectionRadius = FMath::Max(100.0f, Settings.CellSize * 1.75f);
	Settings.ProjectionExtent = FVector(ProjectionRadius, ProjectionRadius, 650.0f);
	FVector ProjectedLocation = FVector::ZeroVector;
	if (!FSecondarySearchSolver::ProjectPointToWalkable(World, CandidateGoal, Settings, ProjectedLocation))
	{
		OutReason = TEXT("candidate did not project to custom walkable floor");
		return false;
	}

	OutValidatedGoal = ProjectedLocation;
	OutReason = TEXT("projected to custom walkable floor");
	return true;
}

FEnemyInterceptDecision AMyAIController::ChooseSmartNavigationGoal(APawn* PlayerPawn)
{
	if (!PlayerPawn)
	{
		FEnemyInterceptDecision Decision;
		Decision.Reason = TEXT("missing player pawn");
		return Decision;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	FEnemyInterceptObservation Observation = BuildInterceptObservation(PlayerPawn);
	if (!IsInterceptPredictionEnabled() && !bOverrideInterceptModeForTraining)
	{
		FEnemyInterceptDecision Decision = MakeCurrentPlayerLocationDecision(PlayerPawn, TEXT("intercept prediction disabled"));
		LogInterceptDecision(Decision, Observation, CurrentTime);
		DrawInterceptDebug(PlayerPawn, Observation, Decision, GetEnemyInterceptRuntimeModeName());
		return Decision;
	}

	EEnemyInterceptMode Mode = EEnemyInterceptMode::CurrentLocation;
	FString Reason;

	if (bOverrideInterceptModeForTraining)
	{
		Mode = TrainingOverrideInterceptMode;
		Reason = TEXT("forced by training override");
	}
	else
	{
		const int32 RuntimeMode = GetEnemyInterceptRuntimeMode();
		EEnemyInterceptMode RuntimeForcedMode = EEnemyInterceptMode::CurrentLocation;
		FString RuntimeModeReason;
		if (RuntimeMode == 1)
		{
			Mode = ChooseDeterministicInterceptMode(Observation);
			Reason = TEXT("runtime mode deterministic policy");
		}
		else if (RuntimeMode == 2)
		{
			Mode = ChooseLearnedInterceptModeOrFallback(Observation, Reason);
			Reason = FString::Printf(TEXT("runtime mode learned policy; %s"), *Reason);
		}
		else if (ResolveInterceptModeFromRuntimeMode(RuntimeMode, RuntimeForcedMode, RuntimeModeReason))
		{
			Mode = RuntimeForcedMode;
			Reason = RuntimeModeReason;
		}
		else
		{
			EEnemyInterceptMode ConsoleForcedMode = EEnemyInterceptMode::CurrentLocation;
			const int32 ForcedModeValue = CVarEnemyInterceptForceMode.GetValueOnGameThread();
			if (TryInterceptModeFromInt(ForcedModeValue, ConsoleForcedMode))
			{
				Mode = ConsoleForcedMode;
				Reason = TEXT("forced by console");
			}
			else
			{
				if (CVarEnemyInterceptUseLearnedPolicy.GetValueOnGameThread() != 0)
				{
					Mode = ChooseLearnedInterceptModeOrFallback(Observation, Reason);
				}
				else
				{
					Mode = ChooseDeterministicInterceptMode(Observation);
					Reason = TEXT("deterministic policy");
				}
			}
		}
	}

	if (Mode == EEnemyInterceptMode::CurrentLocation)
	{
		FEnemyInterceptDecision Decision = MakeCurrentPlayerLocationDecision(PlayerPawn, Reason);
		LogInterceptDecision(Decision, Observation, CurrentTime);
		DrawInterceptDebug(PlayerPawn, Observation, Decision, GetEnemyInterceptRuntimeModeName());
		return Decision;
	}

	const FVector CandidateGoal = ComputeGoalForInterceptMode(PlayerPawn, Mode);
	FVector ValidatedGoal = FVector::ZeroVector;
	FString ValidationReason;
	if (!TryValidateInterceptGoal(CandidateGoal, ValidatedGoal, ValidationReason))
	{
		InvalidInterceptTargetCount++;
		FEnemyInterceptDecision Decision;
		Decision.Mode = EEnemyInterceptMode::CurrentLocation;
		Decision.ChosenGoal = PlayerPawn->GetActorLocation();
		Decision.PredictionTime = 0.0f;
		Decision.bWasPredicted = false;
		Decision.bWasValid = false;
		Decision.Reason = FString::Printf(TEXT("%s invalid (%s); fallback to current player location"), InterceptModeToString(Mode), *ValidationReason);
		LogInterceptDecision(Decision, Observation, CurrentTime);
		DrawInterceptDebug(PlayerPawn, Observation, Decision, GetEnemyInterceptRuntimeModeName());
		return Decision;
	}

	FEnemyInterceptDecision Decision;
	Decision.Mode = Mode;
	Decision.ChosenGoal = ValidatedGoal;
	Decision.PredictionTime = GetPredictionTimeForMode(Mode);
	Decision.bWasPredicted = FVector::DistSquared2D(CandidateGoal, PlayerPawn->GetActorLocation()) > FMath::Square(1.0f);
	Decision.bWasValid = true;
	Decision.Reason = FString::Printf(TEXT("%s; %s"), *Reason, *ValidationReason);
	LogInterceptDecision(Decision, Observation, CurrentTime);
	DrawInterceptDebug(PlayerPawn, Observation, Decision, GetEnemyInterceptRuntimeModeName());
	return Decision;
}

int32 AMyAIController::GetEnemyInterceptRuntimeMode() const
{
	return CVarEnemyInterceptRuntimeMode.GetValueOnGameThread();
}

FString AMyAIController::GetEnemyInterceptRuntimeModeName() const
{
	return EnemyInterceptRuntimeModeToString(GetEnemyInterceptRuntimeMode());
}

bool AMyAIController::ResolveInterceptModeFromRuntimeMode(int32 RuntimeMode, EEnemyInterceptMode& OutMode, FString& OutReason) const
{
	if (!TryResolveForcedModeFromRuntimeMode(RuntimeMode, OutMode))
	{
		return false;
	}

	OutReason = FString::Printf(TEXT("forced by runtime mode %s"), EnemyInterceptRuntimeModeToString(RuntimeMode));
	return true;
}

void AMyAIController::CycleEnemyInterceptMode()
{
	CycleEnemyInterceptRuntimeModeConsole();
}

void AMyAIController::SetTrainingInterceptOverride(EEnemyInterceptMode Mode)
{
	bOverrideInterceptModeForTraining = true;
	TrainingOverrideInterceptMode = Mode;
}

void AMyAIController::ClearTrainingInterceptOverride()
{
	bOverrideInterceptModeForTraining = false;
	TrainingOverrideInterceptMode = EEnemyInterceptMode::CurrentLocation;
}

bool AMyAIController::IsTrainingInterceptOverrideEnabled() const
{
	return bOverrideInterceptModeForTraining;
}

EEnemyInterceptMode AMyAIController::GetTrainingInterceptOverrideMode() const
{
	return TrainingOverrideInterceptMode;
}

void AMyAIController::SetTrainingTargetPlayer(APawn* PlayerPawn)
{
	TrainingTargetPlayer = PlayerPawn;
}

void AMyAIController::ClearTrainingTargetPlayer()
{
	TrainingTargetPlayer = nullptr;
}

FEnemyInterceptDecision AMyAIController::MakeCurrentPlayerLocationDecision(APawn* PlayerPawn, const FString& Reason) const
{
	FEnemyInterceptDecision Decision;
	Decision.Mode = EEnemyInterceptMode::CurrentLocation;
	Decision.ChosenGoal = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	Decision.PredictionTime = 0.0f;
	Decision.bWasPredicted = false;
	Decision.bWasValid = PlayerPawn != nullptr;
	Decision.Reason = Reason;
	return Decision;
}

bool AMyAIController::IsInterceptPredictionEnabled() const
{
	const int32 RuntimeMode = GetEnemyInterceptRuntimeMode();
	if (RuntimeMode == 0)
	{
		return false;
	}
	if (RuntimeMode >= 1 && RuntimeMode <= 7)
	{
		return true;
	}

	return CVarEnemyInterceptEnablePrediction.GetValueOnGameThread() != 0;
}

bool AMyAIController::LoadLearnedInterceptPolicyIfNeeded(FString& OutReason)
{
	const FString ConfiguredPath = GEnemyInterceptLearnedPolicyPath.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		OutReason = TEXT("learned policy path is empty");
		return false;
	}

	const FString ResolvedPath = ResolveEnemyInterceptPolicyPath(ConfiguredPath);
	if (LearnedInterceptPolicy.IsLoaded() && LastLearnedPolicyPath == ResolvedPath)
	{
		OutReason = FString::Printf(TEXT("loaded policy: %s"), *LearnedInterceptPolicy.GetModelSummary());
		return true;
	}

	FString LoadError;
	FEnemyInterceptTreePolicy NewPolicy;
	if (!NewPolicy.LoadFromFile(ResolvedPath, LoadError))
	{
		const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		if (CVarEnemyInterceptLearnedPolicyDebug.GetValueOnGameThread() != 0
			&& CurrentTime - LastLearnedPolicyLoadWarningTime >= 2.0f)
		{
			LastLearnedPolicyLoadWarningTime = CurrentTime;
			UE_LOG(LogSoul_and_dungeon, Warning,
				TEXT("EnemyIntercept learned policy load failed: %s (%s)"),
				*LoadError,
				*ResolvedPath);
		}

		OutReason = LoadError;
		return false;
	}

	LearnedInterceptPolicy = MoveTemp(NewPolicy);
	LastLearnedPolicyPath = ResolvedPath;
	OutReason = FString::Printf(TEXT("loaded policy: %s"), *LearnedInterceptPolicy.GetModelSummary());

	if (CVarEnemyInterceptLearnedPolicyDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogSoul_and_dungeon, Display,
			TEXT("EnemyIntercept learned policy loaded: %s from %s"),
			*LearnedInterceptPolicy.GetModelSummary(),
			*ResolvedPath);
	}

	return true;
}

EEnemyInterceptMode AMyAIController::ChooseLearnedInterceptModeOrFallback(const FEnemyInterceptObservation& Observation, FString& OutReason)
{
	FString LoadReason;
	if (!LoadLearnedInterceptPolicyIfNeeded(LoadReason))
	{
		OutReason = FString::Printf(TEXT("learned policy fallback (%s); deterministic policy"), *LoadReason);
		return ChooseDeterministicInterceptMode(Observation);
	}

	const FEnemyInterceptPolicyResult PolicyResult = LearnedInterceptPolicy.ChooseMode(Observation);
	if (!PolicyResult.bSuccess)
	{
		OutReason = FString::Printf(TEXT("learned policy fallback (%s); deterministic policy"), *PolicyResult.Reason);
		return ChooseDeterministicInterceptMode(Observation);
	}

	OutReason = PolicyResult.Reason;
	return PolicyResult.Mode;
}

bool AMyAIController::IsInterceptDebugLoggingEnabled() const
{
	return CVarEnemyInterceptDebug.GetValueOnGameThread() != 0;
}

void AMyAIController::LogInterceptDecision(const FEnemyInterceptDecision& Decision, const FEnemyInterceptObservation& Observation, float CurrentTime)
{
	if (!IsInterceptDebugLoggingEnabled())
	{
		return;
	}

	const float LogInterval = FMath::Max(0.1f, CVarEnemyInterceptDebugLogInterval.GetValueOnGameThread());
	const bool bModeChanged = !bHasLoggedInterceptMode || LastLoggedInterceptMode != Decision.Mode;
	if (!bModeChanged && CurrentTime - LastInterceptDebugLogTime < LogInterval)
	{
		return;
	}

	LastInterceptDebugLogTime = CurrentTime;
	LastLoggedInterceptMode = Decision.Mode;
	bHasLoggedInterceptMode = true;

	const float GoalOffset2D = FVector::Dist2D(Observation.PlayerLocation, Decision.ChosenGoal);
	UE_LOG(LogSoul_and_dungeon, Display,
		TEXT("EnemyIntercept: mode=%s predicted=%s valid=%s player=(%.1f, %.1f, %.1f) goal=(%.1f, %.1f, %.1f) goal_offset_2d=%.1f prediction_time=%.2f dist=%.1f player_speed=%.1f los=%s dot=%.2f reason=%s"),
		InterceptModeToString(Decision.Mode),
		Decision.bWasPredicted ? TEXT("true") : TEXT("false"),
		Decision.bWasValid ? TEXT("true") : TEXT("false"),
		Observation.PlayerLocation.X,
		Observation.PlayerLocation.Y,
		Observation.PlayerLocation.Z,
		Decision.ChosenGoal.X,
		Decision.ChosenGoal.Y,
		Decision.ChosenGoal.Z,
		GoalOffset2D,
		Decision.PredictionTime,
		Observation.DistanceToPlayer,
		Observation.PlayerSpeed,
		Observation.bHasLineOfSight ? TEXT("true") : TEXT("false"),
		Observation.DotPlayerMoveWithEnemyDirection,
		*Decision.Reason);
}

void AMyAIController::DrawInterceptDebug(APawn* PlayerPawn, const FEnemyInterceptObservation& Observation, const FEnemyInterceptDecision& Decision, const FString& SourceText) const
{
	if (CVarEnemyInterceptVisualDebug.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	APawn* AIPawn = GetPawn();
	if (!World || !PlayerPawn || !AIPawn)
	{
		return;
	}

	const FVector PlayerMarker = Observation.PlayerLocation + FVector(0.0f, 0.0f, 55.0f);
	const FVector GoalMarker = Decision.ChosenGoal + FVector(0.0f, 0.0f, 65.0f);
	const FVector EnemyMarker = AIPawn->GetActorLocation() + FVector(0.0f, 0.0f, 90.0f);
	const float Duration = 0.08f;
	const bool bCompareWithCurrent = CVarEnemyInterceptCompareWithCurrent.GetValueOnGameThread() != 0;

	DrawDebugSphere(World, PlayerMarker, 28.0f, 12, FColor::Cyan, false, Duration, 0, 2.0f);

	FColor GoalColor = FColor::Green;
	if (!Decision.bWasValid)
	{
		GoalColor = FColor::Red;
	}
	else if (Decision.Reason.Contains(TEXT("learned"), ESearchCase::IgnoreCase))
	{
		GoalColor = FColor::Purple;
	}
	else if (Decision.Reason.Contains(TEXT("forced"), ESearchCase::IgnoreCase))
	{
		GoalColor = FColor::Yellow;
	}
	else if (!Decision.bWasPredicted)
	{
		GoalColor = FColor::Orange;
	}

	if (Decision.bWasPredicted || bCompareWithCurrent || Decision.Mode != EEnemyInterceptMode::CurrentLocation)
	{
		DrawDebugSphere(World, GoalMarker, 36.0f, 16, GoalColor, false, Duration, 0, 3.0f);
		DrawDebugLine(World, PlayerMarker, GoalMarker, GoalColor, false, Duration, 0, 2.0f);
	}

	if (bCompareWithCurrent)
	{
		DrawDebugLine(World, EnemyMarker, PlayerMarker, FColor::Blue, false, Duration, 0, 1.5f);
		DrawDebugLine(World, EnemyMarker, GoalMarker, GoalColor, false, Duration, 0, 2.0f);
		if (FVector::DistSquared2D(PlayerMarker, GoalMarker) > FMath::Square(10.0f))
		{
			DrawDebugString(World, PlayerMarker + FVector(0.0f, 0.0f, 35.0f), TEXT("Current"), nullptr, FColor::Cyan, Duration);
			DrawDebugString(World, GoalMarker + FVector(0.0f, 0.0f, 35.0f), TEXT("Predicted"), nullptr, GoalColor, Duration);
		}
	}

	const FString DebugLabel = FString::Printf(
		TEXT("Intercept: %s %s %s"),
		*SourceText,
		InterceptModeToString(Decision.Mode),
		Decision.bWasValid ? TEXT("valid") : TEXT("fallback"));
	DrawDebugString(World, EnemyMarker + FVector(0.0f, 0.0f, 95.0f), DebugLabel, nullptr, GoalColor, Duration);
}



void AMyAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const bool bUsingTrainingTarget = IsValid(TrainingTargetPlayer);
	APawn* Player = bUsingTrainingTarget ? TrainingTargetPlayer.Get() : UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	APawn* AI = GetPawn();
	if (!Player || !AI)
	{
		return;
	}

	if (!bUsingTrainingTarget)
	{
		if (ALevelManager* LevelManager = ALevelManager::GetActiveLevelManager(this))
		{
			if (LevelManager->IsObjectiveComplete())
			{
				if (bIsCurrentlyAttacking)
				{
					bIsCurrentlyAttacking = false;
					if (ACharacter* AICharacter = Cast<ACharacter>(AI))
					{
						if (UAnimInstance* AnimInstance = AICharacter->GetMesh()->GetAnimInstance())
						{
							SetAttackAnimationState(AnimInstance, false);
						}
					}
				}

				StopMovement();
				ResetAStarNavigation();
				return;
			}
		}

		if (ASoul_and_dungeonCharacter* PlayerChar = Cast<ASoul_and_dungeonCharacter>(Player))
		{
			if (PlayerChar->bIsDead)
			{
				if (bIsCurrentlyAttacking)
				{
					bIsCurrentlyAttacking = false;
					if (ACharacter* AICharacter = Cast<ACharacter>(AI))
					{
						if (UAnimInstance* AnimInstance = AICharacter->GetMesh()->GetAnimInstance())
						{
							SetAttackAnimationState(AnimInstance, bIsCurrentlyAttacking);
						}
					}
				}
				return; // Do nothing if player is dead
			}
		}
	}

	ACharacter* AICharacter = Cast<ACharacter>(AI);
	if (!AICharacter)
	{
		return;
	}


	UAnimInstance* AnimInstance = AICharacter->GetMesh()->GetAnimInstance();
	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// Calculate attack state with hysteresis to prevent animation resetting when the player moves slightly
	const float AttackDistance = bIsCurrentlyAttacking ? (StopDistance + AttackHysteresis) : StopDistance;
	bool bAttackRequested = HasAttackReach(AI, Player, AttackDistance);
	
	// Ensure we don't exit an attack too quickly
	if (bIsCurrentlyAttacking && (CurrentTime - LastAttackStartTime) < MinAttackDuration)
	{
		bAttackRequested = true;
	}

	// Check if we are currently reacting to a hit (non-montage overlay)
	bool bIsReacting = false;
	if (ASoul_and_dungeonCharacter* SoulChar = Cast<ASoul_and_dungeonCharacter>(AI))
	{
		bIsReacting = SoulChar->bHitReactionOverlay;
	}

	if (bAttackRequested && !bIsCurrentlyAttacking)
	{
		bIsCurrentlyAttacking = true;
		LastAttackStartTime = CurrentTime;
	}
	else if (!bAttackRequested && bIsCurrentlyAttacking)
	{
		bIsCurrentlyAttacking = false;
	}

	if (bIsCurrentlyAttacking || bIsReacting)
	{
		StopMovement();
		ResetAStarNavigation();
		UpdateNavigationMetrics(AI, Player, AI->GetActorLocation(), DeltaTime, false);

		const FVector Direction = Player->GetActorLocation() - AI->GetActorLocation();
		const FRotator LookRotation = Direction.Rotation();
		AI->SetActorRotation(FRotator(0.0f, LookRotation.Yaw, 0.0f));
	}
	else
	{
		UpdateAStarNavigation(AI, Player, CurrentTime, DeltaTime);
	}

	UpdateSecondarySearchDebug(AI, Player, CurrentTime);
	SetAttackAnimationState(AnimInstance, bIsCurrentlyAttacking);

	// Velocity-Based Deceleration: Slow down as we approach the target to prevent jitter and overshooting
	if (ACharacter* Char = Cast<ACharacter>(AI))
	{
		float TargetSpeed = 600.0f; // Default base speed
		const float DistToTarget = FVector::Dist(AI->GetActorLocation(), Player->GetActorLocation());

		const bool bHasClearRouteToPlayer = HasClearNavigationSegment(AI->GetActorLocation(), Player->GetActorLocation());
		if (bHasClearRouteToPlayer && DistToTarget < 500.0f)
		{
			// Linearly scale speed down to 200.0f as we get closer to the StopDistance
			const float Alpha = FMath::Clamp((DistToTarget - StopDistance) / (500.0f - StopDistance), 0.0f, 1.0f);
			TargetSpeed = FMath::Lerp(200.0f, 600.0f, Alpha);
		}

		if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
		{
			MoveComp->MaxWalkSpeed = TargetSpeed;
		}
	}
}


void AMyAIController::UpdateAStarNavigation(APawn* AI, APawn* Player, float CurrentTime, float DeltaTime)
{
	if (!AI || !Player)
	{
		return;
	}

	FSecondarySearchSettings AStarSettings = BuildSecondarySearchSettings();
	AStarSettings.CellSize = FMath::Clamp(AStarSettings.CellSize, 40.0f, 80.0f);
	AStarSettings.PathPointReachRadius = FMath::Max(45.0f, AStarSettings.CellSize * 0.75f);
	const float ProjectionRadius = FMath::Max(100.0f, AStarSettings.CellSize * 1.75f);
	AStarSettings.ProjectionExtent = FVector(ProjectionRadius, ProjectionRadius, 650.0f);
	const float DistanceToPlayer = FVector::Dist(AI->GetActorLocation(), Player->GetActorLocation());
	AStarSettings.MaxSearchDistance = FMath::Max(AStarSettings.MaxSearchDistance, DistanceToPlayer * 3.0f + 2000.0f);
	AStarSettings.MaxExpandedNodes = FMath::Max(
		AStarSettings.MaxExpandedNodes,
		FMath::Clamp(FMath::CeilToInt(FMath::Square(DistanceToPlayer / AStarSettings.CellSize) * 0.1f), 1500, 8000));

	const FEnemyInterceptDecision InterceptDecision = ChooseSmartNavigationGoal(Player);
	const FVector GoalLocation = InterceptDecision.ChosenGoal;
	const bool bIsStuck = StuckSeconds > 1.2f;
	const bool bShouldReplan = bIsStuck || ShouldReplanAStarPath(AI->GetActorLocation(), GoalLocation, CurrentTime, AStarSettings);
	if (bShouldReplan)
	{
		LastAStarReplanTime = CurrentTime;
		if (bIsStuck)
		{
			// Give stuck AI more search capacity to find a way out
			AStarSettings.MaxExpandedNodes *= 2;
			StuckSeconds = 0.0f; // Reset after triggering replan
		}
		BuildAStarPath(AI, GoalLocation, AStarSettings);
	}

	const EEnemyNavigationMode NavigationMode = GetNavigationMode();
	TArray<FVector>& PathToFollow = NavigationMode == EEnemyNavigationMode::AStarOnly ? ActiveAStarPath : ActiveSmoothedPath;
	int32& WaypointIndex = NavigationMode == EEnemyNavigationMode::AStarOnly ? ActiveAStarWaypointIndex : ActiveSmoothedWaypointIndex;
	const bool bFollowingPath = IsFollowingAPath();

	if (PathToFollow.IsValidIndex(WaypointIndex) && !HasClearNavigationSegment(AI->GetActorLocation(), PathToFollow[WaypointIndex]))
	{
		ResetAStarNavigation();
		LastAStarReplanTime = CurrentTime;
		BuildAStarPath(AI, GoalLocation, AStarSettings);
	}

	bool bUsedFallback = false;
	FVector PathTarget = FVector::ZeroVector;
	if (PathToFollow.IsValidIndex(WaypointIndex))
	{
		PathTarget = CalculatePathFollowTarget(AI->GetActorLocation(), PathToFollow, WaypointIndex, AStarSettings);
	}

	if (!PathTarget.IsNearlyZero())
	{
		if (TryDirectCommandletPathFollow(AI, PathTarget, DeltaTime, AStarSettings.PathPointReachRadius * 0.45f))
		{
			LastIssuedMoveTarget = PathTarget;
			LastActivePathTarget = PathTarget;
			bHasIssuedMoveTarget = true;
			bLastIssuedMoveWasFallback = false;
			LastMoveIssueTime = CurrentTime;
			UpdateNavigationMetrics(AI, Player, PathTarget, DeltaTime, false);
			return;
		}

		const float TargetChangeThreshold = FMath::Max(20.0f, AStarSettings.PathPointReachRadius * 0.35f);
		const bool bTargetChanged = !bHasIssuedMoveTarget ||
			FVector::DistSquared2D(LastIssuedMoveTarget, PathTarget) > FMath::Square(TargetChangeThreshold);
		const bool bMoveRetryExpired = (CurrentTime - LastMoveIssueTime) >= AStarReplanInterval;
		const bool bShouldIssueMove = !bFollowingPath || bTargetChanged || bLastIssuedMoveWasFallback || bMoveRetryExpired;
		if (bShouldIssueMove)
		{
			if (TryMoveAlongVerifiedPath(PathTarget, AStarSettings.PathPointReachRadius * 0.45f))
			{
				LastIssuedMoveTarget = PathTarget;
				LastActivePathTarget = PathTarget;
				bHasIssuedMoveTarget = true;
				bLastIssuedMoveWasFallback = false;
				LastMoveIssueTime = CurrentTime;
			}
			else
			{
				MoveToActor(Player, StopDistance * 0.5f, false, true, true, nullptr, false);
				LastIssuedMoveTarget = Player->GetActorLocation();
				LastActivePathTarget = Player->GetActorLocation();
				bHasIssuedMoveTarget = true;
				bLastIssuedMoveWasFallback = true;
				LastMoveIssueTime = CurrentTime;
				bUsedFallback = true;
			}
		}
		else
		{
			LastActivePathTarget = PathTarget;
		}
		UpdateNavigationMetrics(AI, Player, PathTarget, DeltaTime, bUsedFallback);
		return;
	}

	bUsedFallback = true;
	if (!bFollowingPath || !bLastIssuedMoveWasFallback || (CurrentTime - LastMoveIssueTime) >= AStarReplanInterval)
	{
		MoveToActor(Player, StopDistance * 0.5f, false, true, true, nullptr, false);
		TryDirectCommandletPathFollow(AI, Player->GetActorLocation(), DeltaTime, StopDistance * 0.5f);
		LastIssuedMoveTarget = Player->GetActorLocation();
		LastActivePathTarget = Player->GetActorLocation();
		bHasIssuedMoveTarget = true;
		bLastIssuedMoveWasFallback = true;
		LastMoveIssueTime = CurrentTime;
	}
	UpdateNavigationMetrics(AI, Player, Player->GetActorLocation(), DeltaTime, bUsedFallback);
}



void AMyAIController::ResetAStarNavigation()
{
	ActiveAStarPath.Reset();
	ActiveSmoothedPath.Reset();
	ActiveAStarWaypointIndex = 0;
	ActiveSmoothedWaypointIndex = 0;
	LastAStarGoal = FVector::ZeroVector;
	LastAStarReplanTime = -1000000.0f;
	LastPathProgress = 0.0f;
	LastIssuedMoveTarget = FVector::ZeroVector;
	LastActivePathTarget = FVector::ZeroVector;
	LastMoveIssueTime = -1000000.0f;
	bHasIssuedMoveTarget = false;
	bLastIssuedMoveWasFallback = false;
}

bool AMyAIController::ShouldReplanAStarPath(
	const FVector& AILocation,
	const FVector& GoalLocation,
	float CurrentTime,
	const FSecondarySearchSettings& Settings) const
{
	if (ActiveAStarPath.Num() == 0 || !ActiveAStarPath.IsValidIndex(ActiveAStarWaypointIndex))
	{
		return (CurrentTime - LastAStarReplanTime) >= AStarReplanInterval;
	}

	if ((CurrentTime - LastAStarReplanTime) < AStarReplanInterval)
	{
		return false;
	}

	const float GoalMoveThreshold = FMath::Max(Settings.CellSize * 2.0f, StopDistance * 0.5f);
	if (FVector::DistSquared2D(GoalLocation, LastAStarGoal) > FMath::Square(GoalMoveThreshold))
	{
		return true;
	}

	const float OffPathThreshold = Settings.CellSize * 3.0f;
	return FVector::DistSquared2D(AILocation, ActiveAStarPath[ActiveAStarWaypointIndex]) > FMath::Square(OffPathThreshold);
}

bool AMyAIController::BuildAStarPath(APawn* AI, const FVector& GoalLocation, const FSecondarySearchSettings& Settings)
{
	AStarReplanCount++;
	if (!AI)
	{
		AStarPathFailureCount++;
		return false;
	}

	const FSecondarySearchResult Result = FSecondarySearchSolver::FindPath(
		GetWorld(),
		AI->GetActorLocation(),
		GoalLocation,
		ESecondarySearchMode::AStar,
		Settings);

	if (!Result.bSuccess || Result.Path.Num() < 2)
	{
		AStarPathFailureCount++;
		return false;
	}

	ActiveAStarPath = Result.Path;
	ActiveAStarWaypointIndex = 1;
	ActiveSmoothedWaypointIndex = 1;
	LastAStarGoal = GoalLocation;

	if (!BuildSmoothedPath(ActiveAStarPath, Settings, ActiveSmoothedPath))
	{
		ActiveSmoothedPath = ActiveAStarPath;
	}

	return true;
}

bool AMyAIController::BuildSmoothedPath(const TArray<FVector>& RawPath, const FSecondarySearchSettings& Settings, TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	if (RawPath.Num() < 2)
	{
		return false;
	}

	TArray<FVector> ShortcutPath;
	int32 SourceIndex = 0;
	ShortcutPath.Add(RawPath[SourceIndex]);
	while (SourceIndex < RawPath.Num() - 1)
	{
		int32 BestIndex = SourceIndex + 1;
		for (int32 CandidateIndex = RawPath.Num() - 1; CandidateIndex > SourceIndex + 1; --CandidateIndex)
		{
			if (HasClearNavigationSegment(RawPath[SourceIndex], RawPath[CandidateIndex]))
			{
				BestIndex = CandidateIndex;
				break;
			}
		}

		ShortcutPath.Add(RawPath[BestIndex]);
		SourceIndex = BestIndex;
	}

	if (ShortcutPath.Num() < 3)
	{
		OutPath = ShortcutPath;
		return true;
	}

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!World || !NavSystem)
	{
		OutPath = ShortcutPath;
		return true;
	}

	TArray<FVector> CurvedPath;
	CurvedPath.Add(ShortcutPath[0]);
	for (int32 Index = 0; Index < ShortcutPath.Num() - 1; ++Index)
	{
		const FVector& P0 = ShortcutPath[FMath::Max(Index - 1, 0)];
		const FVector& P1 = ShortcutPath[Index];
		const FVector& P2 = ShortcutPath[Index + 1];
		const FVector& P3 = ShortcutPath[FMath::Min(Index + 2, ShortcutPath.Num() - 1)];
		const int32 SampleCount = FMath::Clamp(FMath::CeilToInt(FVector::Dist2D(P1, P2) / FMath::Max(Settings.CellSize * 0.5f, 1.0f)), 2, 8);

		for (int32 SampleIndex = 1; SampleIndex <= SampleCount; ++SampleIndex)
		{
			const float T = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
			const FVector Candidate = CatmullRomPoint(P0, P1, P2, P3, T);
			FNavLocation ProjectedLocation;
			if (!NavSystem->ProjectPointToNavigation(Candidate, ProjectedLocation, Settings.ProjectionExtent))
			{
				OutPath = ShortcutPath;
				return true;
			}

			if (!HasClearNavigationSegment(CurvedPath.Last(), ProjectedLocation.Location))
			{
				OutPath = ShortcutPath;
				return true;
			}

			CurvedPath.Add(ProjectedLocation.Location);
		}
	}

	OutPath = CurvedPath.Num() >= 2 ? CurvedPath : ShortcutPath;
	return true;
}

bool AMyAIController::HasClearNavigationSegment(const FVector& From, const FVector& To) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	const FVector Direction = (To - From).GetSafeNormal2D();
	if (Direction.IsNearlyZero()) return true;

	if (FMath::Abs(From.Z - To.Z) > 55.0f) return false;
	if (HasBlockingObstacleOnSegment(From, To)) return false;

	return true;
}

bool AMyAIController::HasBlockingObstacleOnSegment(const FVector& From, const FVector& To) const
{
	UWorld* World = GetWorld();
	if (!World || FVector::DistSquared2D(From, To) <= FMath::Square(1.0f))
	{
		return false;
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyNavigationObstacleSweep), false);
	if (const APawn* AI = GetPawn())
	{
		QueryParams.AddIgnoredActor(AI);
	}

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(34.0f);
	const FVector SweepOffset(0.0f, 0.0f, 55.0f);
	FHitResult Hit;
	return World->SweepSingleByObjectType(
		Hit,
		From + SweepOffset,
		To + SweepOffset,
		FQuat::Identity,
		ObjectQueryParams,
		SweepShape,
		QueryParams);
}

bool AMyAIController::HasAttackReach(APawn* AI, APawn* Player, float AttackDistance) const
{
	if (!AI || !Player)
	{
		return false;
	}

	const FVector AILocation = AI->GetActorLocation();
	const FVector PlayerLocation = Player->GetActorLocation();
	if (FVector::Dist2D(AILocation, PlayerLocation) > AttackDistance)
	{
		return false;
	}

	if (FMath::Abs(AILocation.Z - PlayerLocation.Z) > 150.0f)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult VisibilityHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyAttackReach), false);
	QueryParams.AddIgnoredActor(AI);
	QueryParams.AddIgnoredActor(Player);
	const FVector TraceStart = AILocation + FVector(0.0f, 0.0f, 45.0f);
	const FVector TraceEnd = PlayerLocation + FVector(0.0f, 0.0f, 45.0f);
	if (World->LineTraceSingleByChannel(VisibilityHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return false;
	}

	return true;
}

bool AMyAIController::TryMoveAlongVerifiedPath(const FVector& PathTarget, float AcceptanceRadius)
{
	const APawn* AI = GetPawn();
	if (!AI || !HasClearNavigationSegment(AI->GetActorLocation(), PathTarget))
	{
		return false;
	}

	// The secondary solver already chose a walkable, obstacle-clear segment.
	// Keep this as a direct follow request so Recast does not reproject the custom waypoint.
	const EPathFollowingRequestResult::Type MoveResult = MoveToLocation(
		PathTarget,
		AcceptanceRadius,
		false,
		true,
		false,
		false,
		nullptr,
		false);
	return MoveResult != EPathFollowingRequestResult::Failed;
}

bool AMyAIController::TryDirectCommandletPathFollow(APawn* AI, const FVector& PathTarget, float DeltaTime, float AcceptanceRadius)
{
	if (!IsRunningCommandlet() || !AI || PathTarget.IsNearlyZero())
	{
		return false;
	}

	const FVector CurrentLocation = AI->GetActorLocation();
	const FVector ToTarget = PathTarget - CurrentLocation;
	const float Distance = ToTarget.Size2D();
	if (Distance <= FMath::Max(1.0f, AcceptanceRadius))
	{
		return true;
	}

	FVector Direction = ToTarget.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const float MoveSpeed = 600.0f;
	const float StepDistance = FMath::Min(Distance, MoveSpeed * FMath::Max(0.0f, DeltaTime));
	const FVector NewLocation = CurrentLocation + Direction * StepDistance;
	FHitResult Hit;
	AI->SetActorRotation(Direction.Rotation());
	AI->SetActorLocation(NewLocation, true, &Hit, ETeleportType::None);
	if (FVector::DistSquared2D(AI->GetActorLocation(), CurrentLocation) <= FMath::Square(1.0f))
	{
		AI->SetActorLocation(NewLocation, false, nullptr, ETeleportType::None);
	}
	if (UPawnMovementComponent* MovementComponent = AI->GetMovementComponent())
	{
		MovementComponent->Velocity = Direction * MoveSpeed;
	}
	if (USceneComponent* Root = AI->GetRootComponent())
	{
		Root->ComponentVelocity = Direction * MoveSpeed;
	}

	return true;
}

FVector AMyAIController::CalculatePathFollowTarget(const FVector& AILocation, TArray<FVector>& Path, int32& WaypointIndex, const FSecondarySearchSettings& Settings) const
{
	// Skip points we have already reached or passed
	while (Path.IsValidIndex(WaypointIndex) && FVector::Dist(AILocation, Path[WaypointIndex]) <= Settings.PathPointReachRadius)
	{
		WaypointIndex++;
	}

	if (!Path.IsValidIndex(WaypointIndex))
	{
		return FVector::ZeroVector;
	}

	int32 TargetIndex = WaypointIndex;
	float AccumulatedDist = 0.0f;
	const float LookaheadDistance = FMath::Max(SmoothedPathLookAheadDistance, Settings.CellSize * 1.5f);
	while (Path.IsValidIndex(TargetIndex + 1) && AccumulatedDist < LookaheadDistance)
	{
		AccumulatedDist += FVector::Dist(Path[TargetIndex], Path[TargetIndex + 1]);
		TargetIndex++;
	}

	while (TargetIndex > WaypointIndex && !HasClearNavigationSegment(AILocation, Path[TargetIndex]))
	{
		TargetIndex--;
	}

	return Path[TargetIndex];
}



void AMyAIController::UpdateNavigationMetrics(APawn* AI, APawn* Player, const FVector& PathTarget, float DeltaTime, bool bUsedFallback)
{
	if (!AI || !Player)
	{
		return;
	}

	const FVector AILocation = AI->GetActorLocation();
	const float MovedDistance = LastNavigationLocation.IsNearlyZero() ? BIG_NUMBER : FVector::Dist2D(AILocation, LastNavigationLocation);
	const float CurrentSpeed = AI->GetVelocity().Size2D();
	const float ExpectedStep = CurrentSpeed * FMath::Max(DeltaTime, 0.0f);
	const float MoveThreshold = FMath::Max(2.0f, ExpectedStep * 0.25f);
	StuckSeconds = MovedDistance < MoveThreshold ? StuckSeconds + FMath::Max(DeltaTime, 0.0f) : 0.0f;
	LastNavigationLocation = AILocation;

	if (bUsedFallback)
	{
		AStarFallbackCount++;
	}
}

EEnemyNavigationMode AMyAIController::GetNavigationMode() const
{
	const int32 ModeValue = CVarEnemyNavigationMode.GetValueOnGameThread();
	if (ModeValue <= 0)
	{
		return EEnemyNavigationMode::AStarOnly;
	}
	return EEnemyNavigationMode::SmoothedAStar;
}

void AMyAIController::UpdateSecondarySearchDebug(APawn* AI, APawn* Player, float CurrentTime)
{
	if (!AI || !Player)
	{
		return;
	}

	if (!FSecondarySearchDebug::IsEnabled())
	{
		if (bDebugSearchWasEnabled || BFSTask.IsActive() || UCSTask.IsActive() || AStarTask.IsActive() || bHasSearchResult)
		{
			BFSTask.Reset();
			UCSTask.Reset();
			AStarTask.Reset();
			BFSResult = FSecondarySearchResult();
			UCSResult = FSecondarySearchResult();
			AStarResult = FSecondarySearchResult();
			LastSearchResult = FSecondarySearchResult();
			DebugBaseGridNodes.Reset();
			bHasSearchResult = false;
			bDebugSearchWasEnabled = false;
			HideSecondarySearchVisualizer();
		}
		return;
	}

	bDebugSearchWasEnabled = true;
	EnsureSecondarySearchVisualizer(AI);

	FSecondarySearchSettings EffectiveSearchSettings = BuildSecondarySearchSettings();

	if (FSecondarySearchDebug::ConsumeClearRequested())
	{
		BFSTask.Reset();
		UCSTask.Reset();
		AStarTask.Reset();
		BFSResult = FSecondarySearchResult();
		UCSResult = FSecondarySearchResult();
		AStarResult = FSecondarySearchResult();
		LastSearchResult = FSecondarySearchResult();
		DebugBaseGridNodes.Reset();
		LastSearchFailureReason.Empty();
		bHasSearchResult = false;
		ActiveSecondarySearchSettings = EffectiveSearchSettings;
		LastVisualizerUpdateTime = -1000000.0f;
		LastBaseGridMaxNodes = -1;

		if (IsValid(SearchVisualizer))
		{
			SearchVisualizer->ClearVisualization();
		}
		return;
	}

	if (ShouldRebuildDebugBaseGrid(AI->GetActorLocation(), EffectiveSearchSettings))
	{
		RebuildDebugBaseGrid(AI->GetActorLocation(), EffectiveSearchSettings);
	}

	const float DistToPlayer = FVector::Dist(AI->GetActorLocation(), Player->GetActorLocation());
	EffectiveSearchSettings.MaxSearchDistance = FMath::Max(EffectiveSearchSettings.MaxSearchDistance, DistToPlayer * 3.0f + 2000.0f);
	const int32 RequiredNodes = FMath::Clamp(FMath::CeilToInt(FMath::Square(DistToPlayer / EffectiveSearchSettings.CellSize) * 0.1f), 10000, 100000);
	EffectiveSearchSettings.MaxExpandedNodes = FMath::Max(EffectiveSearchSettings.MaxExpandedNodes, RequiredNodes);

	FVector SearchGoalLocation = Player->GetActorLocation();

	const ESecondarySearchMode SearchMode = FSecondarySearchDebug::GetMode();
	if (!BFSTask.IsActive() && !UCSTask.IsActive() && !AStarTask.IsActive() && ShouldRefreshSearchDebug(SearchGoalLocation, CurrentTime, SearchMode, EffectiveSearchSettings))
	{
		LastSearchMode = SearchMode;
		LastSearchGoal = SearchGoalLocation;
		LastSearchTime = CurrentTime;
		LastDebugRevision = FSecondarySearchDebug::GetRevision();
		ActiveSecondarySearchSettings = EffectiveSearchSettings;

		BFSTask.Start(GetWorld(), AI->GetActorLocation(), SearchGoalLocation, ESecondarySearchMode::BFS, ActiveSecondarySearchSettings);
		UCSTask.Start(GetWorld(), AI->GetActorLocation(), SearchGoalLocation, ESecondarySearchMode::UCS, ActiveSecondarySearchSettings);
		AStarTask.Start(GetWorld(), AI->GetActorLocation(), SearchGoalLocation, ESecondarySearchMode::AStar, ActiveSecondarySearchSettings);
	}

	const FSecondarySearchSettings& StepSettings = ActiveSecondarySearchSettings;
	BFSTask.Step(GetWorld(), StepSettings, StepSettings.MaxDebugSearchStepsPerTick);
	UCSTask.Step(GetWorld(), StepSettings, StepSettings.MaxDebugSearchStepsPerTick);
	AStarTask.Step(GetWorld(), StepSettings, StepSettings.MaxDebugSearchStepsPerTick);

	BFSResult = BFSTask.BuildDebugResult();
	UCSResult = UCSTask.BuildDebugResult();
	AStarResult = AStarTask.BuildDebugResult();

	// Select the "Main" result based on current debug mode for expansion/frontier drawing
	switch (SearchMode)
	{
		case ESecondarySearchMode::BFS: LastSearchResult = BFSResult; break;
		case ESecondarySearchMode::UCS: LastSearchResult = UCSResult; break;
		case ESecondarySearchMode::AStar: LastSearchResult = AStarResult; break;
	}

	// Always populate specific algorithm paths
	LastSearchResult.BFSPath = BFSResult.Path;
	LastSearchResult.UCSPath = UCSResult.Path;
	LastSearchResult.AStarPath = AStarResult.Path;

	// Populate metrics
	LastSearchResult.BFSCount = BFSResult.ExpandedCount;
	LastSearchResult.UCSCount = UCSResult.ExpandedCount;
	LastSearchResult.AStarCount = AStarResult.ExpandedCount;
	LastSearchResult.BFSMs = BFSResult.ElapsedMs;
	LastSearchResult.UCSMs = UCSResult.ElapsedMs;
	LastSearchResult.AStarMs = AStarResult.ElapsedMs;

	auto CalcCost = [](const TArray<FVector>& Path) -> float {
		float Total = 0.0f;
		for (int32 i = 1; i < Path.Num(); ++i) Total += FVector::Dist(Path[i-1], Path[i]);
		return Total;
	};
	LastSearchResult.BFSCost = CalcCost(BFSResult.Path);
	LastSearchResult.UCSCost = CalcCost(UCSResult.Path);
	LastSearchResult.AStarCost = CalcCost(AStarResult.Path);

	const EEnemyNavigationMode NavigationMode = GetNavigationMode();
	const TArray<FVector>& ActiveMovementPath = NavigationMode == EEnemyNavigationMode::AStarOnly ? ActiveAStarPath : ActiveSmoothedPath;
	const int32 ActiveMovementWaypointIndex = NavigationMode == EEnemyNavigationMode::AStarOnly ? ActiveAStarWaypointIndex : ActiveSmoothedWaypointIndex;
	auto BuildRemainingMovementPath = [&](const TArray<FVector>& SourcePath, int32 SourceWaypointIndex) -> TArray<FVector>
	{
		TArray<FVector> OutPath;
		OutPath.Reserve(FMath::Max(2, SourcePath.Num() - SourceWaypointIndex + 1));
		OutPath.Add(AI->GetActorLocation());

		if (SourcePath.IsValidIndex(SourceWaypointIndex))
		{
			for (int32 Index = SourceWaypointIndex; Index < SourcePath.Num(); ++Index)
			{
				const FVector& Point = SourcePath[Index];
				if (!OutPath.Last().Equals(Point, 1.0f))
				{
					OutPath.Add(Point);
				}
			}
		}

		if (OutPath.Num() < 2)
		{
			OutPath.Add(Player->GetActorLocation());
		}

		return OutPath;
	};
	const TArray<FVector> MovementPath = BuildRemainingMovementPath(ActiveMovementPath, ActiveMovementWaypointIndex);
	LastSearchResult.Path = MovementPath;
	LastSearchResult.PreviewPath = MovementPath;

	LastSearchResult.SampledNodes = DebugBaseGridNodes;
	LastSearchResult.StartLocation = AI->GetActorLocation();
	LastSearchResult.GoalLocation = Player->GetActorLocation();
	LastSearchResult.CurrentTarget = LastActivePathTarget.IsNearlyZero() ? Player->GetActorLocation() : LastActivePathTarget;
	LastSearchResult.Mode = SearchMode;

	// Determine if the selected debug search task is finished
	bool bSelectedTaskFinished = false;
	switch (SearchMode)
	{
		case ESecondarySearchMode::BFS: bSelectedTaskFinished = BFSTask.HasResult(); break;
		case ESecondarySearchMode::UCS: bSelectedTaskFinished = UCSTask.HasResult(); break;
		case ESecondarySearchMode::AStar: bSelectedTaskFinished = AStarTask.HasResult(); break;
	}

	if (bSelectedTaskFinished)
	{
		bHasSearchResult = true;

		// Suppress failure logs
		const bool bNeuralSteeringActive = false;

		if (LastSearchResult.bSuccess || bNeuralSteeringActive)
		{
			LastSearchFailureReason.Empty();
		}
		else if (LastSearchFailureReason != LastSearchResult.FailureReason || (CurrentTime - LastFailureLogTime) > 2.0f)
		{
			LastSearchFailureReason = LastSearchResult.FailureReason;
			LastFailureLogTime = CurrentTime;
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("Secondary search failed: %s"), *LastSearchFailureReason);
		}
	}

	if (IsValid(SearchVisualizer) && (CurrentTime - LastVisualizerUpdateTime) >= EffectiveSearchSettings.DebugVisualizerUpdateInterval)
	{
		SearchVisualizer->UpdateVisualization(
			LastSearchResult,
			EffectiveSearchSettings,
			FSecondarySearchDebug::IsXRayEnabled(),
			FSecondarySearchDebug::GetNodeDensity(),
			FSecondarySearchDebug::GetVisualStyle(),
			FSecondarySearchDebug::GetVisualSpeed(),
			FSecondarySearchDebug::AreTrailsEnabled(),
			FSecondarySearchDebug::GetWaveSpeed(),
			FSecondarySearchDebug::GetPathHistoryCount(),
			FSecondarySearchDebug::GetNodeScale(),
			FSecondarySearchDebug::ShouldShowBaseGrid(),
			FSecondarySearchDebug::GetTargetSmoothing(),
			FSecondarySearchDebug::GetNodePulse(),
			FSecondarySearchDebug::GetNodeFadeTime(),
			FSecondarySearchDebug::GetPathFadeTime(),
			FSecondarySearchDebug::ShouldUseLastPathFallback(),
			FSecondarySearchDebug::GetVisualQuality(),
			FSecondarySearchDebug::GetGlowIntensity(),
			FSecondarySearchDebug::GetFlowBandWidth(),
			FSecondarySearchDebug::GetNodeSoftness());
		LastVisualizerUpdateTime = CurrentTime;
	}
}

FSecondarySearchSettings AMyAIController::BuildSecondarySearchSettings() const
{
	FSecondarySearchSettings Settings = SecondarySearchSettings;
	Settings.CellSize = FMath::Clamp(FSecondarySearchDebug::GetCellSize(), 20.0f, 200.0f);
	Settings.MaxSearchDistance = FMath::Clamp(FSecondarySearchDebug::GetFieldRadius(), 1000.0f, 8000.0f);
	const float ProjectionRadius = FMath::Max(80.0f, Settings.CellSize * 1.25f);
	Settings.ProjectionExtent = FVector(ProjectionRadius, ProjectionRadius, 650.0f);
	Settings.GoalAcceptanceRadius = FMath::Max(36.0f, Settings.CellSize * 0.75f);
	Settings.PathPointReachRadius = FMath::Max(22.0f, Settings.CellSize * 0.5f);
	Settings.MaxDebugDrawNodes = FSecondarySearchDebug::GetNodeDensity();
	Settings.MaxExpandedNodes = FMath::Max(Settings.MaxExpandedNodes, FMath::Min(Settings.MaxDebugDrawNodes, 5000));
	return Settings;
}

bool AMyAIController::ShouldRefreshSearchDebug(const FVector& GoalLocation, float CurrentTime, ESecondarySearchMode SearchMode, const FSecondarySearchSettings& Settings) const
{
	if (!bHasSearchResult || LastSearchMode != SearchMode || LastDebugRevision != FSecondarySearchDebug::GetRevision())
	{
		return true;
	}

	if ((CurrentTime - LastSearchTime) < SearchRefreshInterval)
	{
		return false;
	}

	const float GoalMoveThreshold = Settings.CellSize * 0.5f;
	return FVector::DistSquared2D(GoalLocation, LastSearchGoal) > FMath::Square(GoalMoveThreshold);
}

bool AMyAIController::ShouldRebuildDebugBaseGrid(const FVector& CenterLocation, const FSecondarySearchSettings& Settings) const
{
	if (!FSecondarySearchDebug::ShouldShowBaseGrid())
	{
		return false;
	}

	if (DebugBaseGridNodes.Num() == 0)
	{
		return true;
	}

	if (LastBaseGridMaxNodes != Settings.MaxDebugDrawNodes ||
		!FMath::IsNearlyEqual(LastBaseGridCellSize, Settings.CellSize) ||
		!FMath::IsNearlyEqual(LastBaseGridRadius, Settings.MaxSearchDistance))
	{
		return true;
	}

	// Periodic refresh to catch door openings / navmesh updates
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if ((CurrentTime - LastBaseGridRebuildTime) >= BaseGridRebuildInterval)
	{
		return true;
	}

	return false;
}

void AMyAIController::RebuildDebugBaseGrid(const FVector& CenterLocation, const FSecondarySearchSettings& Settings)
{
	DebugBaseGridNodes.Reset();

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!World || !NavSystem)
	{
		return;
	}

	const FBox SearchArea(
		CenterLocation - FVector(Settings.MaxSearchDistance, Settings.MaxSearchDistance, 1000.0f),
		CenterLocation + FVector(Settings.MaxSearchDistance, Settings.MaxSearchDistance, 1000.0f));

	TArray<FBox> BoundsList;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox(true);
		FBox Clipped = Bounds.Overlap(SearchArea);
		if (Clipped.IsValid)
		{
			BoundsList.Add(Clipped);
		}
	}

	if (BoundsList.Num() == 0)
	{
		BoundsList.Add(SearchArea);
	}

	int32 CandidateCellCount = 0;
	for (const FBox& Bounds : BoundsList)
	{
		const FVector Extent = Bounds.GetSize();
		CandidateCellCount += (FMath::FloorToInt(Extent.X / Settings.CellSize) + 1) *
			(FMath::FloorToInt(Extent.Y / Settings.CellSize) + 1);
	}

	const int32 SamplingStride = CandidateCellCount > Settings.MaxDebugDrawNodes
		? FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(CandidateCellCount) / static_cast<float>(Settings.MaxDebugDrawNodes))))
		: 1;
	const float SampleSpacing = Settings.CellSize * SamplingStride;

	TSet<FIntPoint> SeenCells;
	DebugBaseGridNodes.Reserve(Settings.MaxDebugDrawNodes);
	for (const FBox& Bounds : BoundsList)
	{
		for (float X = Bounds.Min.X; X <= Bounds.Max.X && DebugBaseGridNodes.Num() < Settings.MaxDebugDrawNodes; X += SampleSpacing)
		{
			for (float Y = Bounds.Min.Y; Y <= Bounds.Max.Y && DebugBaseGridNodes.Num() < Settings.MaxDebugDrawNodes; Y += SampleSpacing)
			{
				const FVector Candidate(X, Y, CenterLocation.Z);
				FNavLocation ProjectedLocation;
				if (!NavSystem->ProjectPointToNavigation(Candidate, ProjectedLocation, Settings.ProjectionExtent))
				{
					continue;
				}

				const FIntPoint Key(
					FMath::RoundToInt(ProjectedLocation.Location.X / Settings.CellSize),
					FMath::RoundToInt(ProjectedLocation.Location.Y / Settings.CellSize));
				if (SeenCells.Contains(Key))
				{
					continue;
				}

				SeenCells.Add(Key);
				DebugBaseGridNodes.Add(ProjectedLocation.Location);
			}
		}
	}

	LastBaseGridMaxNodes = Settings.MaxDebugDrawNodes;
	LastBaseGridCellSize = Settings.CellSize;
	LastBaseGridRadius = Settings.MaxSearchDistance;
	LastBaseGridCenter = CenterLocation;
	LastBaseGridRebuildTime = World->GetTimeSeconds();
}

void AMyAIController::EnsureSecondarySearchVisualizer(APawn* AI)
{
#if !UE_BUILD_SHIPPING
	if (IsValid(SearchVisualizer) || !GetWorld())
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	SearchVisualizer = GetWorld()->SpawnActor<ASecondarySearchVisualizerActor>(
		ASecondarySearchVisualizerActor::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParameters);
#endif
}

void AMyAIController::EnsureNavMeshBounds(APawn* AI, APawn* Player)
{
}

void AMyAIController::HideSecondarySearchVisualizer()
{
#if !UE_BUILD_SHIPPING
	if (IsValid(SearchVisualizer))
	{
		SearchVisualizer->ClearVisualization();
	}
#endif
}

void AMyAIController::SetAttackAnimationState(UAnimInstance* AnimInstance, bool bIsAttacking) const
{
	if (!AnimInstance)
	{
		return;
	}

	const FName VarName = "IsAttacking";
	FProperty* Prop = AnimInstance->GetClass()->FindPropertyByName(VarName);
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue_InContainer(AnimInstance, bIsAttacking);
	}
}
