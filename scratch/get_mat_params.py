import unreal

mat_path = '/Game/InteractableObjectHighlight/Materials/MaterialInstances/MI_LineMaterial.MI_LineMaterial'
mat = unreal.EditorAssetLibrary.load_asset(mat_path)
if not mat:
    print("Could not load material instance!")
else:
    print(f"Loaded: {mat.get_name()}")
    try:
        scalar_params = unreal.MaterialEditingLibrary.get_scalar_parameter_names(mat)
        for param in scalar_params:
            val = unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(mat, param)
            print(f"Scalar: {param} = {val}")
    except Exception as e:
        print(f"Error reading parameters: {e}")
