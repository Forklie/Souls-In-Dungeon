#include "TrainingAutoPlayerComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Soul_and_dungeon.h"

namespace
{
static TAutoConsoleVariable<int32> CVarTrainingAutoPlayerEnableScripted(
	TEXT("sd.Training.AutoPlayer.EnableScripted"),
	1,
	TEXT("Enable scripted training auto-player movement. 0=disabled, 1=enabled."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTrainingAutoPlayerDebug(
	TEXT("sd.Training.AutoPlayer.Debug"),
	0,
	TEXT("Log training auto-player decisions and movement. 0=off, 1=on."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTrainingAutoPlayerMinDuration(
	TEXT("sd.Training.AutoPlayer.MinDuration"),
	0.5f,
	TEXT("Minimum scripted auto-player action duration."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTrainingAutoPlayerMaxDuration(
	TEXT("sd.Training.AutoPlayer.MaxDuration"),
	2.0f,
	TEXT("Maximum scripted auto-player action duration."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTrainingAutoPlayerSpeedScaleMin(
	TEXT("sd.Training.AutoPlayer.SpeedScaleMin"),
	0.75f,
	TEXT("Minimum scripted auto-player speed scale."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTrainingAutoPlayerSpeedScaleMax(
	TEXT("sd.Training.AutoPlayer.SpeedScaleMax"),
	1.25f,
	TEXT("Maximum scripted auto-player speed scale."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTrainingAutoPlayerRandomSeed(
	TEXT("sd.Training.AutoPlayer.RandomSeed"),
	1234,
	TEXT("Default scripted auto-player random seed."),
	ECVF_Default);

static FVector SanitizeDirection(FVector Direction)
{
	Direction.Z = 0.0f;
	return Direction.GetSafeNormal2D();
}

static FVector PerpendicularLeft(const FVector& Direction)
{
	return FVector(-Direction.Y, Direction.X, 0.0f).GetSafeNormal2D();
}

static bool TraceNearbyObstacle(UWorld* World, const APawn* Pawn, float Radius)
{
	if (!World || !Pawn)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TrainingAutoPlayerNearbyObstacle), false);
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	const FVector Origin = Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 45.0f);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	return World->SweepSingleByChannel(Hit, Origin, Origin, FQuat::Identity, ECC_Visibility, Shape, Params);
}
}

UTrainingAutoPlayerComponent::UTrainingAutoPlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UTrainingAutoPlayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickAutoMovement(DeltaTime);
}

void UTrainingAutoPlayerComponent::StartAutoMovement(APawn* InTrainingPlayer, APawn* InEnemy)
{
	TrainingPlayer = InTrainingPlayer;
	EnemyPawn = InEnemy;
	CurrentActionIndex = 0;
	CurrentActionElapsed = 0.0f;
	TimeSinceLastAction = 0.0f;
	ZigZagElapsed = 0.0f;
	bAutoMovementActive = TrainingPlayer != nullptr && EnemyPawn != nullptr && ActionPlan.Num() > 0;
	SetComponentTickEnabled(bAutoMovementActive);

	if (bAutoMovementActive && CVarTrainingAutoPlayerDebug.GetValueOnGameThread() != 0)
	{
		const FTrainingPlayerActionDecision& Decision = ActionPlan[CurrentActionIndex];
		UE_LOG(LogSoul_and_dungeon, Display,
			TEXT("TrainingAutoPlayer: start action=%s duration=%.2f source=%s"),
			TrainingPlayerActionToString(Decision.Action),
			Decision.DurationSeconds,
			*Decision.Source);
	}
}

void UTrainingAutoPlayerComponent::StopAutoMovement()
{
	bAutoMovementActive = false;
	SetComponentTickEnabled(false);
	TrainingPlayer = nullptr;
	EnemyPawn = nullptr;
	CurrentActionIndex = 0;
	CurrentActionElapsed = 0.0f;
	TimeSinceLastAction = 0.0f;
	ZigZagElapsed = 0.0f;
}

void UTrainingAutoPlayerComponent::SetActionPlan(const TArray<FTrainingPlayerActionDecision>& InActionPlan)
{
	ActionPlan = InActionPlan;
	CurrentActionIndex = 0;
	CurrentActionElapsed = 0.0f;
	TimeSinceLastAction = 0.0f;
	ZigZagElapsed = 0.0f;
}

void UTrainingAutoPlayerComponent::GenerateSeededActionPlan(int32 Seed, float TotalDurationSeconds, TArray<FTrainingPlayerActionDecision>& OutActionPlan) const
{
	const float MinDuration = FMath::Max(0.05f, CVarTrainingAutoPlayerMinDuration.GetValueOnGameThread());
	const float MaxDuration = FMath::Max(MinDuration, CVarTrainingAutoPlayerMaxDuration.GetValueOnGameThread());
	const float SpeedScaleMin = FMath::Max(0.01f, CVarTrainingAutoPlayerSpeedScaleMin.GetValueOnGameThread());
	const float SpeedScaleMax = FMath::Max(SpeedScaleMin, CVarTrainingAutoPlayerSpeedScaleMax.GetValueOnGameThread());

	FRandomStream RandomStream(Seed == 0 ? CVarTrainingAutoPlayerRandomSeed.GetValueOnGameThread() : Seed);
	OutActionPlan.Reset();

	float AccumulatedTime = 0.0f;
	while (AccumulatedTime < TotalDurationSeconds)
	{
		FTrainingPlayerActionDecision Decision = ChooseScriptedPlayerAction(RandomStream, MinDuration, MaxDuration, SpeedScaleMin, SpeedScaleMax);
		Decision.DurationSeconds = FMath::Min(Decision.DurationSeconds, TotalDurationSeconds - AccumulatedTime);
		if (Decision.DurationSeconds < 0.05f)
		{
			break;
		}

		OutActionPlan.Add(Decision);
		AccumulatedTime += Decision.DurationSeconds;
	}
}

void UTrainingAutoPlayerComponent::TickAutoMovement(float DeltaSeconds)
{
	if (!ShouldRunAutoPlayer())
	{
		return;
	}

	AdvanceActionIfNeeded();
	if (!ShouldRunAutoPlayer())
	{
		return;
	}

	const FTrainingPlayerActionDecision& Decision = ActionPlan[CurrentActionIndex];
	ExecuteTrainingPlayerAction(Decision, DeltaSeconds);

	CurrentActionElapsed += DeltaSeconds;
	TimeSinceLastAction += DeltaSeconds;
	ZigZagElapsed += DeltaSeconds;
}

FAutoPlayerMovementObservation UTrainingAutoPlayerComponent::BuildAutoPlayerObservation(APawn* InTrainingPlayer, APawn* InEnemy) const
{
	FAutoPlayerMovementObservation Observation;
	if (!InTrainingPlayer || !InEnemy)
	{
		return Observation;
	}

	Observation.PlayerLocation = InTrainingPlayer->GetActorLocation();
	Observation.PlayerVelocity = InTrainingPlayer->GetVelocity();
	Observation.EnemyLocation = InEnemy->GetActorLocation();
	Observation.EnemyVelocity = InEnemy->GetVelocity();
	Observation.PlayerSpeed = Observation.PlayerVelocity.Size2D();
	Observation.EnemySpeed = Observation.EnemyVelocity.Size2D();
	Observation.DistanceToEnemy = FVector::Dist2D(Observation.PlayerLocation, Observation.EnemyLocation);
	Observation.ZDelta = Observation.PlayerLocation.Z - Observation.EnemyLocation.Z;
	Observation.TimeSinceLastAction = TimeSinceLastAction;

	const FVector ToEnemy = (Observation.EnemyLocation - Observation.PlayerLocation).GetSafeNormal2D();
	const FVector PlayerForward = InTrainingPlayer->GetActorForwardVector().GetSafeNormal2D();
	if (!ToEnemy.IsNearlyZero() && !PlayerForward.IsNearlyZero())
	{
		const float Dot = FMath::Clamp(FVector::DotProduct(PlayerForward, ToEnemy), -1.0f, 1.0f);
		Observation.EnemyAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
		const float Side = FVector::CrossProduct(PlayerForward, ToEnemy).Z;
		if (Side < 0.0f)
		{
			Observation.EnemyAngleDegrees *= -1.0f;
		}
	}

	UWorld* World = InTrainingPlayer->GetWorld();
	if (World)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TrainingAutoPlayerLineOfSight), false);
		Params.AddIgnoredActor(InTrainingPlayer);
		Params.AddIgnoredActor(InEnemy);
		const FVector TraceStart = Observation.PlayerLocation + FVector(0.0f, 0.0f, 45.0f);
		const FVector TraceEnd = Observation.EnemyLocation + FVector(0.0f, 0.0f, 45.0f);
		Observation.bHasLineOfSight = !World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
		Observation.bNearObstacle = TraceNearbyObstacle(World, InTrainingPlayer, 120.0f);
		Observation.bNearWall = Observation.bNearObstacle;
	}

	if (Observation.PlayerLocation.ContainsNaN())
	{
		Observation.PlayerLocation = FVector::ZeroVector;
	}
	if (Observation.PlayerVelocity.ContainsNaN())
	{
		Observation.PlayerVelocity = FVector::ZeroVector;
	}
	if (Observation.EnemyLocation.ContainsNaN())
	{
		Observation.EnemyLocation = FVector::ZeroVector;
	}
	if (Observation.EnemyVelocity.ContainsNaN())
	{
		Observation.EnemyVelocity = FVector::ZeroVector;
	}

	Observation.DistanceToEnemy = FMath::IsFinite(Observation.DistanceToEnemy) ? Observation.DistanceToEnemy : 0.0f;
	Observation.PlayerSpeed = FMath::IsFinite(Observation.PlayerSpeed) ? Observation.PlayerSpeed : 0.0f;
	Observation.EnemySpeed = FMath::IsFinite(Observation.EnemySpeed) ? Observation.EnemySpeed : 0.0f;
	Observation.ZDelta = FMath::IsFinite(Observation.ZDelta) ? Observation.ZDelta : 0.0f;
	Observation.EnemyAngleDegrees = FMath::IsFinite(Observation.EnemyAngleDegrees) ? Observation.EnemyAngleDegrees : 0.0f;
	Observation.TimeSinceLastAction = FMath::IsFinite(Observation.TimeSinceLastAction) ? Observation.TimeSinceLastAction : 0.0f;
	return Observation;
}

void UTrainingAutoPlayerComponent::ExecuteTrainingPlayerAction(const FTrainingPlayerActionDecision& Decision, float DeltaSeconds)
{
	if (!TrainingPlayer)
	{
		return;
	}

	if (Decision.Action == ETrainingPlayerAction::StopAndTurn)
	{
		if (EnemyPawn)
		{
			const FVector AwayFromEnemy = SanitizeDirection(TrainingPlayer->GetActorLocation() - EnemyPawn->GetActorLocation());
			if (!AwayFromEnemy.IsNearlyZero())
			{
				TrainingPlayer->SetActorRotation(AwayFromEnemy.Rotation());
			}
		}
		return;
	}

	const FVector Direction = ComputeMovementDirectionForAction(Decision.Action, DeltaSeconds);
	ApplyMovementDirection(Direction, DeltaSeconds, Decision.SpeedScale);
}

FVector UTrainingAutoPlayerComponent::ComputeMovementDirectionForAction(ETrainingPlayerAction Action, float DeltaSeconds) const
{
	if (!TrainingPlayer)
	{
		return FVector::ZeroVector;
	}

	const FVector PlayerLocation = TrainingPlayer->GetActorLocation();
	const FVector EnemyLocation = EnemyPawn ? EnemyPawn->GetActorLocation() : PlayerLocation - TrainingPlayer->GetActorForwardVector();
	const FVector AwayFromEnemy = SanitizeDirection(PlayerLocation - EnemyLocation);
	const FVector TowardEnemy = -AwayFromEnemy;
	const FVector Left = PerpendicularLeft(TowardEnemy);
	const FVector Right = -Left;

	const FTrainingPlayerActionDecision* CurrentDecision = ActionPlan.IsValidIndex(CurrentActionIndex) ? &ActionPlan[CurrentActionIndex] : nullptr;
	const FVector PlannedDirection = CurrentDecision ? SanitizeDirection(CurrentDecision->Direction) : FVector::ZeroVector;

	switch (Action)
	{
	case ETrainingPlayerAction::RunAwayFromEnemy:
		return AwayFromEnemy;
	case ETrainingPlayerAction::RunStraight:
		return PlannedDirection.IsNearlyZero() ? SanitizeDirection(TrainingPlayer->GetActorForwardVector()) : PlannedDirection;
	case ETrainingPlayerAction::StrafeLeft:
		return Left;
	case ETrainingPlayerAction::StrafeRight:
		return Right;
	case ETrainingPlayerAction::ZigZag:
	{
		const bool bUseLeft = FMath::FloorToInt((ZigZagElapsed + DeltaSeconds) / 0.35f) % 2 == 0;
		const FVector Side = bUseLeft ? Left : Right;
		return (AwayFromEnemy * 0.75f + Side * 0.65f).GetSafeNormal2D();
	}
	case ETrainingPlayerAction::CircleEnemy:
		return Left.IsNearlyZero() ? SanitizeDirection(TrainingPlayer->GetActorRightVector()) : Left;
	case ETrainingPlayerAction::DodgeLeft:
		return Left;
	case ETrainingPlayerAction::DodgeRight:
		return Right;
	case ETrainingPlayerAction::RandomExplore:
		return PlannedDirection.IsNearlyZero() ? SanitizeDirection(TrainingPlayer->GetActorForwardVector()) : PlannedDirection;
	case ETrainingPlayerAction::StopAndTurn:
	default:
		return FVector::ZeroVector;
	}
}

const TCHAR* UTrainingAutoPlayerComponent::TrainingPlayerActionToString(ETrainingPlayerAction Action)
{
	switch (Action)
	{
	case ETrainingPlayerAction::RunAwayFromEnemy:
		return TEXT("RunAwayFromEnemy");
	case ETrainingPlayerAction::RunStraight:
		return TEXT("RunStraight");
	case ETrainingPlayerAction::StrafeLeft:
		return TEXT("StrafeLeft");
	case ETrainingPlayerAction::StrafeRight:
		return TEXT("StrafeRight");
	case ETrainingPlayerAction::ZigZag:
		return TEXT("ZigZag");
	case ETrainingPlayerAction::CircleEnemy:
		return TEXT("CircleEnemy");
	case ETrainingPlayerAction::StopAndTurn:
		return TEXT("StopAndTurn");
	case ETrainingPlayerAction::DodgeLeft:
		return TEXT("DodgeLeft");
	case ETrainingPlayerAction::DodgeRight:
		return TEXT("DodgeRight");
	case ETrainingPlayerAction::RandomExplore:
		return TEXT("RandomExplore");
	default:
		return TEXT("Unknown");
	}
}

bool UTrainingAutoPlayerComponent::TryTrainingPlayerActionFromString(const FString& ActionName, ETrainingPlayerAction& OutAction)
{
	const FString Normalized = ActionName.TrimStartAndEnd();
	for (uint8 Index = 0; Index <= static_cast<uint8>(ETrainingPlayerAction::RandomExplore); ++Index)
	{
		const ETrainingPlayerAction Candidate = static_cast<ETrainingPlayerAction>(Index);
		if (Normalized.Equals(TrainingPlayerActionToString(Candidate), ESearchCase::IgnoreCase))
		{
			OutAction = Candidate;
			return true;
		}
	}
	return false;
}

void UTrainingAutoPlayerComponent::GetAllowedActionNames(TArray<FString>& OutActionNames)
{
	OutActionNames.Reset();
	for (uint8 Index = 0; Index <= static_cast<uint8>(ETrainingPlayerAction::RandomExplore); ++Index)
	{
		OutActionNames.Add(TrainingPlayerActionToString(static_cast<ETrainingPlayerAction>(Index)));
	}
}

FTrainingPlayerActionDecision UTrainingAutoPlayerComponent::ChooseScriptedPlayerAction(
	FRandomStream& RandomStream,
	float MinDuration,
	float MaxDuration,
	float SpeedScaleMin,
	float SpeedScaleMax)
{
	struct FWeightedAction
	{
		ETrainingPlayerAction Action;
		int32 Weight;
	};

	const FWeightedAction WeightedActions[] = {
		{ ETrainingPlayerAction::RunAwayFromEnemy, 20 },
		{ ETrainingPlayerAction::ZigZag, 20 },
		{ ETrainingPlayerAction::CircleEnemy, 15 },
		{ ETrainingPlayerAction::StrafeLeft, 10 },
		{ ETrainingPlayerAction::StrafeRight, 10 },
		{ ETrainingPlayerAction::RunStraight, 10 },
		{ ETrainingPlayerAction::StopAndTurn, 5 },
		{ ETrainingPlayerAction::DodgeLeft, 5 },
		{ ETrainingPlayerAction::DodgeRight, 5 },
	};

	int32 TotalWeight = 0;
	for (const FWeightedAction& WeightedAction : WeightedActions)
	{
		TotalWeight += WeightedAction.Weight;
	}

	const int32 Roll = RandomStream.RandRange(1, TotalWeight);
	int32 AccumulatedWeight = 0;
	ETrainingPlayerAction ChosenAction = ETrainingPlayerAction::RunAwayFromEnemy;
	for (const FWeightedAction& WeightedAction : WeightedActions)
	{
		AccumulatedWeight += WeightedAction.Weight;
		if (Roll <= AccumulatedWeight)
		{
			ChosenAction = WeightedAction.Action;
			break;
		}
	}

	FTrainingPlayerActionDecision Decision;
	Decision.Action = ChosenAction;
	Decision.DurationSeconds = RandomStream.FRandRange(FMath::Max(0.05f, MinDuration), FMath::Max(MinDuration, MaxDuration));
	Decision.SpeedScale = RandomStream.FRandRange(FMath::Max(0.01f, SpeedScaleMin), FMath::Max(SpeedScaleMin, SpeedScaleMax));
	Decision.Source = TEXT("ScriptedRandom");
	Decision.Reason = TEXT("weighted seeded scripted action");

	const float AngleRadians = RandomStream.FRandRange(-PI, PI);
	Decision.Direction = FVector(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
	return Decision;
}

bool UTrainingAutoPlayerComponent::ShouldRunAutoPlayer() const
{
	return bAutoMovementActive &&
		CVarTrainingAutoPlayerEnableScripted.GetValueOnGameThread() != 0 &&
		TrainingPlayer != nullptr &&
		EnemyPawn != nullptr &&
		ActionPlan.IsValidIndex(CurrentActionIndex);
}

void UTrainingAutoPlayerComponent::AdvanceActionIfNeeded()
{
	while (ActionPlan.IsValidIndex(CurrentActionIndex) &&
		CurrentActionElapsed >= FMath::Max(0.01f, ActionPlan[CurrentActionIndex].DurationSeconds))
	{
		CurrentActionIndex++;
		CurrentActionElapsed = 0.0f;
		TimeSinceLastAction = 0.0f;
		ZigZagElapsed = 0.0f;

		if (ActionPlan.IsValidIndex(CurrentActionIndex) && CVarTrainingAutoPlayerDebug.GetValueOnGameThread() != 0)
		{
			const FTrainingPlayerActionDecision& Decision = ActionPlan[CurrentActionIndex];
			UE_LOG(LogSoul_and_dungeon, Display,
				TEXT("TrainingAutoPlayer: action=%s duration=%.2f speed=%.2f source=%s"),
				TrainingPlayerActionToString(Decision.Action),
				Decision.DurationSeconds,
				Decision.SpeedScale,
				*Decision.Source);
		}
	}

	if (!ActionPlan.IsValidIndex(CurrentActionIndex))
	{
		bAutoMovementActive = false;
		SetComponentTickEnabled(false);
	}
}

void UTrainingAutoPlayerComponent::ApplyMovementDirection(const FVector& Direction, float DeltaSeconds, float SpeedScale)
{
	if (!TrainingPlayer)
	{
		return;
	}

	const FVector MoveDirection = SanitizeDirection(Direction);
	if (MoveDirection.IsNearlyZero())
	{
		return;
	}

	TrainingPlayer->SetActorRotation(MoveDirection.Rotation());
	TrainingPlayer->AddMovementInput(MoveDirection, FMath::Max(0.0f, SpeedScale), true);

	UPawnMovementComponent* MovementComponent = TrainingPlayer->GetMovementComponent();
	if (MovementComponent && MovementComponent->UpdatedComponent && !IsRunningCommandlet())
	{
		return;
	}

	const float Distance = FallbackMoveSpeed * FMath::Max(0.0f, SpeedScale) * FMath::Max(0.0f, DeltaSeconds);
	if (Distance <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FHitResult Hit;
	const FVector Start = TrainingPlayer->GetActorLocation();
	const FVector End = Start + MoveDirection * Distance;
	TrainingPlayer->SetActorLocation(End, true, &Hit, ETeleportType::None);
	if (IsRunningCommandlet() && FVector::DistSquared2D(TrainingPlayer->GetActorLocation(), Start) <= FMath::Square(1.0f))
	{
		TrainingPlayer->SetActorLocation(End, false, nullptr, ETeleportType::None);
	}
	if (MovementComponent)
	{
		MovementComponent->Velocity = MoveDirection * (FallbackMoveSpeed * FMath::Max(0.0f, SpeedScale));
	}
	if (USceneComponent* Root = TrainingPlayer->GetRootComponent())
	{
		Root->ComponentVelocity = MoveDirection * (FallbackMoveSpeed * FMath::Max(0.0f, SpeedScale));
	}
}
