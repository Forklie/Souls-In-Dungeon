import json
import os

import unreal


LEVEL_PATH = "/Game/Fantastic_Dungeon_Pack/maps/map_dungeon_level_5_bossroom"
OUTPUT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "bossroom_spawner_inspection.json")


def actor_text(actor):
    label = ""
    try:
        label = actor.get_actor_label()
    except Exception:
        pass
    return f"{actor.get_name()} {label} {actor.get_class().get_name()}".lower()


def vector_data(vector):
    return [round(vector.x, 2), round(vector.y, 2), round(vector.z, 2)]


def rotator_data(rotator):
    return [round(rotator.pitch, 2), round(rotator.yaw, 2), round(rotator.roll, 2)]


def actor_data(actor):
    origin = unreal.Vector()
    extent = unreal.Vector()
    try:
        origin, extent = actor.get_actor_bounds(False)
    except Exception:
        pass

    data = {
        "name": actor.get_name(),
        "label": actor.get_actor_label(),
        "class": actor.get_class().get_name(),
        "location": vector_data(actor.get_actor_location()),
        "rotation": rotator_data(actor.get_actor_rotation()),
        "bounds_origin": vector_data(origin),
        "bounds_extent": vector_data(extent),
    }

    box = actor.get_component_by_class(unreal.BoxComponent)
    if box:
        data["box_relative_location"] = vector_data(box.get_editor_property("relative_location"))
        data["box_relative_rotation"] = rotator_data(box.get_editor_property("relative_rotation"))
        data["box_unscaled_extent"] = vector_data(box.get_unscaled_box_extent())
        data["box_scaled_extent"] = vector_data(box.get_scaled_box_extent())
        data["box_generate_overlap_events"] = bool(box.get_editor_property("generate_overlap_events"))
        try:
            data["box_collision_profile"] = str(box.get_collision_profile_name())
        except Exception:
            data["box_collision_profile"] = None

    if "bp_bossroomskeletonspawndirector" in actor_text(actor):
        fog_ref = actor.get_editor_property("FogDoorActor")
        enemy_class = actor.get_editor_property("EnemyClass")
        fade_material = actor.get_editor_property("SpawnFadeMaterial")
        data["fog_ref"] = fog_ref.get_actor_label() if fog_ref else None
        data["enemy_class"] = enemy_class.get_name() if enemy_class else None
        data["spawn_budget"] = actor.get_editor_property("SpawnBudget")
        data["max_alive"] = actor.get_editor_property("MaxAlive")
        data["initial_delay"] = actor.get_editor_property("InitialDelay")
        data["base_delay"] = actor.get_editor_property("BaseDelay")
        data["fade_duration"] = actor.get_editor_property("FadeDuration")
        data["fade_material"] = fade_material.get_path_name() if fade_material else None

    return data


def main():
    if not unreal.EditorLevelLibrary.load_level(LEVEL_PATH):
        raise RuntimeError(f"Could not load level: {LEVEL_PATH}")

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    interesting = []
    for actor in actors:
        text = actor_text(actor)
        if (
            "bp_bossroomskeletonspawndirector" in text
            or "bp_fx_fog_door_dungeon_01" in text
            or "bp_comp_door_interactive_large" in text
            or "playerstart" in text
            or "navmeshboundsvolume" in text
            or "boss" in text
            or "spawn" in text
        ):
            interesting.append(actor_data(actor))

    result = {
        "level": LEVEL_PATH,
        "actor_count": len(actors),
        "interesting_count": len(interesting),
        "interesting": interesting,
    }

    with open(OUTPUT_PATH, "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)

    unreal.log(f"Bossroom skeleton spawner inspection written to {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
