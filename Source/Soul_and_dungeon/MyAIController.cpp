#include "MyAIController.h"

#include "Animation/AnimInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "SecondarySearchVisualizerActor.h"
#include "Soul_and_dungeon.h"
#include "Soul_and_dungeonCharacter.h"

AMyAIController::AMyAIController()
{
	PrimaryActorTick.bCanEverTick = true;
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

		const FVector Direction = Player->GetActorLocation() - AI->GetActorLocation();
		const FRotator LookRotation = Direction.Rotation();
		AI->SetActorRotation(FRotator(0.0f, LookRotation.Yaw, 0.0f));

		if (LastAttackStartTime == 0.0f)
		{
			LastAttackStartTime = CurrentTime;
		}

		if ((CurrentTime - LastAttackStartTime) >= AttackDelay && (CurrentTime - LastDamageTime) > DamageCooldown)
		{
			if (ASoul_and_dungeonCharacter* PlayerChar = Cast<ASoul_and_dungeonCharacter>(Player))
			{
				PlayerChar->TakeDamageSimple(10.0f);
			}
			LastDamageTime = CurrentTime;
		}
	}
	else
	{
		LastAttackStartTime = 0.0f;
		MoveToLocation(Player->GetActorLocation());
	}

	UpdateSecondarySearchDebug(AI, Player, CurrentTime);
	SetAttackAnimationState(AnimInstance, bIsAttacking);
}

void AMyAIController::UpdateSecondarySearchDebug(APawn* AI, APawn* Player, float CurrentTime)
{
	if (!AI || !Player)
	{
		return;
	}

	if (!FSecondarySearchDebug::IsEnabled())
	{
		if (bDebugSearchWasEnabled || SearchTask.IsActive() || AStarPreviewTask.IsActive() || bHasSearchResult)
		{
			SearchTask.Reset();
			AStarPreviewTask.Reset();
			LastSearchResult = FSecondarySearchResult();
			LastAStarPreviewResult = FSecondarySearchResult();
			DebugBaseGridNodes.Reset();
			bHasSearchResult = false;
			bDebugSearchWasEnabled = false;
			HideSecondarySearchVisualizer();
		}
		return;
	}

	bDebugSearchWasEnabled = true;
	EnsureSecondarySearchVisualizer(AI);

	const FSecondarySearchSettings EffectiveSearchSettings = BuildSecondarySearchSettings();

	if (FSecondarySearchDebug::ConsumeClearRequested())
	{
		SearchTask.Reset();
		AStarPreviewTask.Reset();
		LastSearchResult = FSecondarySearchResult();
		LastAStarPreviewResult = FSecondarySearchResult();
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

	FVector SearchGoalLocation = Player->GetActorLocation();
	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation ProjectedGoal;
		if (NavSystem->ProjectPointToNavigation(Player->GetActorLocation(), ProjectedGoal, EffectiveSearchSettings.ProjectionExtent))
		{
			SearchGoalLocation = ProjectedGoal.Location;
		}
	}

	const ESecondarySearchMode SearchMode = FSecondarySearchDebug::GetMode();
	if (!SearchTask.IsActive() && ShouldRefreshSearchDebug(SearchGoalLocation, CurrentTime, SearchMode, EffectiveSearchSettings))
	{
		LastSearchMode = SearchMode;
		LastSearchGoal = SearchGoalLocation;
		LastSearchTime = CurrentTime;
		LastDebugRevision = FSecondarySearchDebug::GetRevision();
		ActiveSecondarySearchSettings = EffectiveSearchSettings;

		SearchTask.Start(
			GetWorld(),
			AI->GetActorLocation(),
			SearchGoalLocation,
			SearchMode,
			ActiveSecondarySearchSettings);

		AStarPreviewTask.Start(
			GetWorld(),
			AI->GetActorLocation(),
			SearchGoalLocation,
			ESecondarySearchMode::AStar,
			ActiveSecondarySearchSettings);
	}

	const FSecondarySearchSettings& StepSettings = SearchTask.IsActive() ? ActiveSecondarySearchSettings : EffectiveSearchSettings;
	SearchTask.Step(GetWorld(), StepSettings, StepSettings.MaxDebugSearchStepsPerTick);
	AStarPreviewTask.Step(GetWorld(), StepSettings, FMath::Max(4, StepSettings.MaxDebugSearchStepsPerTick / 2));

	LastSearchResult = SearchTask.BuildDebugResult();
	LastAStarPreviewResult = AStarPreviewTask.BuildDebugResult();
	if (LastAStarPreviewResult.bSuccess)
	{
		LastSearchResult.PreviewPath = LastAStarPreviewResult.Path;
	}

	LastSearchResult.SampledNodes = DebugBaseGridNodes;
	LastSearchResult.CurrentTarget = SearchGoalLocation;
	LastSearchResult.Mode = SearchMode;

	if (SearchTask.HasResult())
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
	Settings.CellSize = FMath::Clamp(FSecondarySearchDebug::GetCellSize(), 30.0f, 200.0f);
	Settings.MaxSearchDistance = FMath::Clamp(FSecondarySearchDebug::GetFieldRadius(), 1000.0f, 8000.0f);
	const float ProjectionRadius = FMath::Max(80.0f, Settings.CellSize * 1.25f);
	Settings.ProjectionExtent = FVector(ProjectionRadius, ProjectionRadius, 300.0f);
	Settings.GoalAcceptanceRadius = FMath::Max(36.0f, Settings.CellSize * 0.75f);
	Settings.PathPointReachRadius = FMath::Max(22.0f, Settings.CellSize * 0.5f);
	Settings.MaxDebugDrawNodes = FSecondarySearchDebug::GetNodeDensity();
	Settings.MaxExpandedNodes = FMath::Max(Settings.MaxExpandedNodes, Settings.MaxDebugDrawNodes);
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
