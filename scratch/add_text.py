import unreal

def add_text_to_widget():
    asset_path = "/Game/UI/WBP_InteractPrompt2"
    widget_bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not widget_bp:
        print("Could not load WBP_InteractPrompt2")
        return

    # Get the Widget Blueprint's Root Widget
    # Note: In Python, we usually interact with the WidgetTree
    widget_tree = unreal.WidgetBlueprintLibrary.get_blueprint_widget_tree(widget_bp)
    
    # Create a Canvas Panel if root is missing
    root = widget_tree.get_root_widget()
    if not root:
        root = widget_tree.construct_widget(unreal.CanvasPanel, "RootCanvas")
        widget_tree.set_root_widget(root)

    # Create Text Block
    text_block = widget_tree.construct_widget(unreal.TextBlock, "InteractText")
    text_block.set_text("Press [E] to Interact")
    
    # Anchor to Center
    root.add_child_to_canvas(text_block)
    slot = text_block.slot
    if isinstance(slot, unreal.CanvasPanelSlot):
        anchors = unreal.Anchors(0.5, 0.5, 0.5, 0.5)
        slot.set_anchors(anchors)
        slot.set_alignment(unreal.Vector2D(0.5, 0.5))
        slot.set_offsets(unreal.Margin(0, 0, 300, 50)) # Width, Height
        slot.set_position(unreal.Vector2D(0, 100)) # Slightly below center

    # Save and Compile
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.WidgetBlueprintLibrary.compile_widget_blueprint(widget_bp)
    print("Successfully added text to WBP_InteractPrompt2 and compiled!")

add_text_to_widget()
