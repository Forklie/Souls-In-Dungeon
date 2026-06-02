#include "LevelChestSpawnDirector.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LevelManager.h"
#include "MinimapDataProvider.h"
#include "NavigationSystem.h"
#include "Soul_and_dungeon.h"
#include "UObject/ConstructorHelpers.h"

ALevelChestSpawnDirector::ALevelChestSpawnDirector()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	SpawnBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBounds"));
	SpawnBounds->SetupAttachment(SceneRoot);
	SpawnBounds->SetBoxExtent(FVector(1600.0f, 1200.0f, 400.0f));
	SpawnBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnBounds->SetGenerateOverlapEvents(false);
	FloorSurfaceNameTokens = { FName(TEXT("floor")), FName(TEXT("ground")) };

	static ConstructorHelpers::FClassFinder<AActor> DefaultChestClass(TEXT("/Game/Characters/Assests/Interactive_Chest/BP_PROP_chest_Interactive.BP_PROP_chest_Interactive_C"));
	if (DefaultChestClass.Succeeded())
	{
		ChestClass = DefaultChestClass.Class;
	}
}

void ALevelChestSpawnDirector::BeginPlay()
{
	Super::BeginPlay();

	if (!bEnableChestSpawning)
	{
		UE_LOG(LogSoul_and_dungeon, Log, TEXT("LevelChestSpawnDirector[%s]: chest spawning disabled."), *LevelId.ToString());
		return;
	}

	if (!GetWorld() || !ChestClass || !SpawnBounds)
	{
		UE_LOG(LogSoul_and_dungeon, Warning, TEXT("LevelChestSpawnDirector[%s]: missing world, chest class, or spawn bounds."), *LevelId.ToString());
		return;
	}

	ALevelManager* LevelManager = ResolveLevelManager();
	if (!LevelManager)
	{
		UE_LOG(LogSoul_and_dungeon, Warning, TEXT("LevelChestSpawnDirector[%s]: could not resolve LevelManager."), *LevelId.ToString());
		return;
	}

	if (bResetLevelObjectives)
	{
		LevelManager->ResetObjectives();
	}
	LevelManager->SetCompleteGameWhenObjectivesComplete(bCompleteGameWhenAllChestsOpen);

	MinimapData = NewObject<UMinimapDataProvider>(this);
	MinimapData->RegisterRoomDirect(SpawnBounds->GetComponentLocation(), SpawnBounds->GetScaledBoxExtent(), true, true, false);

	TArray<FVector> AcceptedFloorLocations;
	AcceptedFloorLocations.Reserve(ChestCount);
	SpawnedChests.Reset();
	SpawnedChests.Reserve(ChestCount);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 ChestIndex = 0; ChestIndex < ChestCount; ++ChestIndex)
	{
		FTransform SpawnTransform;
		FVector FloorLocation = FVector::ZeroVector;
		if (!BuildChestSpawnTransform(AcceptedFloorLocations, SpawnTransform, FloorLocation))
		{
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("LevelChestSpawnDirector[%s]: only placed %d/%d chests; no valid floor point found after %d attempts."),
				*LevelId.ToString(),
				SpawnedChests.Num(),
				ChestCount,
				MaxPlacementAttempts);
			break;
		}

		AActor* Chest = GetWorld()->SpawnActor<AActor>(ChestClass, SpawnTransform, SpawnParams);
		if (!Chest)
		{
			UE_LOG(LogSoul_and_dungeon, Warning, TEXT("LevelChestSpawnDirector[%s]: failed to spawn chest %d/%d."),
				*LevelId.ToString(),
				ChestIndex + 1,
				ChestCount);
			continue;
		}

		MakeActorMovableForRuntimePlacement(Chest);
		AlignActorBottomToFloor(Chest, FloorLocation);
		Chest->Tags.AddUnique(FName(TEXT("Interactable")));
		SpawnedChests.Add(Chest);
		AcceptedFloorLocations.Add(FloorLocation);
		LevelManager->RegisterChest(Chest);
		MinimapData->RegisterIcon(Chest, EMinimapIconType::Chest);
	}

	if (SpawnedChests.Num() != ChestCount)
	{
		LevelManager->TotalRequiredChests = ChestCount;
		UE_LOG(LogSoul_and_dungeon, Warning, TEXT("LevelChestSpawnDirector[%s]: spawned %d/%d required chests. Increase bounds or lower spacing."),
			*LevelId.ToString(),
			SpawnedChests.Num(),
			ChestCount);
	}
	else
	{
		UE_LOG(LogSoul_and_dungeon, Log, TEXT("LevelChestSpawnDirector[%s]: spawned %d randomized floor chests."),
			*LevelId.ToString(),
			SpawnedChests.Num());
	}
}

bool ALevelChestSpawnDirector::BuildChestSpawnTransform(TArray<FVector>& AcceptedFloorLocations, FTransform& OutTransform, FVector& OutFloorLocation) const
{
	const FTransform BoundsTransform = SpawnBounds->GetComponentTransform();
	const FVector Extent = SpawnBounds->GetScaledBoxExtent();
	const FRotator SpawnRotation(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);

	for (int32 Attempt = 0; Attempt < MaxPlacementAttempts; ++Attempt)
	{
		const FVector LocalPoint(
			FMath::FRandRange(-Extent.X, Extent.X),
			FMath::FRandRange(-Extent.Y, Extent.Y),
			0.0f);
		const FVector CandidateLocation = BoundsTransform.TransformPositionNoScale(LocalPoint);
		FVector FloorLocation = FVector::ZeroVector;
		if (!ProjectToFloor(CandidateLocation, FloorLocation))
		{
			continue;
		}

		if (!IsFarEnoughFromAcceptedChests(FloorLocation, AcceptedFloorLocations))
		{
			continue;
		}

		OutFloorLocation = FloorLocation;
		OutTransform = FTransform(SpawnRotation, FloorLocation + FVector(0.0f, 0.0f, SpawnFloorClearance));
		return true;
	}

	return false;
}

bool ALevelChestSpawnDirector::ProjectToFloor(const FVector& CandidateLocation, FVector& OutFloorLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FVector ProjectedLocation = CandidateLocation;
	if (UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World))
	{
		FNavLocation NavLocation;
		if (NavSystem->ProjectPointToNavigation(CandidateLocation, NavLocation, NavProjectionExtent))
		{
			ProjectedLocation = NavLocation.Location;
		}
		else if (bRequireNavigableFloor)
		{
			return false;
		}
	}
	else if (bRequireNavigableFloor)
	{
		return false;
	}

	FHitResult FloorHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LevelChestSpawnFloorTrace), false, this);
	QueryParams.AddIgnoredActor(this);

	const float TraceUp = FMath::Min(FloorTraceHeight, 250.0f);
	const float TraceDown = FMath::Min(FloorTraceHeight, 500.0f);
	const FVector TraceStart = ProjectedLocation + FVector(0.0f, 0.0f, TraceUp);
	const FVector TraceEnd = ProjectedLocation - FVector(0.0f, 0.0f, TraceDown);
	if (!World->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return false;
	}

	if (FloorHit.ImpactNormal.Z < 0.65f)
	{
		return false;
	}

	if (!IsFloorSurface(FloorHit))
	{
		return false;
	}

	if (!SpawnBounds->IsOverlappingComponent(FloorHit.GetComponent()) && !SpawnBounds->Bounds.GetBox().IsInsideOrOn(FloorHit.ImpactPoint))
	{
		return false;
	}

	if (!IsPlacementClear(FloorHit.ImpactPoint))
	{
		return false;
	}

	OutFloorLocation = FloorHit.ImpactPoint;
	return true;
}

bool ALevelChestSpawnDirector::IsFloorSurface(const FHitResult& FloorHit) const
{
	if (FloorSurfaceNameTokens.IsEmpty())
	{
		return true;
	}

	const AActor* HitActor = FloorHit.GetActor();
	const UPrimitiveComponent* HitComponent = FloorHit.GetComponent();
	const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(HitComponent);
	const UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;

	FString SurfaceText;
	if (HitActor)
	{
		SurfaceText += HitActor->GetName();
		SurfaceText += TEXT(" ");
		SurfaceText += HitActor->GetClass()->GetName();
	}
	if (HitComponent)
	{
		SurfaceText += TEXT(" ");
		SurfaceText += HitComponent->GetName();
		SurfaceText += TEXT(" ");
		SurfaceText += HitComponent->GetPathName();
	}
	if (StaticMesh)
	{
		SurfaceText += TEXT(" ");
		SurfaceText += StaticMesh->GetName();
		SurfaceText += TEXT(" ");
		SurfaceText += StaticMesh->GetPathName();
	}

	SurfaceText = SurfaceText.ToLower();
	for (const FName& TokenName : FloorSurfaceNameTokens)
	{
		const FString Token = TokenName.ToString().ToLower();
		if (!Token.IsEmpty() && SurfaceText.Contains(Token))
		{
			return true;
		}
	}

	return false;
}

bool ALevelChestSpawnDirector::IsPlacementClear(const FVector& FloorLocation) const
{
	if (!GetWorld() ||
		PlacementClearanceExtent.X <= KINDA_SMALL_NUMBER ||
		PlacementClearanceExtent.Y <= KINDA_SMALL_NUMBER ||
		PlacementClearanceExtent.Z <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LevelChestSpawnClearance), false, this);
	QueryParams.AddIgnoredActor(this);

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	const FVector TestCenter = FloorLocation + FVector(0.0f, 0.0f, SpawnFloorClearance + PlacementClearanceExtent.Z);
	const FCollisionShape TestShape = FCollisionShape::MakeBox(PlacementClearanceExtent);

	TArray<FOverlapResult> Overlaps;
	if (!GetWorld()->OverlapMultiByObjectType(Overlaps, TestCenter, FQuat::Identity, ObjectQueryParams, TestShape, QueryParams))
	{
		return true;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.bBlockingHit)
		{
			return false;
		}
	}

	return true;
}

bool ALevelChestSpawnDirector::IsFarEnoughFromAcceptedChests(const FVector& FloorLocation, const TArray<FVector>& AcceptedFloorLocations) const
{
	for (const FVector& AcceptedLocation : AcceptedFloorLocations)
	{
		if (FVector::DistSquared2D(FloorLocation, AcceptedLocation) < FMath::Square(MinChestSpacing))
		{
			return false;
		}
	}

	return true;
}

void ALevelChestSpawnDirector::MakeActorMovableForRuntimePlacement(AActor* Actor) const
{
	if (!Actor)
	{
		return;
	}

	TArray<USceneComponent*> SceneComponents;
	Actor->GetComponents(SceneComponents);
	for (USceneComponent* Component : SceneComponents)
	{
		if (Component && Component->Mobility != EComponentMobility::Movable)
		{
			Component->SetMobility(EComponentMobility::Movable);
		}
	}
}

void ALevelChestSpawnDirector::AlignActorBottomToFloor(AActor* Actor, const FVector& FloorLocation) const
{
	if (!Actor)
	{
		return;
	}

	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	Actor->GetActorBounds(false, Origin, Extent);

	if (Extent.Z <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float CurrentBottomZ = Origin.Z - Extent.Z;
	const float DesiredBottomZ = FloorLocation.Z + SpawnFloorClearance;
	const float DeltaZ = DesiredBottomZ - CurrentBottomZ;
	if (!FMath::IsNearlyZero(DeltaZ, 0.5f))
	{
		Actor->AddActorWorldOffset(FVector(0.0f, 0.0f, DeltaZ), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

ALevelManager* ALevelChestSpawnDirector::ResolveLevelManager() const
{
	if (ALevelManager* ExistingManager = ALevelManager::GetActiveLevelManager(this))
	{
		return ExistingManager;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = const_cast<ALevelChestSpawnDirector*>(this);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ALevelManager>(ALevelManager::StaticClass(), FTransform::Identity, SpawnParams);
}
