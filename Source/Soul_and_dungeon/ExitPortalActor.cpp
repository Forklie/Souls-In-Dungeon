#include "ExitPortalActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "DungeonProgressionState.h"
#include "EngineUtils.h"

AExitPortalActor::AExitPortalActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create portal mesh (visible placeholder)
	PortalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalMesh"));
	RootComponent = PortalMesh;
	
	// Use a basic cylinder as placeholder mesh
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PortalMesh->SetStaticMesh(CylinderMesh.Object);
	}
	PortalMesh->SetRelativeScale3D(FVector(1.5f, 1.5f, 2.0f));
	PortalMesh->SetCollisionProfileName(TEXT("NoCollision"));
	
	// Create trigger box for overlap detection
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(RootComponent);
	TriggerBox->SetBoxExtent(FVector(150.0f, 150.0f, 200.0f));
	TriggerBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerBox->SetGenerateOverlapEvents(true);
}

void AExitPortalActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Bind overlap event
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AExitPortalActor::OnTriggerOverlap);
	
	// Start locked — set a red-ish tint
	if (PortalMesh)
	{
		UMaterialInterface* DefaultMat = PortalMesh->GetMaterial(0);
		if (DefaultMat)
		{
			UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(DefaultMat, this);
			if (DynMat)
			{
				DynMat->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.5f, 0.1f, 0.1f));
				PortalMesh->SetMaterial(0, DynMat);
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("ExitPortal: Spawned - LOCKED"));
}

void AExitPortalActor::UnlockPortal()
{
	if (bIsUnlocked)
	{
		UE_LOG(LogTemp, Log, TEXT("ExitPortal: Already unlocked, ignoring duplicate call"));
		return;
	}
	
	bIsUnlocked = true;
	UE_LOG(LogTemp, Log, TEXT("ExitPortal: Unlocked"));
	
	// Visual feedback — change to green tint
	if (PortalMesh)
	{
		UMaterialInterface* DefaultMat = PortalMesh->GetMaterial(0);
		if (DefaultMat)
		{
			UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(DefaultMat, this);
			if (DynMat)
			{
				DynMat->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.1f, 0.8f, 0.2f));
				PortalMesh->SetMaterial(0, DynMat);
			}
		}
	}
	
	// Screen message
	UKismetSystemLibrary::PrintString(this, TEXT("PORTAL UNLOCKED!"), true, true, FLinearColor::Green, 5.0f);
}

void AExitPortalActor::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// Only respond to player characters
	ACharacter* PlayerChar = Cast<ACharacter>(OtherActor);
	if (!PlayerChar || !PlayerChar->IsPlayerControlled())
	{
		return;
	}
	
	if (!bIsUnlocked)
	{
		UE_LOG(LogTemp, Log, TEXT("ExitPortal: Player touched portal but it is LOCKED"));
		UKismetSystemLibrary::PrintString(this, TEXT("Portal is LOCKED - Open all chests first!"), true, true, FLinearColor::Red, 3.0f);
		return;
	}
	
	// Prevent double-trigger during cleanup/regeneration
	if (bTransitioning)
	{
		return;
	}
	bTransitioning = true;
	
	UE_LOG(LogTemp, Log, TEXT("ExitPortal: Floor Complete! Triggering transition."));
	UKismetSystemLibrary::PrintString(this, TEXT("FLOOR COMPLETE!"), true, true, FLinearColor::Green, 3.0f);
	
	// Delegate to progression state
	ADungeonProgressionState* Progression = ADungeonProgressionState::Get(this);
	if (Progression)
	{
		if (Progression->AdvanceToNextFloor())
		{
			// Find the dungeon generator and trigger regeneration
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				if (It->GetClass()->GetName().Contains(TEXT("DungeonGenerator")))
				{
					UFunction* RegenFunc = It->FindFunction(FName(TEXT("RegenerateDungeon")));
					if (RegenFunc)
					{
						It->ProcessEvent(RegenFunc, nullptr);
					}
					break;
				}
			}
		}
		// else: game is complete, progression state handles the victory
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExitPortal: No DungeonProgressionState found!"));
	}
}
