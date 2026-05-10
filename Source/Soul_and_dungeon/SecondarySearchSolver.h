#pragma once

#include "CoreMinimal.h"

enum class ESecondarySearchMode : uint8
{
	BFS,
	UCS,
	AStar
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

enum class ESecondarySearchVisualQuality : uint8
{
	Low,
	Medium,
	High
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
	float CellSize = 28.0f;
	bool bAllowDiagonal = false;
	int32 MaxExpandedNodes = 5000;
	float MaxSearchDistance = 4000.0f;
	float GoalAcceptanceRadius = 55.0f;
	float PathPointReachRadius = 35.0f;
	FVector ProjectionExtent = FVector(80.0f, 80.0f, 300.0f);
	float DebugDrawDuration = 0.75f;
	float DebugPointZOffset = 80.0f;
	int32 MaxDebugDrawNodes = 12000;
	int32 MaxDebugSearchStepsPerTick = 96;
	float DebugVisualizerUpdateInterval = 0.05f;
	float PathTubeRadius = 5.0f;
	float VisualizationRevealRate = 900.0f;
	float FluidNodeLifetime = 3.0f;
	float FluidPathRevealSpeed = 1100.0f;
	float WavePathRetentionSeconds = 4.0f;
	float WaveTravelSeconds = 0.8f;
	float DebugExpandedNodeRadius = 24.0f;
	float DebugFrontierNodeRadius = 29.0f;
	float DebugNodeHeight = 2.0f;
	float DebugFrontierNodeHeight = 2.5f;
	float DebugEndpointRadius = 44.0f;
	float DebugTargetRadius = 38.0f;
};

struct FSecondarySearchResult
{
	bool bSuccess = false;
	ESecondarySearchMode Mode = ESecondarySearchMode::UCS;
	FString FailureReason;
	TArray<FVector> Path;
	TArray<FVector> PreviewPath;
	TArray<FVector> BFSPath;
	TArray<FVector> UCSPath;
	TArray<FVector> AStarPath;
	TArray<FVector> SampledNodes;
	TArray<FVector> ExpandedNodes;
	TArray<FVector> FrontierNodes;
	TArray<float> PathCosts;
	int32 BFSCount = 0;
	int32 UCSCount = 0;
	int32 AStarCount = 0;
	double BFSMs = 0.0;
	double UCSMs = 0.0;
	double AStarMs = 0.0;
	float BFSCost = 0.0f;
	float UCSCost = 0.0f;
	float AStarCost = 0.0f;
	FVector StartLocation = FVector::ZeroVector;
	FVector GoalLocation = FVector::ZeroVector;
	FVector CurrentTarget = FVector::ZeroVector;
	int32 ExpandedCount = 0;
	double ElapsedMs = 0.0;
	float DebugStartSeconds = 0.0f;
	int32 VisualizationRevision = 0;
	int32 SearchGeneration = 0;
	TArray<FSecondarySearchVisualEvent> VisualEvents;

	// NN Metrics for HUD
	bool bIsLearningMode = false;
	float NNSteeringMag = 0.0f;
	float NNAlignment = 0.0f;
	int32 NNFallbackTotal = 0;
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
	int32 TieBreaker = 0;
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
	void Finish(bool bSuccess, const FString& FailureReason, const FSecondarySearchSettings& Settings);

	FSecondarySearchResult Result;
	ESecondarySearchMode Mode = ESecondarySearchMode::UCS;
	FVector Origin = FVector::ZeroVector;
	FIntPoint StartKey = FIntPoint::ZeroValue;
	FIntPoint EndKey = FIntPoint::ZeroValue;
	TMap<FIntPoint, FVector> ProjectedCache;
	TSet<FIntPoint> RejectedProjectionCache;
	TMap<FIntPoint, FSecondarySearchNodeRecord> Records;
	TSet<FIntPoint> ClosedSet;
	TArray<FIntPoint> NeighborOffsets;
	TArray<FIntPoint> Queue;
	TArray<FSecondarySearchOpenItem> OpenSet;
	int32 QueueHead = 0;
	int32 OpenSequence = 0;
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
	static float GetNodePulse();
	static float GetNodeFadeTime();
	static float GetPathFadeTime();
	static bool ShouldUseLastPathFallback();
	static ESecondarySearchVisualQuality GetVisualQuality();
	static float GetGlowIntensity();
	static float GetFlowBandWidth();
	static float GetNodeSoftness();
	static float GetCellSize();
	static float GetNodeScale();
	static int32 GetNodeDensity();
	static bool ShouldShowBaseGrid();
	static float GetFieldRadius();
	static float GetTargetSmoothing();
	static FString GetModeName(ESecondarySearchMode Mode);
	static FString GetVisualStyleName(ESecondarySearchVisualStyle Style);
	static FString GetVisualQualityName(ESecondarySearchVisualQuality Quality);
};
