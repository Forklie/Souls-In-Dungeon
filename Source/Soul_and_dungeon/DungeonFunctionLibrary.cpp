#include "DungeonFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"

FTransform UDungeonFunctionLibrary::CalculateRoomTransform(const FTransform& ExitTransform, const FTransform& EntranceTransform)
{
	// 1. New Room Rotation = Exit Rotation + 180 (so entrance faces exit)
	FRotator ExitRot = ExitTransform.Rotator();
	FRotator NewRoomRot = ExitRot + FRotator(0, 180.0f, 0);
	
	// 2. New Room Location = Exit Location - (Local Entrance Location rotated by New Room Rotation)
	// We use the inverse transform approach to find where the actor origin should be
	FVector LocalEntranceLoc = EntranceTransform.GetLocation();
	FVector RotatedEntranceLoc = NewRoomRot.RotateVector(LocalEntranceLoc);
	FVector NewRoomLoc = ExitTransform.GetLocation() - RotatedEntranceLoc;
	
	return FTransform(NewRoomRot, NewRoomLoc);
}

bool UDungeonFunctionLibrary::CheckRoomOverlap(const UObject* WorldContextObject, AActor* CandidateRoom, const TArray<AActor*>& ExistingRooms, float Tolerance)
{
	if (!CandidateRoom) return false;

	// Find the BoxComponent on the candidate (RoomBounds)
	UBoxComponent* CandidateBounds = CandidateRoom->FindComponentByClass<UBoxComponent>();
	if (!CandidateBounds)
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Candidate room %s has no BoxComponent for overlap check!"), *CandidateRoom->GetName());
		return false;
	}

	FVector CandidateCenter = CandidateBounds->GetComponentLocation();
	FVector CandidateExtent = CandidateBounds->GetScaledBoxExtent() - FVector(Tolerance);

	for (AActor* ExistingRoom : ExistingRooms)
	{
		if (!ExistingRoom || ExistingRoom == CandidateRoom) continue;

		UBoxComponent* ExistingBounds = ExistingRoom->FindComponentByClass<UBoxComponent>();
		if (!ExistingBounds) continue;

		FVector ExistingCenter = ExistingBounds->GetComponentLocation();
		FVector ExistingExtent = ExistingBounds->GetScaledBoxExtent() - FVector(Tolerance);

		// Simple AABB overlap check
		bool bOverlapX = FMath::Abs(CandidateCenter.X - ExistingCenter.X) < (CandidateExtent.X + ExistingExtent.X);
		bool bOverlapY = FMath::Abs(CandidateCenter.Y - ExistingCenter.Y) < (CandidateExtent.Y + ExistingExtent.Y);
		bool bOverlapZ = FMath::Abs(CandidateCenter.Z - ExistingCenter.Z) < (CandidateExtent.Z + ExistingExtent.Z);

		if (bOverlapX && bOverlapY && bOverlapZ)
		{
			return true; // Overlap detected
		}
	}

	return false;
}
