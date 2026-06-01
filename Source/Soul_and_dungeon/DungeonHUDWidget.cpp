#include "DungeonHUDWidget.h"
#include "Components/TextBlock.h"
#include "LevelManager.h"
#include "DungeonProgressionState.h"
#include "Kismet/GameplayStatics.h"

void UDungeonHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();
    RefreshDisplay();
}

void UDungeonHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // Refresh display every 0.5 seconds
    RefreshTimer += InDeltaTime;
    if (RefreshTimer >= 0.5f)
    {
        RefreshTimer = 0.0f;
        RefreshDisplay();
    }

    // Handle center message fade
    if (CenterMessageTimer > 0.0f)
    {
        CenterMessageTimer -= InDeltaTime;
        if (CenterMessageTimer <= 0.0f && CenterMessageText)
        {
            CenterMessageText->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

void UDungeonHUDWidget::RefreshDisplay()
{
    // Get progression state
    ADungeonProgressionState* Progression = ADungeonProgressionState::Get(this);
    ALevelManager* LevelMgr = ALevelManager::GetActiveLevelManager(this);

    // Floor display
    if (FloorText)
    {
        int32 Floor = Progression ? Progression->CurrentFloor : 1;
        int32 Total = Progression ? Progression->TotalFloors : 3;

        FString FloorName;
        switch (Floor)
        {
        case 1: FloorName = TEXT("THE CATACOMBS"); break;
        case 2: FloorName = TEXT("THE PRISON"); break;
        case 3: FloorName = TEXT("THE TREASURE VAULT"); break;
        default: FloorName = FString::Printf(TEXT("FLOOR %d"), Floor); break;
        }

        FloorText->SetText(FText::FromString(
            FString::Printf(TEXT("%s  (Floor %d / %d)"), *FloorName, Floor, Total)));
    }

    // Chest counter
    if (ChestText && LevelMgr)
    {
        ChestText->SetText(FText::FromString(
            FString::Printf(TEXT("Chests: %d / %d"),
                LevelMgr->OpenedChests,
                LevelMgr->TotalRequiredChests)));
    }
    else if (ChestText)
    {
        ChestText->SetText(FText::FromString(TEXT("Chests: -- / --")));
    }

    // Portal status
    if (PortalText && LevelMgr)
    {
        if (Progression && Progression->bGameComplete)
        {
            PortalText->SetText(FText::FromString(TEXT("DUNGEON CONQUERED!")));
        }
        else if (LevelMgr->IsObjectiveComplete())
        {
            PortalText->SetText(FText::FromString(TEXT("Portal: UNLOCKED")));
        }
        else
        {
            PortalText->SetText(FText::FromString(TEXT("Portal: LOCKED")));
        }
    }
}

void UDungeonHUDWidget::ShowCenterMessage(const FString& Message, float Duration)
{
    if (CenterMessageText)
    {
        CenterMessageText->SetText(FText::FromString(Message));
        CenterMessageText->SetVisibility(ESlateVisibility::Visible);
        CenterMessageTimer = Duration;
    }
}
