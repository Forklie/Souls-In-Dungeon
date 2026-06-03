// Copyright Epic Games, Inc. All Rights Reserved.


#include "Soul_and_dungeonPlayerController.h"
#include "Components/InputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "PauseMenuWidget.h"
#include "SecondarySearchSolver.h"
#include "Soul_and_dungeon.h"
#include "Widgets/Input/SVirtualJoystick.h"

namespace
{
const TCHAR* EnemyInterceptRuntimeModeName(int32 RuntimeMode)
{
	switch (RuntimeMode)
	{
	case 0:
		return TEXT("Off_CurrentLocationOnly");
	case 1:
		return TEXT("DeterministicPrediction");
	case 2:
		return TEXT("LearnedPrediction");
	case 3:
		return TEXT("ForceCurrentLocation");
	case 4:
		return TEXT("ForcePredict035");
	case 5:
		return TEXT("ForcePredict075");
	case 6:
		return TEXT("ForcePredict125");
	case 7:
		return TEXT("ForcePredict175");
	default:
		return TEXT("LegacyCVars");
	}
}

IConsoleVariable* GetEnemyInterceptModeVariable()
{
	return IConsoleManager::Get().FindConsoleVariable(TEXT("sd.EnemyIntercept.Mode"));
}

void ShowEnemyInterceptModeMessage(int32 RuntimeMode)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			913708,
			1.8f,
			RuntimeMode == 0 ? FColor::Silver : FColor::Cyan,
			FString::Printf(TEXT("EnemyIntercept: %s"), EnemyInterceptRuntimeModeName(RuntimeMode)));
	}
}
}

void ASoul_and_dungeonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController())
	{
		SetPause(false);
		bShowMouseCursor = false;
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);

		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}

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
			InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ASoul_and_dungeonPlayerController::ToggleSecondarySearchDebug);
			InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ASoul_and_dungeonPlayerController::ToggleEnemyInterceptPrediction);
			InputComponent->BindKey(EKeys::U, IE_Pressed, this, &ASoul_and_dungeonPlayerController::CycleEnemyInterceptMode);
			FInputKeyBinding& PauseBinding = InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ASoul_and_dungeonPlayerController::TogglePauseMenu);
			PauseBinding.bExecuteWhenPaused = true;
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

void ASoul_and_dungeonPlayerController::ToggleEnemyInterceptPrediction()
{
	IConsoleVariable* ModeVariable = GetEnemyInterceptModeVariable();
	if (!ModeVariable)
	{
		return;
	}

	const int32 CurrentMode = ModeVariable->GetInt();
	const int32 NextMode = CurrentMode == 0 ? 1 : 0;
	ModeVariable->Set(NextMode, ECVF_SetByConsole);
	ShowEnemyInterceptModeMessage(NextMode);
}

void ASoul_and_dungeonPlayerController::CycleEnemyInterceptMode()
{
	ConsoleCommand(TEXT("sd.EnemyIntercept.CycleMode"));

	if (IConsoleVariable* ModeVariable = GetEnemyInterceptModeVariable())
	{
		ShowEnemyInterceptModeMessage(ModeVariable->GetInt());
	}
}

void ASoul_and_dungeonPlayerController::TogglePauseMenu()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (PauseMenuWidget)
	{
		ClosePauseMenu();
		return;
	}

	SetPause(true);
	bShowMouseCursor = true;

	PauseMenuWidget = CreateWidget<UPauseMenuWidget>(this, UPauseMenuWidget::StaticClass());
	if (PauseMenuWidget)
	{
		PauseMenuWidget->SetMenuOwner(this);
		PauseMenuWidget->AddToViewport(100);

		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(PauseMenuWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	else
	{
		SetPause(false);
		bShowMouseCursor = false;
	}
}

void ASoul_and_dungeonPlayerController::ClosePauseMenu()
{
	if (PauseMenuWidget)
	{
		PauseMenuWidget->RemoveFromParent();
		PauseMenuWidget = nullptr;
	}

	SetPause(false);
	bShowMouseCursor = false;

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
}

bool ASoul_and_dungeonPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
