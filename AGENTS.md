# Souls-In-Dungeon MCP Routing

Use the MCP that matches the task. Do not guess.

## Monolith MCP

Use Monolith first for deep asset and graph work.

Best for:
- Blueprint graph editing and Blueprint class inspection
- Animation work, especially AnimBPs, state machines, blend graphs, and hit/death reaction fixes
- Material and material instance authoring
- Niagara authoring and effect graph edits
- UI/widget Blueprint work
- Config inspection and project/source search
- Mesh, project, and engine-source queries
- AI, audio, and other domain-specific authoring where Monolith exposes a direct namespace

Typical use cases:
- Fixing `ABP_Kino`, hit reaction overlays, or death animation issues
- Rewiring Blueprint nodes or inspecting exact asset state before editing
- Searching engine/project symbols before making code changes

Endpoint:
- `http://localhost:9316/mcp`

## Unreal MCP

Use the Unreal MCP bridge for broader editor automation and runtime-oriented editor control.

Best for:
- Asset management, import/export, rename, delete, duplicate
- Actor control, transforms, spawning, and component work
- Editor control such as PIE, screenshots, bookmarks, and viewport tasks
- Level management, streaming, world partition, and data layers
- Sequencer/cinematic tasks
- Gameplay, AI, audio, and system-level editor operations
- Build, log, console, test, and project-setting interactions

Typical use cases:
- Driving the Unreal Editor directly
- Changing project/plugin settings
- Working with levels, actors, PIE sessions, screenshots, or logs
- Using the native MCP transport at `http://localhost:3000/mcp`

## Selection Rule

- If the task is primarily animation, Blueprint, material, Niagara, UI, config, or source search, use Monolith first.
- If the task is primarily editor orchestration, actor/level work, PIE, screenshots, logs, build/test, or project settings, use Unreal MCP first.
- If both apply, use Monolith for asset/graph mutation and Unreal MCP for editor/session/system control.

## Current Local Endpoints

- Monolith MCP: `http://localhost:9316/mcp`
- Unreal MCP: `http://localhost:3000/mcp`

## Unreal editor

 - Once the edit is complete restart via Scripts/restart-project.sh

## Kino Player Hit Reaction

- Active player hit reactions for the ThirdPerson test setup should use `/Game/Characters/Kino/Animations/ABP_Kino.ABP_Kino`.
- The correct route is an AnimGraph overlay, not a full-body state-machine hit state:
  - `Locomotion` state machine as the base pose
  - `Player_Get_Hit_Back` as the overlay pose
  - `LayeredBoneBlend` controlled by `HitReactionOverlayWeight`
  - `bHitReactionOverlay` only selects the hit sequence while the overlay is active
- Keep the overlay torso/head focused. Current branch filter should include `Spine`, `Neck`, and `Head`, and exclude shoulder branches so arms, hands, root, and legs stay owned by locomotion.
- `ASoul_and_dungeonCharacter::StartBackHitReaction()` must set only `HitReactionOverlayWeightTarget = 1.0f`; do not force the current weight to `1.0f`, or the hit reaction snaps instead of blending in.
- `AnimGraphNode_BlendListByBool_0` should keep `bResetChildOnActivation=True` so separate hits restart `Player_Get_Hit_Back` cleanly after the previous overlay finishes.
- Do not use direct `PlayAnimation(Player_Get_Hit_Back)` for the player hit reaction. It switches the mesh into single-node animation playback and can make the Kino/Mixamo root/hips pose sink through the floor. Keep the hit reaction on the `ABP_Kino` layered overlay path instead.
- Keep the C++ constructor default asset reference to `/Game/Characters/Kino/Animations/Player_Get_Hit_Back.Player_Get_Hit_Back`; this matched the earlier working timing implementation and prevents a null `HitReactionAnimation` if Blueprint defaults drift.
