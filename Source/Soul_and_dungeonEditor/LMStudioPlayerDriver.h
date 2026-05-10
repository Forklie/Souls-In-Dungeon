#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"

class AMyAIController;
class APawn;
class UWorld;

enum class ELMStudioPlayerBehavior : uint8
{
	Static,
	Moving,
	LMStudioEvasive,
	DeterministicEvasive
};

struct FLMStudioPlayerDriverSettings
{
	bool bUseLMStudio = false;
	FString Endpoint = TEXT("http://localhost:1234/v1/chat/completions");
	FString Model;
	float DecisionInterval = 1.0f;
	float TimeoutSeconds = 2.0f;
	float MoveSpeed = 500.0f;
	int32 CandidateCount = 12;
	int32 Seed = 1234;
};

struct FLMStudioPlayerDriverMetrics
{
	int32 DecisionRequests = 0;
	int32 DecisionResponses = 0;
	int32 DecisionTimeouts = 0;
	int32 FallbackDecisions = 0;
	int32 InvalidResponses = 0;
};

class FLMStudioPlayerDriver
{
public:
	void Configure(const FLMStudioPlayerDriverSettings& InSettings);
	void Reset(APawn* InPlayerPawn, AMyAIController* InEnemyController, ELMStudioPlayerBehavior InBehavior, const FVector& StartLocation);
	void Tick(UWorld* World, float DeltaSeconds, float CurrentTime);

	const FLMStudioPlayerDriverMetrics& GetMetrics() const;

private:
	void BuildCandidateGoals(UWorld* World, const FVector& PlayerLocation, const FVector& EnemyLocation);
	void RequestLMStudioDecision(const FVector& PlayerLocation, const FVector& EnemyLocation, float DistanceToEnemy);
	bool RequestLMStudioDecisionSynchronous(const FString& Body);
	bool ApplyLMStudioResponse(const FString& ResponseBody);
	void HandleLMStudioResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void ChooseDeterministicGoal(const FVector& PlayerLocation, const FVector& EnemyLocation, float CurrentTime);
	void MoveTowardCurrentGoal(UWorld* World, float DeltaSeconds);
	bool ProjectGoalToNavigation(UWorld* World, const FVector& Candidate, FVector& OutLocation) const;

	FLMStudioPlayerDriverSettings Settings;
	FLMStudioPlayerDriverMetrics Metrics;
	TWeakObjectPtr<APawn> PlayerPawn;
	TWeakObjectPtr<AMyAIController> EnemyController;
	ELMStudioPlayerBehavior Behavior = ELMStudioPlayerBehavior::Static;
	TArray<FVector> CandidateGoals;
	FVector CurrentGoal = FVector::ZeroVector;
	FVector EpisodeStartLocation = FVector::ZeroVector;
	float LastDecisionTime = -1000000.0f;
	float PendingRequestStartTime = 0.0f;
	bool bRequestPending = false;
	FRandomStream RandomStream;
};
