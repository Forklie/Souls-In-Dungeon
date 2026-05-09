// Copyright Epic Games, Inc. All Rights Reserved.


#include "Soul_and_dungeonPlayerController.h"
#include "Components/InputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "SecondarySearchSolver.h"
#include "Soul_and_dungeon.h"
#include "Widgets/Input/SVirtualJoystick.h"

void ASoul_and_dungeonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogSoul_and_dungeon, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void ASoul_and_dungeonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		if (InputComponent)
		{
			InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ASoul_and_dungeonPlayerController::ToggleSecondarySearchDebug);
		}

		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

void ASoul_and_dungeonPlayerController::ToggleSecondarySearchDebug()
{
	FSecondarySearchDebug::Toggle();
}

bool ASoul_and_dungeonPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
