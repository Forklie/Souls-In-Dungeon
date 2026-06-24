import json
import os

import unreal


LEVEL_PATH = "/Game/Fantastic_Dungeon_Pack/maps/map_dungeon_level_5_bossroom"
OUTPUT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "bossroom_chest_spawner_verification.json")


def actor_name(actor):
    label = ""
    try:
        label = actor.get_actor_label()
    except Exception:
        pass
    return f"{actor.get_name()} {label} {actor.get_class().get_name()}".lower()


def main():
    if not unreal.EditorLevelLibrary.load_level(LEVEL_PATH):
        raise RuntimeError(f"Could not load level: {LEVEL_PATH}")

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    directors = [actor for actor in actors if "bp_bossroomchestspawndirector" in actor_name(actor)]
    preplaced_chests = [
        actor for actor in actors
        if "bp_prop_chest_interactive" in actor_name(actor)
    ]

    result = {
        "level": LEVEL_PATH,
        "director_count": len(directors),
        "preplaced_interactive_chest_count": len(preplaced_chests),
        "directors": [],
    }

    if len(directors) != 1:
        raise RuntimeError(f"Expected exactly one bossroom chest spawn director, found {len(directors)}")

    director = directors[0]
    spawn_bounds = director.get_component_by_class(unreal.BoxComponent)
    if not spawn_bounds:
        raise RuntimeError("Bossroom chest spawn director is missing SpawnBounds BoxComponent")

    extent = spawn_bounds.get_unscaled_box_extent()
    chest_class = director.get_editor_property("ChestClass")
    floor_tokens = [str(token).lower() for token in director.get_editor_property("FloorSurfaceNameTokens")]
    clearance_extent = director.get_editor_property("PlacementClearanceExtent")
    loc = director.get_actor_location()
    rot = director.get_actor_rotation()

    result["directors"].append({
        "label": director.get_actor_label(),
        "class": director.get_class().get_name(),
        "chest_class": chest_class.get_name() if chest_class else None,
        "spawning_enabled": director.get_editor_property("bEnableChestSpawning"),
        "chest_count": director.get_editor_property("ChestCount"),
        "complete_game": director.get_editor_property("bCompleteGameWhenAllChestsOpen"),
        "floor_tokens": floor_tokens,
        "clearance_extent": [round(clearance_extent.x, 2), round(clearance_extent.y, 2), round(clearance_extent.z, 2)],
        "location": [round(loc.x, 2), round(loc.y, 2), round(loc.z, 2)],
        "rotation": [round(rot.pitch, 2), round(rot.yaw, 2), round(rot.roll, 2)],
        "bounds_extent": [round(extent.x, 2), round(extent.y, 2), round(extent.z, 2)],
    })

    if not chest_class or "BP_PROP_chest_Interactive" not in chest_class.get_name():
        raise RuntimeError(f"Director has wrong ChestClass: {chest_class}")

    if director.get_editor_property("ChestCount") != 10:
        raise RuntimeError("Bossroom chest director ChestCount must be 10")

    if director.get_editor_property("bEnableChestSpawning"):
        raise RuntimeError("Bossroom chest director spawning must be disabled while chests are manually placed")

    if not director.get_editor_property("bCompleteGameWhenAllChestsOpen"):
        raise RuntimeError("Bossroom chest director must complete the game after all chests open")

    if "floor" not in floor_tokens:
        raise RuntimeError(f"Bossroom chest director must require floor placement tokens: {floor_tokens}")

    if clearance_extent.x < 100.0 or clearance_extent.y < 100.0 or clearance_extent.z < 60.0:
        raise RuntimeError(f"Bossroom chest placement clearance is too small: {clearance_extent}")

    if extent.x < 1200.0 or extent.y < 900.0 or extent.z < 300.0:
        raise RuntimeError(f"Bossroom chest spawn bounds are too small: {extent}")

    with open(OUTPUT_PATH, "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)

    unreal.log("BossroomChestSpawnerVerification: " + json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
