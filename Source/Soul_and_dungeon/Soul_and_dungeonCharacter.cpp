// Copyright Epic Games, Inc. All Rights Reserved.

#include "Soul_and_dungeonCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Soul_and_dungeon.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "GameEndMenuWidget.h"
#include "InteractPromptWidget.h"
#include "HealthBarWidget.h"
#include "MinimapWidget.h"
#include "MinimapDataProvider.h"
#include "DungeonGenerator.h"
#include "LevelChestSpawnDirector.h"
#include "LevelManager.h"
#include "TimerManager.h"
#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "SecondarySearchSolver.h"
#include "HAL/IConsoleManager.h"
#include "Engine/OverlapResult.h"

namespace
{
	TMap<TWeakObjectPtr<AActor>, float> GEnemyHealthByActor;

	bool NameLooksLikeDoor(const FString& Name)
	{
		return Name.Contains(TEXT("BP_COMP_Door_Interactive_Large"), ESearchCase::IgnoreCase);
	}

	bool NameLooksLikeChest(const FString& Name)
	{
		return Name.Contains(TEXT("BP_PROP_chest_Interactive"), ESearchCase::IgnoreCase);
	}

	void KillEnemyActor(AActor* Enemy, const FVector& AttackDirection)
	{
		if (!Enemy || Enemy->ActorHasTag(TEXT("Dead")))
		{
			return;
		}

		Enemy->Tags.AddUnique(TEXT("Dead"));
		Enemy->SetCanBeDamaged(false);

		if (APawn* EnemyPawn = Cast<APawn>(Enemy))
		{
			if (AAIController* AIController = Cast<AAIController>(EnemyPawn->GetController()))
			{
				AIController->StopMovement();
			}
		}

		if (ACharacter* EnemyCharacter = Cast<ACharacter>(Enemy))
		{
			// Load and play the skeleton death sound
			USoundBase* SkeletonDeathSound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, TEXT("/Game/Characters/Skeleton/Sound/Skeleton_death.Skeleton_death")));
			if (SkeletonDeathSound)
			{
				UGameplayStatics::PlaySoundAtLocation(EnemyCharacter, SkeletonDeathSound, EnemyCharacter->GetActorLocation());
			}

			if (UCharacterMovementComponent* MovementComponent = EnemyCharacter->GetCharacterMovement())
			{
				MovementComponent->StopMovementImmediately();
				MovementComponent->DisableMovement();
			}

			EnemyCharacter->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (USkeletalMeshComponent* MeshComponent = EnemyCharacter->GetMesh())
			{
				UAnimSequence* DeathAnim = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, TEXT("/Game/Characters/Skeleton/Animation/Mini_skeleton_Orc_Death.Mini_skeleton_Orc_Death")));
				if (DeathAnim)
				{
					MeshComponent->SetSimulatePhysics(false);
					MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
					MeshComponent->PlayAnimation(DeathAnim, false);
				}
				else
				{
					MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
					MeshComponent->SetSimulatePhysics(true);
					MeshComponent->AddImpulse((AttackDirection.GetSafeNormal2D() + FVector(0.0f, 0.0f, 0.35f)) * 35000.0f, NAME_None, true);
				}
			}
		}
		else
		{
			Enemy->SetActorEnableCollision(false);
		}

		GEnemyHealthByActor.Remove(TWeakObjectPtr<AActor>(Enemy));
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

	// 🗺️ MINIMAP — Top-down scene capture for the minimap texture.
	// Uses a plain USceneComponent (NOT a spring arm) because USpringArmComponent
	// has its own PostPhysics tick that recomputes rotation from the parent.
	// Location and rotation are absolute so attack root-motion jitter cannot move
	// the minimap camera; Tick positions it from a stable minimap center.
	MinimapBoom = CreateDefaultSubobject<USceneComponent>(TEXT("MinimapBoom"));
	MinimapBoom->SetupAttachment(RootComponent);
	MinimapBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 3000.0f));
	MinimapBoom->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	MinimapBoom->SetAbsolute(true, true, false);

	MinimapCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MinimapCapture"));
	MinimapCapture->SetupAttachment(MinimapBoom);
	MinimapCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	MinimapCapture->OrthoWidth = MinimapWorldRadius * 2.0f;
	MinimapCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	MinimapCapture->bCaptureEveryFrame = false; // We manually capture for performance
	MinimapCapture->bCaptureOnMovement = false;
	MinimapCapture->bAlwaysPersistRenderingState = true;
	MinimapCapture->MaxViewDistanceOverride = 5000.0f;

	// Optimized show flags for clean map rendering
	MinimapCapture->ShowFlags.SetFog(false);
	MinimapCapture->ShowFlags.SetVolumetricFog(false);
	MinimapCapture->ShowFlags.SetAtmosphere(false);
	MinimapCapture->ShowFlags.SetBloom(false);
	MinimapCapture->ShowFlags.SetMotionBlur(false);
	MinimapCapture->ShowFlags.SetSkyLighting(false);
	MinimapCapture->ShowFlags.SetDynamicShadows(false);
	MinimapCapture->ShowFlags.SetParticles(false);
	MinimapCapture->ShowFlags.SetDecals(true);
	MinimapCapture->ShowFlags.SetPostProcessing(false);
	MinimapCapture->ShowFlags.SetAntiAliasing(false);
	MinimapCapture->ShowFlags.SetTemporalAA(false);
	MinimapCapture->ShowFlags.SetEyeAdaptation(false);
	MinimapCapture->ShowFlags.SetScreenSpaceReflections(false);
	MinimapCapture->ShowFlags.SetAmbientOcclusion(false);

	PrimaryActorTick.bCanEverTick = true; // Make sure tick is enabled
	Health = MaxHealth;

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> DefaultAttackAnimation(TEXT("/Game/Characters/Kino/Animations/Player_Standing_Melee_Attack_Downward.Player_Standing_Melee_Attack_Downward"));
	if (DefaultAttackAnimation.Succeeded())
	{
		AttackAnimation = DefaultAttackAnimation.Object;
	}
}

void ASoul_and_dungeonCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

		EnsurePlayerHudWidgets();

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

	const FVector ActorLocation = GetActorLocation();
	if (!bHasStableMinimapCenterLocation || !bIsAttacking)
	{
		StableMinimapCenterLocation = ActorLocation;
		bHasStableMinimapCenterLocation = true;
	}

	const FVector MinimapCenterLocation = StableMinimapCenterLocation;

	if (MinimapBoom)
	{
		MinimapBoom->SetWorldLocation(MinimapCenterLocation + FVector(0.0f, 0.0f, MinimapCaptureHeight));
		MinimapBoom->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
	}

	// Update minimap player marker + scene capture
	if (MinimapWidget)
	{
		// Pass character rotation for the arrow, and control rotation (mouse/camera) for map alignment
		MinimapWidget->UpdatePlayerState(MinimapCenterLocation, GetActorRotation().Yaw, GetControlRotation().Yaw, MinimapWorldRadius);
	}

	// Throttle minimap scene capture to ~10fps for performance
	if (MinimapCapture && MinimapCapture->TextureTarget)
	{
		MinimapCaptureTimer += DeltaTime;
		if (MinimapCaptureTimer >= MinimapCaptureInterval)
		{
			MinimapCaptureTimer = 0.0f;

			// Update ortho width in case it changed
			MinimapCapture->OrthoWidth = MinimapWorldRadius * 2.0f;

			MinimapCapture->CaptureScene();
		}
	}

	UpdateMinimapDynamicIcons(DeltaTime);
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

		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ASoul_and_dungeonCharacter::DoInteract);
		}



		// 🧠 DEBUG TOGGLE: 'M' for Algorithm Cycle
		if (AttackAction)
		{
			EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &ASoul_and_dungeonCharacter::DoAttack);
		}

		PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ASoul_and_dungeonCharacter::DoAttack);
		PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &ASoul_and_dungeonCharacter::DoAttack);
		PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &ASoul_and_dungeonCharacter::CycleSearchAlgorithm);
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

void ASoul_and_dungeonCharacter::DoAttack()
{
	if (bIsDead || bIsAttacking || !GetWorld())
	{
		return;
	}

	bIsAttacking = true;
	GetWorld()->GetTimerManager().ClearTimer(AttackTraceTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(AttackEndTimerHandle);

	float AttackLength = AttackCooldown;
	if (AttackAnimation)
	{
		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			if (UAnimMontage* Montage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
				AttackAnimation,
				TEXT("DefaultSlot"),
				0.08f,
				0.18f,
				1.0f,
				1))
			{
				AttackLength = FMath::Max(AttackCooldown, Montage->GetPlayLength() * 0.65f);
			}
		}
	}

	GetWorld()->GetTimerManager().SetTimer(
		AttackTraceTimerHandle,
		this,
		&ASoul_and_dungeonCharacter::PerformAttackTrace,
		AttackTraceDelay,
		false);

	GetWorld()->GetTimerManager().SetTimer(
		AttackEndTimerHandle,
		this,
		&ASoul_and_dungeonCharacter::FinishAttack,
		AttackLength,
		false);
}

void ASoul_and_dungeonCharacter::PerformAttackTrace()
{
	if (bIsDead || !GetWorld())
	{
		return;
	}

	const FVector PlayerLoc = GetActorLocation();
	const FVector PlayerForward = GetActorForwardVector();

	const float MaxHorizontalReach = AttackTraceDistance + AttackTraceRadius;
	const float MaxVerticalDifference = 250.0f;
	const float MaxConeHalfAngle = 100.0f; // 200 degrees total cone for very generous swing arcs

	TArray<FOverlapResult> OverlapResults;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KinoSwordAttack), false);
	QueryParams.AddIgnoredActor(this);
	QueryParams.bFindInitialOverlaps = true;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// Direct spatial query (overlap check) using a vertically-aligned capsule matching our reach and height
	FCollisionShape CollisionShape = FCollisionShape::MakeCapsule(MaxHorizontalReach, MaxVerticalDifference);

	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		PlayerLoc,
		FQuat::Identity,
		ObjectParams,
		CollisionShape,
		QueryParams);

	AActor* BestTarget = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : OverlapResults)
	{
		APawn* HitPawn = Cast<APawn>(Overlap.GetActor());
		if (!HitPawn || HitPawn == this || HitPawn->IsPlayerControlled())
		{
			continue;
		}

		if (HitPawn->ActorHasTag(TEXT("Dead")))
		{
			continue;
		}

		const FVector EnemyLoc = HitPawn->GetActorLocation();

		// 1. Vertical difference check
		const float VerticalDiff = FMath::Abs(EnemyLoc.Z - PlayerLoc.Z);
		if (VerticalDiff > MaxVerticalDifference)
		{
			continue;
		}

		// 2. Horizontal distance check (accounting for target capsule radius)
		FVector PlayerToEnemy = EnemyLoc - PlayerLoc;
		PlayerToEnemy.Z = 0.0f;
		const float HorizontalDist = PlayerToEnemy.Size();

		float EnemyRadius = 0.0f;
		if (ACharacter* HitCharacter = Cast<ACharacter>(HitPawn))
		{
			if (UCapsuleComponent* EnemyCapsule = HitCharacter->GetCapsuleComponent())
			{
				EnemyRadius = EnemyCapsule->GetScaledCapsuleRadius();
			}
		}

		if (HorizontalDist > (MaxHorizontalReach + EnemyRadius))
		{
			continue;
		}

		// 3. Horizontal cone angle check (bypassed if extremely close to prevent dead zones)
		if (HorizontalDist > 100.0f)
		{
			FVector PlayerForward2D = PlayerForward;
			PlayerForward2D.Z = 0.0f;

			if (!PlayerToEnemy.IsNearlyZero() && !PlayerForward2D.IsNearlyZero())
			{
				PlayerToEnemy.Normalize();
				PlayerForward2D.Normalize();

				const float DotProduct = FVector::DotProduct(PlayerForward2D, PlayerToEnemy);
				const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));

				if (AngleDegrees > MaxConeHalfAngle)
				{
					continue;
				}
			}
		}

		const float DistanceSq = FVector::DistSquared(EnemyLoc, PlayerLoc);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestTarget = HitPawn;
		}
	}

	if (BestTarget)
	{
		const float ClampedEnemyMaxHealth = FMath::Max(1.0f, EnemyMaxHealth);
		const TWeakObjectPtr<AActor> TargetKey(BestTarget);
		float& TargetHealth = GEnemyHealthByActor.FindOrAdd(TargetKey, ClampedEnemyMaxHealth);
		TargetHealth = FMath::Clamp(TargetHealth - AttackDamage, 0.0f, ClampedEnemyMaxHealth);

		UE_LOG(LogSoul_and_dungeon, Log, TEXT("Kino hit %s for %.1f damage (%.1f / %.1f HP)."), *GetNameSafe(BestTarget), AttackDamage, TargetHealth, ClampedEnemyMaxHealth);

		if (TargetHealth <= 0.0f)
		{
			KillEnemyActor(BestTarget, GetActorForwardVector());
		}
	}
}

void ASoul_and_dungeonCharacter::FinishAttack()
{
	bIsAttacking = false;
}



float ASoul_and_dungeonCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead) return 0.0f;

	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	
	if (ActualDamage > 0.0f)
	{
		Health = FMath::Clamp(Health - ActualDamage, 0.0f, MaxHealth);

		// 🎬 Trigger hit reaction overlay
		StartBackHitReaction();

		UpdateHealthBarWidget();

		// 💀 PLAYER DEAD
		if (Health <= 0 && !bIsDead)
		{
			bIsDead = true;
			PlayDeathSequence();
		}
	}

	return ActualDamage;
}

void ASoul_and_dungeonCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	if (MaxHealth < 1.0f)
	{
		MaxHealth = 100.0f;
	}

	if (Health <= 0.0f || Health > MaxHealth)
	{
		Health = MaxHealth;
	}

	EnsurePlayerHudWidgets();
	UpdateHealthBarWidget();
	UpdateChestCounterWidget();

	// Create minimap render target at runtime
	if (!MinimapRenderTarget)
	{
		MinimapRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MinimapRT"));
		// Set clear color to match the MinimapWidget BgColor so the void blends seamlessly
		MinimapRenderTarget->ClearColor = FLinearColor(0.005f, 0.005f, 0.015f, 1.0f);
		MinimapRenderTarget->InitAutoFormat(1024, 1024);
		MinimapRenderTarget->UpdateResourceImmediate();

		if (MinimapCapture)
		{
			MinimapCapture->TextureTarget = MinimapRenderTarget;

			// Hide the player pawn from the minimap camera
			MinimapCapture->HiddenActors.Add(this);

			UE_LOG(LogSoul_and_dungeon, Log, TEXT("Minimap: RenderTarget 1024x1024 created and assigned to SceneCapture."));
		}
	}

	// Feed the render target to the minimap widget
	if (MinimapWidget && MinimapRenderTarget)
	{
		MinimapWidget->SetRenderTarget(MinimapRenderTarget);
	}

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	GetComponents<UStaticMeshComponent>(StaticMeshComponents);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (!StaticMeshComponent)
		{
			continue;
		}

		const FString ComponentName = StaticMeshComponent->GetName();
		const FString MeshName = GetNameSafe(StaticMeshComponent->GetStaticMesh());
		const bool bLooksLikeWeapon =
			ComponentName.Contains(TEXT("Sword"), ESearchCase::IgnoreCase) ||
			ComponentName.Contains(TEXT("Weapon"), ESearchCase::IgnoreCase) ||
			MeshName.Contains(TEXT("sword"), ESearchCase::IgnoreCase) ||
			MeshName.Contains(TEXT("weapon"), ESearchCase::IgnoreCase);

		if (bLooksLikeWeapon)
		{
			StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			StaticMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
			StaticMeshComponent->SetGenerateOverlapEvents(false);
			StaticMeshComponent->SetCanEverAffectNavigation(false);
		}
	}
}

void ASoul_and_dungeonCharacter::EnsurePlayerHudWidgets()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (!InteractPromptWidget)
	{
		InteractPromptWidget = CreateWidget<UInteractPromptWidget>(PlayerController, UInteractPromptWidget::StaticClass());
		if (InteractPromptWidget)
		{
			InteractPromptWidget->AddToViewport(10);
			InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (!HealthBarWidget)
	{
		// Always create our new premium C++ HealthBarWidget
		HealthBarWidget = CreateWidget<UHealthBarWidget>(PlayerController, UHealthBarWidget::StaticClass());
		if (HealthBarWidget)
			{
				HealthBarWidget->AddToViewport(50);
				HealthBarWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			UE_LOG(LogSoul_and_dungeon, Log, TEXT("Redesigned Health Bar Initialized at bottom-left."));
		}
	}

	if (HealthBarWidget)
	{
		UpdateHealthBarWidget();
		UpdateChestCounterWidget();
	}

	// Minimap Widget (bottom-right proximity radar)
	if (!MinimapWidget)
	{
		MinimapWidget = CreateWidget<UMinimapWidget>(PlayerController, UMinimapWidget::StaticClass());
		if (MinimapWidget)
		{
			MinimapWidget->AddToViewport(40);
			MinimapWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			bool bFoundGenerator = false;

			// Strategy 1: DungeonGenerator provides data
			if (GetWorld())
			{
				TArray<AActor*> DungeonGenerators;
				UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADungeonGenerator::StaticClass(), DungeonGenerators);
				for (AActor* DG : DungeonGenerators)
				{
					ADungeonGenerator* Generator = Cast<ADungeonGenerator>(DG);
					if (Generator && Generator->MinimapData && Generator->MinimapData->HasData())
					{
						MinimapWidget->SetDataProvider(Generator->MinimapData);
						ActiveMinimapDataProvider = Generator->MinimapData;
						bFoundGenerator = true;
						break;
					}
				}
			}

			// Strategy 1.5: Static levels can provide chest/minimap data through a chest spawn director.
			if (!bFoundGenerator && GetWorld())
			{
				TArray<AActor*> ChestSpawnDirectors;
				UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALevelChestSpawnDirector::StaticClass(), ChestSpawnDirectors);
				for (AActor* DirectorActor : ChestSpawnDirectors)
				{
					ALevelChestSpawnDirector* Director = Cast<ALevelChestSpawnDirector>(DirectorActor);
					if (Director && Director->GetMinimapDataProvider() && Director->GetMinimapDataProvider()->HasData())
					{
						MinimapWidget->SetDataProvider(Director->GetMinimapDataProvider());
						ActiveMinimapDataProvider = Director->GetMinimapDataProvider();
						bFoundGenerator = true;

						// Dynamic Zoom & Height Optimization for handcrafted BossRoom!
						if (Director->GetLevelId() == TEXT("BossRoom"))
						{
							MinimapWorldRadius = 1200.0f;
							MinimapCaptureHeight = 1100.0f; // Below ceiling but above player
							UE_LOG(LogSoul_and_dungeon, Log, TEXT("EnsurePlayerHudWidgets: BossRoom detected. Zooming in minimap to radius 1200.0 and lowering capture height to 1100.0."));
						}
						break;
					}
				}
			}

			// Strategy 2: Auto-scan world for chests/enemies (radar doesn't need room geometry)
			if (!bFoundGenerator && GetWorld())
			{
				UMinimapDataProvider* AutoScanData = NewObject<UMinimapDataProvider>(this);

				TArray<AActor*> AllActors;
				UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), AllActors);

				// Register a dummy room at world origin so HasData() returns true
				AutoScanData->RegisterRoomDirect(FVector::ZeroVector, FVector(5000, 5000, 500), false, true, false);

				for (AActor* Actor : AllActors)
				{
					if (!Actor) continue;
					FString ActorName = Actor->GetClass()->GetName();

					// Chests
					if (IsChestActor(Actor))
					{
						AutoScanData->RegisterIcon(Actor, EMinimapIconType::Chest);
					}
					// Enemies
					else if (ActorName.Contains(TEXT("Skeleton"), ESearchCase::IgnoreCase) ||
						ActorName.Contains(TEXT("Enemy"), ESearchCase::IgnoreCase))
					{
						AutoScanData->RegisterIcon(Actor, EMinimapIconType::Enemy);
					}
				}

				MinimapWidget->SetDataProvider(AutoScanData);
				ActiveMinimapDataProvider = AutoScanData;
				UE_LOG(LogSoul_and_dungeon, Log, TEXT("MinimapWidget: Auto-scan radar — %d icons registered"),
					AutoScanData->GetIcons().Num());
			}

			UE_LOG(LogSoul_and_dungeon, Log, TEXT("Minimap radar initialized."));
		}
	}
}

void ASoul_and_dungeonCharacter::UpdateHealthBarWidget()
{
	if (HealthBarWidget)
	{
		HealthBarWidget->SetHealthState(Health, MaxHealth);
	}
}

ALevelManager* ASoul_and_dungeonCharacter::GetLevelManager() const
{
	if (ALevelManager* ActiveManager = ALevelManager::GetActiveLevelManager(this))
	{
		CachedLevelManager = ActiveManager;
		return ActiveManager;
	}

	if (CachedLevelManager.IsValid())
	{
		return CachedLevelManager.Get();
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	// If missing (like in test maps), spawn one to handle objective logic.
	if (!CachedLevelManager.IsValid())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CachedLevelManager = GetWorld()->SpawnActor<ALevelManager>(ALevelManager::StaticClass(), FTransform::Identity, SpawnParams);

		if (CachedLevelManager.IsValid())
		{
			UE_LOG(LogSoul_and_dungeon, Log, TEXT("GetLevelManager: No manager found. Spawned new ALevelManager to track objectives."));
		}
	}

	return CachedLevelManager.Get();
}

void ASoul_and_dungeonCharacter::UpdateChestCounterWidget()
{
	if (!HealthBarWidget || !GetWorld())
	{
		return;
	}

	// 🚀 OPTIMIZATION: Throttling updates to every ~300ms instead of every single frame (Tick).
	// For visual progress tracking like this, high-frequency polling is unnecessary.
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastChestUpdateTime < ChestUpdateInterval)
	{
		return;
	}
	LastChestUpdateTime = CurrentTime;

	ALevelManager* LevelManager = GetLevelManager();
	if (LevelManager)
	{
		// 🛠 ROBUST POLLING: Instead of relying on events, we check the actual visual state of chests in the world.
		// This handles cases where the Blueprint doesn't notify the manager correctly.
		int32 VisualOpenCount = LevelManager->GetOpenChestCount();

		// Synchronize the internal count and completion state just in case
		LevelManager->SyncObjectiveStateFromVisualCount(VisualOpenCount);

		HealthBarWidget->SetChestCounter(LevelManager->OpenedChests, LevelManager->TotalRequiredChests);

		// Sync minimap chest states
		if (MinimapWidget)
		{
			MinimapWidget->RefreshChestStates(LevelManager);
		}
	}
	else
	{
		HealthBarWidget->SetChestCounter(0, 0);
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
		// Identify if this is a chest before interacting
		bool bIsChest = IsChestActor(CurrentInteractable);

		// Try to call the Interact function (from Interact_BPI or any blueprint)
		UFunction* InteractFunc = CurrentInteractable->FindFunction(FName("Interact"));
		if (InteractFunc)
		{
			uint8* Buffer = (uint8*)FMemory_Alloca(InteractFunc->ParmsSize);
			FMemory::Memzero(Buffer, InteractFunc->ParmsSize);
			CurrentInteractable->ProcessEvent(InteractFunc, Buffer);
			
			// C++ Fallback: If the Blueprint doesn't notify the LevelManager, we do it here.
			// LevelManager::NotifyChestOpened has its own internal check to prevent double-counting.
			if (bIsChest)
			{
				if (ALevelManager* LM = GetLevelManager())
				{
					LM->NotifyChestOpened(CurrentInteractable);
					UpdateChestCounterWidget();
				}
			}

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

	// Play a random hit sound (SC_Kino_Hit SoundCue) at the character's location
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation());
	}

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



void ASoul_and_dungeonCharacter::PlayDeathSequence()
{
	// Disable player input
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}

	// Stop any movement
	GetCharacterMovement()->StopMovementImmediately();

	// Disable collision so the corpse doesn't block anything
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Play death sound
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation(), 1.0f, 1.0f, DeathSoundStartTime);
	}

	// Play the death montage (visual only — UI is on a fixed timer below)
	if (DeathMontage)
	{
		UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
		if (AnimInst)
		{
			AnimInst->Montage_Play(DeathMontage, 1.0f);
		}
	}

	// Always show the death screen after 5 seconds, regardless of montage state
	GetWorld()->GetTimerManager().SetTimer(DeathUITimerHandle, this,
		&ASoul_and_dungeonCharacter::OnDeathMontageEnded, 5.0f, false);
}

void ASoul_and_dungeonCharacter::OnDeathMontageEnded()
{
	UE_LOG(LogSoul_and_dungeon, Log, TEXT("OnDeathMontageEnded: Showing death screen"));

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC)
	{
		UGameEndMenuWidget* DeathWidget = CreateWidget<UGameEndMenuWidget>(PC, UGameEndMenuWidget::StaticClass());
		if (DeathWidget)
		{
			DeathWidget->ConfigureForDeath();
			DeathWidget->AddToViewport(100);
		}

		// Show mouse cursor and set UI input mode
		PC->bShowMouseCursor = true;
		FInputModeUIOnly UIMode;
		if (DeathWidget)
		{
			UIMode.SetWidgetToFocus(DeathWidget->TakeWidget());
		}
		PC->SetInputMode(UIMode);
	}
}

void ASoul_and_dungeonCharacter::CycleSearchAlgorithm()
{
	FSecondarySearchDebug::CycleMode();
}

void ASoul_and_dungeonCharacter::UpdateMinimapDynamicIcons(float DeltaTime)
{
	if (!ActiveMinimapDataProvider.IsValid() || !GetWorld())
	{
		return;
	}

	DynamicIconUpdateTimer += DeltaTime;
	if (DynamicIconUpdateTimer < DynamicIconUpdateInterval)
	{
		return;
	}
	DynamicIconUpdateTimer = 0.0f;

	UMinimapDataProvider* Provider = ActiveMinimapDataProvider.Get();
	TArray<FMinimapIconData>& Icons = Provider->GetIconsMutable();

	// 1. Remove dead or destroyed enemies from the icon list
	for (int32 i = Icons.Num() - 1; i >= 0; --i)
	{
		if (Icons[i].IconType == EMinimapIconType::Enemy)
		{
			AActor* Tracked = Icons[i].TrackedActor.Get();
			if (!Tracked || Tracked->IsPendingKillPending() || Tracked->ActorHasTag(TEXT("Dead")))
			{
				Icons.RemoveAt(i);
			}
		}
	}

	// 2. Scan the level for all active enemies
	TArray<AActor*> Skeletons;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Skeletons);

	for (AActor* Actor : Skeletons)
	{
		if (!Actor || Actor == this || Actor->ActorHasTag(TEXT("Dead")))
		{
			continue;
		}

		FString ActorName = Actor->GetClass()->GetName();
		if (ActorName.Contains(TEXT("Skeleton"), ESearchCase::IgnoreCase) ||
			ActorName.Contains(TEXT("Enemy"), ESearchCase::IgnoreCase))
		{
			// Check if already registered
			bool bAlreadyRegistered = false;
			for (const FMinimapIconData& Icon : Icons)
			{
				if (Icon.TrackedActor.Get() == Actor)
				{
					bAlreadyRegistered = true;
					break;
				}
			}

			if (!bAlreadyRegistered)
			{
				Provider->RegisterIcon(Actor, EMinimapIconType::Enemy);
			}
		}
	}
}
