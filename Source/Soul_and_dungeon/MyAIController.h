#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SecondarySearchSolver.h"
#include "MyAIController.generated.h"

class UAnimInstance;
class ASecondarySearchVisualizerActor;

UCLASS()
class SOUL_AND_DUNGEON_API AMyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMyAIController();

protected:
	virtual void Tick(float DeltaTime) override;

private:
	void UpdateSecondarySearchDebug(APawn* AI, APawn* Player, float CurrentTime);
	FSecondarySearchSettings BuildSecondarySearchSettings() const;
	bool ShouldRefreshSearchDebug(const FVector& GoalLocation, float CurrentTime, ESecondarySearchMode SearchMode, const FSecondarySearchSettings& Settings) const;
	bool ShouldRebuildDebugBaseGrid(const FVector& CenterLocation, const FSecondarySearchSettings& Settings) const;
	void RebuildDebugBaseGrid(const FVector& CenterLocation, const FSecondarySearchSettings& Settings);
	void EnsureSecondarySearchVisualizer(APawn* AI);
	void HideSecondarySearchVisualizer();
	void SetAttackAnimationState(UAnimInstance* AnimInstance, bool bIsAttacking) const;

	float StopDistance = 150.0f;

	float DamageCooldown = 3.0f;
	float LastDamageTime = 0.0f;
	float AttackDelay = 0.5f;
	float LastAttackStartTime = 0.0f;

	FSecondarySearchSettings SecondarySearchSettings;
	FSecondarySearchSettings ActiveSecondarySearchSettings;
	FSecondarySearchTask SearchTask;
	FSecondarySearchTask AStarPreviewTask;
	FSecondarySearchResult LastSearchResult;
	FSecondarySearchResult LastAStarPreviewResult;
	TArray<FVector> DebugBaseGridNodes;

	UPROPERTY()
	TObjectPtr<ASecondarySearchVisualizerActor> SearchVisualizer;

	ESecondarySearchMode LastSearchMode = ESecondarySearchMode::UCS;
	FVector LastSearchGoal = FVector::ZeroVector;
	FString LastSearchFailureReason;
	float LastSearchTime = -1000000.0f;
	float LastFailureLogTime = -1000000.0f;
	float LastVisualizerUpdateTime = -1000000.0f;
	float SearchRefreshInterval = 0.35f;
	int32 LastDebugRevision = -1;
	int32 LastBaseGridMaxNodes = -1;
	float LastBaseGridCellSize = 0.0f;
	float LastBaseGridRadius = 0.0f;
	FVector LastBaseGridCenter = FVector::ZeroVector;
	bool bHasSearchResult = false;
	bool bDebugSearchWasEnabled = false;
};
