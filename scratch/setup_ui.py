import unreal

def create_and_setup_widget():
    # 1. Ensure the WBP_InteractPrompt exists and is loaded properly.
    asset_path = "/Game/UI/WBP_InteractPrompt"
    
    # Try to load if it already exists
    widget_bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    
    # If not, create it
    if not widget_bp:
        factory = unreal.WidgetBlueprintFactory()
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        widget_bp = asset_tools.create_asset("WBP_InteractPrompt", "/Game/UI", unreal.WidgetBlueprint, factory)
    
    if not widget_bp:
        print("Failed to create or load WBP_InteractPrompt")
        return

    print("Widget BP loaded.")

    # In UE5 Python, modifying widget trees directly can be tricky.
    # But we can try to set the InteractWidgetClass on the character blueprint.

    char_bp_path = "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"
    char_bp = unreal.EditorAssetLibrary.load_asset(char_bp_path)
    if not char_bp:
        print("Failed to load character BP.")
        return

    # To set the default value of InteractWidgetClass, we need the CDO of the generated class.
    gen_class = char_bp.generated_class
    if gen_class:
        cdo = unreal.get_default_object(gen_class)
        if cdo:
            # We want to set the InteractWidgetClass property.
            # The class of the widget is widget_bp.generated_class
            try:
                cdo.set_editor_property("InteractWidgetClass", widget_bp.generated_class)
                unreal.EditorAssetLibrary.save_asset(char_bp_path)
                print("Successfully set InteractWidgetClass on BP_ThirdPersonCharacter!")
            except Exception as e:
                print(f"Error setting property: {e}")
                
create_and_setup_widget()
