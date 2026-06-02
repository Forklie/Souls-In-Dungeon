import json
import os

import unreal


LEVEL_PATH = "/Game/Fantastic_Dungeon_Pack/maps/map_dungeon_level_5_bossroom"
OUTPUT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "bossroom_spawner_verification.json")


def actor_name(actor):
    try:
        label = actor.get_actor_label()
    except Exception:
        label = ""
    return f"{actor.get_name()} {label} {actor.get_class().get_name()}".lower()


def main():
    if not unreal.EditorLevelLibrary.load_level(LEVEL_PATH):
        raise RuntimeError(f"Could not load level: {LEVEL_PATH}")

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    directors = [actor for actor in actors if "bp_bossroomskeletonspawndirector" in actor_name(actor)]
    fog_doors = [actor for actor in actors if "bp_fx_fog_door_dungeon_01" in actor_name(actor)]
    doors = [actor for actor in actors if "bp_comp_door_interactive_large" in actor_name(actor)]

    result = {
        "level": LEVEL_PATH,
        "director_count": len(directors),
        "fog_door_count": len(fog_doors),
        "interactive_large_door_count": len(doors),
        "directors": [],
    }

    for director in directors:
        fog_ref = director.get_editor_property("FogDoorActor")
        loc = director.get_actor_location()
        rot = director.get_actor_rotation()
        trigger = director.get_component_by_class(unreal.BoxComponent)
        extent = trigger.get_unscaled_box_extent() if trigger else unreal.Vector()
        result["directors"].append({
            "label": director.get_actor_label(),
            "class": director.get_class().get_name(),
            "fog_ref": fog_ref.get_actor_label() if fog_ref else None,
            "location": [round(loc.x, 2), round(loc.y, 2), round(loc.z, 2)],
            "rotation": [round(rot.pitch, 2), round(rot.yaw, 2), round(rot.roll, 2)],
            "trigger_extent": [round(extent.x, 2), round(extent.y, 2), round(extent.z, 2)],
        })

    if len(directors) != 1:
        raise RuntimeError(f"Expected exactly one bossroom skeleton spawn director, found {len(directors)}")

    if not directors[0].get_editor_property("FogDoorActor"):
        raise RuntimeError("Bossroom skeleton spawn director is missing FogDoorActor reference")

    if not fog_doors:
        raise RuntimeError("No BP_FX_fog_door_dungeon_01 actor found in bossroom level")

    if not doors:
        raise RuntimeError("No BP_COMP_Door_Interactive_Large actor found in bossroom level")

    trigger = directors[0].get_component_by_class(unreal.BoxComponent)
    if not trigger:
        raise RuntimeError("Bossroom skeleton spawn director is missing TriggerBox component")

    extent = trigger.get_unscaled_box_extent()
    if extent.x < 800.0 or extent.y < 500.0 or extent.z < 250.0:
        raise RuntimeError(f"Bossroom skeleton spawn trigger is too small: {extent}")

    rotation = directors[0].get_actor_rotation()
    if abs(rotation.pitch) > 1.0 or abs(rotation.roll) > 1.0:
        raise RuntimeError(f"Bossroom skeleton spawn director trigger is tilted: {rotation}")

    with open(OUTPUT_PATH, "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)

    unreal.log("BossroomSkeletonSpawnerVerification: " + json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
