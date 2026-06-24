// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MainMenuPlayerController.generated.h"

/**
 * PlayerController class for the Main Menu.
 * Handles mouse cursor visibility and restricts inputs to UI only.
 */
UCLASS()
class SOUL_AND_DUNGEON_API AMainMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMainMenuPlayerController();

protected:
	virtual void BeginPlay() override;
};
