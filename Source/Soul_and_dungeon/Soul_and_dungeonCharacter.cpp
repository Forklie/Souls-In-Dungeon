// Copyright Epic Games, Inc. All Rights Reserved.

#include "Soul_and_dungeonCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Soul_and_dungeon.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

ASoul_and_dungeonCharacter::ASoul_and_dungeonCharacter()
{
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> DefaultHitReactionAnimation(TEXT("/Game/Characters/Kino/Animations/Player_Get_Hit_Back.Player_Get_Hit_Back"));
	if (DefaultHitReactionAnimation.Succeeded())
	{
		HitReactionAnimation = DefaultHitReactionAnimation.Object;
	}

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

	PrimaryActorTick.bCanEverTick = true;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
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



void ASoul_and_dungeonCharacter::TakeDamageSimple(float DamageAmount, AActor* DamageCauser)
{
	Health -= DamageAmount;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.0f,
			FColor::Red,
			FString::Printf(TEXT("Player Hit! Health: %f"), Health)
		);
	}

	// 💀 PLAYER DEAD
	if (Health <= 0)
	{
		GetWorldTimerManager().ClearTimer(HitReactionTimer);
		EndHitReaction();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("Player Dead"));
		}

		// 🔄 RESTART LEVEL
		UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetName()));
	}
	else
	{
		if (IsBackHit(DamageCauser))
		{
			PlayHitReaction();
		}
	}
}

void ASoul_and_dungeonCharacter::BeginPlay()
{
	Super::BeginPlay();

	HitReactionOverlayWeightCurrent = 0.0f;
	HitReactionOverlayWeightTarget = 0.0f;
	SetAnimInstanceBool(TEXT("bHitReactionOverlay"), false);
	SetAnimInstanceFloat(TEXT("HitReactionOverlayWeight"), 0.0f);

	if (UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/ThirdPerson/UI/WBP_HealthBar.WBP_HealthBar_C")))
	{
		UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), WidgetClass);
		if (Widget)
		{
			Widget->AddToViewport();
		}
	}
}

void ASoul_and_dungeonCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HitReactionTimer);
	Super::EndPlay(EndPlayReason);
}

void ASoul_and_dungeonCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float BlendTime = HitReactionOverlayWeightTarget > HitReactionOverlayWeightCurrent
		? HitReactionBlendInTime
		: HitReactionBlendOutTime;
	const float InterpSpeed = BlendTime > KINDA_SMALL_NUMBER ? 1.0f / BlendTime : BIG_NUMBER;
	const float NewWeight = FMath::FInterpConstantTo(
		HitReactionOverlayWeightCurrent,
		HitReactionOverlayWeightTarget,
		DeltaSeconds,
		InterpSpeed);

	if (!FMath::IsNearlyEqual(NewWeight, HitReactionOverlayWeightCurrent, KINDA_SMALL_NUMBER))
	{
		HitReactionOverlayWeightCurrent = NewWeight;
		SetAnimInstanceFloat(TEXT("HitReactionOverlayWeight"), HitReactionOverlayWeightCurrent);
	}

	if (HitReactionOverlayWeightTarget <= KINDA_SMALL_NUMBER
		&& HitReactionOverlayWeightCurrent <= KINDA_SMALL_NUMBER)
	{
		SetAnimInstanceBool(TEXT("bHitReactionOverlay"), false);
	}
}

void ASoul_and_dungeonCharacter::PlayHitReaction()
{
	if (bHitReactionActive)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance == nullptr)
	{
		return;
	}

	if (!SetAnimInstanceBool(TEXT("bHitReactionOverlay"), true))
	{
		return;
	}

	HitReactionOverlayWeightTarget = 1.0f;
	SetAnimInstanceFloat(TEXT("HitReactionOverlayWeight"), HitReactionOverlayWeightCurrent);

	// Keep the old state-machine hit route inactive. The active hit path is the
	// AnimGraph blend that switches to the composed hit pose.
	SetAnimInstanceBool(TEXT("bHitReacting"), false);
	SetAnimInstanceBool(TEXT("bHitReactionFinished"), true);

	bHitReactionActive = true;

	const float HitReactionDuration = HitReactionAnimation ? HitReactionAnimation->GetPlayLength() / HitReactionPlayRate : 1.75f;
	GetWorldTimerManager().SetTimer(HitReactionTimer, this, &ASoul_and_dungeonCharacter::EndHitReaction, HitReactionDuration, false);
}

void ASoul_and_dungeonCharacter::EndHitReaction()
{
	HitReactionOverlayWeightTarget = 0.0f;
	SetAnimInstanceBool(TEXT("bHitReacting"), false);
	SetAnimInstanceBool(TEXT("bHitReactionFinished"), true);
	bHitReactionActive = false;
}

bool ASoul_and_dungeonCharacter::IsBackHit(AActor* DamageCauser) const
{
	if (DamageCauser == nullptr)
	{
		return false;
	}

	const FVector ToCauser = (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (ToCauser.IsNearlyZero())
	{
		return false;
	}

	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const float ForwardDotToCauser = FVector::DotProduct(Forward, ToCauser);
	return ForwardDotToCauser <= BackHitDotThreshold;
}

bool ASoul_and_dungeonCharacter::SetAnimInstanceBool(FName VariableName, bool bValue) const
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance == nullptr)
	{
		return false;
	}

	FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(AnimInstance->GetClass(), VariableName);
	if (BoolProperty == nullptr)
	{
		return false;
	}

	BoolProperty->SetPropertyValue_InContainer(AnimInstance, bValue);
	return true;
}

bool ASoul_and_dungeonCharacter::SetAnimInstanceFloat(FName VariableName, float Value) const
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance == nullptr)
	{
		return false;
	}

	FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(AnimInstance->GetClass(), VariableName);
	if (FloatProperty == nullptr)
	{
		return false;
	}

	FloatProperty->SetPropertyValue_InContainer(AnimInstance, Value);
	return true;
}
