#pragma once

#include "CoreMinimal.h"

enum class ESecondarySearchMode : uint8
{
	BFS,
	UCS
};

enum class ESecondarySearchVisualEventType : uint8
{
	NodeDiscovered,
	NodeExpanded,
	PathSegmentConfirmed,
	SearchFailed,
	SearchComplete
};

enum class ESecondarySearchVisualStyle : uint8
{
	Simple,
	Fluid
};

struct FSecondarySearchVisualEvent
{
	ESecondarySearchVisualEventType Type = ESecondarySearchVisualEventType::NodeDiscovered;
	FVector Location = FVector::ZeroVector;
	FVector EndLocation = FVector::ZeroVector;
	float Cost = 0.0f;
	int32 Sequence = 0;
	int32 SearchGeneration = 0;
};

struct FSecondarySearchSettings
{
	float CellSize = 100.0f;
	bool bAllowDiagonal = false;
	int32 MaxExpandedNodes = 900;
	float MaxSearchDistance = 4000.0f;
	float GoalAcceptanceRadius = 70.0f;
	float PathPointReachRadius = 50.0f;
	FVector ProjectionExtent = FVector(50.0f, 50.0f, 250.0f);
	float DebugDrawDuration = 0.75f;
	float DebugPointZOffset = 80.0f;
	int32 MaxDebugDrawNodes = 900;
	int32 MaxDebugSearchStepsPerTick = 16;
	float DebugVisualizerUpdateInterval = 0.05f;
	float PathTubeRadius = 5.0f;
	float VisualizationRevealRate = 900.0f;
	float FluidNodeLifetime = 3.0f;
	float FluidPathRevealSpeed = 1100.0f;
	float WavePathRetentionSeconds = 4.0f;
	float WaveTravelSeconds = 0.8f;
	float DebugExpandedNodeRadius = 26.0f;
	float DebugFrontierNodeRadius = 32.0f;
	float DebugNodeHeight = 2.0f;
	float DebugFrontierNodeHeight = 2.5f;
	float DebugEndpointRadius = 50.0f;
	float DebugTargetRadius = 44.0f;
};

struct FSecondarySearchResult
{
	bool bSuccess = false;
	ESecondarySearchMode Mode = ESecondarySearchMode::UCS;
	FString FailureReason;
	TArray<FVector> Path;
	TArray<FVector> ExpandedNodes;
	TArray<FVector> FrontierNodes;
	TArray<float> PathCosts;
	FVector StartLocation = FVector::ZeroVector;
	FVector GoalLocation = FVector::ZeroVector;
	FVector CurrentTarget = FVector::ZeroVector;
	int32 ExpandedCount = 0;
	double ElapsedMs = 0.0;
	float DebugStartSeconds = 0.0f;
	int32 VisualizationRevision = 0;
	int32 SearchGeneration = 0;
	TArray<FSecondarySearchVisualEvent> VisualEvents;
};

struct FSecondarySearchNodeRecord
{
	FIntPoint CameFrom = FIntPoint::ZeroValue;
	float Cost = 0.0f;
	bool bHasCameFrom = false;
};

struct FSecondarySearchOpenItem
{
	FIntPoint Key = FIntPoint::ZeroValue;
	float Priority = 0.0f;
};

class FSecondarySearchSolver
{
public:
	static FSecondarySearchResult FindPath(
		UWorld* World,
		const FVector& Start,
		const FVector& Goal,
		ESecondarySearchMode Mode,
		const FSecondarySearchSettings& Settings);
};

class FSecondarySearchTask
{
public:
	void Reset();
	void Start(
		UWorld* World,
		const FVector& Start,
		const FVector& Goal,
		ESecondarySearchMode Mode,
		const FSecondarySearchSettings& Settings);
	void Step(UWorld* World, const FSecondarySearchSettings& Settings, int32 MaxSteps);

	bool IsActive() const;
	bool HasResult() const;
	const FSecondarySearchResult& GetResult() const;
	FSecondarySearchResult BuildDebugResult() const;

private:
	void Finish(bool bSuccess, const FString& FailureReason);

	FSecondarySearchResult Result;
	ESecondarySearchMode Mode = ESecondarySearchMode::UCS;
	FVector Origin = FVector::ZeroVector;
	FIntPoint StartKey = FIntPoint::ZeroValue;
	FIntPoint EndKey = FIntPoint::ZeroValue;
	TMap<FIntPoint, FVector> ProjectedCache;
	TMap<FIntPoint, FSecondarySearchNodeRecord> Records;
	TSet<FIntPoint> ClosedSet;
	TArray<FIntPoint> NeighborOffsets;
	TArray<FIntPoint> Queue;
	TArray<FSecondarySearchOpenItem> OpenSet;
	int32 QueueHead = 0;
	bool bActive = false;
	bool bFinished = false;
};

class FSecondarySearchDebug
{
public:
	static bool IsEnabled();
	static void Toggle();
	static ESecondarySearchMode GetMode();
	static void SetMode(ESecondarySearchMode Mode);
	static int32 GetRevision();
	static bool ConsumeClearRequested();
	static bool IsXRayEnabled();
	static int32 GetMaxDebugNodes();
	static ESecondarySearchVisualStyle GetVisualStyle();
	static float GetVisualSpeed();
	static bool AreTrailsEnabled();
	static float GetWaveSpeed();
	static int32 GetPathHistoryCount();
	static float GetCellSize();
	static float GetNodeScale();
	static int32 GetNodeDensity();
	static FString GetModeName(ESecondarySearchMode Mode);
	static FString GetVisualStyleName(ESecondarySearchVisualStyle Style);
};
