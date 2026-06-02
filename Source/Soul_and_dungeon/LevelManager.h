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

	/** Returns the most authoritative level manager currently in the world. */
	static ALevelManager* GetActiveLevelManager(const UObject* WorldContextObject);

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	int32 TotalRequiredChests = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	int32 OpenedChests = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	AActor* ExitPortalRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objectives")
	bool bCompleteGameWhenObjectivesComplete = false;

	/** Called by objective actors (like chests) when completed */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void NotifyChestOpened(AActor* ChestActor = nullptr);

	/** Returns true once the level objectives have been completed. */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	bool IsObjectiveComplete() const;

	/** Registers a new objective chest */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void RegisterChest(AActor* ChestActor = nullptr);

	/** Clears runtime objective state before a new procedural generation pass. */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void ResetObjectives();

	/** Counts currently open chests based on visual state */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	int32 GetOpenChestCount() const;

	/** Checks if a specific chest actor has been opened (handles child/parent hierarchy) */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	bool IsChestOpened(AActor* ChestActor) const;

	/** Checks if a specific chest actor is visually open by inspecting its lid rotation */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	bool IsChestVisuallyOpen(AActor* ChestActor) const;

	/** Syncs objective state from the observed open chest count. */
	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void SyncObjectiveStateFromVisualCount(int32 VisualOpenCount);

	UFUNCTION(BlueprintCallable, Category = "Objectives")
	void SetCompleteGameWhenObjectivesComplete(bool bShouldCompleteGame) { bCompleteGameWhenObjectivesComplete = bShouldCompleteGame; }

private:
	struct FCachedChest
	{
		TWeakObjectPtr<AActor> ChestActor;
		TWeakObjectPtr<USceneComponent> LidComponent;
	};

	UPROPERTY(Transient)
	mutable TArray<AActor*> AllChestActors;

	mutable TArray<FCachedChest> CachedChests;

	UPROPERTY(Transient)
	TSet<AActor*> OpenedChestsSet;

	UPROPERTY(Transient)
	bool bObjectivesComplete = false;

	/** Refreshes the tracked chest cache from the current world state. */
	void RefreshChestCache();

	const FCachedChest* FindCachedChest(AActor* ChestActor) const;

	/** Checks if all requirements are met and unlocks the portal */
	void CheckObjectiveComplete();
};
