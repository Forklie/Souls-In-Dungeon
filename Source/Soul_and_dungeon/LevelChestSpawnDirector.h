#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelChestSpawnDirector.generated.h"

class ALevelManager;
class UBoxComponent;
class UMinimapDataProvider;

UCLASS(Blueprintable)
class SOUL_AND_DUNGEON_API ALevelChestSpawnDirector : public AActor
{
	GENERATED_BODY()

public:
	ALevelChestSpawnDirector();

	UFUNCTION(BlueprintPure, Category = "Level Chests")
	UMinimapDataProvider* GetMinimapDataProvider() const { return MinimapData; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SpawnBounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	TSubclassOf<AActor> ChestClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	bool bEnableChestSpawning = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests", meta = (ClampMin = "1"))
	int32 ChestCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests", meta = (ClampMin = "1"))
	int32 MaxPlacementAttempts = 700;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests", meta = (ClampMin = "0.0"))
	float MinChestSpacing = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	bool bRequireNavigableFloor = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	FVector NavProjectionExtent = FVector(350.0f, 350.0f, 700.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests", meta = (ClampMin = "0.0"))
	float FloorTraceHeight = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests", meta = (ClampMin = "0.0"))
	float SpawnFloorClearance = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	TArray<FName> FloorSurfaceNameTokens;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	FVector PlacementClearanceExtent = FVector(120.0f, 120.0f, 90.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	bool bResetLevelObjectives = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	bool bCompleteGameWhenAllChestsOpen = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Chests")
	FName LevelId = TEXT("BossRoom");

private:
	bool BuildChestSpawnTransform(TArray<FVector>& AcceptedFloorLocations, FTransform& OutTransform, FVector& OutFloorLocation) const;
	bool ProjectToFloor(const FVector& CandidateLocation, FVector& OutFloorLocation) const;
	bool IsFloorSurface(const FHitResult& FloorHit) const;
	bool IsPlacementClear(const FVector& FloorLocation) const;
	bool IsFarEnoughFromAcceptedChests(const FVector& FloorLocation, const TArray<FVector>& AcceptedFloorLocations) const;
	void MakeActorMovableForRuntimePlacement(AActor* Actor) const;
	void AlignActorBottomToFloor(AActor* Actor, const FVector& FloorLocation) const;
	ALevelManager* ResolveLevelManager() const;

	UPROPERTY(Transient)
	TObjectPtr<UMinimapDataProvider> MinimapData;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedChests;
};
