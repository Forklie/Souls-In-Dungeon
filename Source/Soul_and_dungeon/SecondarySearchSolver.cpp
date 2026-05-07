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
	FVector& OutLocation)
{
	if (const FVector* CachedLocation = ProjectedCache.Find(Key))
	{
		OutLocation = *CachedLocation;
		return true;
	}

	const FVector Candidate = GridToWorld(Key, Origin, Settings.CellSize);
	if (FVector::Dist2D(Origin, Candidate) > Settings.MaxSearchDistance)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavSystem->ProjectPointToNavigation(Candidate, ProjectedLocation, Settings.ProjectionExtent))
	{
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
	const float AcceptanceRadius = FMath::Max3(60.0f, Settings.GoalAcceptanceRadius, Settings.CellSize * 0.7f);
	return FVector::Dist2D(Location, Goal) <= AcceptanceRadius;
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

#if !UE_BUILD_SHIPPING
namespace SecondarySearchDebugState
{
	static bool bEnabled = false;
	static bool bPendingClear = false;
	static bool bXRayEnabled = true;
	static bool bTrailsEnabled = true;
	static ESecondarySearchMode Mode = ESecondarySearchMode::UCS;
	static ESecondarySearchVisualStyle VisualStyle = ESecondarySearchVisualStyle::Fluid;
	static int32 Revision = 0;
	static int32 MaxDebugNodes = 900;
	static float VisualSpeed = 1.0f;
	static float WaveSpeed = 1.0f;
	static float CellSize = 100.0f;
	static float NodeScale = 0.45f;
	static int32 PathHistoryCount = 3;

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
		else
		{
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("Unknown secondary search mode '%s'. Use BFS or UCS."), *Args[0]);
			return;
		}

		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search mode set to %s"), *FSecondarySearchDebug::GetModeName(Mode));

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				913701,
				2.5f,
				FColor::Cyan,
				FString::Printf(TEXT("Secondary Search Mode: %s"), *FSecondarySearchDebug::GetModeName(Mode)));
		}
	}

	static void ClearCommand()
	{
		bPendingClear = true;
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search debug clear requested."));
	}

	static void XRayCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search XRay: %s"), bXRayEnabled ? TEXT("enabled") : TEXT("disabled"));
			return;
		}

		bXRayEnabled = FCString::Atoi(*Args[0]) != 0;
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search XRay %s"), bXRayEnabled ? TEXT("enabled") : TEXT("disabled"));
	}

	static void MaxNodesCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search max debug nodes: %d"), MaxDebugNodes);
			return;
		}

		MaxDebugNodes = FMath::Clamp(FCString::Atoi(*Args[0]), 128, 1600);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search max debug nodes set to %d"), MaxDebugNodes);
	}

	static void NodeDensityCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search node density: %d"), MaxDebugNodes);
			return;
		}

		MaxDebugNodes = FMath::Clamp(FCString::Atoi(*Args[0]), 128, 1600);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search node density set to %d"), MaxDebugNodes);
	}

	static void CellSizeCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search cell size: %.1f"), CellSize);
			return;
		}

		CellSize = FMath::Clamp(FCString::Atof(*Args[0]), 70.0f, 200.0f);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search cell size set to %.1f"), CellSize);
	}

	static void NodeScaleCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search node scale: %.2f"), NodeScale);
			return;
		}

		NodeScale = FMath::Clamp(FCString::Atof(*Args[0]), 0.25f, 1.25f);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search node scale set to %.2f"), NodeScale);
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
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search visual style set to %s"), *FSecondarySearchDebug::GetVisualStyleName(VisualStyle));
	}

	static void SpeedCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search visual speed: %.2f"), VisualSpeed);
			return;
		}

		VisualSpeed = FMath::Clamp(FCString::Atof(*Args[0]), 0.1f, 5.0f);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search visual speed set to %.2f"), VisualSpeed);
	}

	static void TrailsCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search path history trails: %s"), bTrailsEnabled ? TEXT("enabled") : TEXT("disabled"));
			return;
		}

		bTrailsEnabled = FCString::Atoi(*Args[0]) != 0;
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search path history trails %s"), bTrailsEnabled ? TEXT("enabled") : TEXT("disabled"));
	}

	static void WaveSpeedCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search wave speed: %.2f"), WaveSpeed);
			return;
		}

		WaveSpeed = FMath::Clamp(FCString::Atof(*Args[0]), 0.1f, 5.0f);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search wave speed set to %.2f"), WaveSpeed);
	}

	static void PathHistoryCommand(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search path history count: %d"), PathHistoryCount);
			return;
		}

		PathHistoryCount = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 8);
		Revision++;
		UE_LOG(LogSoul_and_dungeon, Display, TEXT("Secondary search path history count set to %d"), PathHistoryCount);
	}

	static FAutoConsoleCommand ToggleConsoleCommand(
		TEXT("sd.SearchDebug.Toggle"),
		TEXT("Toggle secondary search debug drawing."),
		FConsoleCommandDelegate::CreateStatic(&ToggleCommand));

	static FAutoConsoleCommand ModeConsoleCommand(
		TEXT("sd.SearchDebug.Mode"),
		TEXT("Set secondary search mode. Usage: sd.SearchDebug.Mode BFS or sd.SearchDebug.Mode UCS."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ModeCommand));

	static FAutoConsoleCommand ClearConsoleCommand(
		TEXT("sd.SearchDebug.Clear"),
		TEXT("Clear secondary search debug drawing."),
		FConsoleCommandDelegate::CreateStatic(&ClearCommand));

	static FAutoConsoleCommand XRayConsoleCommand(
		TEXT("sd.SearchDebug.XRay"),
		TEXT("Set secondary search depth-priority rendering. Usage: sd.SearchDebug.XRay 0 or 1."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&XRayCommand));

	static FAutoConsoleCommand MaxNodesConsoleCommand(
		TEXT("sd.SearchDebug.MaxNodes"),
		TEXT("Set secondary search max visible nodes. Usage: sd.SearchDebug.MaxNodes 900."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&MaxNodesCommand));

	static FAutoConsoleCommand NodeDensityConsoleCommand(
		TEXT("sd.SearchDebug.NodeDensity"),
		TEXT("Set secondary search visible node density. Usage: sd.SearchDebug.NodeDensity 900."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&NodeDensityCommand));

	static FAutoConsoleCommand CellSizeConsoleCommand(
		TEXT("sd.SearchDebug.CellSize"),
		TEXT("Set secondary search sampled grid cell size. Usage: sd.SearchDebug.CellSize 100."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CellSizeCommand));

	static FAutoConsoleCommand NodeScaleConsoleCommand(
		TEXT("sd.SearchDebug.NodeScale"),
		TEXT("Set secondary search visual node scale. Usage: sd.SearchDebug.NodeScale 0.45."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&NodeScaleCommand));

	static FAutoConsoleCommand StyleConsoleCommand(
		TEXT("sd.SearchDebug.Style"),
		TEXT("Set secondary search visual style. Usage: sd.SearchDebug.Style Simple or Fluid."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&StyleCommand));

	static FAutoConsoleCommand SpeedConsoleCommand(
		TEXT("sd.SearchDebug.Speed"),
		TEXT("Set secondary search animation speed. Usage: sd.SearchDebug.Speed 1.0."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SpeedCommand));

	static FAutoConsoleCommand TrailsConsoleCommand(
		TEXT("sd.SearchDebug.Trails"),
		TEXT("Set secondary search path history trails. Usage: sd.SearchDebug.Trails 0 or 1."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TrailsCommand));

	static FAutoConsoleCommand WaveSpeedConsoleCommand(
		TEXT("sd.SearchDebug.WaveSpeed"),
		TEXT("Set secondary search path wave speed. Usage: sd.SearchDebug.WaveSpeed 1.0."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&WaveSpeedCommand));

	static FAutoConsoleCommand PathHistoryConsoleCommand(
		TEXT("sd.SearchDebug.PathHistory"),
		TEXT("Set secondary search retained path count. Usage: sd.SearchDebug.PathHistory 3."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PathHistoryCommand));
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
	FSecondarySearchResult Result;
	Result.Mode = Mode;
	Result.StartLocation = Start;
	Result.GoalLocation = Goal;
	Result.DebugStartSeconds = World ? World->GetTimeSeconds() : 0.0f;
	Result.SearchGeneration = NextSearchGeneration();
	Result.VisualizationRevision++;

	const double StartSeconds = FPlatformTime::Seconds();

	if (!World)
	{
		Result.FailureReason = TEXT("Invalid world.");
		return Result;
	}

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem)
	{
		Result.FailureReason = TEXT("Navigation system is not available.");
		return Result;
	}

	FNavLocation ProjectedStart;
	FNavLocation ProjectedGoal;
	if (!NavSystem->ProjectPointToNavigation(Start, ProjectedStart, Settings.ProjectionExtent))
	{
		Result.FailureReason = TEXT("Start point is not on navigation.");
		return Result;
	}

	if (!NavSystem->ProjectPointToNavigation(Goal, ProjectedGoal, Settings.ProjectionExtent))
	{
		Result.FailureReason = TEXT("Goal point is not on navigation.");
		return Result;
	}

	Result.StartLocation = ProjectedStart.Location;
	Result.GoalLocation = ProjectedGoal.Location;

	const FVector Origin = Result.StartLocation;
	const FIntPoint StartKey(0, 0);
	const FIntPoint GoalKey = WorldToGrid(Result.GoalLocation, Origin, Settings.CellSize);

	TMap<FIntPoint, FVector> ProjectedCache;
	ProjectedCache.Add(StartKey, Result.StartLocation);
	ProjectedCache.Add(GoalKey, Result.GoalLocation);

	TMap<FIntPoint, FSecondarySearchNodeRecord> Records;
	Records.Add(StartKey, FSecondarySearchNodeRecord());

	TSet<FIntPoint> ClosedSet;
	TArray<FIntPoint> NeighborOffsets;
	AddNeighborOffsets(Settings.bAllowDiagonal, NeighborOffsets);

	bool bReachedGoal = false;
	FIntPoint EndKey = StartKey;

	if (IsGoalReached(Result.StartLocation, Result.GoalLocation, Settings))
	{
		bReachedGoal = true;
	}
	else if (Mode == ESecondarySearchMode::BFS)
	{
		TArray<FIntPoint> Queue;
		Queue.Add(StartKey);
		int32 QueueHead = 0;

		while (QueueHead < Queue.Num() && Result.ExpandedCount < Settings.MaxExpandedNodes)
		{
			const FIntPoint CurrentKey = Queue[QueueHead++];
			if (ClosedSet.Contains(CurrentKey))
			{
				continue;
			}

			FVector CurrentLocation;
			if (!GetProjectedPoint(World, NavSystem, CurrentKey, Origin, Settings, ProjectedCache, CurrentLocation))
			{
				continue;
			}

			ClosedSet.Add(CurrentKey);
			Result.ExpandedNodes.Add(CurrentLocation);
			AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeExpanded, CurrentLocation, FVector::ZeroVector, Records.FindChecked(CurrentKey).Cost);
			Result.ExpandedCount++;

			if (IsGoalReached(CurrentLocation, Result.GoalLocation, Settings))
			{
				EndKey = CurrentKey;
				bReachedGoal = true;
				break;
			}

			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const FIntPoint NextKey = AddGridOffset(CurrentKey, Offset);
				if (Records.Contains(NextKey) || ClosedSet.Contains(NextKey))
				{
					continue;
				}

				FVector NextLocation;
				if (!GetProjectedPoint(World, NavSystem, NextKey, Origin, Settings, ProjectedCache, NextLocation))
				{
					continue;
				}

				if (!HasClearSegment(World, CurrentLocation, NextLocation))
				{
					continue;
				}

				FSecondarySearchNodeRecord NextRecord;
				NextRecord.CameFrom = CurrentKey;
				NextRecord.Cost = Records.FindChecked(CurrentKey).Cost + 1.0f;
				NextRecord.bHasCameFrom = true;
				Records.Add(NextKey, NextRecord);
				Queue.Add(NextKey);
				Result.FrontierNodes.Add(NextLocation);
				AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeDiscovered, NextLocation, FVector::ZeroVector, NextRecord.Cost);
			}
		}
	}
	else
	{
		TArray<FSecondarySearchOpenItem> OpenSet;
		OpenSet.Add({ StartKey, 0.0f });

		while (OpenSet.Num() > 0 && Result.ExpandedCount < Settings.MaxExpandedNodes)
		{
			int32 BestIndex = 0;
			float BestPriority = OpenSet[0].Priority;
			for (int32 Index = 1; Index < OpenSet.Num(); ++Index)
			{
				if (OpenSet[Index].Priority < BestPriority)
				{
					BestPriority = OpenSet[Index].Priority;
					BestIndex = Index;
				}
			}

			const FIntPoint CurrentKey = OpenSet[BestIndex].Key;
			OpenSet.RemoveAtSwap(BestIndex, 1, EAllowShrinking::No);

			if (ClosedSet.Contains(CurrentKey))
			{
				continue;
			}

			FVector CurrentLocation;
			if (!GetProjectedPoint(World, NavSystem, CurrentKey, Origin, Settings, ProjectedCache, CurrentLocation))
			{
				continue;
			}

			ClosedSet.Add(CurrentKey);
			Result.ExpandedNodes.Add(CurrentLocation);
			AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeExpanded, CurrentLocation, FVector::ZeroVector, Records.FindChecked(CurrentKey).Cost);
			Result.ExpandedCount++;

			if (IsGoalReached(CurrentLocation, Result.GoalLocation, Settings))
			{
				EndKey = CurrentKey;
				bReachedGoal = true;
				break;
			}

			const float CurrentCost = Records[CurrentKey].Cost;
			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const FIntPoint NextKey = AddGridOffset(CurrentKey, Offset);
				if (ClosedSet.Contains(NextKey))
				{
					continue;
				}

				FVector NextLocation;
				if (!GetProjectedPoint(World, NavSystem, NextKey, Origin, Settings, ProjectedCache, NextLocation))
				{
					continue;
				}

				if (!HasClearSegment(World, CurrentLocation, NextLocation))
				{
					continue;
				}

				const float StepCost = FMath::Max(FVector::Dist2D(CurrentLocation, NextLocation), KINDA_SMALL_NUMBER);
				const float NewCost = CurrentCost + StepCost;
				FSecondarySearchNodeRecord* ExistingRecord = Records.Find(NextKey);
				if (!ExistingRecord || NewCost < ExistingRecord->Cost)
				{
					FSecondarySearchNodeRecord NextRecord;
					NextRecord.CameFrom = CurrentKey;
					NextRecord.Cost = NewCost;
					NextRecord.bHasCameFrom = true;
					Records.Add(NextKey, NextRecord);
					OpenSet.Add({ NextKey, NewCost });
					Result.FrontierNodes.Add(NextLocation);
					AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeDiscovered, NextLocation, FVector::ZeroVector, NextRecord.Cost);
				}
			}
		}
	}

	if (bReachedGoal)
	{
		BuildPath(StartKey, EndKey, Records, ProjectedCache, Result);
		AddPathVisualEvents(Result);
		AddVisualEvent(Result, ESecondarySearchVisualEventType::SearchComplete, Result.GoalLocation);
	}
	else if (Result.ExpandedCount >= Settings.MaxExpandedNodes)
	{
		Result.FailureReason = FString::Printf(TEXT("Search reached max expanded node limit: %d."), Settings.MaxExpandedNodes);
		AddVisualEvent(Result, ESecondarySearchVisualEventType::SearchFailed, Result.GoalLocation);
	}
	else
	{
		Result.FailureReason = TEXT("No path found.");
		AddVisualEvent(Result, ESecondarySearchVisualEventType::SearchFailed, Result.GoalLocation);
	}

	Result.ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}

void FSecondarySearchTask::Reset()
{
	Result = FSecondarySearchResult();
	ProjectedCache.Reset();
	Records.Reset();
	ClosedSet.Reset();
	NeighborOffsets.Reset();
	Queue.Reset();
	OpenSet.Reset();
	QueueHead = 0;
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

	const double StartSeconds = FPlatformTime::Seconds();

	if (!World)
	{
		Finish(false, TEXT("Invalid world."));
		return;
	}

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem)
	{
		Finish(false, TEXT("Navigation system is not available."));
		return;
	}

	FNavLocation ProjectedStart;
	FNavLocation ProjectedGoal;
	if (!NavSystem->ProjectPointToNavigation(Start, ProjectedStart, Settings.ProjectionExtent))
	{
		Finish(false, TEXT("Start point is not on navigation."));
		return;
	}

	if (!NavSystem->ProjectPointToNavigation(Goal, ProjectedGoal, Settings.ProjectionExtent))
	{
		Finish(false, TEXT("Goal point is not on navigation."));
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

	if (IsGoalReached(Result.StartLocation, Result.GoalLocation, Settings))
	{
		Finish(true, FString());
	}
	else if (Mode == ESecondarySearchMode::BFS)
	{
		Queue.Add(StartKey);
		bActive = true;
	}
	else
	{
		OpenSet.Add({ StartKey, 0.0f });
		bActive = true;
	}

	Result.ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
}

void FSecondarySearchTask::Step(UWorld* World, const FSecondarySearchSettings& Settings, int32 MaxSteps)
{
	if (!bActive || bFinished || MaxSteps <= 0)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!World || !NavSystem)
	{
		Finish(false, TEXT("Navigation system is not available."));
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
				Finish(false, TEXT("No path found."));
				break;
			}

			CurrentKey = Queue[QueueHead++];
		}
		else
		{
			if (OpenSet.Num() == 0)
			{
				Finish(false, TEXT("No path found."));
				break;
			}

			int32 BestIndex = 0;
			float BestPriority = OpenSet[0].Priority;
			for (int32 Index = 1; Index < OpenSet.Num(); ++Index)
			{
				if (OpenSet[Index].Priority < BestPriority)
				{
					BestPriority = OpenSet[Index].Priority;
					BestIndex = Index;
				}
			}

			CurrentKey = OpenSet[BestIndex].Key;
			OpenSet.RemoveAtSwap(BestIndex, 1, EAllowShrinking::No);
		}

		if (ClosedSet.Contains(CurrentKey))
		{
			continue;
		}

		FVector CurrentLocation;
		if (!GetProjectedPoint(World, NavSystem, CurrentKey, Origin, Settings, ProjectedCache, CurrentLocation))
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
			Finish(true, FString());
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
			if (!GetProjectedPoint(World, NavSystem, NextKey, Origin, Settings, ProjectedCache, NextLocation))
			{
				continue;
			}

			if (!HasClearSegment(World, CurrentLocation, NextLocation))
			{
				continue;
			}

			const float StepCost = Mode == ESecondarySearchMode::BFS
				? 1.0f
				: FMath::Max(FVector::Dist2D(CurrentLocation, NextLocation), KINDA_SMALL_NUMBER);
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
					OpenSet.Add({ NextKey, NewCost });
				}

				Result.FrontierNodes.Add(NextLocation);
				AddVisualEvent(Result, ESecondarySearchVisualEventType::NodeDiscovered, NextLocation, FVector::ZeroVector, NewCost);
				Result.VisualizationRevision++;
			}
		}
	}

	if (!bFinished && Result.ExpandedCount >= Settings.MaxExpandedNodes)
	{
		Finish(false, FString::Printf(TEXT("Search reached max expanded node limit: %d."), Settings.MaxExpandedNodes));
	}

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

void FSecondarySearchTask::Finish(bool bSuccess, const FString& FailureReason)
{
	bActive = false;
	bFinished = true;
	Result.bSuccess = bSuccess;
	Result.FailureReason = FailureReason;
	Result.VisualizationRevision++;

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

float FSecondarySearchDebug::GetCellSize()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::CellSize;
#else
	return 100.0f;
#endif
}

float FSecondarySearchDebug::GetNodeScale()
{
#if !UE_BUILD_SHIPPING
	return SecondarySearchDebugState::NodeScale;
#else
	return 0.45f;
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

FString FSecondarySearchDebug::GetModeName(ESecondarySearchMode Mode)
{
	return Mode == ESecondarySearchMode::BFS ? TEXT("BFS") : TEXT("UCS");
}

FString FSecondarySearchDebug::GetVisualStyleName(ESecondarySearchVisualStyle Style)
{
	return Style == ESecondarySearchVisualStyle::Fluid ? TEXT("Fluid") : TEXT("Simple");
}
