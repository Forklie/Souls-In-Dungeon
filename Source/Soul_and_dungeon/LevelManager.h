#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelManager.generated.h"

/**
 * Tracks dungeon objectives and unlocks the exit portal.
 */
UCLASS()
class SOUL_AND_DUNGEON_API ALevelManager : public AActor
{
	GENERATED_BODY()
	
public:	
	ALevelManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	int32 TotalRequiredChests = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	int32 OpenedChests = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	AActor* ExitPortalRef = nullptr;

	/** Called by objective actors (like chests) when completed */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void NotifyChestOpened();

	/** Registers a new objective chest */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void RegisterChest();

private:
	/** Checks if all requirements are met and unlocks the portal */
	void CheckObjectiveComplete();
};
