#pragma once

#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UBorder;
class UButton;
class UCheckBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class SOUL_AND_DUNGEON_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildWidget();
	UButton* AddMenuButton(UVerticalBox* Parent, const FString& Label, bool bEnabled = true);
	UTextBlock* AddPanelLine(UVerticalBox* Parent, const FString& Text, int32 FontSize = 18);
	void ShowPanel(UVerticalBox* PanelToShow);
	FSlateBrush MakeBrush(FLinearColor Color, float Rounding = 8.0f, float OutlineWidth = 0.0f, FLinearColor OutlineColor = FLinearColor::Transparent) const;

	UFUNCTION()
	void HandleNewGame();

	UFUNCTION()
	void HandleSettings();

	UFUNCTION()
	void HandleCredits();

	UFUNCTION()
	void HandleQuit();

	UFUNCTION()
	void HandleBack();

	UFUNCTION()
	void HandleFullscreenChanged(bool bIsChecked);

	UPROPERTY(Transient)
	UVerticalBox* ButtonPanel = nullptr;

	UPROPERTY(Transient)
	UVerticalBox* SettingsPanel = nullptr;

	UPROPERTY(Transient)
	UVerticalBox* CreditsPanel = nullptr;
};
