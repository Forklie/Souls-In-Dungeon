#include "SecondarySearchSolver.h"

#include "Algo/Reverse.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Soul_and_dungeon.h"

namespace
{
static FIntPoint WorldToGrid(const FVector& Location, const FVector& Origin, float CellSize)
{
	return FIntPoint(
		FMath::RoundToInt((Location.X - Origin.X) / CellSize),
		FMath::RoundToInt((Location.Y - Origin.Y) / CellSize));
}

static FVector GridToWorld(const FIntPoint& Key, const FVector& Origin, float CellSize)
{
	return Origin + FVector(Key.X * CellSize, Key.Y * CellSize, 0.0f);
}

static FIntPoint AddGridOffset(const FIntPoint& Key, const FIntPoint& Offset)
{
	return FIntPoint(Key.X + Offset.X, Key.Y + Offset.Y);
}

static bool GetProjectedPoint(
	UWorld* World,
	UNavigationSystemV1* NavSystem,
	const FIntPoint& Key,
	const FVector& Origin,
	const FSecondarySearchSettings& Settings,
	TMap<FIntPoint, FVector>& ProjectedCache,
	TSet<FIntPoint>& RejectedProjectionCache,
	FVector& OutLocation)
{
	if (const FVector* CachedLocation = ProjectedCache.Find(Key))
	{
		OutLocation = *CachedLocation;
		return true;
	}

	if (RejectedProjectionCache.Contains(Key))
	{
		return false;
	}

	const FVector Candidate = GridToWorld(Key, Origin, Settings.CellSize);
	if (FVector::Dist2D(Origin, Candidate) > Settings.MaxSearchDistance)
	{
		RejectedProjectionCache.Add(Key);
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavSystem->ProjectPointToNavigation(Candidate, ProjectedLocation, Settings.ProjectionExtent))
	{
		RejectedProjectionCache.Add(Key);
		return false;
	}

	OutLocation = ProjectedLocation.Location;
	ProjectedCache.Add(Key, OutLocation);
	return true;
}

static bool HasClearSegment(UWorld* World, const FVector& From, const FVector& To)
{
	FVector HitLocation = FVector::ZeroVector;
	return !UNavigationSystemV1::NavigationRaycast(World, From, To, HitLocation);
}

static void AddNeighborOffsets(bool bAllowDiagonal, TArray<FIntPoint>& OutOffsets)
{
	OutOffsets = {
		FIntPoint(1, 0),
		FIntPoint(-1, 0),
		FIntPoint(0, 1),
		FIntPoint(0, -1)
	};

	if (bAllowDiagonal)
	{
		OutOffsets.Append({
			FIntPoint(1, 1),
			FIntPoint(1, -1),
			FIntPoint(-1, 1),
			FIntPoint(-1, -1)
		});
	}
}

static bool IsGoalReached(const FVector& Location, const FVector& Goal, const FSecondarySearchSettings& Settings)
{
	const float AcceptanceRadius = FMath::Max3(45.0f, Settings.GoalAcceptanceRadius, Settings.CellSize * 0.75f);
	return FVector::Dist2D(Location, Goal) <= AcceptanceRadius;
}

static float GetSearchPriority(ESecondarySearchMode Mode, const FVector& Location, const FVector& Goal, float CostSoFar)
{
	return Mode == ESecondarySearchMode::AStar ? CostSoFar + FVector::Dist2D(Location, Goal) : CostSoFar;
}

static bool HasHigherOpenPriority(const FSecondarySearchOpenItem& A, const FSecondarySearchOpenItem& B)
{
	if (!FMath::IsNearlyEqual(A.Priority, B.Priority))
	{
		return A.Priority < B.Priority;
	}
	return A.TieBreaker < B.TieBreaker;
}

static void PushOpenItem(TArray<FSecondarySearchOpenItem>& OpenSet, const FSecondarySearchOpenItem& Item)
{
	int32 Index = OpenSet.Add(Item);
	while (Index > 0)
	{
		const int32 ParentIndex = (Index - 1) / 2;
		if (!HasHigherOpenPriority(OpenSet[Index], OpenSet[ParentIndex]))
		{
			break;
		}
		OpenSet.Swap(Index, ParentIndex);
		Index = ParentIndex;
	}
}

static bool PopBestOpenItem(TArray<FSecondarySearchOpenItem>& OpenSet, FSecondarySearchOpenItem& OutItem)
{
	if (OpenSet.Num() == 0)
	{
		return false;
	}

	OutItem = OpenSet[0];
	const FSecondarySearchOpenItem LastItem = OpenSet.Pop(EAllowShrinking::No);
	if (OpenSet.Num() == 0)
	{
		return true;
	}

	OpenSet[0] = LastItem;
	int32 Index = 0;
	while (true)
	{
		const int32 LeftChildIndex = Index * 2 + 1;
		const int32 RightChildIndex = LeftChildIndex + 1;
		int32 BestChildIndex = Index;

		if (OpenSet.IsValidIndex(LeftChildIndex) && HasHigherOpenPriority(OpenSet[LeftChildIndex], OpenSet[BestChildIndex]))
		{
			BestChildIndex = LeftChildIndex;
		}
		if (OpenSet.IsValidIndex(RightChildIndex) && HasHigherOpenPriority(OpenSet[RightChildIndex], OpenSet[BestChildIndex]))
		{
			BestChildIndex = RightChildIndex;
		}
		if (BestChildIndex == Index)
		{
			break;
		}

		OpenSet.Swap(Index, BestChildIndex);
		Index = BestChildIndex;
	}
	return true;
}

static void BuildPath(
	const FIntPoint& StartKey,
	const FIntPoint& EndKey,
	const TMap<FIntPoint, FSecondarySearchNodeRecord>& Records,
	const TMap<FIntPoint, FVector>& ProjectedCache,
	FSecondarySearchResult& Result)
{
	TArray<FIntPoint> ReversedKeys;
	FIntPoint CurrentKey = EndKey;
	ReversedKeys.Add(CurrentKey);

	while (CurrentKey != StartKey)
	{
		const FSecondarySearchNodeRecord* Record = Records.Find(CurrentKey);
		if (!Record || !Record->bHasCameFrom)
		{
			Result.bSuccess = false;
			Result.FailureReason = TEXT("Path reconstruction failed.");
			return;
		}

		CurrentKey = Record->CameFrom;
		ReversedKeys.Add(CurrentKey);
	}

	Algo::Reverse(ReversedKeys);
	for (const FIntPoint& Key : ReversedKeys)
	{
		if (const FVector* Location = ProjectedCache.Find(Key))
		{
			Result.Path.Add(*Location);
		}
		if (const FSecondarySearchNodeRecord* Record = Records.Find(Key))
		{
			Result.PathCosts.Add(Record->Cost);
		}
	}

	Result.bSuccess = Result.Path.Num() > 0;
}

static int32 NextSearchGeneration()
{
	static int32 SearchGeneration = 0;
	return ++SearchGeneration;
}

static void AddVisualEvent(
	FSecondarySearchResult& Result,
	ESecondarySearchVisualEventType Type,
	const FVector& Location,
	const FVector& EndLocation = FVector::ZeroVector,
	float Cost = 0.0f)
{
	FSecondarySearchVisualEvent Event;
	Event.Type = Type;
	Event.Location = Location;
	Event.EndLocation = EndLocation;
	Event.Cost = Cost;
	Event.Sequence = Result.VisualEvents.Num();
	Event.SearchGeneration = Result.SearchGeneration;
	Result.VisualEvents.Add(Event);
}

static void AddPathVisualEvents(FSecondarySearchResult& Result)
{
	for (int32 Index = 1; Index < Result.Path.Num(); ++Index)
	{
		const float Cost = Result.PathCosts.IsValidIndex(Index) ? Result.PathCosts[Index] : 0.0f;
		AddVisualEvent(
			Result,
			ESecondarySearchVisualEventType::PathSegmentConfirmed,
			Result.Path[Index - 1],
			Result.Path[Index],
			Cost);
	}
}

static void SyncSampledNodes(
	const TMap<FIntPoint, FVector>& ProjectedCache,
	const FSecondarySearchSettings& Settings,
	FSecondarySearchResult& Result)
{
	Result.SampledNodes.Reset();
	Result.SampledNodes.Reserve(FMath::Min(ProjectedCache.Num(), Settings.MaxDebugDrawNodes));
	for (const TPair<FIntPoint, FVector>& ProjectedPoint : ProjectedCache)
	{
		if (Result.SampledNodes.Num() >= Settings.MaxDebugDrawNodes)
		{
			break;
		}
		Result.SampledNodes.Add(ProjectedPoint.Value);
	}
}

#if !UE_BUILD_SHIPPING
namespace SecondarySearchDebugState
{
	static bool bEnabled = false;
	static bool bPendingClear = false;
	static bool bXRayEnabled = true;
	static bool bTrailsEnabled = true;
	static bool bShowBaseGrid = true;
	static bool bLastPathFallback = true;
	static ESecondarySearchMode Mode = ESecondarySearchMode::UCS;
	static ESecondarySearchVisualStyle VisualStyle = ESecondarySearchVisualStyle::Fluid;
	static ESecondarySearchVisualQuality VisualQuality = ESecondarySearchVisualQuality::High;
	static int32 Revision = 0;
	static int32 MaxDebugNodes = 12000;
	static int32 PathHistoryCount = 3;
	static float VisualSpeed = 1.0f;
	static float WaveSpeed = 1.0f;
	static float NodePulse = 1.0f;
	static float NodeFadeTime = 1.2f;
	static float PathFadeTime = 4.0f;
	static float GlowIntensity = 1.0f;
	static float FlowBandWidth = 0.18f;
	static float NodeSoftness = 0.75f;
	static float CellSize = 28.0f;
	static float NodeScale = 0.24f;
	static float FieldRadius = 4000.0f;
	static float TargetSmoothing = 18.0f;

	static void ToggleCommand()
	{
		bEnabled = !bEnabled;
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search debug %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				913700,
				2.5f,
				bEnabled ? FColor::Green : FColor::Silver,
				FString::Printf(TEXT("Secondary Search Debug: %s"), bEnabled ? TEXT("ON") : TEXT("OFF")));
		}
	}

	static void ModeCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search mode: %s"), *FSecondarySearchDebug::GetModeName(Mode));
			return;
		}

		if (Args[0].Equals(TEXT("BFS"), ESearchCase::IgnoreCase))
		{
			Mode = ESecondarySearchMode::BFS;
		}
		else if (Args[0].Equals(TEXT("UCS"), ESearchCase::IgnoreCase))
		{
			Mode = ESecondarySearchMode::UCS;
		}
		else if (Args[0].Equals(TEXT("AStar"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("A*"), ESearchCase::IgnoreCase))
		{
			Mode = ESecondarySearchMode::AStar;
		}
		else
		{
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("Unknown secondary search mode '%s'. Use BFS, UCS, or AStar."), *Args[0]);
			return;
		}

		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search mode set to %s"), *FSecondarySearchDebug::GetModeName(Mode));
	}

	static void ClearCommand()
	{
		bPendingClear = true;
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search debug clear requested."));
	}

	static void BoolCommand(const TArray<FString>& Args, bool& Value, const TCHAR* Label)
	{
		if (Args.Num() > 0)
		{
			Value = FCString::Atoi(*Args[0]) != 0;
			Revision++;
		}
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("%s: %s"), Label, Value ? TEXT("enabled") : TEXT("disabled"));
	}

	static void IntCommand(const TArray<FString>& Args, int32& Value, int32 MinValue, int32 MaxValue, const TCHAR* Label)
	{
		if (Args.Num() > 0)
		{
			Value = FMath::Clamp(FCString::Atoi(*Args[0]), MinValue, MaxValue);
			Revision++;
		}
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("%s: %d"), Label, Value);
	}

	static void FloatCommand(const TArray<FString>& Args, float& Value, float MinValue, float MaxValue, const TCHAR* Label)
	{
		if (Args.Num() > 0)
		{
			Value = FMath::Clamp(FCString::Atof(*Args[0]), MinValue, MaxValue);
			Revision++;
		}
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("%s: %.2f"), Label, Value);
	}

	static void StyleCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search visual style: %s"), *FSecondarySearchDebug::GetVisualStyleName(VisualStyle));
			return;
		}
		if (Args[0].Equals(TEXT("Simple"), ESearchCase::IgnoreCase))
		{
			VisualStyle = ESecondarySearchVisualStyle::Simple;
		}
		else if (Args[0].Equals(TEXT("Fluid"), ESearchCase::IgnoreCase))
		{
			VisualStyle = ESecondarySearchVisualStyle::Fluid;
		}
		else
		{
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("Unknown secondary search visual style '%s'. Use Simple or Fluid."), *Args[0]);
			return;
		}
		Revision++;
	}

	static void QualityCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search visual quality: %s"), *FSecondarySearchDebug::GetVisualQualityName(VisualQuality));
			return;
		}
		if (Args[0].Equals(TEXT("Low"), ESearchCase::IgnoreCase))
		{
			VisualQuality = ESecondarySearchVisualQuality::Low;
		}
		else if (Args[0].Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
		{
			VisualQuality = ESecondarySearchVisualQuality::Medium;
		}
		else if (Args[0].Equals(TEXT("High"), ESearchCase::IgnoreCase))
		{
			VisualQuality = ESecondarySearchVisualQuality::High;
		}
		else
		{
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("Unknown secondary search visual quality '%s'. Use Low, Medium, or High."), *Args[0]);
			return;
		}
		Revision++;
	}

	static FAutoConsoleCommand ToggleConsoleCommand(TEXT("sd.SearchDebug.Toggle"), TEXT("Toggle secondary search debug drawing."), FConsoleCommandDelegate::CreateStatic(&ToggleCommand));
	static FAutoConsoleCommand ModeConsoleCommand(TEXT("sd.SearchDebug.Mode"), TEXT("Usage: sd.SearchDebug.Mode BFS, UCS, or AStar."), FConsoleCommandWithArgsDelegate::CreateStatic(&ModeCommand));
	static FAutoConsoleCommand ClearConsoleCommand(TEXT("sd.SearchDebug.Clear"), TEXT("Clear secondary search debug drawing."), FConsoleCommandDelegate::CreateStatic(&ClearCommand));
	static FAutoConsoleCommand XRayConsoleCommand(TEXT("sd.SearchDebug.XRay"), TEXT("Usage: sd.SearchDebug.XRay 0 or 1."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { BoolCommand(Args, bXRayEnabled, TEXT("Secondary search XRay")); }));
	static FAutoConsoleCommand MaxNodesConsoleCommand(TEXT("sd.SearchDebug.MaxNodes"), TEXT("Usage: sd.SearchDebug.MaxNodes 12000."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { IntCommand(Args, MaxDebugNodes, 128, 16000, TEXT("Secondary search max nodes")); }));
	static FAutoConsoleCommand NodeDensityConsoleCommand(TEXT("sd.SearchDebug.NodeDensity"), TEXT("Usage: sd.SearchDebug.NodeDensity 12000."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { IntCommand(Args, MaxDebugNodes, 128, 16000, TEXT("Secondary search node density")); }));
	static FAutoConsoleCommand CellSizeConsoleCommand(TEXT("sd.SearchDebug.CellSize"), TEXT("Usage: sd.SearchDebug.CellSize 28."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, CellSize, 20.0f, 200.0f, TEXT("Secondary search cell size")); }));
	static FAutoConsoleCommand NodeScaleConsoleCommand(TEXT("sd.SearchDebug.NodeScale"), TEXT("Usage: sd.SearchDebug.NodeScale 0.24."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, NodeScale, 0.12f, 1.25f, TEXT("Secondary search node scale")); }));
	static FAutoConsoleCommand ShowBaseGridConsoleCommand(TEXT("sd.SearchDebug.ShowBaseGrid"), TEXT("Usage: sd.SearchDebug.ShowBaseGrid 1."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { BoolCommand(Args, bShowBaseGrid, TEXT("Secondary search base grid")); }));
	static FAutoConsoleCommand FieldRadiusConsoleCommand(TEXT("sd.SearchDebug.FieldRadius"), TEXT("Usage: sd.SearchDebug.FieldRadius 4000."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, FieldRadius, 1000.0f, 8000.0f, TEXT("Secondary search field radius")); }));
	static FAutoConsoleCommand TargetSmoothingConsoleCommand(TEXT("sd.SearchDebug.TargetSmoothing"), TEXT("Usage: sd.SearchDebug.TargetSmoothing 18."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, TargetSmoothing, 1.0f, 40.0f, TEXT("Secondary search target smoothing")); }));
	static FAutoConsoleCommand StyleConsoleCommand(TEXT("sd.SearchDebug.Style"), TEXT("Usage: sd.SearchDebug.Style Simple or Fluid."), FConsoleCommandWithArgsDelegate::CreateStatic(&StyleCommand));
	static FAutoConsoleCommand SpeedConsoleCommand(TEXT("sd.SearchDebug.Speed"), TEXT("Usage: sd.SearchDebug.Speed 1.0."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, VisualSpeed, 0.1f, 5.0f, TEXT("Secondary search visual speed")); }));
	static FAutoConsoleCommand TrailsConsoleCommand(TEXT("sd.SearchDebug.Trails"), TEXT("Usage: sd.SearchDebug.Trails 0 or 1."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { BoolCommand(Args, bTrailsEnabled, TEXT("Secondary search trails")); }));
	static FAutoConsoleCommand WaveSpeedConsoleCommand(TEXT("sd.SearchDebug.WaveSpeed"), TEXT("Usage: sd.SearchDebug.WaveSpeed 1.0."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, WaveSpeed, 0.1f, 5.0f, TEXT("Secondary search wave speed")); }));
	static FAutoConsoleCommand PathHistoryConsoleCommand(TEXT("sd.SearchDebug.PathHistory"), TEXT("Usage: sd.SearchDebug.PathHistory 3."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { IntCommand(Args, PathHistoryCount, 1, 8, TEXT("Secondary search path history")); }));
	static FAutoConsoleCommand NodePulseConsoleCommand(TEXT("sd.SearchDebug.NodePulse"), TEXT("Usage: sd.SearchDebug.NodePulse 1.0."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, NodePulse, 0.0f, 3.0f, TEXT("Secondary search node pulse")); }));
	static FAutoConsoleCommand NodeFadeTimeConsoleCommand(TEXT("sd.SearchDebug.NodeFadeTime"), TEXT("Usage: sd.SearchDebug.NodeFadeTime 1.2."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, NodeFadeTime, 0.2f, 5.0f, TEXT("Secondary search node fade time")); }));
	static FAutoConsoleCommand PathFadeTimeConsoleCommand(TEXT("sd.SearchDebug.PathFadeTime"), TEXT("Usage: sd.SearchDebug.PathFadeTime 4.0."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, PathFadeTime, 0.5f, 10.0f, TEXT("Secondary search path fade time")); }));
	static FAutoConsoleCommand LastPathFallbackConsoleCommand(TEXT("sd.SearchDebug.LastPathFallback"), TEXT("Usage: sd.SearchDebug.LastPathFallback 1."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { BoolCommand(Args, bLastPathFallback, TEXT("Secondary search last path fallback")); }));
	static FAutoConsoleCommand QualityConsoleCommand(TEXT("sd.SearchDebug.Quality"), TEXT("Usage: sd.SearchDebug.Quality Low, Medium, or High."), FConsoleCommandWithArgsDelegate::CreateStatic(&QualityCommand));
	static FAutoConsoleCommand GlowConsoleCommand(TEXT("sd.SearchDebug.Glow"), TEXT("Usage: sd.SearchDebug.Glow 1.0."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, GlowIntensity, 0.0f, 3.0f, TEXT("Secondary search glow")); }));
	static FAutoConsoleCommand FlowBandConsoleCommand(TEXT("sd.SearchDebug.FlowBand"), TEXT("Usage: sd.SearchDebug.FlowBand 0.18."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, FlowBandWidth, 0.05f, 0.5f, TEXT("Secondary search flow band")); }));
	static FAutoConsoleCommand NodeSoftnessConsoleCommand(TEXT("sd.SearchDebug.NodeSoftness"), TEXT("Usage: sd.SearchDebug.NodeSoftness 0.75."), FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args) { FloatCommand(Args, NodeSoftness, 0.1f, 1.0f, TEXT("Secondary search node softness")); }));
}
#endif
}

FSecondarySearchResult FSecondarySearchSolver::FindPath(
	UWorld* World,
	const FVector& Start,
	const FVector& Goal,
	ESecondarySearchMode Mode,
	const FSecondarySearchSettings& Settings)
{
	FSecondarySearchTask Task;
	Task.Start(World, Start, Goal, Mode, Settings);
	while (Task.IsActive())
	{
		Task.Step(World, Settings, Settings.MaxExpandedNodes);
	}
	return Task.BuildDebugResult();
}

void FSecondarySearchTask::Reset()
{
	Result = FSecondarySearchResult();
	ProjectedCache.Reset();
	RejectedProjectionCache.Reset();
	Records.Reset();
	ClosedSet.Reset();
	NeighborOffsets.Reset();
	Queue.Reset();
	OpenSet.Reset();
	QueueHead = 0;
	OpenSequence = 0;
	bActive = false;
	bFinished = false;
}

void FSecondarySearchTask::Start(
	UWorld* World,
	const FVector& Start,
	const FVector& Goal,
	ESecondarySearchMode InMode,
	const FSecondarySearchSettings& Settings)
{
	Reset();
	Mode = InMode;
	Result.Mode = InMode;
	Result.StartLocation = Start;
	Result.GoalLocation = Goal;
	Result.DebugStartSeconds = World ? World->GetTimeSeconds() : 0.0f;
	Result.SearchGeneration = NextSearchGeneration();
	Result.VisualizationRevision++;

	if (!World)
	{
		Finish(false, TEXT("Invalid world."), Settings);
		return;
	}

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem)
	{
		Finish(false, TEXT("Navigation system is not available."), Settings);
		return;
	}

	FNavLocation ProjectedStart;
	FNavLocation ProjectedGoal;
	if (!NavSystem->ProjectPointToNavigation(Start, ProjectedStart, Settings.ProjectionExtent))
	{
		Finish(false, TEXT("Start point is not on navigation."), Settings);
		return;
	}
	if (!NavSystem->ProjectPointToNavigation(Goal, ProjectedGoal, Settings.ProjectionExtent))
	{
		Finish(false, TEXT("Goal point is not on navigation."), Settings);
		return;
	}

	Result.StartLocation = ProjectedStart.Location;
	Result.GoalLocation = ProjectedGoal.Location;
	Origin = Result.StartLocation;
	StartKey = FIntPoint::ZeroValue;
	EndKey = StartKey;

	const FIntPoint GoalKey = WorldToGrid(Result.GoalLocation, Origin, Settings.CellSize);
	ProjectedCache.Add(StartKey, Result.StartLocation);
	ProjectedCache.Add(GoalKey, Result.GoalLocation);
	Records.Add(StartKey, FSecondarySearchNodeRecord());
	AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeDiscovered, Result.StartLocation, FVector::ZeroVector, 0.0f);
	AddNeighborOffsets(Settings.bAllowDiagonal, NeighborOffsets);
	SyncSampledNodes(ProjectedCache, Settings, Result);

	if (IsGoalReached(Result.StartLocation, Result.GoalLocation, Settings))
	{
		Finish(true, FString(), Settings);
	}
	else if (Mode == ESecondarySearchMode::BFS)
	{
		Queue.Add(StartKey);
		bActive = true;
	}
	else
	{
		PushOpenItem(OpenSet, { StartKey, GetSearchPriority(Mode, Result.StartLocation, Result.GoalLocation, 0.0f), OpenSequence++ });
		bActive = true;
	}
}

void FSecondarySearchTask::Step(UWorld* World, const FSecondarySearchSettings& Settings, int32 MaxSteps)
{
	if (!bActive || bFinished || MaxSteps <= 0)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!World || !NavSystem)
	{
		Finish(false, TEXT("Navigation system is not available."), Settings);
		return;
	}

	int32 StepsTaken = 0;
	while (StepsTaken < MaxSteps && Result.ExpandedCount < Settings.MaxExpandedNodes && !bFinished)
	{
		FIntPoint CurrentKey = FIntPoint::ZeroValue;
		if (Mode == ESecondarySearchMode::BFS)
		{
			if (QueueHead >= Queue.Num())
			{
				Finish(false, TEXT("No path found."), Settings);
				break;
			}
			CurrentKey = Queue[QueueHead++];
		}
		else
		{
			FSecondarySearchOpenItem CurrentItem;
			if (!PopBestOpenItem(OpenSet, CurrentItem))
			{
				Finish(false, TEXT("No path found."), Settings);
				break;
			}
			CurrentKey = CurrentItem.Key;
		}

		if (ClosedSet.Contains(CurrentKey))
		{
			continue;
		}

		FVector CurrentLocation;
		if (!GetProjectedPoint(World, NavSystem, CurrentKey, Origin, Settings, ProjectedCache, RejectedProjectionCache, CurrentLocation))
		{
			continue;
		}

		ClosedSet.Add(CurrentKey);
		Result.ExpandedNodes.Add(CurrentLocation);
		AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeExpanded, CurrentLocation, FVector::ZeroVector, Records.FindChecked(CurrentKey).Cost);
		Result.ExpandedCount++;
		Result.VisualizationRevision++;
		StepsTaken++;

		if (IsGoalReached(CurrentLocation, Result.GoalLocation, Settings))
		{
			EndKey = CurrentKey;
			Finish(true, FString(), Settings);
			break;
		}

		const float CurrentCost = Records.FindChecked(CurrentKey).Cost;
		for (const FIntPoint& Offset : NeighborOffsets)
		{
			const FIntPoint NextKey = AddGridOffset(CurrentKey, Offset);
			if (ClosedSet.Contains(NextKey) || (Mode == ESecondarySearchMode::BFS && Records.Contains(NextKey)))
			{
				continue;
			}

			FVector NextLocation;
			if (!GetProjectedPoint(World, NavSystem, NextKey, Origin, Settings, ProjectedCache, RejectedProjectionCache, NextLocation))
			{
				continue;
			}
			if (!HasClearSegment(World, CurrentLocation, NextLocation))
			{
				continue;
			}

			const float StepCost = Mode == ESecondarySearchMode::BFS ? 1.0f : FMath::Max(FVector::Dist2D(CurrentLocation, NextLocation), KINDA_SMALL_NUMBER);
			const float NewCost = CurrentCost + StepCost;
			FSecondarySearchNodeRecord* ExistingRecord = Records.Find(NextKey);
			if (!ExistingRecord || NewCost < ExistingRecord->Cost)
			{
				FSecondarySearchNodeRecord NextRecord;
				NextRecord.CameFrom = CurrentKey;
				NextRecord.Cost = NewCost;
				NextRecord.bHasCameFrom = true;
				Records.Add(NextKey, NextRecord);

				if (Mode == ESecondarySearchMode::BFS)
				{
					Queue.Add(NextKey);
				}
				else
				{
					PushOpenItem(OpenSet, { NextKey, GetSearchPriority(Mode, NextLocation, Result.GoalLocation, NewCost), OpenSequence++ });
				}

				Result.FrontierNodes.Add(NextLocation);
				AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeDiscovered, NextLocation, FVector::ZeroVector, NewCost);
				Result.VisualizationRevision++;
			}
		}
	}

	if (!bFinished && Result.ExpandedCount >= Settings.MaxExpandedNodes)
	{
		Finish(false, FString::Printf(TEXT("Search reached max expanded node limit: %d."), Settings.MaxExpandedNodes), Settings);
	}

	SyncSampledNodes(ProjectedCache, Settings, Result);
	Result.ElapsedMs += (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
}

bool FSecondarySearchTask::IsActive() const
{
	return bActive;
}

bool FSecondarySearchTask::HasResult() const
{
	return bFinished;
}

const FSecondarySearchResult& FSecondarySearchTask::GetResult() const
{
	return Result;
}

FSecondarySearchResult FSecondarySearchTask::BuildDebugResult() const
{
	return Result;
}

void FSecondarySearchTask::Finish(bool bSuccess, const FString& FailureReason, const FSecondarySearchSettings& Settings)
{
	bActive = false;
	bFinished = true;
	Result.bSuccess = bSuccess;
	Result.FailureReason = FailureReason;
	Result.VisualizationRevision++;
	SyncSampledNodes(ProjectedCache, Settings, Result);

	if (bSuccess)
	{
		Result.Path.Reset();
		Result.PathCosts.Reset();
		BuildPath(StartKey, EndKey, Records, ProjectedCache, Result);
		AddPathVisualEvents(Result);
		AddVisualEvent(Result, ESecondarySearchVisualEventType::SearchComplete, Result.GoalLocation);
	}
	else
	{
		AddVisualEvent(Result, ESecondarySearchVisualEventType::SearchFailed, Result.GoalLocation);
	}
}

bool FSecondarySearchDebug::IsEnabled()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::bEnabled;
#else
	return false;
#endif
}

void FSecondarySearchDebug::Toggle()
{
#if !UE_BUILD_SHIPPING
	SecondarySearchDebugState::ToggleCommand();
#endif
}

ESecondarySearchMode FSecondarySearchDebug::GetMode()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::Mode;
#else
	return ESecondarySearchMode::UCS;
#endif
}

void FSecondarySearchDebug::SetMode(ESecondarySearchMode Mode)
{
#if !UE_BUILD_SHIPPING
	SecondarySearchDebugState::Mode = Mode;
	SecondarySearchDebugState::Revision++;
#endif
}

int32 FSecondarySearchDebug::GetRevision()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::Revision;
#else
	return 0;
#endif
}

bool FSecondarySearchDebug::ConsumeClearRequested()
{
#if !UE_BUILD_SHIPPING
	if (SecondarySearchDebugState::bPendingClear)
	{
		SecondarySearchDebugState::bPendingClear = false;
		return true;
	}
#endif
	return false;
}

bool FSecondarySearchDebug::IsXRayEnabled()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::bXRayEnabled;
#else
	return false;
#endif
}

int32 FSecondarySearchDebug::GetMaxDebugNodes()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::MaxDebugNodes;
#else
	return 0;
#endif
}

ESecondarySearchVisualStyle FSecondarySearchDebug::GetVisualStyle()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::VisualStyle;
#else
	return ESecondarySearchVisualStyle::Simple;
#endif
}

float FSecondarySearchDebug::GetVisualSpeed()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::VisualSpeed;
#else
	return 1.0f;
#endif
}

bool FSecondarySearchDebug::AreTrailsEnabled()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::bTrailsEnabled;
#else
	return false;
#endif
}

float FSecondarySearchDebug::GetWaveSpeed()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::WaveSpeed;
#else
	return 1.0f;
#endif
}

int32 FSecondarySearchDebug::GetPathHistoryCount()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::PathHistoryCount;
#else
	return 1;
#endif
}

float FSecondarySearchDebug::GetNodePulse()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::NodePulse;
#else
	return 1.0f;
#endif
}

float FSecondarySearchDebug::GetNodeFadeTime()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::NodeFadeTime;
#else
	return 1.2f;
#endif
}

float FSecondarySearchDebug::GetPathFadeTime()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::PathFadeTime;
#else
	return 4.0f;
#endif
}

bool FSecondarySearchDebug::ShouldUseLastPathFallback()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::bLastPathFallback;
#else
	return false;
#endif
}

ESecondarySearchVisualQuality FSecondarySearchDebug::GetVisualQuality()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::VisualQuality;
#else
	return ESecondarySearchVisualQuality::Low;
#endif
}

float FSecondarySearchDebug::GetGlowIntensity()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::GlowIntensity;
#else
	return 0.0f;
#endif
}

float FSecondarySearchDebug::GetFlowBandWidth()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::FlowBandWidth;
#else
	return 0.18f;
#endif
}

float FSecondarySearchDebug::GetNodeSoftness()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::NodeSoftness;
#else
	return 0.75f;
#endif
}

float FSecondarySearchDebug::GetCellSize()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::CellSize;
#else
	return 28.0f;
#endif
}

float FSecondarySearchDebug::GetNodeScale()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::NodeScale;
#else
	return 0.24f;
#endif
}

int32 FSecondarySearchDebug::GetNodeDensity()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::MaxDebugNodes;
#else
	return 0;
#endif
}

bool FSecondarySearchDebug::ShouldShowBaseGrid()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::bShowBaseGrid;
#else
	return false;
#endif
}

float FSecondarySearchDebug::GetFieldRadius()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::FieldRadius;
#else
	return 4000.0f;
#endif
}

float FSecondarySearchDebug::GetTargetSmoothing()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::TargetSmoothing;
#else
	return 18.0f;
#endif
}

FString FSecondarySearchDebug::GetModeName(ESecondarySearchMode Mode)
{
	if (Mode == ESecondarySearchMode::BFS)
	{
		return TEXT("BFS");
	}
	if (Mode == ESecondarySearchMode::AStar)
	{
		return TEXT("A*");
	}
	return TEXT("UCS");
}

FString FSecondarySearchDebug::GetVisualStyleName(ESecondarySearchVisualStyle Style)
{
	return Style == ESecondarySearchVisualStyle::Fluid ? TEXT("Fluid") : TEXT("Simple");
}

FString FSecondarySearchDebug::GetVisualQualityName(ESecondarySearchVisualQuality Quality)
{
	if (Quality == ESecondarySearchVisualQuality::Low)
	{
		return TEXT("Low");
	}
	if (Quality == ESecondarySearchVisualQuality::Medium)
	{
		return TEXT("Medium");
	}
	return TEXT("High");
}
