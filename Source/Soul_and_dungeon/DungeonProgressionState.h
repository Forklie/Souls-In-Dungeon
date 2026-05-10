#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonProgressionState.generated.h"

/**
 * Configuration for a single dungeon floor.
 */
USTRUCT(BlueprintType)
struct FDungeonFloorConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinMainRooms = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxMainRooms = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinBranches = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxBranches = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ChestCount = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinEnemiesPerRoom = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxEnemiesPerRoom = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinCombatRooms = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxCombatRooms = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 TreasureRoomCount = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PropDensityMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 FloorThemeIndex = 0; // 0=Catacombs, 1=Prison, 2=Vault
};

/**
 * Tracks multi-floor dungeon progression.
 * Persists across floor regenerations within a single play session.
 */
UCLASS(Blueprintable)
class SOUL_AND_DUNGEON_API ADungeonProgressionState : public AActor
{
    GENERATED_BODY()

public:
    ADungeonProgressionState();

    /** Returns the active progression state in the world, or nullptr */
    static ADungeonProgressionState* Get(const UObject* WorldContextObject);

    /** Get the configuration for the current floor */
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Progression")
    FDungeonFloorConfig GetCurrentFloorConfig() const;

    /** Advance to the next floor. Returns true if game continues, false if game complete. */
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Progression")
    bool AdvanceToNextFloor();

    /** Called when all 3 floors are complete */
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Progression")
    void CompleteGame();

    /** Reset to floor 1 for a new game */
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Progression")
    void ResetProgression();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Progression")
    int32 CurrentFloor = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Progression")
    int32 TotalFloors = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Progression")
    int32 ChestsPerFloor = 10;

    UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Progression")
    bool bGameComplete = false;

    UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Progression")
    bool bTransitioning = false;
};
