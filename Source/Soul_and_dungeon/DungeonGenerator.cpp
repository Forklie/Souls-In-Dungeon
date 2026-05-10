#include "DungeonGenerator.h"
#include "DungeonFunctionLibrary.h"
#include "LevelManager.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"

ADungeonGenerator::ADungeonGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADungeonGenerator::BeginPlay()
{
	Super::BeginPlay();
	GenerateDungeon();
}

void ADungeonGenerator::GenerateDungeon()
{
	if (!StartRoomClass || !ExitRoomClass || StandardRoomClasses.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: Classes not assigned in %s!"), *GetName());
		return;
	}

	// 0. Spawn Level Manager to track objectives
	ALevelManager* LevelManager = nullptr;
	if (LevelManagerClass)
	{
		LevelManager = GetWorld()->SpawnActor<ALevelManager>(LevelManagerClass, FTransform::Identity);
	}

	// 1. Spawn Start Room at the generator's location
	FActorSpawnParameters SpawnParams;
	AActor* StartRoom = GetWorld()->SpawnActor<AActor>(StartRoomClass, GetActorTransform(), SpawnParams);
	if (!StartRoom) return;
	SpawnedRooms.Add(StartRoom);

	FTransform CurrentExit = GetExitTransform(StartRoom);

	// 2. Generate main path
	int32 RequiredChests = 0;
	int32 TreasureRoomsSpawned = 0;
	for (int32 i = 0; i < MaxStandardRooms; ++i)
	{
		TSubclassOf<AActor> SelectedClass = StandardRoomClasses[FMath::RandRange(0, StandardRoomClasses.Num() - 1)];

		bool bIsTreasure = false;
		if (TreasureRoomsSpawned < MaxTreasureRooms && TreasureRoomClasses.Num() > 0)
		{
			if (FMath::RandRange(0, 2) == 0 || i == MaxStandardRooms - 1)
			{
				SelectedClass = TreasureRoomClasses[FMath::RandRange(0, TreasureRoomClasses.Num() - 1)];
				TreasureRoomsSpawned++;
				bIsTreasure = true;
			}
		}

		AActor* NewRoom = TrySpawnRoom(SelectedClass, CurrentExit);
		if (NewRoom)
		{
			SpawnedRooms.Add(NewRoom);
			CurrentExit = GetExitTransform(NewRoom);

			// Handle Objectives
			if (bIsTreasure && ChestClass)
			{
				FTransform ChestSpawn = GetSpawnPointTransform(NewRoom, TEXT("Chest"));
				AActor* NewChest = GetWorld()->SpawnActor<AActor>(ChestClass, ChestSpawn, SpawnParams);
				if (NewChest && LevelManager)
				{
					LevelManager->RegisterChest();
					RequiredChests++;
				}
			}

			// Handle Enemies
			if (EnemySpawnerClass)
			{
				FTransform EnemySpawn = GetSpawnPointTransform(NewRoom, TEXT("Enemy"));
				// Only spawn if the component exists (don't spawn at actor root if no marker)
				if (!EnemySpawn.Equals(NewRoom->GetActorTransform()))
				{
					GetWorld()->SpawnActor<AActor>(EnemySpawnerClass, EnemySpawn, SpawnParams);
				}
			}
		}
	}

	// 3. Spawn Exit Room at the very end
	AActor* ExitRoom = TrySpawnRoom(ExitRoomClass, CurrentExit);
	if (ExitRoom)
	{
		SpawnedRooms.Add(ExitRoom);
		
		// Spawn Portal in Exit Room
		if (ExitPortalClass)
		{
			FTransform PortalSpawn = GetSpawnPointTransform(ExitRoom, TEXT("Exit")); // Or Portal spawn point
			AActor* Portal = GetWorld()->SpawnActor<AActor>(ExitPortalClass, PortalSpawn, SpawnParams);
			if (LevelManager && Portal)
			{
				LevelManager->ExitPortalRef = Portal;
			}
		}
	}
	
	if (LevelManager)
	{
		LevelManager->TotalRequiredChests = RequiredChests;
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: LevelManager initialized with %d required chests"), RequiredChests);
	}
	
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Completed. Total Rooms Spawned: %d"), SpawnedRooms.Num());
}

AActor* ADungeonGenerator::TrySpawnRoom(TSubclassOf<AActor> RoomClass, const FTransform& ExitTransform)
{
	for (int32 Attempt = 0; Attempt < MaxSpawnAttemptsPerRoom; ++Attempt)
	{
		// Spawn deferred so we can align before FinishSpawning
		AActor* Candidate = GetWorld()->SpawnActorDeferred<AActor>(RoomClass, FTransform::Identity);
		if (!Candidate) continue;

		FTransform LocalEntrance = GetLocalEntranceTransform(Candidate);
		FTransform NewWorldTransform = UDungeonFunctionLibrary::CalculateRoomTransform(ExitTransform, LocalEntrance);

		Candidate->FinishSpawning(NewWorldTransform);

		// Overlap check via C++ helper (AABB check using BoxComponent)
		if (!UDungeonFunctionLibrary::CheckRoomOverlap(this, Candidate, SpawnedRooms))
		{
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: Room %s accepted at attempt %d"), *RoomClass->GetName(), Attempt);
			return Candidate;
		}

		// Overlap detected, destroy and retry
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Room %s rejected due to overlap (Attempt %d)"), *RoomClass->GetName(), Attempt);
		Candidate->Destroy();
	}

	return nullptr;
}

FTransform ADungeonGenerator::GetExitTransform(AActor* Room)
{
	if (!Room) return FTransform::Identity;
	
	TArray<USceneComponent*> Components;
	Room->GetComponents(Components);
	for (USceneComponent* Comp : Components)
	{
		// Match components named "Exit_01", "Exit_02", etc.
		if (Comp->GetName().Contains(TEXT("Exit")))
		{
			return Comp->GetComponentTransform();
		}
	}
	return Room->GetActorTransform();
}

FTransform ADungeonGenerator::GetLocalEntranceTransform(AActor* Room)
{
	if (!Room) return FTransform::Identity;
	
	TArray<USceneComponent*> Components;
	Room->GetComponents(Components);
	for (USceneComponent* Comp : Components)
	{
		// Match component named "Entrance"
		if (Comp->GetName().Contains(TEXT("Entrance")))
		{
			return Comp->GetRelativeTransform();
		}
	}
	return FTransform::Identity;
}

FTransform ADungeonGenerator::GetSpawnPointTransform(AActor* Room, FString NamePart)
{
	if (!Room) return FTransform::Identity;
	
	TArray<USceneComponent*> Components;
	Room->GetComponents(Components);
	for (USceneComponent* Comp : Components)
	{
		if (Comp->GetName().Contains(NamePart))
		{
			return Comp->GetComponentTransform();
		}
	}
	return Room->GetActorTransform();
}
