#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "EnemyInterceptTypes.h"
#include "EnemyInterceptTreePolicy.h"
#include "SecondarySearchSolver.h"
#include "MyAIController.generated.h"

class UAnimInstance;
class ASecondarySearchVisualizerActor;

UENUM(BlueprintType)
enum class EEnemyNavigationMode : uint8
{
	AStarOnly,
	SmoothedAStar
};



UCLASS()
class SOUL_AND_DUNGEON_API AMyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMyAIController();

	int32 GetAStarFallbackCount() const;
	int32 GetInvalidInterceptTargetCount() const;
	int32 GetAStarReplanCount() const;
	int32 GetAStarPathFailureCount() const;
	float GetTrainingAttackRange() const;
	void ResetEnemyInterceptMetricsForTraining();
	void TickTrainingNavigationForCommandlet(float DeltaTime);

	FEnemyInterceptObservation BuildInterceptObservation(APawn* PlayerPawn);
	EEnemyInterceptMode ChooseDeterministicInterceptMode(const FEnemyInterceptObservation& Observation) const;
	float GetPredictionTimeForMode(EEnemyInterceptMode Mode) const;
	FVector ComputeGoalForInterceptMode(APawn* PlayerPawn, EEnemyInterceptMode Mode) const;
	bool TryValidateInterceptGoal(const FVector& CandidateGoal, FVector& OutValidatedGoal, FString& OutReason) const;
	FEnemyInterceptDecision ChooseSmartNavigationGoal(APawn* PlayerPawn);
	int32 GetEnemyInterceptRuntimeMode() const;
	FString GetEnemyInterceptRuntimeModeName() const;
	bool ResolveInterceptModeFromRuntimeMode(int32 RuntimeMode, EEnemyInterceptMode& OutMode, FString& OutReason) const;
	void CycleEnemyInterceptMode();

	void SetTrainingInterceptOverride(EEnemyInterceptMode Mode);
	void ClearTrainingInterceptOverride();
	bool IsTrainingInterceptOverrideEnabled() const;
	EEnemyInterceptMode GetTrainingInterceptOverrideMode() const;
	void SetTrainingTargetPlayer(APawn* PlayerPawn);
	void ClearTrainingTargetPlayer();


protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	FEnemyInterceptDecision MakeCurrentPlayerLocationDecision(APawn* PlayerPawn, const FString& Reason) const;
	bool IsInterceptPredictionEnabled() const;
	bool LoadLearnedInterceptPolicyIfNeeded(FString& OutReason);
	EEnemyInterceptMode ChooseLearnedInterceptModeOrFallback(const FEnemyInterceptObservation& Observation, FString& OutReason);
	bool IsInterceptDebugLoggingEnabled() const;
	void LogInterceptDecision(const FEnemyInterceptDecision& Decision, const FEnemyInterceptObservation& Observation, float CurrentTime);
	void DrawInterceptDebug(APawn* PlayerPawn, const FEnemyInterceptObservation& Observation, const FEnemyInterceptDecision& Decision, const FString& SourceText) const;
	void UpdateAStarNavigation(APawn* AI, APawn* Player, float CurrentTime, float DeltaTime);
	void ResetAStarNavigation();
	bool ShouldReplanAStarPath(const FVector& AILocation, const FVector& GoalLocation, float CurrentTime, const FSecondarySearchSettings& Settings) const;
	bool BuildAStarPath(APawn* AI, const FVector& GoalLocation, const FSecondarySearchSettings& Settings);
	bool BuildSmoothedPath(const TArray<FVector>& RawPath, const FSecondarySearchSettings& Settings, TArray<FVector>& OutPath) const;
	bool HasClearNavigationSegment(const FVector& From, const FVector& To) const;
	bool HasBlockingObstacleOnSegment(const FVector& From, const FVector& To) const;
	bool HasAttackReach(APawn* AI, APawn* Player, float AttackDistance) const;
	bool TryMoveAlongVerifiedPath(const FVector& PathTarget, float AcceptanceRadius);
	bool TryDirectCommandletPathFollow(APawn* AI, const FVector& PathTarget, float DeltaTime, float AcceptanceRadius);
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
	FVector LastIssuedMoveTarget = FVector::ZeroVector;
	FVector LastActivePathTarget = FVector::ZeroVector;
	float LastAStarReplanTime = -1000000.0f;
	float LastMoveIssueTime = -1000000.0f;
	float AStarReplanInterval = 0.35f;
	float SmoothedPathLookAheadDistance = 260.0f;
	FVector LastNavigationLocation = FVector::ZeroVector;
	float StuckSeconds = 0.0f;
	float LastPathProgress = 0.0f;
	int32 AStarFallbackCount = 0;
	int32 InvalidInterceptTargetCount = 0;
	int32 AStarReplanCount = 0;
	int32 AStarPathFailureCount = 0;
	bool bOverrideInterceptModeForTraining = false;
	EEnemyInterceptMode TrainingOverrideInterceptMode = EEnemyInterceptMode::CurrentLocation;
	UPROPERTY()
	TObjectPtr<APawn> TrainingTargetPlayer;
	FVector LastPlayerMoveDirectionForIntercept = FVector::ZeroVector;
	float LastPlayerDirectionChangeTime = -1000000.0f;
	float LastInterceptDebugLogTime = -1000000.0f;
	float LastLearnedPolicyLoadWarningTime = -1000000.0f;
	EEnemyInterceptMode LastLoggedInterceptMode = EEnemyInterceptMode::CurrentLocation;
	bool bHasLoggedInterceptMode = false;
	bool bHasIssuedMoveTarget = false;
	bool bLastIssuedMoveWasFallback = false;
	FString LastLearnedPolicyPath;
	FEnemyInterceptTreePolicy LearnedInterceptPolicy;

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
	
	// Attack State Management
	bool bIsCurrentlyAttacking = false;
	float LastAttackStartTime = 0.0f;
	float MinAttackDuration = 0.8f; // Ensure at least one full swing can play
	float AttackHysteresis = 80.0f; // Distance buffer to prevent rapid switching
};
