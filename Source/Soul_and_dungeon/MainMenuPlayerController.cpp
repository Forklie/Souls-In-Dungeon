// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuPlayerController.h"

AMainMenuPlayerController::AMainMenuPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Set input mode to UI only so gameplay controls aren't active in the main menu
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}
