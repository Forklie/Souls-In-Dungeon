import unreal


MAT_PATH = "/Game/Characters/Skeleton/Skeleton/M_Skeleton_SpawnFade"


def main():
    material = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
    if not material:
        raise RuntimeError(f"Missing material: {MAT_PATH}")

    if hasattr(unreal, "MaterialEditingLibrary") and hasattr(unreal.MaterialEditingLibrary, "set_material_usage"):
        unreal.MaterialEditingLibrary.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    print(f"Updated skeletal mesh usage for {material.get_path_name()}")


if __name__ == "__main__":
    main()
