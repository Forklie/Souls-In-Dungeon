#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "EnemyLearningTrainingEnvironment.generated.h"

class AMyAIController;

USTRUCT()
struct FEnemyLearningEpisodeState
{
	GENERATED_BODY()

	FVector EnemyStartLocation = FVector::ZeroVector;
	FRotator EnemyStartRotation = FRotator::ZeroRotator;
	FVector PlayerStartLocation = FVector::ZeroVector;
	FRotator PlayerStartRotation = FRotator::ZeroRotator;
	float LastDistanceToPlayer = 0.0f;
	float LastPathProgress = 0.0f;
	int32 LastFallbackCount = 0;
	int32 StepCount = 0;
};

UCLASS()
class UEnemyLearningTrainingEnvironment : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	void Configure(APawn* InPlayerPawn, int32 InMaxEpisodeSteps, float InAttackRange);

	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;

	int32 GetCompletedEpisodeCount() const;
	int32 GetTruncatedEpisodeCount() const;
	int32 GetStuckEpisodeCount() const;
	float GetRewardSum() const;
	int32 GetRewardSampleCount() const;

private:
	AMyAIController* GetController(const int32 AgentId);
	FEnemyLearningEpisodeState& GetOrCreateState(const int32 AgentId, AMyAIController* Controller);

	UPROPERTY()
	TObjectPtr<APawn> PlayerPawn;

	UPROPERTY()
	TMap<int32, FEnemyLearningEpisodeState> EpisodeStates;

	int32 MaxEpisodeSteps = 1200;
	float AttackRange = 160.0f;
	int32 CompletedEpisodeCount = 0;
	int32 TruncatedEpisodeCount = 0;
	int32 StuckEpisodeCount = 0;
	float RewardSum = 0.0f;
	int32 RewardSampleCount = 0;
};
