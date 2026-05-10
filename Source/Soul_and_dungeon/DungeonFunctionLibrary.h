#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DungeonFunctionLibrary.generated.h"

/**
 * Helper library for procedural dungeon generation.
 * Handles room alignment and overlap checks.
 */
UCLASS()
class SOUL_AND_DUNGEON_API UDungeonFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Calculates the world transform for a room such that its Entrance aligns perfectly with the provided Exit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon")
	static FTransform CalculateRoomTransform(const FTransform& ExitTransform, const FTransform& EntranceTransform);

	/**
	 * Checks if a candidate room's BoxComponent overlaps with any existing rooms.
	 * Uses AABB check for efficiency.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon", meta = (WorldContext = "WorldContextObject"))
	static bool CheckRoomOverlap(const UObject* WorldContextObject, AActor* CandidateRoom, const TArray<AActor*>& ExistingRooms, float Tolerance = 5.0f);
};
