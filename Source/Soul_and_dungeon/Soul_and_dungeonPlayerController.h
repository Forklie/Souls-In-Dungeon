// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Soul_and_dungeonPlayerController.generated.h"

class UInputMappingContext;
class UPauseMenuWidget;
class UUserWidget;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class ASoul_and_dungeonPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Pointer to the active pause menu widget */
	UPROPERTY(Transient)
	TObjectPtr<UPauseMenuWidget> PauseMenuWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Toggle secondary search debug drawing */
	void ToggleSecondarySearchDebug();

	/** Toggle enemy intercept prediction between off and deterministic prediction */
	void ToggleEnemyInterceptPrediction();

	/** Cycle enemy intercept mode */
	void CycleEnemyInterceptMode();

	/** Opens or closes the in-game pause menu */
	UFUNCTION(BlueprintCallable, Category = "Souls|UI")
	void TogglePauseMenu();

public:
	/** Closes the pause menu and returns to gameplay input */
	UFUNCTION(BlueprintCallable, Category = "Souls|UI")
	void ClosePauseMenu();

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

};
