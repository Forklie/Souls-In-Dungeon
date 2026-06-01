# Enemy A* Navigation Handoff

## Changed files

- `Source/Soul_and_dungeon/SecondarySearchSolver.cpp`
- `Source/Soul_and_dungeon/MyAIController.h`
- `Source/Soul_and_dungeon/MyAIController.cpp`
- `Source/Soul_and_dungeon/EnemyLearningInteractor.h`
- `Source/Soul_and_dungeon/EnemyLearningInteractor.cpp`
- `Source/Soul_and_dungeon/Soul_and_dungeon.Build.cs`
- `Docs/AStarNavigationHandoff.md`
- `Docs/ReinforcementLearningEnemyNavigation.md`

## Implementation summary

The skeleton enemy now uses the existing secondary search solver's A* mode as its real chase route. `AMyAIController` replans on a throttle, stores the latest successful A* path, advances through path waypoints, and falls back to the last valid path or direct `MoveToLocation` if no A* path is available.

The debug visualizer still runs BFS, UCS, and A* side by side when enabled. That keeps the algorithm comparison available while allowing A* to drive the actual enemy movement.

The current chase path is now smoothed by default with `sd.EnemyNavigation.Mode 1`. Use `sd.EnemyNavigation.Mode 0` to compare raw A* waypointing, or `sd.EnemyNavigation.Mode 2` to allow Learning Agents steering when a policy is wired.

## Priority queue and heuristic

The A* open set is a binary min-heap stored in a `TArray<FSecondarySearchOpenItem>`. `PushOpenItem` bubbles lower-priority entries upward, and `PopBestOpenItem` removes the lowest-priority entry then restores heap order.

A* uses:

```text
f(n) = g(n) + h(n)
```

- `g(n)` is the path cost already paid to reach the node.
- `h(n)` is the 2D Euclidean straight-line distance from that node to the goal.

The heuristic is admissible because straight-line distance never overestimates the shortest walkable route. Every legal movement segment has nonnegative physical distance cost, so any real route to the goal must be at least as long as the direct 2D distance. It is also consistent because moving from one node to a neighbor cannot reduce the straight-line distance by more than the movement cost between those two nodes.

## Editor test steps

1. Build:

   ```sh
   "/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" Soul_and_dungeonEditor Mac Development -Project="/Users/chaturnakasturiratna/Documents/Unreal Projects/Souls-In-Dungeon/Soul_and_dungeon.uproject" -WaitMutex
   ```

2. Restart the project:

   ```sh
   Scripts/restart-project.sh
   ```

3. In `/Game/ThirdPerson/Lvl_ThirdPerson`, play the level and verify the skeleton follows the player through A* waypoints and still stops to attack within range.
4. Press `B` or run `sd.SearchDebug.Toggle`, then run `sd.SearchDebug.Mode AStar` to show the A* debug path.
5. Compare A* with UCS in the debug HUD. A* should usually expand fewer nodes while keeping a similar path cost.
