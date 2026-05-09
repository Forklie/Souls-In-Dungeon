// Copyright Epic Games, Inc. All Rights Reserved.

#include "Soul_and_dungeonCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Soul_and_dungeon.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "InteractPromptWidget.h"
#include "TimerManager.h"

namespace
{
	bool NameLooksLikeDoor(const FString& Name)
	{
		return Name.Contains(TEXT("Door"), ESearchCase::IgnoreCase);
	}

	bool NameLooksLikeChest(const FString& Name)
	{
		return Name.Contains(TEXT("Chest"), ESearchCase::IgnoreCase);
	}
}

ASoul_and_dungeonCharacter::ASoul_and_dungeonCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	PrimaryActorTick.bCanEverTick = true; // Make sure tick is enabled
}

void ASoul_and_dungeonCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	TraceForInteractables();

	// Smoothly blend hit reaction overlay weight toward target
	if (!FMath::IsNearlyEqual(HitReactionOverlayWeight, HitReactionOverlayWeightTarget, 0.001f))
	{
		const float BlendSpeed = (HitReactionOverlayWeightTarget > HitReactionOverlayWeight)
			? HitReactionBlendInSpeed
			: HitReactionBlendOutSpeed;
		HitReactionOverlayWeight = FMath::FInterpTo(HitReactionOverlayWeight, HitReactionOverlayWeightTarget, DeltaTime, BlendSpeed);
	}
	else
	{
		HitReactionOverlayWeight = HitReactionOverlayWeightTarget;
	}
}

void ASoul_and_dungeonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASoul_and_dungeonCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ASoul_and_dungeonCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASoul_and_dungeonCharacter::Look);

		// Interaction
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ASoul_and_dungeonCharacter::DoInteract);
		}
	}
	else
	{
		UE_LOG(LogSoul_and_dungeon, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ASoul_and_dungeonCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void ASoul_and_dungeonCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void ASoul_and_dungeonCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void ASoul_and_dungeonCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ASoul_and_dungeonCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void ASoul_and_dungeonCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}



float ASoul_and_dungeonCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	
	if (ActualDamage > 0.0f)
	{
		Health -= ActualDamage;

		// 🎬 Trigger hit reaction overlay
		StartBackHitReaction();

		// 💀 PLAYER DEAD
		if (Health <= 0)
		{
			// 🔄 RESTART LEVEL
			UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetName()));
		}
	}

	return ActualDamage;
}

void ASoul_and_dungeonCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	// 🖼️ Create the C++ interaction prompt widget (no blueprint needed!)
	InteractPromptWidget = CreateWidget<UInteractPromptWidget>(GetWorld()->GetFirstPlayerController(), UInteractPromptWidget::StaticClass());
	if (InteractPromptWidget)
	{
		InteractPromptWidget->AddToViewport(10); // High Z-order so it renders on top
		InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Health bar widget (existing)
	if (UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/ThirdPerson/UI/WBP_HealthBar.WBP_HealthBar_C")))
	{
		UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), WidgetClass);
		if (Widget)
		{
			Widget->AddToViewport();
		}
	}
}

void ASoul_and_dungeonCharacter::TraceForInteractables()
{
	if (!FollowCamera) return;

	FVector TraceStart = FollowCamera->GetComponentLocation();
	FVector TraceEnd = TraceStart + (FollowCamera->GetForwardVector() * InteractTraceDistance);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// Use SphereTrace for smoother, more forgiving detection
	const float SphereRadius = 25.0f;
	bool bHit = GetWorld()->SweepSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(SphereRadius),
		QueryParams
	);

	AActor* HitActor = bHit ? HitResult.GetActor() : nullptr;

	if (HitActor && !IsInteractableActor(HitActor))
	{
		HitActor = nullptr;
	}
	else if (HitActor && IsChestActor(HitActor) && IsChestOpen(HitActor))
	{
		HitActor = nullptr;
	}

	// Only update when the target changes
	if (HitActor != CurrentInteractable)
	{
		// Remove highlight from the old target
		if (CurrentInteractable)
		{
			TArray<UPrimitiveComponent*> Comps;
			CurrentInteractable->GetComponents(Comps);
			for (UPrimitiveComponent* Comp : Comps)
			{
				Comp->SetRenderCustomDepth(false);
				Comp->MarkRenderStateDirty();
			}
		}

		CurrentInteractable = HitActor;

		// Add highlight + show prompt for the new target
		if (CurrentInteractable)
		{
			TArray<UPrimitiveComponent*> Comps;
			CurrentInteractable->GetComponents(Comps);
			for (UPrimitiveComponent* Comp : Comps)
			{
				Comp->SetRenderCustomDepth(true);
				Comp->SetCustomDepthStencilValue(1);
				Comp->MarkRenderStateDirty();
			}

			if (InteractPromptWidget)
			{
				InteractPromptWidget->SetPromptText(GetInteractPromptText(CurrentInteractable));
				InteractPromptWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
		else
		{
			if (InteractPromptWidget)
			{
				InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	// Every tick: update the widget position to track the object in 3D
	if (CurrentInteractable && InteractPromptWidget && InteractPromptWidget->GetVisibility() != ESlateVisibility::Collapsed)
	{
		// Get the object's bounding box to determine a good anchor point
		FVector Origin, BoxExtent;
		CurrentInteractable->GetActorBounds(false, Origin, BoxExtent);
		
		FVector PromptWorldPos;
		if (BoxExtent.Z > 80.0f) // Tall object like a door
		{
			// Use actor location (usually at the base) and add 170cm (standard head height)
			PromptWorldPos = CurrentInteractable->GetActorLocation();
			PromptWorldPos.Z += 170.0f; 
		}
		else // Short object like a chest
		{
			PromptWorldPos = Origin + FVector(0, 0, BoxExtent.Z + 15.0f);
		}

		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			InteractPromptWidget->UpdateWorldPosition(PC, PromptWorldPos);
		}
	}
}

void ASoul_and_dungeonCharacter::DoInteract()
{
	if (CurrentInteractable)
	{
		// Try to call the Interact function (from Interact_BPI or any blueprint)
		UFunction* InteractFunc = CurrentInteractable->FindFunction(FName("Interact"));
		if (InteractFunc)
		{
			uint8* Buffer = (uint8*)FMemory_Alloca(InteractFunc->ParmsSize);
			FMemory::Memzero(Buffer, InteractFunc->ParmsSize);
			CurrentInteractable->ProcessEvent(InteractFunc, Buffer);
			
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, TEXT("Interacted with object!"));
			}
		}
	}
}

bool ASoul_and_dungeonCharacter::IsInteractableActor(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->ActorHasTag(FName("Interactable")))
	{
		return true;
	}

	if (Actor->GetClass()->FindFunctionByName(FName("Interact")))
	{
		return true;
	}

	for (const FImplementedInterface& Interface : Actor->GetClass()->Interfaces)
	{
		if (Interface.Class && Interface.Class->GetName().Contains(TEXT("Interact"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return IsDoorActor(Actor);
}

bool ASoul_and_dungeonCharacter::IsDoorActor(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	if (NameLooksLikeDoor(Actor->GetName()) ||
		NameLooksLikeDoor(Actor->GetClass()->GetName()))
	{
		return true;
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (const UActorComponent* Component : Components)
	{
		if (Component && NameLooksLikeDoor(Component->GetName()))
		{
			return true;
		}
	}

	return false;
}

bool ASoul_and_dungeonCharacter::IsChestActor(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	if (NameLooksLikeChest(Actor->GetName()) ||
		NameLooksLikeChest(Actor->GetClass()->GetName()))
	{
		return true;
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (const UActorComponent* Component : Components)
	{
		if (Component && NameLooksLikeChest(Component->GetName()))
		{
			return true;
		}
	}

	return false;
}

bool ASoul_and_dungeonCharacter::IsChestOpen(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	TArray<USceneComponent*> SceneComponents;
	Actor->GetComponents(SceneComponents);
	for (const USceneComponent* Component : SceneComponents)
	{
		if (!Component || !NameLooksLikeChest(Component->GetName()) || !Component->GetName().Contains(TEXT("top"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FRotator RelativeRotation = Component->GetRelativeRotation();
		return FMath::Abs(RelativeRotation.Roll) > 5.0f ||
			FMath::Abs(RelativeRotation.Pitch) > 5.0f ||
			FMath::Abs(RelativeRotation.Yaw) > 5.0f;
	}

	return false;
}

FString ASoul_and_dungeonCharacter::GetInteractPromptText(AActor* Actor) const
{
	return IsDoorActor(Actor)
		? FString(TEXT("Press  [ E ]  to Open Door"))
		: FString(TEXT("Press  [ E ]  to Interact"));
}

void ASoul_and_dungeonCharacter::StartBackHitReaction()
{
	// Per AGENTS.md KI: only set the target, don't force current weight to 1.0
	// This ensures the blend-in is smooth rather than snapping
	HitReactionOverlayWeightTarget = 1.0f;
	bHitReactionOverlay = true;

	// Clear any existing timer so repeated hits restart the duration
	GetWorld()->GetTimerManager().ClearTimer(HitReactionTimerHandle);

	// After HitReactionDuration, begin the blend-out
	GetWorld()->GetTimerManager().SetTimer(
		HitReactionTimerHandle,
		this,
		&ASoul_and_dungeonCharacter::OnHitReactionFinished,
		HitReactionDuration,
		false
	);
}

void ASoul_and_dungeonCharacter::OnHitReactionFinished()
{
	// Begin blend-out: Tick will smoothly interpolate weight back to 0
	HitReactionOverlayWeightTarget = 0.0f;
	bHitReactionOverlay = false;
}
