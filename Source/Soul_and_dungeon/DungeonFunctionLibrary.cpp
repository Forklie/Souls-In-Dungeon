#include "DungeonFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"

namespace
{
	UBoxComponent* FindExactRoomBounds(AActor* Room)
	{
		if (!Room)
		{
			return nullptr;
		}

		TArray<UBoxComponent*> BoundsComponents;
		Room->GetComponents(BoundsComponents);

		for (UBoxComponent* Bounds : BoundsComponents)
		{
			if (Bounds && Bounds->GetFName() == FName(TEXT("RoomBounds_Marker")))
			{
				return Bounds;
			}
		}

		for (UBoxComponent* Bounds : BoundsComponents)
		{
			if (Bounds && Bounds->GetName().StartsWith(TEXT("RoomBounds_Marker"), ESearchCase::IgnoreCase))
			{
				return Bounds;
			}
		}

		return nullptr;
	}

	float Dot2D(const FVector& A, const FVector& B)
	{
		return A.X * B.X + A.Y * B.Y;
	}

	FVector Axis2D(const FQuat& Rotation, const FVector& LocalAxis)
	{
		FVector Axis = Rotation.RotateVector(LocalAxis);
		Axis.Z = 0.0f;
		return Axis.GetSafeNormal();
	}

	bool HasSeparatingAxis2D(
		const FVector& Axis,
		const FVector& CandidateCenter,
		const FVector& CandidateAxisX,
		const FVector& CandidateAxisY,
		const FVector& CandidateExtent,
		const FVector& ExistingCenter,
		const FVector& ExistingAxisX,
		const FVector& ExistingAxisY,
		const FVector& ExistingExtent)
	{
		if (Axis.IsNearlyZero())
		{
			return false;
		}

		const float CandidateRadius =
			FMath::Abs(Dot2D(Axis, CandidateAxisX)) * CandidateExtent.X +
			FMath::Abs(Dot2D(Axis, CandidateAxisY)) * CandidateExtent.Y;
		const float ExistingRadius =
			FMath::Abs(Dot2D(Axis, ExistingAxisX)) * ExistingExtent.X +
			FMath::Abs(Dot2D(Axis, ExistingAxisY)) * ExistingExtent.Y;
		const float CenterDistance = FMath::Abs(Dot2D(Axis, ExistingCenter - CandidateCenter));

		return CenterDistance >= CandidateRadius + ExistingRadius;
	}

	bool DoRoomBoundsOverlap2D(
		const FVector& CandidateCenter,
		const FQuat& CandidateRotation,
		const FVector& CandidateExtent,
		const FVector& ExistingCenter,
		const FQuat& ExistingRotation,
		const FVector& ExistingExtent)
	{
		const bool bOverlapZ = FMath::Abs(CandidateCenter.Z - ExistingCenter.Z) < (CandidateExtent.Z + ExistingExtent.Z);
		if (!bOverlapZ)
		{
			return false;
		}

		const FVector CandidateAxisX = Axis2D(CandidateRotation, FVector::ForwardVector);
		const FVector CandidateAxisY = Axis2D(CandidateRotation, FVector::RightVector);
		const FVector ExistingAxisX = Axis2D(ExistingRotation, FVector::ForwardVector);
		const FVector ExistingAxisY = Axis2D(ExistingRotation, FVector::RightVector);

		const FVector Axes[] = { CandidateAxisX, CandidateAxisY, ExistingAxisX, ExistingAxisY };
		for (const FVector& Axis : Axes)
		{
			if (HasSeparatingAxis2D(
				Axis,
				CandidateCenter,
				CandidateAxisX,
				CandidateAxisY,
				CandidateExtent,
				ExistingCenter,
				ExistingAxisX,
				ExistingAxisY,
				ExistingExtent))
			{
				return false;
			}
		}

		return true;
	}
}

FTransform UDungeonFunctionLibrary::CalculateRoomTransform(const FTransform& ExitTransform, const FTransform& EntranceTransform)
{
	// -- MARKER CONVENTION (verified from BP data) --
	// All room BPs use:
	//   Entrance_Marker at (0,0,0), Yaw=0  (at room origin)
	//   Exit_Marker at (0, Y, 0),   Yaw=0  (forward on +Y axis)
	//
	// Both markers point in the SAME direction (+Y, yaw=0).
	// This means:
	//   - The entrance "opening" faces +Y (the player walks INTO the room along +Y)
	//   - The exit "opening" also faces +Y (the player walks OUT of the room along +Y)
	//
	// When chaining rooms:
	//   - The next room's entrance should be placed AT the previous exit location
	//   - The next room should continue in the SAME direction (no rotation)
	//   - This way rooms chain forward: Start → Room1 → Room2 → ... → Exit
	//
	// Since EntranceTransform is always at (0,0,0) relative with Yaw=0,
	// and we want no rotation change, the new room's origin simply goes
	// to the exit location minus the rotated entrance offset.
	//
	// CRITICAL: We do NOT add 180° because both markers face the same way.
	// Adding 180° would make the next room face backward, causing overlap.

	FRotator ExitRot = ExitTransform.Rotator();
	// The new room keeps the same rotation as the exit direction
	FRotator NewRoomRot = ExitRot;
	NewRoomRot.Normalize();
	
	// The entrance is at a local offset from the room origin.
	// When the room is rotated by NewRoomRot, the entrance's world offset
	// from the room origin becomes: NewRoomRot.RotateVector(LocalEntranceLoc)
	// We need: RoomOrigin + RotatedEntranceLoc = ExitLocation
	// Therefore: RoomOrigin = ExitLocation - RotatedEntranceLoc
	FVector LocalEntranceLoc = EntranceTransform.GetLocation();
	FVector RotatedEntranceLoc = NewRoomRot.RotateVector(LocalEntranceLoc);
	FVector NewRoomLoc = ExitTransform.GetLocation() - RotatedEntranceLoc;
	
	FTransform Result(NewRoomRot, NewRoomLoc);
	
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: === Room Alignment ==="));
	UE_LOG(LogTemp, Log, TEXT("DungeonGen:   PrevExit Loc=(%.1f, %.1f, %.1f) Rot=(%.1f, %.1f, %.1f)"),
		ExitTransform.GetLocation().X, ExitTransform.GetLocation().Y, ExitTransform.GetLocation().Z,
		ExitRot.Pitch, ExitRot.Yaw, ExitRot.Roll);
	UE_LOG(LogTemp, Log, TEXT("DungeonGen:   CandidateEntrance Local=(%.1f, %.1f, %.1f)"),
		LocalEntranceLoc.X, LocalEntranceLoc.Y, LocalEntranceLoc.Z);
	UE_LOG(LogTemp, Log, TEXT("DungeonGen:   FinalTransform Loc=(%.1f, %.1f, %.1f) Rot=(%.1f, %.1f, %.1f)"),
		NewRoomLoc.X, NewRoomLoc.Y, NewRoomLoc.Z,
		NewRoomRot.Pitch, NewRoomRot.Yaw, NewRoomRot.Roll);

	return Result;
}

bool UDungeonFunctionLibrary::CheckRoomOverlap(const UObject* WorldContextObject, AActor* CandidateRoom, const TArray<AActor*>& ExistingRooms, float Tolerance)
{
	if (!CandidateRoom) return false;

	UBoxComponent* CandidateBounds = FindExactRoomBounds(CandidateRoom);
	if (!CandidateBounds)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: Candidate room %s has no exact RoomBounds_Marker for overlap check."),
			*CandidateRoom->GetName());
		return false;
	}

	FVector CandidateCenter = CandidateBounds->GetComponentLocation();
	FVector CandidateExtent = CandidateBounds->GetScaledBoxExtent() - FVector(Tolerance);

	for (AActor* ExistingRoom : ExistingRooms)
	{
		if (!ExistingRoom || ExistingRoom == CandidateRoom) continue;

		UBoxComponent* ExistingBounds = FindExactRoomBounds(ExistingRoom);
		if (!ExistingBounds)
		{
			UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Existing room %s has no exact RoomBounds_Marker; skipping overlap comparison."),
				*ExistingRoom->GetName());
			continue;
		}

		FVector ExistingCenter = ExistingBounds->GetComponentLocation();
		FVector ExistingExtent = ExistingBounds->GetScaledBoxExtent() - FVector(Tolerance);

		if (DoRoomBoundsOverlap2D(
			CandidateCenter,
			CandidateBounds->GetComponentQuat(),
			CandidateExtent,
			ExistingCenter,
			ExistingBounds->GetComponentQuat(),
			ExistingExtent))
		{
			UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Oriented overlap detected. Candidate=%s Bounds=%s Center=(%.1f, %.1f, %.1f) Extent=(%.1f, %.1f, %.1f) RotYaw=%.1f, Existing=%s Bounds=%s Center=(%.1f, %.1f, %.1f) Extent=(%.1f, %.1f, %.1f) RotYaw=%.1f"),
				*CandidateRoom->GetName(),
				*CandidateBounds->GetName(),
				CandidateCenter.X, CandidateCenter.Y, CandidateCenter.Z,
				CandidateExtent.X, CandidateExtent.Y, CandidateExtent.Z,
				CandidateBounds->GetComponentRotation().Yaw,
				*ExistingRoom->GetName(),
				*ExistingBounds->GetName(),
				ExistingCenter.X, ExistingCenter.Y, ExistingCenter.Z,
				ExistingExtent.X, ExistingExtent.Y, ExistingExtent.Z,
				ExistingBounds->GetComponentRotation().Yaw);
			return true; // Overlap detected
		}
	}

	return false;
}
