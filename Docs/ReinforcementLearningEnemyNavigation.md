# Hybrid A* + Smooth Steering + Reinforcement Learning Enemy Navigation

## Changed files

- `Soul_and_dungeon.uproject`
- `Source/Soul_and_dungeon/Soul_and_dungeon.Build.cs`
- `Source/Soul_and_dungeon/SecondarySearchSolver.cpp`
- `Source/Soul_and_dungeon/MyAIController.h`
- `Source/Soul_and_dungeon/MyAIController.cpp`
- `Source/Soul_and_dungeon/EnemyLearningInteractor.h`
- `Source/Soul_and_dungeon/EnemyLearningInteractor.cpp`
- `Docs/AStarNavigationHandoff.md`
- `Docs/ReinforcementLearningEnemyNavigation.md`

## Runtime architecture

The enemy keeps A* as the global planner. `AMyAIController` computes a throttled A* route to the player, stores the latest successful raw path, and falls back to direct `MoveToLocation` only if no valid path is available.

The default movement mode is smoothed A*. Use this console variable to switch modes:

```text
sd.EnemyNavigation.Mode 0  # AStarOnly
sd.EnemyNavigation.Mode 1  # SmoothedAStar
sd.EnemyNavigation.Mode 2  # LearningWithAStarFallback
```

`LearningWithAStarFallback` only consumes learned steering when a fresh Learning Agents action exists. Without a trained policy or manager driving `UEnemyLearningInteractor`, it follows the same smoothed A* route.

## Path smoothing

The raw A* path still comes from the grid-based `SecondarySearchSolver`. Before movement, `AMyAIController` post-processes it:

1. Line-of-sight shortcutting checks whether later path points can be reached directly on the navmesh.
2. Catmull-Rom style interpolation samples a curved path through the shortcut points.
3. Every generated sample is projected back to navigation and segment-checked with `NavigationRaycast`.
4. If any curve sample is invalid, the controller uses the shortcut path. If smoothing cannot produce a safe path, it keeps raw A*.

The controller follows a look-ahead target on the active path instead of moving cell corner to cell corner. This is the part that reduces visible zig-zag.

## Learning Agents layer

`UEnemyLearningInteractor` exposes the controller as a Learning Agents interactor. It is designed for local movement and recovery, not global route planning.

### Observation vector

| Index | Value | Normalization |
| --- | --- | --- |
| 0-1 | Direction to player X/Y | Unit 2D direction |
| 2-3 | Direction to smoothed path target X/Y | Unit 2D direction |
| 4-5 | Enemy velocity X/Y | Clamped to 600 uu/s and divided by 600 |
| 6 | Distance to player | `Distance / 3000`, clamped 0-1 |
| 7 | Distance to path target | `Distance / 1000`, clamped 0-1 |
| 8 | Stuck seconds | `Seconds / 5`, clamped 0-1 |
| 9 | Path progress | Waypoint index over path length |
| 10 | Line of sight to player | `1` when clear, otherwise `0` |

### Action vector

| Index | Value | Meaning |
| --- | --- | --- |
| 0 | Local steering X | 2D movement intent |
| 1 | Local steering Y | 2D movement intent |

The action is clamped to length 1 and projected ahead of the enemy. A* remains the safety fallback.

## Training design

Start with imitation learning from smoothed A* behavior. Record expert pairs from the observation vector and the controller's current smoothed-path steering direction.

After imitation produces stable local steering, fine-tune with PPO:

| Reward/Penalty | Intent |
| --- | --- |
| Positive path progress | Move along the smoothed A* route |
| Positive attack range arrival | Reward reaching `StopDistance` |
| Positive successful hit | Reward useful chase completion |
| Heading smoothness reward | Reduce sudden turn oscillation |
| Zig-zag penalty | Penalize large alternating heading changes |
| Stuck penalty | Penalize no-progress time |
| Moving-away penalty | Penalize increasing distance to player |
| Collision/no-progress penalty | Discourage pushing into obstacles |
| Timeout penalty | End failed chase episodes |

## Test steps

1. Build:

   ```sh
   "/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" Soul_and_dungeonEditor Mac Development -Project="/Users/chaturnakasturiratna/Documents/Unreal Projects/Souls-In-Dungeon/Soul_and_dungeon.uproject" -WaitMutex
   ```

2. Restart:

   ```sh
   Scripts/restart-project.sh
   ```

3. In the ThirdPerson level, test:
   - `sd.EnemyNavigation.Mode 0`: raw A* still follows and attacks.
   - `sd.EnemyNavigation.Mode 1`: default smoothed A* reaches attack range with less zig-zag.
   - `sd.EnemyNavigation.Mode 2`: behaves like smoothed A* unless a Learning Agents policy is actively providing actions.

4. Toggle search debug:

   ```text
   sd.SearchDebug.Toggle
   sd.SearchDebug.Mode AStar
   ```

5. Compare against UCS in the debug HUD. A* should keep a similar path cost while expanding fewer nodes in typical cases.

