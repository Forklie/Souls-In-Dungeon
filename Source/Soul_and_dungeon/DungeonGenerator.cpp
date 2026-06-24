#include "DungeonGenerator.h"

#include "DungeonFunctionLibrary.h"
#include "LevelManager.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	bool IsMeaningfulMarkerOffset(const USceneComponent* Component)
	{
		return Component && !Component->GetRelativeLocation().Equals(FVector::ZeroVector, 1.0f);
	}

	FString FormatTransform(const FTransform& Transform)
	{
		const FVector Loc = Transform.GetLocation();
		const FRotator Rot = Transform.Rotator();
		return FString::Printf(TEXT("Loc=(%.1f, %.1f, %.1f) Rot=(%.1f, %.1f, %.1f)"),
			Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
	}

	bool IsLegacyWallComponentName(const FString& ComponentName)
	{
		return ComponentName.StartsWith(TEXT("WL")) ||
			ComponentName.StartsWith(TEXT("WR")) ||
			ComponentName.StartsWith(TEXT("WB")) ||
			ComponentName.StartsWith(TEXT("WF"));
	}

	bool IsLegacyGeneratedVisualName(const FString& ComponentName)
	{
		if (IsLegacyWallComponentName(ComponentName))
		{
			return true;
		}

		if (ComponentName.Len() >= 2 && ComponentName[0] == TCHAR('F') && FChar::IsDigit(ComponentName[1]))
		{
			return true;
		}

		return false;
	}

	bool IsDoorwayMarkerName(const FString& ComponentName)
	{
		return ComponentName.Equals(TEXT("Entrance_Marker"), ESearchCase::IgnoreCase) ||
			ComponentName.Equals(TEXT("Exit_Marker"), ESearchCase::IgnoreCase) ||
			ComponentName.Equals(TEXT("SideExit_Marker"), ESearchCase::IgnoreCase) ||
			ComponentName.Equals(TEXT("TreasureExit_Marker"), ESearchCase::IgnoreCase);
	}

	TSubclassOf<AActor> LoadRoomClassOrFallback(const TCHAR* ClassPath, TSubclassOf<AActor> FallbackClass)
	{
		if (UClass* LoadedClass = LoadClass<AActor>(nullptr, ClassPath))
		{
			return LoadedClass;
		}

		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Could not load room class %s. Falling back to %s."),
			ClassPath,
			FallbackClass ? *FallbackClass->GetName() : TEXT("NULL"));
		return FallbackClass;
	}
}

ADungeonGenerator::ADungeonGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FClassFinder<ACharacter> DefaultEnemy(TEXT("/Game/ThirdPerson/Blueprints/BP_Skeleton.BP_Skeleton_C"));
	if (DefaultEnemy.Succeeded())
	{
		EnemyClass = DefaultEnemy.Class;
	}
	static ConstructorHelpers::FClassFinder<AAIController> DefaultAIController(TEXT("/Game/Variant_Combat/Blueprints/AI/BP_CombatAIController.BP_CombatAIController_C"));
	if (DefaultAIController.Succeeded())
	{
		EnemyAIControllerClass = DefaultAIController.Class;
	}
	else
	{
		EnemyAIControllerClass = nullptr;
	}

	static ConstructorHelpers::FClassFinder<AActor> DefaultEnemySpawner(TEXT("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemySpawner.BP_CombatEnemySpawner_C"));
	if (DefaultEnemySpawner.Succeeded())
	{
		EnemySpawnerClass = DefaultEnemySpawner.Class;
	}

	static ConstructorHelpers::FClassFinder<AActor> DefaultDoor(TEXT("/Game/Characters/Assests/Interactive_Door/BP_COMP_Door_Interactive_Large.BP_COMP_Door_Interactive_Large_C"));
	if (DefaultDoor.Succeeded())
	{
		DoorClass = DefaultDoor.Class;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FloorFinder(TEXT("/Game/Fantastic_Dungeon_Pack/Meshes/modular/floor/pivotEdge/MOD_Floor_01_E_straight_med.MOD_Floor_01_E_straight_med"));
	if (FloorFinder.Succeeded()) FloorMesh = FloorFinder.Object;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> WallFinder(TEXT("/Game/Fantastic_Dungeon_Pack/Meshes/modular/wall/pivotMiddle/MOD_Wall_01_M_straight_med.MOD_Wall_01_M_straight_med"));
	if (WallFinder.Succeeded()) WallMesh = WallFinder.Object;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ColumnFinder(TEXT("/Game/Fantastic_Dungeon_Pack/Meshes/modular/column/MOD_Column_01_med.MOD_Column_01_med"));
	if (ColumnFinder.Succeeded()) ColumnMesh = ColumnFinder.Object;

	static ConstructorHelpers::FClassFinder<AActor> PBarrel(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_barrel_dungeon_01.BP_PROP_barrel_dungeon_01_C"));
	static ConstructorHelpers::FClassFinder<AActor> PTorch(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_torch_wall_dungeon.BP_PROP_torch_wall_dungeon_C"));
	static ConstructorHelpers::FClassFinder<AActor> PTable(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_table_dungeon_01.BP_PROP_table_dungeon_01_C"));
	static ConstructorHelpers::FClassFinder<AActor> PBookshelf(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_bookshelf_dungeon_01.BP_PROP_bookshelf_dungeon_01_C"));
	static ConstructorHelpers::FClassFinder<AActor> PCage(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_cage_dungeon_02.BP_PROP_cage_dungeon_02_C"));
	static ConstructorHelpers::FClassFinder<AActor> PBrazier(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_brazier_dungeon_01.BP_PROP_brazier_dungeon_01_C"));
	static ConstructorHelpers::FClassFinder<AActor> PTomb(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_tomb_dungeon_01.BP_PROP_tomb_dungeon_01_C"));
	static ConstructorHelpers::FClassFinder<AActor> PAltar(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_altar_dungeon_01.BP_PROP_altar_dungeon_01_C"));
	static ConstructorHelpers::FClassFinder<AActor> PSkeleton(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_skeleton_dungeon_02.BP_PROP_skeleton_dungeon_02_C"));
	static ConstructorHelpers::FClassFinder<AActor> PWallShelf(TEXT("/Game/Fantastic_Dungeon_Pack/blueprints/props/BP_PROP_wallshelf_dungeon_01.BP_PROP_wallshelf_dungeon_01_C"));

	if (PBarrel.Succeeded()) PropRules.Add({ PBarrel.Class, EDungeonPropCategory::CornerClutter, 1.0f, false });
	if (PTorch.Succeeded()) PropRules.Add({ PTorch.Class, EDungeonPropCategory::WallProp, 1.0f, false });
	if (PTable.Succeeded()) PropRules.Add({ PTable.Class, EDungeonPropCategory::Furniture, 1.0f, true });
	if (PBookshelf.Succeeded()) PropRules.Add({ PBookshelf.Class, EDungeonPropCategory::Furniture, 1.0f, true });
	if (PCage.Succeeded()) PropRules.Add({ PCage.Class, EDungeonPropCategory::Ambience, 0.7f, true });
	if (PBrazier.Succeeded()) PropRules.Add({ PBrazier.Class, EDungeonPropCategory::Lighting, 0.8f, false });
	if (PTomb.Succeeded()) PropRules.Add({ PTomb.Class, EDungeonPropCategory::Ambience, 0.8f, true });
	if (PAltar.Succeeded()) PropRules.Add({ PAltar.Class, EDungeonPropCategory::TreasureDeco, 0.8f, true });
	if (PSkeleton.Succeeded()) PropRules.Add({ PSkeleton.Class, EDungeonPropCategory::Ambience, 1.0f, false });
	if (PWallShelf.Succeeded()) PropRules.Add({ PWallShelf.Class, EDungeonPropCategory::WallProp, 0.7f, false });
}

void ADungeonGenerator::BeginPlay()
{
	Super::BeginPlay();
	GenerateDungeon();
}

void ADungeonGenerator::GenerateDungeon()
{
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: ====== PLAYABLE V2 GENERATION START ======"));
	DungeonRandom.Initialize(DungeonSeed);

	if (!StartRoomClass || !ExitRoomClass || StandardRoomClasses.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: Required classes are not assigned on %s. Start=%s StandardCount=%d Exit=%s"),
			*GetName(),
			StartRoomClass ? *StartRoomClass->GetName() : TEXT("NULL"),
			StandardRoomClasses.Num(),
			ExitRoomClass ? *ExitRoomClass->GetName() : TEXT("NULL"));
		return;
	}

	SpawnedRooms.Reset();

	ALevelManager* LevelManager = ALevelManager::GetActiveLevelManager(this);
	if (LevelManager)
	{
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: Found existing LevelManager: %s"), *LevelManager->GetName());
		LevelManager->ResetObjectives();
	}
	else if (LevelManagerClass)
	{
		LevelManager = GetWorld()->SpawnActor<ALevelManager>(LevelManagerClass, FTransform::Identity);
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: Spawned LevelManager: %s"), LevelManager ? *LevelManager->GetName() : TEXT("FAILED"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: No LevelManager found and no LevelManagerClass assigned."));
	}

	MinimapData = NewObject<UMinimapDataProvider>(this);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* StartRoom = GetWorld()->SpawnActor<AActor>(StartRoomClass, GetActorTransform(), SpawnParams);
	if (!StartRoom)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: Failed to spawn start room."));
		return;
	}

	RegisterGeneratedRoom(StartRoom, TEXT("StartLarge"), false, true, false, false);
	BuildRoomGeometry(StartRoom, TEXT("StartLarge"));
	MovePlayerToStartRoom(StartRoom);

	FTransform CurrentExit = GetExitTransform(StartRoom);
	int32 RequiredChests = 0;
	int32 TreasureRoomsSpawned = 0;

	const TSubclassOf<AActor> HallwayClass = HallwayRoomClasses.Num() > 0 && HallwayRoomClasses[0]
		? HallwayRoomClasses[0]
		: StandardRoomClasses[0];
	const TSubclassOf<AActor> StandardClass = StandardRoomClasses[0];
	const TSubclassOf<AActor> CombatClass = StandardRoomClasses.Num() > 1 && StandardRoomClasses[1]
		? StandardRoomClasses[1]
		: StandardClass;
	const TSubclassOf<AActor> TreasureClass = TreasureRoomClasses.Num() > 0 ? TreasureRoomClasses[0] : nullptr;
	const TSubclassOf<AActor> TurnLeftClass = LoadRoomClassOrFallback(TEXT("/Game/Blueprints/Dungeon/Rooms/Procedural/BP_Hallway_Turn_Left_01.BP_Hallway_Turn_Left_01_C"), HallwayClass);
	const TSubclassOf<AActor> TurnRightClass = LoadRoomClassOrFallback(TEXT("/Game/Blueprints/Dungeon/Rooms/Procedural/BP_Hallway_Turn_Right_01.BP_Hallway_Turn_Right_01_C"), HallwayClass);
	const TSubclassOf<AActor> BranchClass = LoadRoomClassOrFallback(TEXT("/Game/Blueprints/Dungeon/Rooms/Procedural/BP_Room_Standard_Branch_01.BP_Room_Standard_Branch_01_C"), StandardClass);
	const TSubclassOf<AActor> CombatWideClass = LoadRoomClassOrFallback(TEXT("/Game/Blueprints/Dungeon/Rooms/Procedural/BP_Room_Combat_Wide_01.BP_Room_Combat_Wide_01_C"), CombatClass);
	const TSubclassOf<AActor> TreasureDeadEndClass = LoadRoomClassOrFallback(TEXT("/Game/Blueprints/Dungeon/Rooms/Procedural/BP_Room_Treasure_DeadEnd_01.BP_Room_Treasure_DeadEnd_01_C"), TreasureClass);

	auto SpawnPathRoom = [&](TSubclassOf<AActor> RoomClass, FTransform& InOutExit, const FString& Label, bool bTreasure, bool bCombat, bool bExit, bool bScatterProps, bool bDoorConnection) -> AActor*
	{
		if (!RoomClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Skipping %s because class is not assigned."), *Label);
			return nullptr;
		}

		const FTransform ConnectionTransform = InOutExit;
		AActor* Room = TrySpawnRoom(RoomClass, InOutExit);
		if (!Room)
		{
			UE_LOG(LogTemp, Error, TEXT("DungeonGen: Failed to spawn %s from class %s."),
				*Label, *RoomClass->GetName());
			return nullptr;
		}

		BuildConnectionFloor(ConnectionTransform, Label);
		if (bDoorConnection)
		{
			SpawnDoorAtConnection(ConnectionTransform, Label);
		}
		else
		{
			SpawnOpenGatewayAtConnection(ConnectionTransform, Label);
		}

		RegisterGeneratedRoom(Room, Label, bTreasure, false, bExit, bScatterProps);
		BuildRoomGeometry(Room, Label);

		if (bTreasure)
		{
			TreasureRoomsSpawned++;
			SpawnChestsInRoom(Room, LevelManager, RequiredChests);
		}

		if (bCombat)
		{
			SpawnEnemiesInRoom(Room);
		}

		if (bExit)
		{
			SpawnExitPortalInRoom(Room, LevelManager);
		}

		InOutExit = GetExitTransform(Room);
		return Room;
	};

	// Playable v2 topology: varied main path with a side treasure branch.
	SpawnPathRoom(HallwayClass, CurrentExit, TEXT("Hallway_01"), false, false, false, false, false);
	SpawnPathRoom(TurnLeftClass, CurrentExit, TEXT("Turn_Left_01"), false, false, false, false, false);
	SpawnPathRoom(HallwayClass, CurrentExit, TEXT("Hallway_02"), false, false, false, false, false);
	AActor* BranchRoom = SpawnPathRoom(BranchClass, CurrentExit, TEXT("Standard_Branch_01"), false, false, false, true, true);

	if (BranchRoom)
	{
		FTransform TreasureExit;
		if (!GetMarkerTransform(BranchRoom, { FName(TEXT("TreasureExit_Marker")), FName(TEXT("SideExit_Marker")) }, { TEXT("TreasureExit"), TEXT("SideExit") }, TEXT("TreasureExit"), TreasureExit))
		{
			TreasureExit = BranchRoom->GetActorTransform();
			UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Standard branch room is missing TreasureExit_Marker/SideExit_Marker; using actor transform for branch."));
		}

		SpawnPathRoom(HallwayClass, TreasureExit, TEXT("Treasure_Hallway_01"), false, false, false, false, false);
		SpawnPathRoom(TreasureDeadEndClass, TreasureExit, TEXT("Treasure_DeadEnd_01"), true, false, false, true, true);
	}

	SpawnPathRoom(HallwayClass, CurrentExit, TEXT("Hallway_03"), false, false, false, false, false);
	SpawnPathRoom(CombatWideClass, CurrentExit, TEXT("Combat_Wide_01"), false, true, false, true, true);
	SpawnPathRoom(TurnRightClass, CurrentExit, TEXT("Turn_Right_01"), false, false, false, false, false);
	SpawnPathRoom(HallwayClass, CurrentExit, TEXT("Hallway_04"), false, false, false, false, false);
	SpawnPathRoom(ExitRoomClass, CurrentExit, TEXT("ExitLarge"), false, false, true, false, true);

	if (LevelManager)
	{
		LevelManager->TotalRequiredChests = RequiredChests;
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: LevelManager initialized. RequiredChests=%d Portal=%s"),
			RequiredChests,
			LevelManager->ExitPortalRef ? *LevelManager->ExitPortalRef->GetName() : TEXT("NULL"));
	}

	SpawnDungeonNavMeshBounds();

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: ====== PLAYABLE V2 GENERATION COMPLETE ======"));
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Total Rooms=%d TreasureRooms=%d RequiredChests=%d"),
		SpawnedRooms.Num(), TreasureRoomsSpawned, RequiredChests);
}

AActor* ADungeonGenerator::TrySpawnRoom(TSubclassOf<AActor> RoomClass, const FTransform& ExitTransform)
{
	if (!RoomClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Attempt = 0; Attempt < MaxSpawnAttemptsPerRoom; ++Attempt)
	{
		AActor* Candidate = GetWorld()->SpawnActor<AActor>(RoomClass, FTransform::Identity, SpawnParams);
		if (!Candidate)
		{
			continue;
		}

		USceneComponent* EntranceComponent = nullptr;
		if (!FindExactOrPrefixedComponent(Candidate, { FName(TEXT("Entrance_Marker")) }, {}, TEXT("Entrance"), EntranceComponent))
		{
			UE_LOG(LogTemp, Error, TEXT("DungeonGen: %s is missing exact Entrance_Marker. Destroying candidate."),
				*Candidate->GetName());
			Candidate->Destroy();
			return nullptr;
		}

		const FTransform LocalEntrance = EntranceComponent->GetRelativeTransform();
		const FTransform NewWorldTransform = UDungeonFunctionLibrary::CalculateRoomTransform(ExitTransform, LocalEntrance);

		UE_LOG(LogTemp, Log, TEXT("DungeonGen: Room attempt %d for %s uses Entrance component %s local %s"),
			Attempt + 1,
			*RoomClass->GetName(),
			*EntranceComponent->GetName(),
			*FormatTransform(LocalEntrance));

		Candidate->SetActorTransform(NewWorldTransform);

		if (!UDungeonFunctionLibrary::CheckRoomOverlap(this, Candidate, SpawnedRooms))
		{
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: Room %s ACCEPTED at attempt %d with actor %s %s"),
				*RoomClass->GetName(),
				Attempt + 1,
				*Candidate->GetName(),
				*FormatTransform(Candidate->GetActorTransform()));
			return Candidate;
		}

		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Room %s REJECTED due to RoomBounds_Marker overlap at attempt %d."),
			*RoomClass->GetName(),
			Attempt + 1);
		Candidate->Destroy();
	}

	return nullptr;
}

FTransform ADungeonGenerator::GetExitTransform(AActor* Room)
{
	USceneComponent* ExitComponent = nullptr;
	if (FindExactOrPrefixedComponent(Room, { FName(TEXT("Exit_Marker")) }, {}, TEXT("Exit"), ExitComponent))
	{
		return ExitComponent->GetComponentTransform();
	}

	UE_LOG(LogTemp, Error, TEXT("DungeonGen: Missing exact Exit_Marker on %s. Falling back to actor transform."),
		Room ? *Room->GetName() : TEXT("NULL"));
	return Room ? Room->GetActorTransform() : FTransform::Identity;
}

bool ADungeonGenerator::GetMarkerTransform(AActor* Room, const TArray<FName>& ExactNames, const TArray<FString>& Prefixes, const TCHAR* Purpose, FTransform& OutTransform)
{
	USceneComponent* Marker = nullptr;
	if (FindExactOrPrefixedComponent(Room, ExactNames, Prefixes, Purpose, Marker))
	{
		OutTransform = Marker->GetComponentTransform();
		return true;
	}

	OutTransform = Room ? Room->GetActorTransform() : FTransform::Identity;
	return false;
}

FTransform ADungeonGenerator::GetLocalEntranceTransform(AActor* Room)
{
	USceneComponent* EntranceComponent = nullptr;
	if (FindExactOrPrefixedComponent(Room, { FName(TEXT("Entrance_Marker")) }, {}, TEXT("Entrance"), EntranceComponent))
	{
		return EntranceComponent->GetRelativeTransform();
	}

	UE_LOG(LogTemp, Error, TEXT("DungeonGen: Missing exact Entrance_Marker on %s. Falling back to identity."),
		Room ? *Room->GetName() : TEXT("NULL"));
	return FTransform::Identity;
}

bool ADungeonGenerator::FindSpawnPointTransform(AActor* Room, const FString& NamePart, FTransform& OutTransform)
{
	TArray<FName> ExactNames;
	TArray<FString> Prefixes;
	const FString LowerNamePart = NamePart.ToLower();

	if (LowerNamePart.Contains(TEXT("chest")))
	{
		Prefixes = { TEXT("ChestSpawnPoint") };
	}
	else if (LowerNamePart.Contains(TEXT("enemy")))
	{
		Prefixes = { TEXT("EnemySpawnPoint") };
	}
	else if (LowerNamePart.Contains(TEXT("portal")))
	{
		ExactNames = { FName(TEXT("ExitPortalSpawnPoint_01")) };
		Prefixes = { TEXT("ExitPortalSpawnPoint") };
	}
	else if (LowerNamePart.Contains(TEXT("player")))
	{
		ExactNames = { FName(TEXT("PlayerSpawnPoint_Marker")) };
		Prefixes = { TEXT("PlayerSpawnPoint") };
	}
	else if (LowerNamePart.Contains(TEXT("door")))
	{
		Prefixes = { TEXT("DoorSpawnPoint") };
	}
	else
	{
		Prefixes = { NamePart };
	}

	USceneComponent* Component = nullptr;
	if (FindExactOrPrefixedComponent(Room, ExactNames, Prefixes, *NamePart, Component))
	{
		OutTransform = Component->GetComponentTransform();
		return true;
	}

	OutTransform = Room ? Room->GetActorTransform() : FTransform::Identity;
	return false;
}

FTransform ADungeonGenerator::GetSpawnPointTransform(AActor* Room, FString NamePart)
{
	FTransform Result;
	if (!FindSpawnPointTransform(Room, NamePart, Result))
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Missing supported marker [%s] on [%s]"),
			*NamePart,
			Room ? *Room->GetName() : TEXT("NULL"));
	}
	return Result;
}

void ADungeonGenerator::ValidateRoomMarkers(AActor* Room, const FString& RoomLabel)
{
	if (!Room)
	{
		return;
	}

	USceneComponent* EntranceComponent = nullptr;
	USceneComponent* ExitComponent = nullptr;
	const bool bHasEntrance = FindExactOrPrefixedComponent(Room, { FName(TEXT("Entrance_Marker")) }, {}, TEXT("Entrance"), EntranceComponent);
	const bool bHasExit = FindExactOrPrefixedComponent(Room, { FName(TEXT("Exit_Marker")) }, {}, TEXT("Exit"), ExitComponent);
	const bool bHasBounds = FindRoomBoundsComponent(Room) != nullptr;

	if (!bHasEntrance)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: %s [%s] missing exact Entrance_Marker."),
			*RoomLabel,
			*Room->GetName());
	}
	if (!bHasExit)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: %s [%s] missing exact Exit_Marker."),
			*RoomLabel,
			*Room->GetName());
	}
	if (!bHasBounds)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: %s [%s] missing exact RoomBounds_Marker."),
			*RoomLabel,
			*Room->GetName());
	}
}

void ADungeonGenerator::DressRoom(AActor* Room, const FString& RoomLabel)
{
	if (!Room || PropRules.Num() == 0)
	{
		return;
	}

	UBoxComponent* Bounds = FindRoomBoundsComponent(Room);
	if (!Bounds)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TArray<FDungeonPropRule> ValidRules;
	for (const FDungeonPropRule& Rule : PropRules)
	{
		if (Rule.PropClass)
		{
			ValidRules.Add(Rule);
		}
	}

	if (ValidRules.Num() == 0)
	{
		return;
	}

	const FTransform BoundsTransform = Bounds->GetComponentTransform();
	const FVector Extent = Bounds->GetScaledBoxExtent();
	const float FloorZ = -Extent.Z + 50.0f;
	const int32 RoomPropCount = RoomLabel.Contains(TEXT("Hallway")) || RoomLabel.Contains(TEXT("Turn"))
		? DungeonRandom.RandRange(0, 2)
		: DungeonRandom.RandRange(3, 7);

	TArray<FVector> AvoidLocations;
	TArray<USceneComponent*> Components;
	Room->GetComponents(Components);
	for (USceneComponent* Component : Components)
	{
		if (!Component || !IsMeaningfulMarkerOffset(Component))
		{
			continue;
		}

		const FString Name = Component->GetName();
		if (Name.Contains(TEXT("SpawnPoint")) || Name.Contains(TEXT("Exit")) || Name.Contains(TEXT("Entrance")))
		{
			AvoidLocations.Add(Component->GetComponentLocation());
		}
	}

	auto ChooseRule = [&]() -> FDungeonPropRule
	{
		float TotalWeight = 0.0f;
		for (const FDungeonPropRule& Rule : ValidRules)
		{
			TotalWeight += FMath::Max(0.01f, Rule.Weight);
		}

		float RandomWeight = DungeonRandom.FRandRange(0.0f, TotalWeight);
		for (const FDungeonPropRule& Rule : ValidRules)
		{
			RandomWeight -= FMath::Max(0.01f, Rule.Weight);
			if (RandomWeight <= 0.0f)
			{
				return Rule;
			}
		}
		return ValidRules.Last();
	};

	for (int32 Index = 0; Index < RoomPropCount; ++Index)
	{
		const FDungeonPropRule Rule = ChooseRule();
		if (!Rule.PropClass)
		{
			continue;
		}

		FVector LocalLocation = FVector::ZeroVector;
		bool bFoundSafeSpot = false;
		for (int32 Attempt = 0; Attempt < 12; ++Attempt)
		{
			const bool bUseXWall = DungeonRandom.RandRange(0, 1) == 0;
			const float WallSign = DungeonRandom.RandRange(0, 1) == 0 ? -1.0f : 1.0f;
			const float WallInset = Rule.bBlocksMovement ? 180.0f : 120.0f;
			LocalLocation = FVector(
				bUseXWall ? WallSign * FMath::Max(0.0f, Extent.X - WallInset) : DungeonRandom.FRandRange(-Extent.X * 0.65f, Extent.X * 0.65f),
				bUseXWall ? DungeonRandom.FRandRange(-Extent.Y * 0.70f, Extent.Y * 0.70f) : WallSign * FMath::Max(0.0f, Extent.Y - WallInset),
				FloorZ);

			if (FMath::Abs(LocalLocation.X) < 450.0f && FMath::Abs(LocalLocation.Y) < Extent.Y * 0.85f)
			{
				continue;
			}

			const FVector WorldLocation = BoundsTransform.TransformPosition(LocalLocation);
			bool bTooClose = false;
			for (const FVector& AvoidLocation : AvoidLocations)
			{
				if (FVector::Dist2D(WorldLocation, AvoidLocation) < 500.0f)
				{
					bTooClose = true;
					break;
				}
			}

			if (!bTooClose)
			{
				bFoundSafeSpot = true;
				break;
			}
		}

		if (!bFoundSafeSpot)
		{
			continue;
		}

		FTransform PropTransform(BoundsTransform.GetRotation(), BoundsTransform.TransformPosition(LocalLocation));
		PropTransform.SetRotation((BoundsTransform.GetRotation() * FRotator(0.0f, DungeonRandom.FRandRange(0.0f, 360.0f), 0.0f).Quaternion()).GetNormalized());

		AActor* Prop = GetWorld()->SpawnActor<AActor>(Rule.PropClass, PropTransform, SpawnParams);
		if (Prop)
		{
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: Prop %s placed in %s at %s"),
				*Prop->GetName(),
				*Room->GetName(),
				*FormatTransform(Prop->GetActorTransform()));
		}
	}
}

bool ADungeonGenerator::FindExactOrPrefixedComponent(
	AActor* Room,
	const TArray<FName>& ExactNames,
	const TArray<FString>& AllowedPrefixes,
	const TCHAR* Purpose,
	USceneComponent*& OutComponent) const
{
	OutComponent = nullptr;
	if (!Room)
	{
		return false;
	}

	TArray<USceneComponent*> Components;
	Room->GetComponents(Components);

	USceneComponent* BestComponent = nullptr;
	int32 BestScore = MIN_int32;

	for (USceneComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		const FString ComponentName = Component->GetName();
		int32 Score = MIN_int32;

		for (int32 Index = 0; Index < ExactNames.Num(); ++Index)
		{
			if (Component->GetFName() == ExactNames[Index])
			{
				Score = FMath::Max(Score, 1000 - Index);
			}
		}

		for (const FString& Prefix : AllowedPrefixes)
		{
			if (ComponentName.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				Score = FMath::Max(Score, 500);
			}
		}

		if (Score == MIN_int32)
		{
			continue;
		}

		if (IsMeaningfulMarkerOffset(Component))
		{
			Score += 50;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestComponent = Component;
		}
	}

	if (!BestComponent)
	{
		return false;
	}

	OutComponent = BestComponent;
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: %s marker for %s resolved to component %s world %s local %s"),
		Purpose,
		*Room->GetName(),
		*BestComponent->GetName(),
		*FormatTransform(BestComponent->GetComponentTransform()),
		*FormatTransform(BestComponent->GetRelativeTransform()));
	return true;
}

UBoxComponent* ADungeonGenerator::FindRoomBoundsComponent(AActor* Room) const
{
	if (!Room)
	{
		return nullptr;
	}

	TArray<UBoxComponent*> Components;
	Room->GetComponents(Components);

	for (UBoxComponent* Component : Components)
	{
		if (Component && Component->GetFName() == FName(TEXT("RoomBounds_Marker")))
		{
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: Bounds marker for %s resolved to %s world %s extent=(%.1f, %.1f, %.1f)"),
				*Room->GetName(),
				*Component->GetName(),
				*FormatTransform(Component->GetComponentTransform()),
				Component->GetScaledBoxExtent().X,
				Component->GetScaledBoxExtent().Y,
				Component->GetScaledBoxExtent().Z);
			return Component;
		}
	}

	for (UBoxComponent* Component : Components)
	{
		if (Component && Component->GetName().StartsWith(TEXT("RoomBounds_Marker"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: Bounds marker for %s resolved to prefixed %s world %s extent=(%.1f, %.1f, %.1f)"),
				*Room->GetName(),
				*Component->GetName(),
				*FormatTransform(Component->GetComponentTransform()),
				Component->GetScaledBoxExtent().X,
				Component->GetScaledBoxExtent().Y,
				Component->GetScaledBoxExtent().Z);
			return Component;
		}
	}

	return nullptr;
}

void ADungeonGenerator::RegisterGeneratedRoom(AActor* Room, const FString& RoomLabel, bool bIsTreasure, bool bIsStart, bool bIsExit, bool bScatterProps)
{
	if (!Room)
	{
		return;
	}

	SpawnedRooms.Add(Room);
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: %s registered as %s with actor transform %s"),
		*Room->GetName(),
		*RoomLabel,
		*FormatTransform(Room->GetActorTransform()));

	ValidateRoomMarkers(Room, RoomLabel);

	if (bScatterProps)
	{
		DressRoom(Room, RoomLabel);
	}

	if (MinimapData)
	{
		MinimapData->RegisterRoom(Room, bIsTreasure, bIsStart, bIsExit);
	}
}

void ADungeonGenerator::SpawnDoorAtConnection(const FTransform& ConnectionTransform, const FString& ConnectionLabel)
{
	if (!DoorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: DoorClass not assigned. Skipping door before %s."), *ConnectionLabel);
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform AdjustedTransform = ConnectionTransform;

	AActor* Door = GetWorld()->SpawnActor<AActor>(DoorClass, AdjustedTransform, SpawnParams);
	if (!Door)
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Failed to spawn door before %s at %s."),
			*ConnectionLabel,
			*FormatTransform(AdjustedTransform));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Door %s spawned before %s at %s"),
		*Door->GetName(),
		*ConnectionLabel,
		*FormatTransform(Door->GetActorTransform()));
}

void ADungeonGenerator::SpawnOpenGatewayAtConnection(const FTransform& ConnectionTransform, const FString& ConnectionLabel)
{
	if (!ColumnMesh)
	{
		return;
	}

	const FVector Right = ConnectionTransform.GetRotation().GetAxisX();
	const FVector Up = FVector::UpVector;
	const FVector BaseLocation = ConnectionTransform.GetLocation();
	const FQuat Rotation = ConnectionTransform.GetRotation();

	auto AddColumn = [&](const FVector& Offset)
	{
		UStaticMeshComponent* Column = NewObject<UStaticMeshComponent>(this);
		Column->SetStaticMesh(ColumnMesh);
		Column->SetMobility(EComponentMobility::Movable);
		Column->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Column->SetGenerateOverlapEvents(false);
		AddInstanceComponent(Column);
		Column->RegisterComponent();
		if (USceneComponent* Root = GetRootComponent())
		{
			Column->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
		}
		Column->SetWorldLocationAndRotation(BaseLocation + Offset, Rotation);
	};

	AddColumn(Right * -360.0f + Up * 90.0f);
	AddColumn(Right * 360.0f + Up * 90.0f);

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Open gateway columns spawned before %s at %s"),
		*ConnectionLabel,
		*FormatTransform(ConnectionTransform));
}

void ADungeonGenerator::BuildConnectionFloor(const FTransform& ConnectionTransform, const FString& ConnectionLabel)
{
	const FVector Right = ConnectionTransform.GetRotation().GetAxisX();
	const FVector Forward = ConnectionTransform.GetRotation().GetAxisY();
	const FVector Up = FVector::UpVector;
	const FVector Center = ConnectionTransform.GetLocation() + Forward * 220.0f + Up * 10.0f;
	const FQuat Rotation = ConnectionTransform.GetRotation();

	UBoxComponent* FloorCollision = NewObject<UBoxComponent>(this, MakeUniqueObjectName(this, UBoxComponent::StaticClass(), TEXT("DG_ConnectorFloorCollision")));
	FloorCollision->SetMobility(EComponentMobility::Movable);
	FloorCollision->SetBoxExtent(FVector(480.0f, 360.0f, 24.0f));
	FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
	FloorCollision->SetGenerateOverlapEvents(false);
	FloorCollision->SetHiddenInGame(true);
	FloorCollision->SetVisibility(false);
	FloorCollision->SetWorldLocationAndRotation(Center, Rotation);
	AddInstanceComponent(FloorCollision);
	FloorCollision->RegisterComponent();
	if (USceneComponent* Root = GetRootComponent())
	{
		FloorCollision->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
	}

	if (FloorMesh)
	{
		UStaticMeshComponent* FloorVisual = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("DG_ConnectorFloor")));
		FloorVisual->SetStaticMesh(FloorMesh);
		FloorVisual->SetMobility(EComponentMobility::Movable);
		FloorVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FloorVisual->SetWorldLocationAndRotation(Center - Up * 10.0f, Rotation);
		const FVector MeshSize = FloorMesh->GetBounds().BoxExtent * 2.0f;
		FloorVisual->SetWorldScale3D(FVector(
			960.0f / FMath::Max(MeshSize.X, 1.0f),
			720.0f / FMath::Max(MeshSize.Y, 1.0f),
			1.0f));
		AddInstanceComponent(FloorVisual);
		FloorVisual->RegisterComponent();
		if (USceneComponent* Root = GetRootComponent())
		{
			FloorVisual->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Connector floor built before %s at %s"),
		*ConnectionLabel,
		*FormatTransform(ConnectionTransform));
}

void ADungeonGenerator::SpawnChestsInRoom(AActor* Room, ALevelManager* LevelManager, int32& RequiredChests)
{
	if (!Room || !ChestClass)
	{
		return;
	}

	FTransform ChestSpawn;
	if (!FindSpawnPointTransform(Room, TEXT("ChestSpawnPoint"), ChestSpawn))
	{
		if (UBoxComponent* Bounds = FindRoomBoundsComponent(Room))
		{
			ChestSpawn = Bounds->GetComponentTransform();
			ChestSpawn.SetLocation(Bounds->GetComponentLocation() + FVector(0.0f, 0.0f, -Bounds->GetScaledBoxExtent().Z + 60.0f));
		}
		else
		{
			ChestSpawn = Room->GetActorTransform();
		}

		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Missing ChestSpawnPoint_* on %s. Using fallback %s"),
			*Room->GetName(),
			*FormatTransform(ChestSpawn));
	}

	ChestSpawn.SetLocation(ChestSpawn.GetLocation() + FVector(0.0f, 0.0f, 50.0f));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Chest = GetWorld()->SpawnActor<AActor>(ChestClass, ChestSpawn, SpawnParams);
	if (!Chest)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: Failed to spawn chest in %s."), *Room->GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Chest %s spawned in %s at %s"),
		*Chest->GetName(),
		*Room->GetName(),
		*FormatTransform(Chest->GetActorTransform()));

	if (LevelManager)
	{
		LevelManager->RegisterChest(Chest);
		RequiredChests++;
	}

	if (MinimapData)
	{
		MinimapData->RegisterIcon(Chest, EMinimapIconType::Chest);
	}
}

void ADungeonGenerator::SpawnEnemiesInRoom(AActor* Room)
{
	if (!Room || !EnemySpawnerClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: EnemySpawnerClass is not assigned; combat room will not spawn enemies."));
		return;
	}

	TArray<USceneComponent*> Components;
	Room->GetComponents(Components);

	TArray<USceneComponent*> SpawnMarkers;
	bool bHasMeaningfulMarker = false;
	for (USceneComponent* Component : Components)
	{
		if (!Component || !Component->GetName().StartsWith(TEXT("EnemySpawnPoint"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		SpawnMarkers.Add(Component);
		bHasMeaningfulMarker = bHasMeaningfulMarker || IsMeaningfulMarkerOffset(Component);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 SpawnedCount = 0;
	for (USceneComponent* Marker : SpawnMarkers)
	{
		if (bHasMeaningfulMarker && !IsMeaningfulMarkerOffset(Marker))
		{
			continue;
		}

		AActor* EnemySpawner = GetWorld()->SpawnActor<AActor>(EnemySpawnerClass, Marker->GetComponentTransform(), SpawnParams);
		if (EnemySpawner)
		{
			SpawnedCount++;
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: Enemy spawner %s spawned at marker %s in %s at %s"),
				*EnemySpawner->GetName(),
				*Marker->GetName(),
				*Room->GetName(),
				*FormatTransform(EnemySpawner->GetActorTransform()));

			if (MinimapData)
			{
				MinimapData->RegisterIcon(EnemySpawner, EMinimapIconType::Enemy);
			}
		}
	}

	if (SpawnedCount > 0)
	{
		return;
	}

	UBoxComponent* Bounds = FindRoomBoundsComponent(Room);
	if (!Bounds)
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Combat room %s has no EnemySpawnPoint_* or RoomBounds_Marker; no enemies spawned."),
			*Room->GetName());
		return;
	}

	FTransform FallbackSpawn = Bounds->GetComponentTransform();
	FallbackSpawn.SetLocation(Bounds->GetComponentLocation() + FVector(0.0f, 0.0f, -Bounds->GetScaledBoxExtent().Z + 110.0f));

	AActor* EnemySpawner = GetWorld()->SpawnActor<AActor>(EnemySpawnerClass, FallbackSpawn, SpawnParams);
	if (EnemySpawner)
	{
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: Enemy spawner %s spawned at combat-room fallback in %s at %s"),
			*EnemySpawner->GetName(),
			*Room->GetName(),
			*FormatTransform(EnemySpawner->GetActorTransform()));

		if (MinimapData)
		{
			MinimapData->RegisterIcon(EnemySpawner, EMinimapIconType::Enemy);
		}
	}
}

void ADungeonGenerator::SpawnDungeonNavMeshBounds()
{
	if (SpawnedRooms.Num() == 0)
	{
		return;
	}

	FBox CombinedBounds(ForceInit);

	for (AActor* Room : SpawnedRooms)
	{
		if (UBoxComponent* Bounds = FindRoomBoundsComponent(Room))
		{
			FTransform BoxTransform = Bounds->GetComponentTransform();
			FVector Extent = Bounds->GetScaledBoxExtent();
			
			// Compute all 8 corners and add to bounds
			for (int i = 0; i < 8; ++i)
			{
				FVector Corner = FVector(
					(i & 1) ? Extent.X : -Extent.X,
					(i & 2) ? Extent.Y : -Extent.Y,
					(i & 4) ? Extent.Z : -Extent.Z
				);
				CombinedBounds += BoxTransform.TransformPosition(Corner);
			}
		}
		else
		{
			CombinedBounds += Room->GetComponentsBoundingBox(true);
		}
	}

	CombinedBounds += GetComponentsBoundingBox(true);

	FVector Center = CombinedBounds.GetCenter();
	FVector Extent = CombinedBounds.GetExtent() + FVector(1500.0f, 1500.0f, 1500.0f); // Add generous padding

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANavMeshBoundsVolume* NavVolume = GetWorld()->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), Center, FRotator::ZeroRotator, SpawnParams);
	if (NavVolume)
	{
		NavVolume->SetActorLabel(TEXT("DungeonGen_NavMeshBounds"));
		UBoxComponent* BoxComp = Cast<UBoxComponent>(NavVolume->GetRootComponent());
		if (BoxComp)
		{
			BoxComp->SetBoxExtent(Extent);
		}
		
		UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
		if (NavSys)
		{
			NavSys->Build();
			UE_LOG(LogTemp, Log, TEXT("DungeonGen: NavMeshBoundsVolume spawned and Navigation rebuilt. Extents: %s"), *Extent.ToString());
		}
	}
}

void ADungeonGenerator::SpawnExitPortalInRoom(AActor* ExitRoom, ALevelManager* LevelManager)
{
	if (!ExitRoom || !ExitPortalClass)
	{
		return;
	}

	FTransform PortalSpawn;
	if (!FindSpawnPointTransform(ExitRoom, TEXT("ExitPortalSpawnPoint"), PortalSpawn))
	{
		if (UBoxComponent* Bounds = FindRoomBoundsComponent(ExitRoom))
		{
			PortalSpawn = Bounds->GetComponentTransform();
			PortalSpawn.SetLocation(Bounds->GetComponentLocation() + FVector(0.0f, 0.0f, -Bounds->GetScaledBoxExtent().Z + 80.0f));
		}
		else
		{
			PortalSpawn = ExitRoom->GetActorTransform();
		}

		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Missing ExitPortalSpawnPoint_* on %s. Using fallback %s"),
			*ExitRoom->GetName(),
			*FormatTransform(PortalSpawn));
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Portal = GetWorld()->SpawnActor<AActor>(ExitPortalClass, PortalSpawn, SpawnParams);
	if (!Portal)
	{
		UE_LOG(LogTemp, Error, TEXT("DungeonGen: Failed to spawn exit portal in %s."), *ExitRoom->GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Portal %s spawned in %s at %s"),
		*Portal->GetName(),
		*ExitRoom->GetName(),
		*FormatTransform(Portal->GetActorTransform()));

	if (LevelManager)
	{
		LevelManager->ExitPortalRef = Portal;
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: LevelManager->ExitPortalRef set to %s"), *Portal->GetName());
	}

	if (MinimapData)
	{
		MinimapData->RegisterIcon(Portal, EMinimapIconType::Portal);
	}
}

void ADungeonGenerator::MovePlayerToStartRoom(AActor* StartRoom)
{
	if (!StartRoom)
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: No player pawn available during generation; PlayerStart must already be inside start room."));
		return;
	}

	FTransform PlayerSpawn;
	if (!FindSpawnPointTransform(StartRoom, TEXT("PlayerSpawnPoint"), PlayerSpawn))
	{
		if (UBoxComponent* Bounds = FindRoomBoundsComponent(StartRoom))
		{
			PlayerSpawn = Bounds->GetComponentTransform();
			PlayerSpawn.SetLocation(Bounds->GetComponentLocation() + FVector(0.0f, 0.0f, -Bounds->GetScaledBoxExtent().Z + 120.0f));
		}
		else
		{
			PlayerSpawn = StartRoom->GetActorTransform();
			PlayerSpawn.SetLocation(StartRoom->GetActorLocation() + FVector(0.0f, 400.0f, 120.0f));
		}

		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Missing PlayerSpawnPoint_Marker on %s. Using fallback %s"),
			*StartRoom->GetName(),
			*FormatTransform(PlayerSpawn));
	}

	const FVector SpawnLocation = PlayerSpawn.GetLocation();
	const FRotator SpawnRotation(0.0f, PlayerSpawn.Rotator().Yaw, 0.0f);
	PlayerPawn->SetActorLocationAndRotation(SpawnLocation, SpawnRotation, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Player moved to start room at %s"), *FormatTransform(PlayerPawn->GetActorTransform()));
}

void ADungeonGenerator::BuildRoomGeometry(AActor* Room, const FString& RoomLabel)
{
	if (!Room) return;

	UBoxComponent* Bounds = FindRoomBoundsComponent(Room);
	if (!Bounds) return;

	TArray<UActorComponent*> RoomComponents;
	Room->GetComponents(RoomComponents);
	for (UActorComponent* Comp : RoomComponents)
	{
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			if (IsLegacyGeneratedVisualName(SceneComp->GetName()))
			{
				SceneComp->SetHiddenInGame(true);
				SceneComp->SetVisibility(false, true);
				if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(SceneComp))
				{
					PrimitiveComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
			}
		}
	}

	const FVector Extent = Bounds->GetScaledBoxExtent();
	const FTransform BoundsWorld = Bounds->GetComponentTransform();
	const FVector BoundsCenter = BoundsWorld.GetLocation();
	const FQuat BoundsRotation = BoundsWorld.GetRotation();
	const FVector AxisX = BoundsRotation.GetAxisX();
	const FVector AxisY = BoundsRotation.GetAxisY();
	const FVector AxisZ = BoundsRotation.GetAxisZ();
	const float FloorZ = -Extent.Z;
	const float WallHeight = FMath::Max(Extent.Z * 2.0f, 260.0f);
	const float WallThickness = 80.0f;
	const float DoorwayHalfWidth = 520.0f;
	const bool bIsStartRoom = RoomLabel.Contains(TEXT("Start"));
	const bool bIsExitRoom = RoomLabel.Contains(TEXT("Exit"));
	const bool bIsHallway = RoomLabel.Contains(TEXT("Hallway"));
	const bool bIsTreasureDeadEnd = RoomLabel.Contains(TEXT("Treasure_DeadEnd"));

	const FVector WallSize = WallMesh ? WallMesh->GetBounds().BoxExtent * 2.0f : FVector(400.0f, 40.0f, 260.0f);
	const FVector FloorSize = FloorMesh ? FloorMesh->GetBounds().BoxExtent * 2.0f : FVector(400.0f, 400.0f, 20.0f);

	auto ToWorld = [&](const FVector& LocalPos)
	{
		return BoundsCenter + AxisX * LocalPos.X + AxisY * LocalPos.Y + AxisZ * LocalPos.Z;
	};

	auto AttachRuntimeComponent = [&](USceneComponent* Component)
	{
		Room->AddInstanceComponent(Component);
		Component->RegisterComponent();
		if (USceneComponent* Root = Room->GetRootComponent())
		{
			Component->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
			}
		};

	auto AddBlocker = [&](const TCHAR* NamePrefix, const FVector& LocalPos, const FQuat& WorldRotation, const FVector& BoxExtent)
	{
		UBoxComponent* Blocker = NewObject<UBoxComponent>(Room, MakeUniqueObjectName(Room, UBoxComponent::StaticClass(), FName(NamePrefix)));
		Blocker->SetMobility(EComponentMobility::Movable);
		Blocker->SetBoxExtent(BoxExtent);
		Blocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Blocker->SetCollisionResponseToAllChannels(ECR_Block);
		Blocker->SetGenerateOverlapEvents(false);
		Blocker->SetHiddenInGame(true);
		Blocker->SetVisibility(false);
		Blocker->SetWorldLocationAndRotation(ToWorld(LocalPos), WorldRotation);
		AttachRuntimeComponent(Blocker);
	};

	auto AddVisibleWall = [&](const TCHAR* NamePrefix, const FVector& LocalPos, const FQuat& WorldRotation, float SectionLength)
	{
		if (!WallMesh || SectionLength <= 1.0f || WallSize.X <= 1.0f || WallSize.Z <= 1.0f)
		{
			return;
		}

		UStaticMeshComponent* WallComp = NewObject<UStaticMeshComponent>(Room, MakeUniqueObjectName(Room, UStaticMeshComponent::StaticClass(), FName(NamePrefix)));
		WallComp->SetStaticMesh(WallMesh);
		WallComp->SetMobility(EComponentMobility::Movable);
		WallComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WallComp->SetGenerateOverlapEvents(false);
		WallComp->SetWorldLocationAndRotation(ToWorld(LocalPos), WorldRotation);
		WallComp->SetWorldScale3D(FVector(
			SectionLength / FMath::Max(WallSize.X, 1.0f),
			1.0f,
			WallHeight / FMath::Max(WallSize.Z, 1.0f)));
		AttachRuntimeComponent(WallComp);
	};

	auto AddRuntimeFloor = [&]()
	{
		UBoxComponent* FloorCollision = NewObject<UBoxComponent>(Room, MakeUniqueObjectName(Room, UBoxComponent::StaticClass(), TEXT("DG_RoomFloorCollision")));
		FloorCollision->SetMobility(EComponentMobility::Movable);
		FloorCollision->SetBoxExtent(FVector(Extent.X, Extent.Y, 30.0f));
		FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
		FloorCollision->SetGenerateOverlapEvents(false);
		FloorCollision->SetHiddenInGame(true);
		FloorCollision->SetVisibility(false);
		FloorCollision->SetWorldLocationAndRotation(ToWorld(FVector(0.0f, 0.0f, FloorZ - 20.0f)), BoundsRotation);
		AttachRuntimeComponent(FloorCollision);

		if (!FloorMesh || FloorSize.X <= 1.0f || FloorSize.Y <= 1.0f)
		{
			return;
		}

		const float TileSize = 400.0f;
		const int32 CountX = FMath::Max(1, FMath::CeilToInt((Extent.X * 2.0f) / TileSize));
		const int32 CountY = FMath::Max(1, FMath::CeilToInt((Extent.Y * 2.0f) / TileSize));
		const float StartX = -Extent.X + TileSize * 0.5f;
		const float StartY = -Extent.Y + TileSize * 0.5f;

		for (int32 XIndex = 0; XIndex < CountX; ++XIndex)
		{
			for (int32 YIndex = 0; YIndex < CountY; ++YIndex)
			{
				const float LocalX = FMath::Min(StartX + XIndex * TileSize, Extent.X - TileSize * 0.5f);
				const float LocalY = FMath::Min(StartY + YIndex * TileSize, Extent.Y - TileSize * 0.5f);
				UStaticMeshComponent* FloorTile = NewObject<UStaticMeshComponent>(Room, MakeUniqueObjectName(Room, UStaticMeshComponent::StaticClass(), TEXT("DG_FloorTile")));
				FloorTile->SetStaticMesh(FloorMesh);
				FloorTile->SetMobility(EComponentMobility::Movable);
				FloorTile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				FloorTile->SetGenerateOverlapEvents(false);
				FloorTile->SetWorldLocationAndRotation(ToWorld(FVector(LocalX, LocalY, FloorZ)), BoundsRotation);
				FloorTile->SetWorldScale3D(FVector(
					TileSize / FMath::Max(FloorSize.X, 1.0f),
					TileSize / FMath::Max(FloorSize.Y, 1.0f),
					1.0f));
				AttachRuntimeComponent(FloorTile);
			}
		}
	};

	enum class EWallSide : uint8
	{
		Back,
		Front,
		Left,
		Right
	};

	struct FDoorOpening
	{
		EWallSide Side = EWallSide::Back;
		float Center = 0.0f;
		float HalfWidth = 0.0f;
		FString Name;
	};

	TArray<FDoorOpening> Openings;
	TArray<USceneComponent*> SceneComponents;
	Room->GetComponents(SceneComponents);
	for (USceneComponent* Component : SceneComponents)
	{
		if (!Component || !IsDoorwayMarkerName(Component->GetName()))
		{
			continue;
		}

		const FString MarkerName = Component->GetName();
		if ((bIsStartRoom && MarkerName.Equals(TEXT("Entrance_Marker"), ESearchCase::IgnoreCase)) ||
			((bIsExitRoom || bIsTreasureDeadEnd) && MarkerName.Equals(TEXT("Exit_Marker"), ESearchCase::IgnoreCase)))
		{
			continue;
		}

		const FVector Local = BoundsWorld.InverseTransformPositionNoScale(Component->GetComponentLocation());
		const float BackDistance = FMath::Abs(Local.Y + Extent.Y);
		const float FrontDistance = FMath::Abs(Local.Y - Extent.Y);
		const float LeftDistance = FMath::Abs(Local.X + Extent.X);
		const float RightDistance = FMath::Abs(Local.X - Extent.X);
		float BestDistance = BackDistance;
		EWallSide BestSide = EWallSide::Back;
		float Center = Local.X;

		if (FrontDistance < BestDistance)
		{
			BestDistance = FrontDistance;
			BestSide = EWallSide::Front;
			Center = Local.X;
		}
		if (LeftDistance < BestDistance)
		{
			BestDistance = LeftDistance;
			BestSide = EWallSide::Left;
			Center = Local.Y;
		}
		if (RightDistance < BestDistance)
		{
			BestDistance = RightDistance;
			BestSide = EWallSide::Right;
			Center = Local.Y;
		}

		if (BestDistance > 420.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Doorway marker %s on %s is %.1f cm from the perimeter; no wall opening created."),
				*MarkerName,
				*Room->GetName(),
				BestDistance);
			continue;
		}

		Openings.Add({ BestSide, Center, DoorwayHalfWidth, MarkerName });
		UE_LOG(LogTemp, Log, TEXT("DungeonGen: Doorway opening for %s [%s] uses %s local=(%.1f, %.1f, %.1f)"),
			*Room->GetName(),
			*RoomLabel,
			*MarkerName,
			Local.X, Local.Y, Local.Z);
	}

	auto AddWallSegment = [&](const TCHAR* NamePrefix, EWallSide Side, float SegmentMin, float SegmentMax)
	{
		const float Length = SegmentMax - SegmentMin;
		if (Length <= 60.0f)
		{
			return;
		}

		const float SegmentCenter = (SegmentMin + SegmentMax) * 0.5f;
		FVector LocalPos = FVector::ZeroVector;
		FQuat VisualRotation = BoundsRotation;
		FVector BlockerExtent = FVector::ZeroVector;

		switch (Side)
		{
		case EWallSide::Back:
			LocalPos = FVector(SegmentCenter, -Extent.Y, FloorZ);
			VisualRotation = BoundsRotation;
			BlockerExtent = FVector(Length * 0.5f, WallThickness * 0.5f, WallHeight * 0.5f);
			break;
		case EWallSide::Front:
			LocalPos = FVector(SegmentCenter, Extent.Y, FloorZ);
			VisualRotation = BoundsRotation;
			BlockerExtent = FVector(Length * 0.5f, WallThickness * 0.5f, WallHeight * 0.5f);
			break;
		case EWallSide::Left:
			LocalPos = FVector(-Extent.X, SegmentCenter, FloorZ);
			VisualRotation = BoundsRotation * FRotator(0.0f, 90.0f, 0.0f).Quaternion();
			BlockerExtent = FVector(WallThickness * 0.5f, Length * 0.5f, WallHeight * 0.5f);
			break;
		case EWallSide::Right:
			LocalPos = FVector(Extent.X, SegmentCenter, FloorZ);
			VisualRotation = BoundsRotation * FRotator(0.0f, 90.0f, 0.0f).Quaternion();
			BlockerExtent = FVector(WallThickness * 0.5f, Length * 0.5f, WallHeight * 0.5f);
			break;
		}

		AddVisibleWall(NamePrefix, LocalPos, VisualRotation, Length);
		AddBlocker(NamePrefix, LocalPos + FVector(0.0f, 0.0f, WallHeight * 0.5f), BoundsRotation, BlockerExtent);
	};

	auto AddWallWithOpenings = [&](const TCHAR* NamePrefix, EWallSide Side, float Min, float Max)
	{
		TArray<FDoorOpening> SideOpenings;
		for (const FDoorOpening& Opening : Openings)
		{
			if (Opening.Side == Side)
			{
				SideOpenings.Add(Opening);
			}
		}

		SideOpenings.Sort([](const FDoorOpening& A, const FDoorOpening& B)
		{
			return A.Center < B.Center;
		});

		float Cursor = Min;
		for (const FDoorOpening& Opening : SideOpenings)
		{
			const float OpeningMin = FMath::Clamp(Opening.Center - Opening.HalfWidth, Min, Max);
			const float OpeningMax = FMath::Clamp(Opening.Center + Opening.HalfWidth, Min, Max);
			AddWallSegment(NamePrefix, Side, Cursor, OpeningMin);
			Cursor = FMath::Max(Cursor, OpeningMax);
		}
		AddWallSegment(NamePrefix, Side, Cursor, Max);
	};

	AddRuntimeFloor();

	AddWallWithOpenings(TEXT("DG_BackWall"), EWallSide::Back, -Extent.X, Extent.X);
	AddWallWithOpenings(TEXT("DG_FrontWall"), EWallSide::Front, -Extent.X, Extent.X);
	AddWallWithOpenings(TEXT("DG_LeftWall"), EWallSide::Left, -Extent.Y, Extent.Y);
	AddWallWithOpenings(TEXT("DG_RightWall"), EWallSide::Right, -Extent.Y, Extent.Y);

	const float CornerInset = 55.0f;
	const float CornerHeightCenter = FloorZ + WallHeight * 0.5f;
	AddBlocker(TEXT("DG_CornerBlocker"), FVector(-Extent.X + CornerInset, -Extent.Y + CornerInset, CornerHeightCenter), BoundsRotation, FVector(120.0f, 120.0f, WallHeight * 0.5f));
	AddBlocker(TEXT("DG_CornerBlocker"), FVector(Extent.X - CornerInset, -Extent.Y + CornerInset, CornerHeightCenter), BoundsRotation, FVector(120.0f, 120.0f, WallHeight * 0.5f));
	AddBlocker(TEXT("DG_CornerBlocker"), FVector(-Extent.X + CornerInset, Extent.Y - CornerInset, CornerHeightCenter), BoundsRotation, FVector(120.0f, 120.0f, WallHeight * 0.5f));
	AddBlocker(TEXT("DG_CornerBlocker"), FVector(Extent.X - CornerInset, Extent.Y - CornerInset, CornerHeightCenter), BoundsRotation, FVector(120.0f, 120.0f, WallHeight * 0.5f));

	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Marker-aware runtime floor/walls built for %s [%s]. Openings=%d Hallway=%s"),
		*Room->GetName(),
		*RoomLabel,
		Openings.Num(),
		bIsHallway ? TEXT("true") : TEXT("false"));
}
