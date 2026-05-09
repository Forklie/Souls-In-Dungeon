#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SecondarySearchSolver.h"
#include "MyAIController.generated.h"

class UAnimInstance;
class ASecondarySearchVisualizerActor;

UENUM(BlueprintType)
enum class EEnemyNavigationMode : uint8
{
	AStarOnly,
	SmoothedAStar,
	LearningWithAStarFallback
};

USTRUCT(BlueprintType)
struct FEnemyLearningObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	FVector DirectionToPlayer = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	FVector DirectionToPath = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	float DistanceToPlayer = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	float DistanceToPathTarget = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	float StuckSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	float PathProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Learning")
	bool bHasLineOfSight = false;
};

UCLASS()
class SOUL_AND_DUNGEON_API AMyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMyAIController();

	void GetEnemyLearningObservation(FEnemyLearningObservation& OutObservation) const;
	FVector2D GetExpertLearningSteeringDirection() const;
	void ApplyLearningSteeringInput(const FVector2D& MoveInput);
	int32 GetAStarFallbackCount() const;
	void SetLearningTrainingPlayer(APawn* Player);

protected:
	virtual void Tick(float DeltaTime) override;

private:
	void UpdateAStarNavigation(APawn* AI, APawn* Player, float CurrentTime, float DeltaTime);
	void ResetAStarNavigation();
	bool ShouldReplanAStarPath(const FVector& AILocation, const FVector& GoalLocation, float CurrentTime, const FSecondarySearchSettings& Settings) const;
	bool BuildAStarPath(APawn* AI, const FVector& GoalLocation, const FSecondarySearchSettings& Settings);
	bool BuildSmoothedPath(const TArray<FVector>& RawPath, const FSecondarySearchSettings& Settings, TArray<FVector>& OutPath) const;
	bool HasClearNavigationSegment(const FVector& From, const FVector& To) const;
	FVector CalculatePathFollowTarget(const FVector& AILocation, TArray<FVector>& Path, int32& WaypointIndex, const FSecondarySearchSettings& Settings) const;
	void UpdateNavigationMetrics(APawn* AI, APawn* Player, const FVector& PathTarget, float DeltaTime, bool bUsedFallback);
	EEnemyNavigationMode GetNavigationMode() const;
	void UpdateSecondarySearchDebug(APawn* AI, APawn* Player, float CurrentTime);
	FSecondarySearchSettings BuildSecondarySearchSettings() const;
	bool ShouldRefreshSearchDebug(const FVector& GoalLocation, float CurrentTime, ESecondarySearchMode SearchMode, const FSecondarySearchSettings& Settings) const;
	bool ShouldRebuildDebugBaseGrid(const FVector& CenterLocation, const FSecondarySearchSettings& Settings) const;
	void RebuildDebugBaseGrid(const FVector& CenterLocation, const FSecondarySearchSettings& Settings);
	void EnsureSecondarySearchVisualizer(APawn* AI);
	void HideSecondarySearchVisualizer();
	void EnsureNavMeshBounds(APawn* AI, APawn* Player);
	void SetAttackAnimationState(UAnimInstance* AnimInstance, bool bIsAttacking) const;

	float StopDistance = 150.0f;



	TArray<FVector> ActiveAStarPath;
	TArray<FVector> ActiveSmoothedPath;
	int32 ActiveAStarWaypointIndex = 0;
	int32 ActiveSmoothedWaypointIndex = 0;
	FVector LastAStarGoal = FVector::ZeroVector;
	float LastAStarReplanTime = -1000000.0f;
	float AStarReplanInterval = 0.35f;
	float SmoothedPathLookAheadDistance = 260.0f;
	float LearningSteeringProjectionDistance = 260.0f;
	FVector2D LastLearningSteeringInput = FVector2D::ZeroVector;
	float LastLearningSteeringTime = -1000000.0f;
	float LearningSteeringMaxAge = 0.25f;
	FEnemyLearningObservation LastLearningObservation;
	TWeakObjectPtr<APawn> LearningTrainingPlayer;
	FVector LastNavigationLocation = FVector::ZeroVector;
	float StuckSeconds = 0.0f;
	float LastPathProgress = 0.0f;
	int32 AStarFallbackCount = 0;

	FSecondarySearchSettings SecondarySearchSettings;
	FSecondarySearchSettings ActiveSecondarySearchSettings;
	FSecondarySearchTask BFSTask;
	FSecondarySearchTask UCSTask;
	FSecondarySearchTask AStarTask;
	FSecondarySearchResult BFSResult;
	FSecondarySearchResult UCSResult;
	FSecondarySearchResult AStarResult;
	FSecondarySearchResult LastSearchResult;
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
	float LastBaseGridRebuildTime = -1000.0f;
	float BaseGridRebuildInterval = 0.5f;
	bool bHasSearchResult = false;
	bool bDebugSearchWasEnabled = false;
	FVector LastNavCheckAIPos = FVector::ZeroVector;
	FVector LastNavCheckPlayerPos = FVector::ZeroVector;
};
