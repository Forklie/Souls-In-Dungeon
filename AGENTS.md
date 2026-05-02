# Project Notes for Codex

Use Monolith MCP to inspect Unreal assets when possible.

## Current Gameplay Test Setup

- The active gameplay test level is usually `/Game/ThirdPerson/Lvl_ThirdPerson`.
- `/Game/ThirdPerson/Lvl_ThirdPerson` uses `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode`.
- `BP_ThirdPersonGameMode` spawns `/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter` as the player pawn.
- `BP_ThirdPersonCharacter` is based on `ASoul_and_dungeonCharacter` in:
  - `Source/Soul_and_dungeon/Soul_and_dungeonCharacter.h`
  - `Source/Soul_and_dungeon/Soul_and_dungeonCharacter.cpp`
- The placed `/Game/ThirdPerson/Blueprints/BP_Skeleton` actors in `Lvl_ThirdPerson` are enemies, not the player.
- Enemy damage in this level currently comes from `Source/Soul_and_dungeon/MyAIController.cpp`, which calls `ASoul_and_dungeonCharacter::TakeDamageSimple()`.

## Player Hit Reaction

- The player mesh is Kino:
  - Skeletal mesh: `/Game/Characters/Kino/Tpose/Player_T-Pose`
  - Anim Blueprint: `/Game/Characters/Kino/Animations/ABP_Kino`
  - Hit animation: `/Game/Characters/Kino/Animations/Player_Get_Hit_Back`
- For `Lvl_ThirdPerson`, wire player hit reactions through `ASoul_and_dungeonCharacter::TakeDamageSimple()`, not through `BP_Skeleton`.
- `BP_Skeleton` uses the skeleton enemy mesh and `/Game/Characters/Skeleton/Animation/ABP_Skeleton`; do not assign player hit-back animation there.
- Hit-back should be driven through `ABP_Kino` variables `bHitReacting` and `bHitReactionFinished` so the `Player Hit Back` state machine transitions can blend.
- The current `ABP_Kino` hit fix for `Lvl_ThirdPerson` is an upper-body overlay in `AnimGraph`, not a full-body locomotion state swap: `Locomotion State Machine -> LayeredBoneBlend BasePose`, `Player_Get_Hit_Back -> BlendPoses_0`, `HitReactionOverlayWeight -> BlendWeights_0`, branching from `Spine`. Keep locomotion driving the legs while the hit reaction is active.
- `HitReactionOverlayWeight` is movement-aware in `ASoul_and_dungeonCharacter`: stronger while idle, lighter while running, so the hit-back pose blends against the current locomotion pose instead of overpowering run or fast-run motion.
- Do not force the player mesh into `AnimationSingleNode` for hit-back unless the AnimBP route is unavailable; direct playback snaps with no blend and restarts badly on overlapping hits.
- Repeated enemy hits should not restart the hit-back animation while it is already active. Damage may still apply, but the animation should finish and blend out.
- `Player_Get_Hit_Back` is a back-hit animation. Only trigger it when the damage causer is behind the player; front/side hits should currently apply damage without playing this back-hit pose until matching front/left/right hit assets exist.
- `MyAIController` should pass the enemy pawn into `ASoul_and_dungeonCharacter::TakeDamageSimple(DamageAmount, DamageCauser)` so the player can classify hit direction.
- `Player_Get_Hit_Back` is tuned faster for gameplay: sequence `RateScale` is `1.5`, and player/combat `HitReactionPlayRate` defaults are `1.5`. Keep the timer and asset rate aligned when retuning.
- `MyAIController` attack timing is cycle-based: `AttackInterval` should match `/Game/Characters/Skeleton/Animation/Mini_skeleton_Attack` length, with damage applied once at `AttackDelay` in each cycle. Do not use a separate long cooldown because it drifts behind the visible attack loop.
- `Player_Get_Hit_Back` lower-body tracks are stabilized from Kino idle so the back-hit reaction does not distort/enlarge legs or sink the player into the ground. Use the exact Kino bone names (`Hips`, `LeftUpLeg`, `RightUpLeg`, etc.) when editing tracks; lowercase names can fail to update the real animation tracks. Keep future edits upper-body focused unless a clean full-body hit asset is imported.

## Combat Variant Note

- `/Game/Variant_Combat/Lvl_Combat` uses `/Game/Variant_Combat/Blueprints/BP_CombatGameMode`.
- `BP_CombatGameMode` spawns `/Game/Variant_Combat/Blueprints/BP_CombatCharacter`.
- Combat-variant damage goes through `ACombatCharacter::ApplyDamage()`.
- Do not assume fixes in `ACombatCharacter` affect the ThirdPerson test level unless the test level is changed to use `BP_CombatGameMode`.
