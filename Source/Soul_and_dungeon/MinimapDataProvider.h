#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MinimapDataProvider.generated.h"

/**
 * Icon types displayed on the minimap.
 */
UENUM(BlueprintType)
enum class EMinimapIconType : uint8
{
	Chest,
	ChestOpened,
	Enemy,
	Portal,
	PlayerSpawn
};

/**
 * Spatial data for a single dungeon room on the minimap.
 */
USTRUCT()
struct FMinimapRoomData
{
	GENERATED_BODY()

	/** Center of the room's bounding box in world space */
	FVector WorldCenter = FVector::ZeroVector;

	/** Half-extents of the room's bounding box */
	FVector WorldExtent = FVector::ZeroVector;

	/** Was this a treasure room? */
	bool bIsTreasureRoom = false;

	/** First room in the dungeon */
	bool bIsStartRoom = false;

	/** Last room (exit) in the dungeon */
	bool bIsExitRoom = false;

	/** Has the player entered this room? (fog-of-war) */
	bool bVisited = false;
};

/**
 * Spatial data for a single icon on the minimap (chest, enemy, portal, etc.).
 */
USTRUCT()
struct FMinimapIconData
{
	GENERATED_BODY()

	/** World location of the icon */
	FVector WorldLocation = FVector::ZeroVector;

	/** What kind of icon this is */
	EMinimapIconType IconType = EMinimapIconType::Chest;

	/** Weak reference to the tracked actor for live position updates */
	TWeakObjectPtr<AActor> TrackedActor;

	/** Index of the room this icon belongs to (-1 if unknown) */
	int32 OwningRoomIndex = -1;
};

/**
 * Collects and stores all dungeon spatial data needed by the minimap.
 * Populated by ADungeonGenerator during level generation,
 * read by UMinimapWidget each tick for rendering.
 */
UCLASS()
class SOUL_AND_DUNGEON_API UMinimapDataProvider : public UObject
{
	GENERATED_BODY()

public:

	// ── Registration (called by DungeonGenerator) ──────────────

	/** Register a room with its bounding box data */
	void RegisterRoom(AActor* Room, bool bTreasure, bool bStart, bool bExit);

	/** Register a room directly with center/extent (for auto-scanned levels) */
	void RegisterRoomDirect(const FVector& Center, const FVector& HalfExtent, bool bTreasure, bool bStart, bool bExit);

	/** Register an icon (chest, enemy, portal, etc.) */
	void RegisterIcon(AActor* Actor, EMinimapIconType Type);

	// ── Queries (called by MinimapWidget) ──────────────────────

	/** Get all registered rooms */
	const TArray<FMinimapRoomData>& GetRooms() const { return Rooms; }

	/** Get mutable rooms (for fog-of-war updates) */
	TArray<FMinimapRoomData>& GetRoomsMutable() { return Rooms; }

	/** Get all registered icons */
	const TArray<FMinimapIconData>& GetIcons() const { return Icons; }

	/** Get mutable icons (for chest state updates) */
	TArray<FMinimapIconData>& GetIconsMutable() { return Icons; }

	/** Get the combined AABB of all rooms (min/max world coords) */
	void GetWorldBounds(FVector& OutMin, FVector& OutMax) const;

	/** Mark a room as visited */
	void MarkRoomVisited(int32 RoomIndex);

	/** Find which room index a world position is inside (-1 if none) */
	int32 FindRoomContaining(const FVector& WorldPos, float Tolerance = 200.0f) const;

	/** Returns true if any rooms have been registered */
	bool HasData() const { return Rooms.Num() > 0; }

private:

	UPROPERTY()
	TArray<FMinimapRoomData> Rooms;

	TArray<FMinimapIconData> Icons;

	/** Cached combined bounds, recalculated on registration */
	FVector CachedMinBound = FVector(BIG_NUMBER);
	FVector CachedMaxBound = FVector(-BIG_NUMBER);

	void RecalculateBounds();
};
