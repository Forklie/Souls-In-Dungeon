#pragma once

#include "CoreMinimal.h"
#include "DungeonPropTypes.generated.h"

UENUM(BlueprintType)
enum class EDungeonPropCategory : uint8
{
    CornerClutter,   // barrels, crates, sacks
    WallProp,        // torches, chains, wall shelves
    Furniture,       // tables, bookshelves, chairs
    TreasureDeco,    // decorative chests, coins, shelves
    Lighting,        // braziers, candles, chandeliers
    Ambience,        // fog, skeletons, cobwebs
    Gateway          // columns, gates for exit room
};

USTRUCT(BlueprintType)
struct FDungeonPropRule
{
    GENERATED_BODY()
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Props")
    TSubclassOf<AActor> PropClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Props")
    EDungeonPropCategory Category = EDungeonPropCategory::CornerClutter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Props")
    float Weight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon Props")
    bool bBlocksMovement = false;
};
