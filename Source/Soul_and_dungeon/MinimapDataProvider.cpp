#include "MinimapDataProvider.h"
#include "Components/BoxComponent.h"

void UMinimapDataProvider::RegisterRoom(AActor* Room, bool bTreasure, bool bStart, bool bExit)
{
	if (!Room) return;

	FMinimapRoomData RoomData;
	RoomData.bIsTreasureRoom = bTreasure;
	RoomData.bIsStartRoom = bStart;
	RoomData.bIsExitRoom = bExit;
	RoomData.bVisited = bStart; // Start room is always visible

	// Extract bounding box from the room's BoxComponent (used for overlap checks)
	UBoxComponent* BoxComp = nullptr;
	TArray<UBoxComponent*> BoxComponents;
	Room->GetComponents(BoxComponents);
	if (BoxComponents.Num() > 0)
	{
		BoxComp = BoxComponents[0];
	}

	if (BoxComp)
	{
		RoomData.WorldCenter = BoxComp->GetComponentLocation();
		RoomData.WorldExtent = BoxComp->GetScaledBoxExtent();
	}
	else
	{
		// Fallback: use actor bounds
		FVector Origin, Extent;
		Room->GetActorBounds(false, Origin, Extent);
		RoomData.WorldCenter = Origin;
		RoomData.WorldExtent = Extent;
	}

	Rooms.Add(RoomData);
	RecalculateBounds();

	UE_LOG(LogTemp, Log, TEXT("MinimapData: Registered room %d — Center=(%.0f, %.0f, %.0f) Extent=(%.0f, %.0f, %.0f) [Start=%d Exit=%d Treasure=%d]"),
		Rooms.Num() - 1,
		RoomData.WorldCenter.X, RoomData.WorldCenter.Y, RoomData.WorldCenter.Z,
		RoomData.WorldExtent.X, RoomData.WorldExtent.Y, RoomData.WorldExtent.Z,
		bStart, bExit, bTreasure);
}

void UMinimapDataProvider::RegisterRoomDirect(const FVector& Center, const FVector& HalfExtent, bool bTreasure, bool bStart, bool bExit)
{
	FMinimapRoomData RoomData;
	RoomData.bIsTreasureRoom = bTreasure;
	RoomData.bIsStartRoom = bStart;
	RoomData.bIsExitRoom = bExit;
	RoomData.bVisited = bStart; // Start room is always visible
	RoomData.WorldCenter = Center;
	RoomData.WorldExtent = HalfExtent;

	Rooms.Add(RoomData);
	RecalculateBounds();

	UE_LOG(LogTemp, Log, TEXT("MinimapData: Registered room (direct) %d — Center=(%.0f, %.0f) Extent=(%.0f, %.0f)"),
		Rooms.Num() - 1, Center.X, Center.Y, HalfExtent.X, HalfExtent.Y);
}

void UMinimapDataProvider::RegisterIcon(AActor* Actor, EMinimapIconType Type)
{
	if (!Actor) return;

	FMinimapIconData IconData;
	IconData.WorldLocation = Actor->GetActorLocation();
	IconData.IconType = Type;
	IconData.TrackedActor = Actor;

	// Figure out which room this icon belongs to
	IconData.OwningRoomIndex = FindRoomContaining(IconData.WorldLocation, 500.0f);

	Icons.Add(IconData);

	UE_LOG(LogTemp, Log, TEXT("MinimapData: Registered icon type %d at (%.0f, %.0f, %.0f) in room %d"),
		(int32)Type, IconData.WorldLocation.X, IconData.WorldLocation.Y, IconData.WorldLocation.Z,
		IconData.OwningRoomIndex);
}

void UMinimapDataProvider::GetWorldBounds(FVector& OutMin, FVector& OutMax) const
{
	OutMin = CachedMinBound;
	OutMax = CachedMaxBound;
}

void UMinimapDataProvider::MarkRoomVisited(int32 RoomIndex)
{
	if (Rooms.IsValidIndex(RoomIndex))
	{
		Rooms[RoomIndex].bVisited = true;
	}
}

int32 UMinimapDataProvider::FindRoomContaining(const FVector& WorldPos, float Tolerance) const
{
	for (int32 i = 0; i < Rooms.Num(); ++i)
	{
		const FMinimapRoomData& R = Rooms[i];
		FVector ExpandedExtent = R.WorldExtent + FVector(Tolerance);

		if (WorldPos.X >= R.WorldCenter.X - ExpandedExtent.X &&
			WorldPos.X <= R.WorldCenter.X + ExpandedExtent.X &&
			WorldPos.Y >= R.WorldCenter.Y - ExpandedExtent.Y &&
			WorldPos.Y <= R.WorldCenter.Y + ExpandedExtent.Y)
		{
			return i;
		}
	}
	return -1;
}

void UMinimapDataProvider::RecalculateBounds()
{
	CachedMinBound = FVector(BIG_NUMBER);
	CachedMaxBound = FVector(-BIG_NUMBER);

	for (const FMinimapRoomData& R : Rooms)
	{
		FVector RoomMin = R.WorldCenter - R.WorldExtent;
		FVector RoomMax = R.WorldCenter + R.WorldExtent;

		CachedMinBound.X = FMath::Min(CachedMinBound.X, RoomMin.X);
		CachedMinBound.Y = FMath::Min(CachedMinBound.Y, RoomMin.Y);
		CachedMinBound.Z = FMath::Min(CachedMinBound.Z, RoomMin.Z);

		CachedMaxBound.X = FMath::Max(CachedMaxBound.X, RoomMax.X);
		CachedMaxBound.Y = FMath::Max(CachedMaxBound.Y, RoomMax.Y);
		CachedMaxBound.Z = FMath::Max(CachedMaxBound.Z, RoomMax.Z);
	}
}
