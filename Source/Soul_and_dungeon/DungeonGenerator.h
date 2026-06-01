#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonPropTypes.h"
#include "MinimapDataProvider.h"
#include "DungeonGenerator.generated.h"

class ALevelManager;
class UBoxComponent;
class USceneComponent;

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
	TArray<TSubclassOf<AActor>> HallwayRoomClasses;

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
	TSubclassOf<ACharacter> EnemyClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<class AAIController> EnemyAIControllerClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> EnemySpawnerClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TSubclassOf<AActor> DoorClass;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Geometry")
	UStaticMesh* FloorMesh;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Geometry")
	UStaticMesh* WallMesh;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Geometry")
	UStaticMesh* ColumnMesh;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	TArray<TSubclassOf<AActor>> RandomPropClasses;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Props")
	TArray<FDungeonPropRule> PropRules;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 DungeonSeed = 12345;

	FRandomStream DungeonRandom;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MinPropsPerRoom = 2;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxPropsPerRoom = 6;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxStandardRooms = 5;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxTreasureRooms = 1;

	UPROPERTY(EditAnywhere, Category = "Dungeon|Config")
	int32 MaxSpawnAttemptsPerRoom = 10;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Runtime")
	TArray<AActor*> SpawnedRooms;

	/** Minimap data populated during dungeon generation */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Runtime")
	UMinimapDataProvider* MinimapData = nullptr;

private:
	/** Internal helper to spawn and align a room */
	AActor* TrySpawnRoom(TSubclassOf<AActor> RoomClass, const FTransform& ExitTransform);

	/** Finds the exact SceneComponent named "Exit_Marker" */
	FTransform GetExitTransform(AActor* Room);

	/** Finds a named marker transform, with optional prefix fallback. */
	bool GetMarkerTransform(AActor* Room, const TArray<FName>& ExactNames, const TArray<FString>& Prefixes, const TCHAR* Purpose, FTransform& OutTransform);

	/** Finds the exact local SceneComponent named "Entrance_Marker" */
	FTransform GetLocalEntranceTransform(AActor* Room);

	/** Finds a supported spawn marker by exact name or supported prefix */
	bool FindSpawnPointTransform(AActor* Room, const FString& NamePart, FTransform& OutTransform);

	/** Finds a component by name substring (legacy wrapper, logs warning if not found) */
	FTransform GetSpawnPointTransform(AActor* Room, FString NamePart);

	/** Validates that a room has all required markers and logs warnings for missing ones */
	void ValidateRoomMarkers(AActor* Room, const FString& RoomLabel);

	/** Randomly scatters props in the room bounds */
	void ScatterPropsInRoom(AActor* TargetRoom);

	/** Finds a scene component without broad substring matching. */
	bool FindExactOrPrefixedComponent(
		AActor* Room,
		const TArray<FName>& ExactNames,
		const TArray<FString>& AllowedPrefixes,
		const TCHAR* Purpose,
		USceneComponent*& OutComponent) const;

	/** Finds the exact RoomBounds_Marker component used for generation overlap checks. */
	UBoxComponent* FindRoomBoundsComponent(AActor* Room) const;

	/** Adds one room to runtime tracking, minimap data, marker checks, and optional props. */
	void RegisterGeneratedRoom(AActor* Room, const FString& RoomLabel, bool bIsTreasure, bool bIsStart, bool bIsExit, bool bScatterProps);

	/** Spawns configured press-E door at the previous exit/current entrance connection. */
	void SpawnDoorAtConnection(const FTransform& ConnectionTransform, const FString& ConnectionLabel);

	/** Spawns an open gateway/arch at non-door module connections. */
	void SpawnOpenGatewayAtConnection(const FTransform& ConnectionTransform, const FString& ConnectionLabel);

	/** Builds a collision-safe floor patch between two connected modules. */
	void BuildConnectionFloor(const FTransform& ConnectionTransform, const FString& ConnectionLabel);

	/** Spawns and registers objective chests in a generated treasure room. */
	void SpawnChestsInRoom(AActor* Room, ALevelManager* LevelManager, int32& RequiredChests);

	/** Spawns configured combat enemy spawners at enemy markers in a generated room. */
	void SpawnEnemiesInRoom(AActor* Room);

	/** Spawns and registers the exit portal in the generated exit room. */
	void SpawnExitPortalInRoom(AActor* ExitRoom, ALevelManager* LevelManager);

	/** Moves the already spawned player pawn to the start-room marker/fallback. */
	void MovePlayerToStartRoom(AActor* StartRoom);

	/** Spawns a dungeon-wide NavMeshBoundsVolume and rebuilding navigation. */
	void SpawnDungeonNavMeshBounds();

	/** Builds procedural geometry for the room (floors, walls) using the assigned meshes. */
	void BuildRoomGeometry(AActor* Room, const FString& RoomLabel);

	/** Replaces ScatterPropsInRoom with a zone-aware prop placement system. */
	void DressRoom(AActor* Room, const FString& RoomLabel);
};
