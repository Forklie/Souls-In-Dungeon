#include "MyAIController.h"

#include "Animation/AnimInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "SecondarySearchVisualizerActor.h"
#include "Soul_and_dungeon.h"
#include "Soul_and_dungeonCharacter.h"

namespace
{
static TAutoConsoleVariable<int32> CVarEnemyNavigationMode(
	TEXT("sd.EnemyNavigation.Mode"),
	1,
	TEXT("Enemy navigation mode: 0=AStarOnly, 1=SmoothedAStar, 2=LearningWithAStarFallback."),
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
}

AMyAIController::AMyAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMyAIController::GetEnemyLearningObservation(FEnemyLearningObservation& OutObservation) const
{
	OutObservation = LastLearningObservation;
}

void AMyAIController::ApplyLearningSteeringInput(const FVector2D& MoveInput)
{
	LastLearningSteeringInput = MoveInput.GetClampedToMaxSize(1.0f);
	LastLearningSteeringTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

void AMyAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	APawn* AI = GetPawn();
	if (!Player || !AI)
	{
		return;
	}

	const float Distance = FVector::Dist(AI->GetActorLocation(), Player->GetActorLocation());
	ACharacter* AICharacter = Cast<ACharacter>(AI);
	if (!AICharacter)
	{
		return;
	}

	UAnimInstance* AnimInstance = AICharacter->GetMesh()->GetAnimInstance();
	const bool bIsAttacking = Distance <= StopDistance;
	const float CurrentTime = GetWorld()->GetTimeSeconds();

	if (bIsAttacking)
	{
		StopMovement();
		ResetAStarNavigation();
		UpdateNavigationMetrics(AI, Player, AI->GetActorLocation(), DeltaTime, false);

		const FVector Direction = Player->GetActorLocation() - AI->GetActorLocation();
		const FRotator LookRotation = Direction.Rotation();
		AI->SetActorRotation(FRotator(0.0f, LookRotation.Yaw, 0.0f));

		// Attack damage is now handled by UAnimNotify_EnemyAttack in the attack animation
	}
	else
	{

		UpdateAStarNavigation(AI, Player, CurrentTime, DeltaTime);
	}

	UpdateSecondarySearchDebug(AI, Player, CurrentTime);
	SetAttackAnimationState(AnimInstance, bIsAttacking);
}

void AMyAIController::UpdateAStarNavigation(APawn* AI, APawn* Player, float CurrentTime, float DeltaTime)
{
	if (!AI || !Player)
	{
		return;
	}

	FSecondarySearchSettings AStarSettings = BuildSecondarySearchSettings();
	AStarSettings.CellSize = FMath::Max(AStarSettings.CellSize, 120.0f);
	AStarSettings.PathPointReachRadius = FMath::Max(60.0f, AStarSettings.CellSize * 0.65f);
	const float ProjectionRadius = FMath::Max(120.0f, AStarSettings.CellSize * 1.25f);
	AStarSettings.ProjectionExtent = FVector(ProjectionRadius, ProjectionRadius, 300.0f);
	const float DistanceToPlayer = FVector::Dist(AI->GetActorLocation(), Player->GetActorLocation());
	AStarSettings.MaxSearchDistance = FMath::Max(AStarSettings.MaxSearchDistance, DistanceToPlayer * 3.0f + 2000.0f);
	AStarSettings.MaxExpandedNodes = FMath::Max(
		AStarSettings.MaxExpandedNodes,
		FMath::Clamp(FMath::CeilToInt(FMath::Square(DistanceToPlayer / AStarSettings.CellSize) * 0.1f), 1500, 8000));

	const FVector GoalLocation = Player->GetActorLocation();
	const bool bShouldReplan = ShouldReplanAStarPath(AI->GetActorLocation(), GoalLocation, CurrentTime, AStarSettings);
	if (bShouldReplan)
	{
		LastAStarReplanTime = CurrentTime;
		BuildAStarPath(AI, GoalLocation, AStarSettings);
	}

	const EEnemyNavigationMode NavigationMode = GetNavigationMode();
	TArray<FVector>& PathToFollow = NavigationMode == EEnemyNavigationMode::AStarOnly ? ActiveAStarPath : ActiveSmoothedPath;
	int32& WaypointIndex = NavigationMode == EEnemyNavigationMode::AStarOnly ? ActiveAStarWaypointIndex : ActiveSmoothedWaypointIndex;

	bool bUsedFallback = false;
	FVector PathTarget = FVector::ZeroVector;
	if (PathToFollow.IsValidIndex(WaypointIndex))
	{
		PathTarget = CalculatePathFollowTarget(AI->GetActorLocation(), PathToFollow, WaypointIndex, AStarSettings);
	}

	if (!PathTarget.IsNearlyZero())
	{
		if (NavigationMode == EEnemyNavigationMode::LearningWithAStarFallback &&
			(CurrentTime - LastLearningSteeringTime) <= LearningSteeringMaxAge &&
			LastLearningSteeringInput.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			const FVector SteeringTarget = AI->GetActorLocation() + FVector(LastLearningSteeringInput.X, LastLearningSteeringInput.Y, 0.0f) * LearningSteeringProjectionDistance;
			MoveToLocation(SteeringTarget, AStarSettings.PathPointReachRadius * 0.35f, false, true, false, true);
			UpdateNavigationMetrics(AI, Player, SteeringTarget, DeltaTime, false);
			return;
		}

		MoveToLocation(PathTarget, AStarSettings.PathPointReachRadius * 0.45f, false, true, false, true);
		UpdateNavigationMetrics(AI, Player, PathTarget, DeltaTime, false);
		return;
	}

	bUsedFallback = true;
	AStarFallbackCount++;
	MoveToLocation(Player->GetActorLocation(), StopDistance * 0.5f);
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
	if (!AI)
	{
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
	if (!World)
	{
		return false;
	}

	FVector HitLocation = FVector::ZeroVector;
	return !UNavigationSystemV1::NavigationRaycast(World, From, To, HitLocation);
}

FVector AMyAIController::CalculatePathFollowTarget(const FVector& AILocation, TArray<FVector>& Path, int32& WaypointIndex, const FSecondarySearchSettings& Settings) const
{
	while (Path.IsValidIndex(WaypointIndex) && FVector::Dist2D(AILocation, Path[WaypointIndex]) <= Settings.PathPointReachRadius)
	{
		WaypointIndex++;
	}

	if (!Path.IsValidIndex(WaypointIndex))
	{
		return FVector::ZeroVector;
	}

	int32 LookAheadIndex = WaypointIndex;
	float TravelDistance = FVector::Dist2D(AILocation, Path[LookAheadIndex]);
	while (Path.IsValidIndex(LookAheadIndex + 1) && TravelDistance < SmoothedPathLookAheadDistance)
	{
		TravelDistance += FVector::Dist2D(Path[LookAheadIndex], Path[LookAheadIndex + 1]);
		LookAheadIndex++;
	}

	return Path[LookAheadIndex];
}

void AMyAIController::UpdateNavigationMetrics(APawn* AI, APawn* Player, const FVector& PathTarget, float DeltaTime, bool bUsedFallback)
{
	if (!AI || !Player)
	{
		return;
	}

	const FVector AILocation = AI->GetActorLocation();
	const float MovedDistance = LastNavigationLocation.IsNearlyZero() ? BIG_NUMBER : FVector::Dist2D(AILocation, LastNavigationLocation);
	StuckSeconds = MovedDistance < 4.0f ? StuckSeconds + FMath::Max(DeltaTime, 0.0f) : 0.0f;
	LastNavigationLocation = AILocation;

	FEnemyLearningObservation Observation;
	Observation.DirectionToPlayer = (Player->GetActorLocation() - AILocation).GetSafeNormal2D();
	Observation.DirectionToPath = (PathTarget - AILocation).GetSafeNormal2D();
	Observation.Velocity = AI->GetVelocity();
	Observation.DistanceToPlayer = FVector::Dist2D(AILocation, Player->GetActorLocation());
	Observation.DistanceToPathTarget = FVector::Dist2D(AILocation, PathTarget);
	Observation.StuckSeconds = StuckSeconds;
	const int32 PathCount = ActiveSmoothedPath.Num() > 0 ? ActiveSmoothedPath.Num() : ActiveAStarPath.Num();
	const int32 PathIndex = ActiveSmoothedPath.Num() > 0 ? ActiveSmoothedWaypointIndex : ActiveAStarWaypointIndex;
	Observation.PathProgress = PathCount > 1 ? static_cast<float>(PathIndex) / static_cast<float>(PathCount - 1) : LastPathProgress;
	Observation.bHasLineOfSight = HasClearNavigationSegment(AILocation, Player->GetActorLocation());
	LastPathProgress = Observation.PathProgress;
	LastLearningObservation = Observation;

	if (bUsedFallback)
	{
		LastLearningObservation.PathProgress = 0.0f;
	}
}

EEnemyNavigationMode AMyAIController::GetNavigationMode() const
{
	const int32 ModeValue = CVarEnemyNavigationMode.GetValueOnGameThread();
	if (ModeValue <= 0)
	{
		return EEnemyNavigationMode::AStarOnly;
	}
	if (ModeValue >= 2)
	{
		return EEnemyNavigationMode::LearningWithAStarFallback;
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
	EnsureNavMeshBounds(AI, Player);

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
	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation ProjectedGoal;
		if (NavSystem->ProjectPointToNavigation(Player->GetActorLocation(), ProjectedGoal, EffectiveSearchSettings.ProjectionExtent) ||
			NavSystem->ProjectPointToNavigation(Player->GetActorLocation(), ProjectedGoal, FVector(320.0f, 320.0f, 650.0f)))
		{
			SearchGoalLocation = ProjectedGoal.Location;
		}
		else if (DebugBaseGridNodes.Num() > 0)
		{
			float BestDistanceSquared = FMath::Square(650.0f);
			bool bFoundFallbackGoal = false;
			for (const FVector& Candidate : DebugBaseGridNodes)
			{
				const float DistanceSquared = FVector::DistSquared(Player->GetActorLocation(), Candidate);
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					SearchGoalLocation = Candidate;
					bFoundFallbackGoal = true;
				}
			}

			if (!bFoundFallbackGoal)
			{
				SearchGoalLocation = LastSearchGoal.IsNearlyZero() ? Player->GetActorLocation() : LastSearchGoal;
			}
		}
	}

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

	// Use AStar as the preview path if the main mode is something else
	if (SearchMode != ESecondarySearchMode::AStar && AStarResult.bSuccess)
	{
		LastSearchResult.PreviewPath = AStarResult.Path;
	}

	LastSearchResult.SampledNodes = DebugBaseGridNodes;
	LastSearchResult.CurrentTarget = SearchGoalLocation;
	LastSearchResult.Mode = SearchMode;

	bool bAnyFinished = BFSTask.HasResult() || UCSTask.HasResult() || AStarTask.HasResult();
	if (bAnyFinished)
	{
		bHasSearchResult = true;
		if (LastSearchResult.bSuccess)
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
	Settings.ProjectionExtent = FVector(ProjectionRadius, ProjectionRadius, 300.0f);
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

	TArray<FBox> BoundsList;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox(true);
		if (Bounds.IsValid)
		{
			BoundsList.Add(Bounds);
		}
	}

	if (BoundsList.Num() == 0)
	{
		BoundsList.Add(FBox(
			CenterLocation - FVector(Settings.MaxSearchDistance, Settings.MaxSearchDistance, 200.0f),
			CenterLocation + FVector(Settings.MaxSearchDistance, Settings.MaxSearchDistance, 200.0f)));
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
	UWorld* World = GetWorld();
	if (!World || !AI || !Player) return;

	const FVector CurrentAIPos = AI->GetActorLocation();
	const FVector CurrentPlayerPos = Player->GetActorLocation();

	// Avoid excessive nav-rebuilds by checking distance thresholds
	if (FVector::DistSquared(CurrentAIPos, LastNavCheckAIPos) < 250000.0f && 
		FVector::DistSquared(CurrentPlayerPos, LastNavCheckPlayerPos) < 250000.0f)
	{
		return;
	}
	
	LastNavCheckAIPos = CurrentAIPos;
	LastNavCheckPlayerPos = CurrentPlayerPos;

	ANavMeshBoundsVolume* BestVolume = nullptr;
	float MaxVolSize = -1.0f;

	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		const float VolSize = It->GetComponentsBoundingBox(true).GetSize().Size();
		if (VolSize > MaxVolSize)
		{
			MaxVolSize = VolSize;
			BestVolume = *It;
		}
	}

	if (BestVolume)
	{
		const FBox CurrentBounds = BestVolume->GetComponentsBoundingBox(true);
		const float Margin = 2000.0f;
		
		if (!CurrentBounds.IsInside(CurrentAIPos) || !CurrentBounds.IsInside(CurrentPlayerPos))
		{
			FBox NeededBounds(0);
			NeededBounds += CurrentAIPos;
			NeededBounds += CurrentPlayerPos;
			NeededBounds = NeededBounds.ExpandBy(FVector(Margin, Margin, 1000.0f));
			
			FBox NewFullBounds = CurrentBounds + NeededBounds;
			FVector NewCenter = NewFullBounds.GetCenter();
			FVector NewExtent = NewFullBounds.GetExtent();
			
			FVector OriginalExtent = CurrentBounds.GetExtent();
			FVector OriginalScale = BestVolume->GetActorScale3D();
			
			BestVolume->SetActorLocation(NewCenter);
			
			FVector TargetScale = OriginalScale;
			if (OriginalExtent.X > 1.0f) TargetScale.X = (NewExtent.X / OriginalExtent.X) * OriginalScale.X;
			if (OriginalExtent.Y > 1.0f) TargetScale.Y = (NewExtent.Y / OriginalExtent.Y) * OriginalScale.Y;
			if (OriginalExtent.Z > 1.0f) TargetScale.Z = (NewExtent.Z / OriginalExtent.Z) * OriginalScale.Z;
			
			BestVolume->SetActorScale3D(TargetScale);
		}
	}
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
