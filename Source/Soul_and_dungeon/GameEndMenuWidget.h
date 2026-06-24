#pragma once

#include "Blueprint/UserWidget.h"
#include "GameEndMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

UCLASS()
class SOUL_AND_DUNGEON_API UGameEndMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureForDeath();
	void ConfigureForVictory();

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildWidget();
	UButton* AddButton(UVerticalBox* Parent, const FString& Label);
	void UpdateText();
	FSlateBrush MakeBrush(FLinearColor Color, float Rounding = 8.0f, float OutlineWidth = 0.0f, FLinearColor OutlineColor = FLinearColor::Transparent) const;

	UFUNCTION()
	void HandlePrimaryAction();

	UFUNCTION()
	void HandleMainMenu();

	UPROPERTY(Transient)
	UTextBlock* TitleText = nullptr;

	UPROPERTY(Transient)
	UTextBlock* SubtitleText = nullptr;

	UPROPERTY(Transient)
	UTextBlock* PrimaryButtonText = nullptr;

	bool bVictory = false;
};
