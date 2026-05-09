#include "EnemyLearningTrainingEnvironment.h"

#include "GameFramework/Pawn.h"
#include "LearningAgentsCompletions.h"
#include "MyAIController.h"

void UEnemyLearningTrainingEnvironment::Configure(APawn* InPlayerPawn, int32 InMaxEpisodeSteps, float InAttackRange)
{
	PlayerPawn = InPlayerPawn;
	MaxEpisodeSteps = FMath::Max(1, InMaxEpisodeSteps);
	AttackRange = FMath::Max(1.0f, InAttackRange);
}

void UEnemyLearningTrainingEnvironment::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	OutReward = 0.0f;

	AMyAIController* Controller = GetController(AgentId);
	if (!Controller || !PlayerPawn)
	{
		return;
	}

	FEnemyLearningEpisodeState& State = GetOrCreateState(AgentId, Controller);

	FEnemyLearningObservation Observation;
	Controller->GetEnemyLearningObservation(Observation);

	const float DistanceProgress = (State.LastDistanceToPlayer - Observation.DistanceToPlayer) * 0.0025f;
	const float PathProgress = (Observation.PathProgress - State.LastPathProgress) * 1.5f;
	const float Alignment = FVector::DotProduct(Observation.DirectionToPlayer, Observation.DirectionToPath) * 0.02f;
	const float CloseReward = Observation.DistanceToPlayer <= AttackRange ? 2.0f : 0.0f;
	const float StuckPenalty = Observation.StuckSeconds > 1.0f ? -0.15f * Observation.StuckSeconds : 0.0f;
	const float FallbackPenalty = Controller->GetAStarFallbackCount() > State.LastFallbackCount ? -0.25f : 0.0f;
	const float LosBonus = Observation.bHasLineOfSight ? 0.01f : 0.0f;

	OutReward = DistanceProgress + PathProgress + Alignment + CloseReward + StuckPenalty + FallbackPenalty + LosBonus;
	RewardSum += OutReward;
	RewardSampleCount++;

	State.LastDistanceToPlayer = Observation.DistanceToPlayer;
	State.LastPathProgress = Observation.PathProgress;
	State.LastFallbackCount = Controller->GetAStarFallbackCount();
	State.StepCount++;
}

void UEnemyLearningTrainingEnvironment::GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	OutCompletion = ELearningAgentsCompletion::Running;

	AMyAIController* Controller = GetController(AgentId);
	if (!Controller || !PlayerPawn)
	{
		OutCompletion = ELearningAgentsCompletion::Termination;
		return;
	}

	FEnemyLearningObservation Observation;
	Controller->GetEnemyLearningObservation(Observation);
	FEnemyLearningEpisodeState& State = GetOrCreateState(AgentId, Controller);

	if (Observation.DistanceToPlayer > 0.0f && Observation.DistanceToPlayer <= AttackRange)
	{
		CompletedEpisodeCount++;
		OutCompletion = ELearningAgentsCompletion::Termination;
		return;
	}

	if (Observation.StuckSeconds >= 4.0f)
	{
		StuckEpisodeCount++;
		OutCompletion = ELearningAgentsCompletion::Truncation;
		return;
	}

	if (State.StepCount >= MaxEpisodeSteps)
	{
		TruncatedEpisodeCount++;
		OutCompletion = ELearningAgentsCompletion::Truncation;
	}
}

void UEnemyLearningTrainingEnvironment::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	AMyAIController* Controller = GetController(AgentId);
	APawn* EnemyPawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !EnemyPawn || !PlayerPawn)
	{
		return;
	}

	FEnemyLearningEpisodeState& State = GetOrCreateState(AgentId, Controller);
	EnemyPawn->SetActorLocationAndRotation(State.EnemyStartLocation, State.EnemyStartRotation, false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->SetActorLocationAndRotation(State.PlayerStartLocation, State.PlayerStartRotation, false, nullptr, ETeleportType::TeleportPhysics);

	State.LastDistanceToPlayer = FVector::Dist2D(State.EnemyStartLocation, State.PlayerStartLocation);
	State.LastPathProgress = 0.0f;
	State.LastFallbackCount = Controller->GetAStarFallbackCount();
	State.StepCount = 0;
}

int32 UEnemyLearningTrainingEnvironment::GetCompletedEpisodeCount() const
{
	return CompletedEpisodeCount;
}

int32 UEnemyLearningTrainingEnvironment::GetTruncatedEpisodeCount() const
{
	return TruncatedEpisodeCount;
}

int32 UEnemyLearningTrainingEnvironment::GetStuckEpisodeCount() const
{
	return StuckEpisodeCount;
}

float UEnemyLearningTrainingEnvironment::GetRewardSum() const
{
	return RewardSum;
}

int32 UEnemyLearningTrainingEnvironment::GetRewardSampleCount() const
{
	return RewardSampleCount;
}

AMyAIController* UEnemyLearningTrainingEnvironment::GetController(const int32 AgentId)
{
	return Cast<AMyAIController>(GetAgent(AgentId));
}

FEnemyLearningEpisodeState& UEnemyLearningTrainingEnvironment::GetOrCreateState(const int32 AgentId, AMyAIController* Controller)
{
	FEnemyLearningEpisodeState& State = EpisodeStates.FindOrAdd(AgentId);
	if (State.EnemyStartLocation.IsNearlyZero() && Controller && Controller->GetPawn() && PlayerPawn)
	{
		State.EnemyStartLocation = Controller->GetPawn()->GetActorLocation();
		State.EnemyStartRotation = Controller->GetPawn()->GetActorRotation();
		State.PlayerStartLocation = PlayerPawn->GetActorLocation();
		State.PlayerStartRotation = PlayerPawn->GetActorRotation();
		State.LastDistanceToPlayer = FVector::Dist2D(State.EnemyStartLocation, State.PlayerStartLocation);
		State.LastFallbackCount = Controller->GetAStarFallbackCount();
	}
	return State;
}
