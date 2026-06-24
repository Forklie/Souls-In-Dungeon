// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuGameMode.h"
#include "MainMenuPlayerController.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	// Disable player pawn spawning in the main menu level
	DefaultPawnClass = nullptr;

	// Use our custom main menu player controller to enable mouse interaction
	PlayerControllerClass = AMainMenuPlayerController::StaticClass();
}
