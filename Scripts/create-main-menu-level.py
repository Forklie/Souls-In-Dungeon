import unreal


LEVEL_PATH = "/Game/Maps/LVL_MainMenu"
GAME_MODE_CLASS_PATH = "/Script/Soul_and_dungeon.MainMenuGameMode"


def main():
    if not unreal.EditorLevelLibrary.new_level(LEVEL_PATH):
        raise RuntimeError(f"Could not create level: {LEVEL_PATH}")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("Could not get editor world after creating main menu level")

    world_settings = world.get_world_settings()
    game_mode_class = unreal.load_class(None, GAME_MODE_CLASS_PATH)
    if not game_mode_class:
        raise RuntimeError(f"Could not load game mode class: {GAME_MODE_CLASS_PATH}")

    world_settings.set_editor_property("default_game_mode", game_mode_class)
    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(f"Created main menu level at {LEVEL_PATH} with MainMenuGameMode override")


if __name__ == "__main__":
    main()
