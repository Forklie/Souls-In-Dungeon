#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonGenerator.generated.h"

/**
 * Procedural Dungeon Generator.
 * Spawns a sequence of modular rooms and aligns them using Entrance/Exit markers.
 */
UCLASS()
class SOUL_AND_DUNGEON_API ADungeonGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	ADungeonGenerator();

protected:
	virtual void BeginPlay() override;

	/** Main generation loop */
	UFUNCTION(BlueprintCallable, Category = "Dungeon")
	void GenerateDungeon();

public:	
	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> StartRoomClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TArray<TSubclassOf<AActor>> StandardRoomClasses;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TArray<TSubclassOf<AActor>> TreasureRoomClasses;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> ExitRoomClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> ChestClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> ExitPortalClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> LevelManagerClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> EnemySpawnerClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxStandardRooms = 5;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxTreasureRooms = 1;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxSpawnAttemptsPerRoom = 10;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Runtime")
	TArray<AActor*> SpawnedRooms;

private:
	/** Internal helper to spawn and align a room */
	AActor* TrySpawnRoom(TSubclassOf<AActor> RoomClass, const FTransform& ExitTransform);

	/** Finds an ArrowComponent or SceneComponent named "Exit_01" */
	FTransform GetExitTransform(AActor* Room);

	/** Finds a SceneComponent named "Entrance" */
	FTransform GetLocalEntranceTransform(AActor* Room);

	/** Finds a component by name substring */
	FTransform GetSpawnPointTransform(AActor* Room, FString NamePart);
};
