#include "DungeonChestActor.h"
#include "LevelManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/ConstructorHelpers.h"

ADungeonChestActor::ADungeonChestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // Only tick while animating

	// Create chest base mesh
	ChestBaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBaseMesh"));
	RootComponent = ChestBaseMesh;
	
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BaseMeshFinder(TEXT("/Game/Fantastic_Dungeon_Pack/Meshes/props/container/SM_PROP_chest_dungeon_01.SM_PROP_chest_dungeon_01"));
	if (BaseMeshFinder.Succeeded())
	{
		ChestBaseMesh->SetStaticMesh(BaseMeshFinder.Object);
	}
	ChestBaseMesh->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
	ChestBaseMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Lid pivot (hinge point at the back-top of the chest)
	LidPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LidPivot"));
	LidPivot->SetupAttachment(RootComponent);
	LidPivot->SetRelativeLocation(FVector(-38.0f, 0.0f, 42.0f));

	// Lid mesh
	ChestLidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestLidMesh"));
	ChestLidMesh->SetupAttachment(LidPivot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LidMeshFinder(TEXT("/Game/Fantastic_Dungeon_Pack/Meshes/props/container/SM_PROP_chest_top_dungeon_01.SM_PROP_chest_top_dungeon_01"));
	if (LidMeshFinder.Succeeded())
	{
		ChestLidMesh->SetStaticMesh(LidMeshFinder.Object);
	}
	ChestLidMesh->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
	ChestLidMesh->SetRelativeLocation(FVector(38.0f, 0.0f, -42.0f));
	ChestLidMesh->SetCollisionProfileName(TEXT("NoCollision"));

	// Interaction trigger box
	InteractBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractBox"));
	InteractBox->SetupAttachment(RootComponent);
	InteractBox->SetBoxExtent(FVector(100.0f, 100.0f, 100.0f));
	InteractBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	InteractBox->SetGenerateOverlapEvents(true);

	Tags.AddUnique(FName(TEXT("Interactable")));
}

void ADungeonChestActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Bind overlap for player interaction
	InteractBox->OnComponentBeginOverlap.AddDynamic(this, &ADungeonChestActor::OnInteractOverlap);
	
	UE_LOG(LogTemp, Log, TEXT("DungeonGen: Chest %s initialized at (%.1f, %.1f, %.1f)"),
		*GetName(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
}

void ADungeonChestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Animate lid opening
	if (bIsAnimating && LidPivot)
	{
		OpenProgress = FMath::Clamp(OpenProgress + DeltaTime * LidOpenSpeed, 0.0f, 1.0f);
		float CurrentAngle = FMath::Lerp(0.0f, LidOpenAngle, OpenProgress);
		LidPivot->SetRelativeRotation(FRotator(CurrentAngle, 0.0f, 0.0f));
		
		if (OpenProgress >= 1.0f)
		{
			bIsAnimating = false;
			SetActorTickEnabled(false); // No more ticking needed
		}
	}
}

void ADungeonChestActor::OnInteractOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// Only respond to player characters
	ACharacter* PlayerChar = Cast<ACharacter>(OtherActor);
	if (!PlayerChar || !PlayerChar->IsPlayerControlled())
	{
		return;
	}
	
	NotifyOpened();
}

void ADungeonChestActor::Interact()
{
	NotifyOpened();
}

void ADungeonChestActor::NotifyOpened()
{
	if (bHasBeenOpened)
	{
		return; // Prevent double-counting
	}
	
	bHasBeenOpened = true;
	bIsAnimating = true;
	SetActorTickEnabled(true); // Start animating
	
	UE_LOG(LogTemp, Log, TEXT("LevelManager: Chest opened"));
	
	// Screen feedback
	UKismetSystemLibrary::PrintString(this, TEXT("Chest Opened!"), true, true, FLinearColor::Yellow, 3.0f);
	
	// Find and notify the active LevelManager
	ALevelManager* LevelManager = ALevelManager::GetActiveLevelManager(this);
	if (LevelManager)
	{
		LevelManager->NotifyChestOpened(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DungeonGen: Chest opened but no active LevelManager found!"));
	}
}
