import json
import math

import unreal


LEVEL_PATH = "/Game/Fantastic_Dungeon_Pack/maps/map_dungeon_level_5_bossroom"
BP_DIR = "/Game/Fantastic_Dungeon_Pack/blueprints/gameplay"
BP_PATH = f"{BP_DIR}/BP_BossRoomChestSpawnDirector"
CHEST_BP_PATH = "/Game/Characters/Assests/Interactive_Chest/BP_PROP_chest_Interactive"
DIRECTOR_CLASS_PATH = "/Script/Soul_and_dungeon.LevelChestSpawnDirector"


def load_asset(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def load_blueprint_class(path):
    return unreal.EditorAssetLibrary.load_blueprint_class(path)


def ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def compile_blueprint_if_available(blueprint):
    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "compile_blueprint"):
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    elif hasattr(unreal, "KismetEditorUtilities"):
        unreal.KismetEditorUtilities.compile_blueprint(blueprint)


def ensure_director_blueprint():
    ensure_dir(BP_DIR)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    blueprint = load_asset(BP_PATH)
    if not blueprint:
        director_class = unreal.load_class(None, DIRECTOR_CLASS_PATH)
        if not director_class:
            raise RuntimeError(f"Missing native director class: {DIRECTOR_CLASS_PATH}")

        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", director_class)
        blueprint = asset_tools.create_asset(
            asset_name="BP_BossRoomChestSpawnDirector",
            package_path=BP_DIR,
            asset_class=unreal.Blueprint,
            factory=factory,
        )

    compile_blueprint_if_available(blueprint)
    bp_class = load_blueprint_class(BP_PATH)
    cdo = unreal.get_default_object(bp_class)

    chest_class = load_blueprint_class(CHEST_BP_PATH)
    if not chest_class:
        raise RuntimeError(f"Missing interactive chest class: {CHEST_BP_PATH}")

    cdo.set_editor_property("ChestClass", chest_class)
    cdo.set_editor_property("bEnableChestSpawning", False)
    cdo.set_editor_property("ChestCount", 10)
    cdo.set_editor_property("MaxPlacementAttempts", 900)
    cdo.set_editor_property("MinChestSpacing", 260.0)
    cdo.set_editor_property("bRequireNavigableFloor", True)
    cdo.set_editor_property("NavProjectionExtent", unreal.Vector(350.0, 350.0, 700.0))
    cdo.set_editor_property("FloorTraceHeight", 1600.0)
    cdo.set_editor_property("SpawnFloorClearance", 3.0)
    cdo.set_editor_property("FloorSurfaceNameTokens", ["floor", "ground"])
    cdo.set_editor_property("PlacementClearanceExtent", unreal.Vector(120.0, 120.0, 90.0))
    cdo.set_editor_property("bResetLevelObjectives", True)
    cdo.set_editor_property("bCompleteGameWhenAllChestsOpen", True)
    cdo.set_editor_property("LevelId", "BossRoom")

    compile_blueprint_if_available(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    return blueprint, bp_class


def actor_name(actor):
    label = ""
    try:
        label = actor.get_actor_label()
    except Exception:
        pass
    return f"{actor.get_name()} {label} {actor.get_class().get_name()}".lower()


def find_actor(actors, needle):
    needle = needle.lower()
    for actor in actors:
        if needle in actor_name(actor):
            return actor
    return None


def normalize_2d(vector):
    length = math.sqrt(vector.x * vector.x + vector.y * vector.y)
    if length <= 0.01:
        return unreal.Vector(1.0, 0.0, 0.0)
    return unreal.Vector(vector.x / length, vector.y / length, 0.0)


def yaw_from_direction(direction):
    return math.degrees(math.atan2(direction.y, direction.x))


def make_yaw_rotator(yaw):
    rotation = unreal.Rotator()
    rotation.set_editor_property("pitch", 0.0)
    rotation.set_editor_property("yaw", yaw)
    rotation.set_editor_property("roll", 0.0)
    return rotation


def update_level_actor(bp_class):
    if not unreal.EditorLevelLibrary.load_level(LEVEL_PATH):
        raise RuntimeError(f"Could not load level: {LEVEL_PATH}")

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    fog = find_actor(actors, "BP_FX_fog_door_dungeon_01")
    door = find_actor(actors, "BP_COMP_Door_Interactive_Large")
    directors = [actor for actor in actors if "bp_bossroomchestspawndirector" in actor_name(actor)]
    director = directors[0] if directors else None

    for extra_director in directors[1:]:
        unreal.EditorLevelLibrary.destroy_actor(extra_director)

    if fog and door:
        direction = normalize_2d(fog.get_actor_location() - door.get_actor_location())
        location = fog.get_actor_location() + (direction * 1050.0)
        rotation = make_yaw_rotator(yaw_from_direction(direction))
    elif fog:
        direction = normalize_2d(fog.get_actor_forward_vector())
        location = fog.get_actor_location() + (direction * 1050.0)
        rotation = make_yaw_rotator(yaw_from_direction(direction))
    else:
        location = unreal.Vector(0.0, 0.0, 120.0)
        rotation = make_yaw_rotator(0.0)

    location.z += 120.0
    bounds_extent = unreal.Vector(1900.0, 1350.0, 650.0)

    if not director:
        director = unreal.EditorLevelLibrary.spawn_actor_from_class(bp_class, location, rotation)
        director.set_actor_label("BP_BossRoomChestSpawnDirector")
    else:
        director.set_actor_location(location, False, False)
        director.set_actor_rotation(rotation, False)

    director.set_editor_property("bEnableChestSpawning", False)

    spawn_bounds = director.get_component_by_class(unreal.BoxComponent)
    if not spawn_bounds:
        raise RuntimeError("BP_BossRoomChestSpawnDirector is missing SpawnBounds BoxComponent")
    spawn_bounds.set_box_extent(bounds_extent, True)

    unreal.EditorLevelLibrary.save_current_level()
    return {
        "director": director.get_actor_label(),
        "director_location": [round(location.x, 2), round(location.y, 2), round(location.z, 2)],
        "director_rotation": [round(rotation.pitch, 2), round(rotation.yaw, 2), round(rotation.roll, 2)],
        "bounds_extent": [round(bounds_extent.x, 2), round(bounds_extent.y, 2), round(bounds_extent.z, 2)],
        "fog_actor": fog.get_actor_label() if fog else None,
        "door_actor": door.get_actor_label() if door else None,
    }


def main():
    blueprint, bp_class = ensure_director_blueprint()
    level_result = update_level_actor(bp_class)
    print(json.dumps({
        "blueprint": blueprint.get_path_name(),
        "chest_class": CHEST_BP_PATH,
        "level": LEVEL_PATH,
        **level_result,
    }, indent=2))


if __name__ == "__main__":
    main()
