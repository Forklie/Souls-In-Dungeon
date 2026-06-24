#pragma once

#include "Blueprint/UserWidget.h"
#include "PauseMenuWidget.generated.h"

class ASoul_and_dungeonPlayerController;
class UButton;
class UCheckBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class SOUL_AND_DUNGEON_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetMenuOwner(ASoul_and_dungeonPlayerController* InOwner);

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildWidget();
	UButton* AddMenuButton(UVerticalBox* Parent, const FString& Label);
	UTextBlock* AddLine(UVerticalBox* Parent, const FString& Text, int32 FontSize = 18);
	void ShowSettings(bool bShow);
	FSlateBrush MakeBrush(FLinearColor Color, float Rounding = 8.0f, float OutlineWidth = 0.0f, FLinearColor OutlineColor = FLinearColor::Transparent) const;

	UFUNCTION()
	void HandleResume();

	UFUNCTION()
	void HandleRestart();

	UFUNCTION()
	void HandleSettings();

	UFUNCTION()
	void HandleMainMenu();

	UFUNCTION()
	void HandleQuit();

	UFUNCTION()
	void HandleBack();

	UFUNCTION()
	void HandleFullscreenChanged(bool bIsChecked);

	UPROPERTY(Transient)
	TObjectPtr<ASoul_and_dungeonPlayerController> MenuOwner = nullptr;

	UPROPERTY(Transient)
	UVerticalBox* ButtonPanel = nullptr;

	UPROPERTY(Transient)
	UVerticalBox* SettingsPanel = nullptr;
};
