// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MainMenuGameMode.generated.h"

/**
 * GameMode for the Main Menu.
 * Disables pawn spawning and sets up the Main Menu PlayerController.
 */
UCLASS()
class SOUL_AND_DUNGEON_API AMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMainMenuGameMode();
};
