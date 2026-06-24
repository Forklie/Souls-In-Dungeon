import unreal

def rebuild_widget():
    asset_path = "/Game/UI/WBP_InteractPrompt2"
    
    # Force delete and recreate to ensure it's clean
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)
    
    factory = unreal.WidgetBlueprintFactory()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    widget_bp = asset_tools.create_asset("WBP_InteractPrompt2", "/Game/UI", unreal.WidgetBlueprint, factory)
    
    if not widget_bp:
        print("Failed to create WBP_InteractPrompt2")
        return

    # Get the tree
    widget_tree = unreal.WidgetBlueprintLibrary.get_blueprint_widget_tree(widget_bp)
    
    # 1. Create Root Canvas
    root_canvas = widget_tree.construct_widget(unreal.CanvasPanel, "RootCanvas")
    widget_tree.set_root_widget(root_canvas)

    # 2. Create Text Block
    text_block = widget_tree.construct_widget(unreal.TextBlock, "InteractText")
    text_block.set_text("Press [E] to Interact")
    
    # Style the text to make sure it's visible
    color = unreal.SlateColor(unreal.LinearColor(1.0, 1.0, 1.0, 1.0)) # Pure White
    text_block.set_color_and_opacity(color)
    
    # Shadow for readability
    text_block.set_shadow_offset(unreal.Vector2D(2.0, 2.0))
    text_block.set_shadow_color_and_opacity(unreal.LinearColor(0, 0, 0, 1.0))

    # Font size
    font_info = text_block.font
    font_info.size = 32
    text_block.set_font(font_info)

    # 3. Add to Canvas and Center
    root_canvas.add_child_to_canvas(text_block)
    slot = text_block.slot
    if isinstance(slot, unreal.CanvasPanelSlot):
        anchors = unreal.Anchors(0.5, 0.5, 0.5, 0.5) # Center
        slot.set_anchors(anchors)
        slot.set_alignment(unreal.Vector2D(0.5, 0.5))
        slot.set_autosize(True)
        slot.set_position(unreal.Vector2D(0, 150)) # Lower center

    # Save and Compile
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.WidgetBlueprintLibrary.compile_widget_blueprint(widget_bp)
    print("Successfully REBUILT WBP_InteractPrompt2 with visible text!")

rebuild_widget()
