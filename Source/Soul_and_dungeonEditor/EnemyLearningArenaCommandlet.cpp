#include "EnemyLearningArenaCommandlet.h"

#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/Selection.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"

namespace
{
struct FEnemyLearningArenaArgs
{
	FString MapName = TEXT("/Game/Maps/EnemyLearningArena");
	float ArenaHalfExtent = 4500.0f;
	float FloorThickness = 100.0f;
	float NavHeight = 1200.0f;
};

static FEnemyLearningArenaArgs ParseArenaArgs(const FString& Params)
{
	FEnemyLearningArenaArgs Args;
	FParse::Value(*Params, TEXT("Map="), Args.MapName);
	FParse::Value(*Params, TEXT("ArenaHalfExtent="), Args.ArenaHalfExtent);
	FParse::Value(*Params, TEXT("FloorThickness="), Args.FloorThickness);
	FParse::Value(*Params, TEXT("NavHeight="), Args.NavHeight);
	Args.ArenaHalfExtent = FMath::Max(2000.0f, Args.ArenaHalfExtent);
	Args.FloorThickness = FMath::Clamp(Args.FloorThickness, 20.0f, 300.0f);
	Args.NavHeight = FMath::Max(500.0f, Args.NavHeight);
	if (!Args.MapName.StartsWith(TEXT("/Game/")))
	{
		Args.MapName = TEXT("/Game/Maps/EnemyLearningArena");
	}
	return Args;
}

static void AddTag(AActor* Actor, const FName& Tag)
{
	if (Actor && !Actor->Tags.Contains(Tag))
	{
		Actor->Tags.Add(Tag);
	}
}

static AStaticMeshActor* SpawnCubeActor(
	UWorld* World,
	const FName Name,
	const FVector& Location,
	const FVector& Scale,
	bool bCollisionEnabled)
{
	if (!World)
	{
		return nullptr;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningArena: failed to load /Engine/BasicShapes/Cube.Cube"));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = MakeUniqueObjectName(World, AStaticMeshActor::StaticClass(), Name);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Actor)
	{
		return nullptr;
	}

	Actor->SetActorLabel(Name.ToString());
	Actor->SetMobility(EComponentMobility::Static);
	Actor->SetActorScale3D(Scale);
	AddTag(Actor, TEXT("EnemyLearningArena"));

	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	if (MeshComponent)
	{
		MeshComponent->SetStaticMesh(CubeMesh);
		MeshComponent->SetMobility(EComponentMobility::Static);
		MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
		MeshComponent->SetCollisionEnabled(bCollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		MeshComponent->SetCollisionResponseToAllChannels(bCollisionEnabled ? ECR_Block : ECR_Ignore);
		MeshComponent->SetCanEverAffectNavigation(bCollisionEnabled);
		MeshComponent->MarkRenderStateDirty();
	}

	return Actor;
}

static ATargetPoint* SpawnTargetMarker(UWorld* World, const FName Name, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = MakeUniqueObjectName(World, ATargetPoint::StaticClass(), Name);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATargetPoint* Marker = World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (Marker)
	{
		Marker->SetActorLabel(Name.ToString());
		AddTag(Marker, Name);
		AddTag(Marker, TEXT("EnemyLearningArena"));
	}
	return Marker;
}

static APlayerStart* SpawnPlayerStart(UWorld* World, const FName Name, const FVector& Location, const FRotator& Rotation)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = MakeUniqueObjectName(World, APlayerStart::StaticClass(), Name);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Location, Rotation, SpawnParams);
	if (PlayerStart)
	{
		PlayerStart->SetActorLabel(Name.ToString());
		PlayerStart->PlayerStartTag = Name;
		AddTag(PlayerStart, Name);
		AddTag(PlayerStart, TEXT("EnemyLearningArena"));
	}
	return PlayerStart;
}

static ANavMeshBoundsVolume* SpawnNavBounds(UWorld* World, const FEnemyLearningArenaArgs& Args)
{
	if (!World)
	{
		return nullptr;
	}

	const float Width = Args.ArenaHalfExtent * 2.2f;
	const FVector Location(0.0f, 0.0f, Args.NavHeight * 0.5f);

	ANavMeshBoundsVolume* Volume = nullptr;
	if (GEditor && World->GetCurrentLevel())
	{
		Volume = Cast<ANavMeshBoundsVolume>(GEditor->AddActor(
			World->GetCurrentLevel(),
			ANavMeshBoundsVolume::StaticClass(),
			FTransform(FRotator::ZeroRotator, Location)));
	}
	if (!Volume)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = MakeUniqueObjectName(World, ANavMeshBoundsVolume::StaticClass(), TEXT("EnemyLearningArena_NavMeshBounds"));
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Volume = World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	}
	if (!Volume)
	{
		return nullptr;
	}

	Volume->SetActorLabel(TEXT("EnemyLearningArena_NavMeshBounds"));
	AddTag(Volume, TEXT("EnemyLearningArena"));

	UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
	CubeBuilder->X = Width;
	CubeBuilder->Y = Width;
	CubeBuilder->Z = Args.NavHeight;
	CubeBuilder->Build(World, Volume);
	Volume->BrushBuilder = CubeBuilder;
	Volume->SetActorLocation(Location, false);
	Volume->ReregisterAllComponents();

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavSystem->OnNavigationBoundsUpdated(Volume);
	}

	return Volume;
}

static void BuildArenaActors(UWorld* World, const FEnemyLearningArenaArgs& Args)
{
	const float FloorWidth = Args.ArenaHalfExtent * 2.0f;
	const FVector FloorScale(FloorWidth / 100.0f, FloorWidth / 100.0f, Args.FloorThickness / 100.0f);
	SpawnCubeActor(
		World,
		TEXT("EnemyLearningArena_Floor"),
		FVector(0.0f, 0.0f, -Args.FloorThickness * 0.5f),
		FloorScale,
		true);

	SpawnTargetMarker(World, TEXT("EnemyTrainingSpawn"), FVector(0.0f, 0.0f, 0.0f));
	SpawnPlayerStart(World, TEXT("PlayerTrainingSpawn"), FVector(1200.0f, 0.0f, 92.0f), FRotator(0.0f, 180.0f, 0.0f));
	SpawnTargetMarker(World, TEXT("ArenaBounds"), FVector(0.0f, 0.0f, 0.0f));

	SpawnNavBounds(World, Args);

	FActorSpawnParameters LightSpawnParams;
	LightSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADirectionalLight* DirectionalLight = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FVector(-1200.0f, -1200.0f, 2200.0f),
		FRotator(-55.0f, 35.0f, 0.0f),
		LightSpawnParams);
	if (DirectionalLight)
	{
		DirectionalLight->SetActorLabel(TEXT("EnemyLearningArena_DirectionalLight"));
		AddTag(DirectionalLight, TEXT("EnemyLearningArena"));
	}

	ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(),
		FVector(0.0f, 0.0f, 900.0f),
		FRotator::ZeroRotator,
		LightSpawnParams);
	if (SkyLight)
	{
		SkyLight->SetActorLabel(TEXT("EnemyLearningArena_SkyLight"));
		AddTag(SkyLight, TEXT("EnemyLearningArena"));
	}

	APointLight* CenterLight = World->SpawnActor<APointLight>(
		APointLight::StaticClass(),
		FVector(0.0f, 0.0f, 750.0f),
		FRotator::ZeroRotator,
		LightSpawnParams);
	if (CenterLight)
	{
		CenterLight->SetActorLabel(TEXT("EnemyLearningArena_CenterLight"));
		AddTag(CenterLight, TEXT("EnemyLearningArena"));
	}
}
}

UEnemyLearningArenaCommandlet::UEnemyLearningArenaCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UEnemyLearningArenaCommandlet::Main(const FString& Params)
{
	const FEnemyLearningArenaArgs Args = ParseArenaArgs(Params);
	UE_LOG(LogTemp, Display,
		TEXT("EnemyLearningArena: creating map=%s half_extent=%.1f floor_thickness=%.1f nav_height=%.1f"),
		*Args.MapName,
		Args.ArenaHalfExtent,
		Args.FloorThickness,
		Args.NavHeight);

	const FString MapFilename = FPackageName::LongPackageNameToFilename(Args.MapName, FPackageName::GetMapPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MapFilename), true);

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningArena: failed to create blank map"));
		return 1;
	}

	BuildArenaActors(World, Args);

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavSystem->Build();
	}

	World->MarkPackageDirty();
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, Args.MapName))
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningArena: failed to save map %s"), *Args.MapName);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningArena: saved %s"), *MapFilename);
	return 0;
}
