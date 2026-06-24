#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DungeonHUDWidget.generated.h"

class UTextBlock;

/**
 * HUD overlay showing dungeon progress:
 * - Current floor / total floors
 * - Chests opened / total chests
 * - Portal lock status
 * - Floor transition and victory messages
 */
UCLASS()
class SOUL_AND_DUNGEON_API UDungeonHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Updates all HUD elements from current game state */
    UFUNCTION(BlueprintCallable, Category = "DungeonHUD")
    void RefreshDisplay();

    /** Show a temporary center-screen message (e.g., floor transitions) */
    UFUNCTION(BlueprintCallable, Category = "DungeonHUD")
    void ShowCenterMessage(const FString& Message, float Duration = 3.0f);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    /** Text blocks bound from Blueprint or created in code */
    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* FloorText = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* ChestText = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* PortalText = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* CenterMessageText = nullptr;

    float CenterMessageTimer = 0.0f;
    float RefreshTimer = 0.0f;
};
