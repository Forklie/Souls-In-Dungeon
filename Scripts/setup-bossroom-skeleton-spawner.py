import json
import math

import unreal


LEVEL_PATH = "/Game/Fantastic_Dungeon_Pack/maps/map_dungeon_level_5_bossroom"
BP_DIR = "/Game/Fantastic_Dungeon_Pack/blueprints/gameplay"
BP_PATH = f"{BP_DIR}/BP_BossRoomSkeletonSpawnDirector"
MAT_DIR = "/Game/Characters/Skeleton/Skeleton"
MAT_PATH = f"{MAT_DIR}/M_Skeleton_SpawnFade"
MI_PATH = f"{MAT_DIR}/MI_Skeleton_SpawnFade"
TEXTURE_PATH = "/Game/Characters/Skeleton/Skeleton/skeleton_texture"
ENEMY_BP_PATH = "/Game/ThirdPerson/Blueprints/BP_Skeleton"
DIRECTOR_CLASS_PATH = "/Script/Soul_and_dungeon.BossRoomSkeletonSpawnDirector"


def load_asset(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def load_blueprint_class(path):
    return unreal.EditorAssetLibrary.load_blueprint_class(path)


def ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def clear_material(material):
    if hasattr(unreal.MaterialEditingLibrary, "delete_all_material_expressions"):
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)


def ensure_fade_material():
    ensure_dir(MAT_DIR)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    material = load_asset(MAT_PATH)
    if not material:
        material = asset_tools.create_asset(
            asset_name="M_Skeleton_SpawnFade",
            package_path=MAT_DIR,
            asset_class=unreal.Material,
            factory=unreal.MaterialFactoryNew(),
        )

    texture = load_asset(TEXTURE_PATH)
    if not texture:
        raise RuntimeError(f"Missing skeleton texture: {TEXTURE_PATH}")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", False)
    clear_material(material)

    texture_node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, -480, -80
    )
    texture_node.set_editor_property("texture", texture)

    fade_param = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -480, 140
    )
    fade_param.set_editor_property("parameter_name", "SpawnFade")
    fade_param.set_editor_property("default_value", 1.0)

    roughness = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -480, 280
    )
    roughness.set_editor_property("r", 0.65)

    unreal.MaterialEditingLibrary.connect_material_property(
        texture_node, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        fade_param, "", unreal.MaterialProperty.MP_OPACITY
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)

    instance = load_asset(MI_PATH)
    if not instance:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        instance = asset_tools.create_asset(
            asset_name="MI_Skeleton_SpawnFade",
            package_path=MAT_DIR,
            asset_class=unreal.MaterialInstanceConstant,
            factory=factory,
        )

    instance.set_editor_property("parent", material)

    unreal.EditorAssetLibrary.save_loaded_asset(instance)
    return material, instance


def ensure_director_blueprint(fade_material):
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
            asset_name="BP_BossRoomSkeletonSpawnDirector",
            package_path=BP_DIR,
            asset_class=unreal.Blueprint,
            factory=factory,
        )

    compile_blueprint_if_available(blueprint)
    bp_class = load_blueprint_class(BP_PATH)
    cdo = unreal.get_default_object(bp_class)

    enemy_class = load_blueprint_class(ENEMY_BP_PATH)
    if enemy_class:
        cdo.set_editor_property("EnemyClass", enemy_class)
    cdo.set_editor_property("SpawnFadeMaterial", fade_material)
    cdo.set_editor_property("SpawnBudget", 10)
    cdo.set_editor_property("MaxAlive", 3)
    cdo.set_editor_property("InitialDelay", 1.5)
    cdo.set_editor_property("BaseDelay", 4.0)
    cdo.set_editor_property("MinDelay", 2.0)
    cdo.set_editor_property("MaxDelay", 8.0)
    cdo.set_editor_property("FadeDuration", 1.2)

    compile_blueprint_if_available(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    return blueprint, bp_class


def compile_blueprint_if_available(blueprint):
    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "compile_blueprint"):
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    elif hasattr(unreal, "KismetEditorUtilities"):
        unreal.KismetEditorUtilities.compile_blueprint(blueprint)


def actor_name(actor):
    label = ""
    try:
        label = actor.get_actor_label()
    except Exception:
        label = ""
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
    director = find_actor(actors, "BP_BossRoomSkeletonSpawnDirector")

    if fog and door:
        direction = normalize_2d(fog.get_actor_location() - door.get_actor_location())
        trigger_location = door.get_actor_location() + (direction * 650.0)
        trigger_rotation = make_yaw_rotator(yaw_from_direction(direction))
        trigger_extent = unreal.Vector(850.0, 520.0, 260.0)
    elif fog:
        direction = normalize_2d(fog.get_actor_forward_vector())
        trigger_location = fog.get_actor_location() - (direction * 550.0)
        trigger_rotation = make_yaw_rotator(yaw_from_direction(direction))
        trigger_extent = unreal.Vector(850.0, 520.0, 260.0)
    else:
        trigger_location = unreal.Vector(0.0, 0.0, 120.0)
        trigger_rotation = make_yaw_rotator(0.0)
        trigger_extent = unreal.Vector(850.0, 520.0, 260.0)

    if not director:
        director = unreal.EditorLevelLibrary.spawn_actor_from_class(
            bp_class, trigger_location, trigger_rotation
        )
        director.set_actor_label("BP_BossRoomSkeletonSpawnDirector")
    else:
        director.set_actor_location(trigger_location, False, False)
        director.set_actor_rotation(trigger_rotation, False)

    if fog:
        director.set_editor_property("FogDoorActor", fog)

    trigger = director.get_component_by_class(unreal.BoxComponent)
    if trigger:
        trigger.set_box_extent(trigger_extent, True)

    unreal.EditorLevelLibrary.save_current_level()
    return {
        "director": director.get_actor_label(),
        "director_location": tuple(round(v, 2) for v in [
            director.get_actor_location().x,
            director.get_actor_location().y,
            director.get_actor_location().z,
        ]),
        "trigger_extent": tuple(round(v, 2) for v in [
            trigger_extent.x,
            trigger_extent.y,
            trigger_extent.z,
        ]),
        "fog_actor": fog.get_actor_label() if fog else None,
        "door_actor": door.get_actor_label() if door else None,
    }


def main():
    material, fade_instance = ensure_fade_material()
    blueprint, bp_class = ensure_director_blueprint(fade_instance)
    level_result = update_level_actor(bp_class)
    print(json.dumps({
        "material": material.get_path_name(),
        "fade_instance": fade_instance.get_path_name(),
        "blueprint": blueprint.get_path_name(),
        "level": LEVEL_PATH,
        **level_result,
    }, indent=2))


if __name__ == "__main__":
    main()
