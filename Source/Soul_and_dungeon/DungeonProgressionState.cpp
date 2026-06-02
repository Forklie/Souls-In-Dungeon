#include "DungeonProgressionState.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"

ADungeonProgressionState::ADungeonProgressionState()
{
    PrimaryActorTick.bCanEverTick = false;
}

ADungeonProgressionState* ADungeonProgressionState::Get(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return nullptr;

    for (TActorIterator<ADungeonProgressionState> It(World); It; ++It)
    {
        return *It;
    }
    return nullptr;
}

FDungeonFloorConfig ADungeonProgressionState::GetCurrentFloorConfig() const
{
    FDungeonFloorConfig Config;
    Config.ChestCount = ChestsPerFloor;

    switch (CurrentFloor)
    {
    case 1: // Catacombs
        Config.MinMainRooms = 6;
        Config.MaxMainRooms = 8;
        Config.MinBranches = 1;
        Config.MaxBranches = 2;
        Config.MinEnemiesPerRoom = 1;
        Config.MaxEnemiesPerRoom = 2;
        Config.MinCombatRooms = 2;
        Config.MaxCombatRooms = 3;
        Config.TreasureRoomCount = 4;
        Config.PropDensityMultiplier = 1.0f;
        Config.FloorThemeIndex = 0;
        break;
    case 2: // Prison
        Config.MinMainRooms = 8;
        Config.MaxMainRooms = 10;
        Config.MinBranches = 2;
        Config.MaxBranches = 3;
        Config.MinEnemiesPerRoom = 2;
        Config.MaxEnemiesPerRoom = 3;
        Config.MinCombatRooms = 3;
        Config.MaxCombatRooms = 4;
        Config.TreasureRoomCount = 4;
        Config.PropDensityMultiplier = 1.3f;
        Config.FloorThemeIndex = 1;
        break;
    default: // Floor 3+ - Treasure Vault
        Config.MinMainRooms = 10;
        Config.MaxMainRooms = 12;
        Config.MinBranches = 3;
        Config.MaxBranches = 4;
        Config.MinEnemiesPerRoom = 3;
        Config.MaxEnemiesPerRoom = 4;
        Config.MinCombatRooms = 4;
        Config.MaxCombatRooms = 5;
        Config.TreasureRoomCount = 4;
        Config.PropDensityMultiplier = 1.6f;
        Config.FloorThemeIndex = 2;
        break;
    }

    return Config;
}

bool ADungeonProgressionState::AdvanceToNextFloor()
{
    if (bGameComplete || bTransitioning)
    {
        return false;
    }

    bTransitioning = true;

    if (CurrentFloor >= TotalFloors)
    {
        CompleteGame();
        return false;
    }

    CurrentFloor++;

    UE_LOG(LogTemp, Log, TEXT("DungeonProgression: Advancing to Floor %d / %d"), CurrentFloor, TotalFloors);

    // Show floor transition message
    FString FloorName;
    switch (CurrentFloor)
    {
    case 1: FloorName = TEXT("THE CATACOMBS"); break;
    case 2: FloorName = TEXT("THE PRISON"); break;
    case 3: FloorName = TEXT("THE TREASURE VAULT"); break;
    default: FloorName = FString::Printf(TEXT("FLOOR %d"), CurrentFloor); break;
    }

    UKismetSystemLibrary::PrintString(this,
        FString::Printf(TEXT("DESCENDING TO %s... (Floor %d/%d)"), *FloorName, CurrentFloor, TotalFloors),
        true, true, FLinearColor(1.0f, 0.8f, 0.2f), 4.0f);

    // The DungeonGenerator will check bTransitioning and call RegenerateDungeon
    // bTransitioning is cleared after regeneration completes
    
    return true;
}

void ADungeonProgressionState::CompleteGame()
{
    bGameComplete = true;
    bTransitioning = false;

    UE_LOG(LogTemp, Log, TEXT("DungeonProgression: GAME COMPLETE! All %d floors cleared."), TotalFloors);

    UKismetSystemLibrary::PrintString(this,
        TEXT("DUNGEON CONQUERED! You found all the treasure!"),
        true, true, FLinearColor(1.0f, 0.84f, 0.0f), 15.0f);

    // Pause the game
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (PC)
    {
        PC->SetPause(true);

        // Show the "YOU WON" victory screen
        if (UClass* WinWidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/ThirdPerson/UI/WBP_WinScreen.WBP_WinScreen_C")))
        {
            UUserWidget* WinWidget = CreateWidget<UUserWidget>(PC, WinWidgetClass);
            if (WinWidget)
            {
                WinWidget->AddToViewport(100);
            }

            // Show mouse cursor and set UI input mode
            PC->bShowMouseCursor = true;
            FInputModeUIOnly UIMode;
            if (WinWidget)
            {
                UIMode.SetWidgetToFocus(WinWidget->TakeWidget());
            }
            PC->SetInputMode(UIMode);
        }
    }
}

void ADungeonProgressionState::ResetProgression()
{
    CurrentFloor = 1;
    bGameComplete = false;
    bTransitioning = false;
    UE_LOG(LogTemp, Log, TEXT("DungeonProgression: Reset to Floor 1"));
}
