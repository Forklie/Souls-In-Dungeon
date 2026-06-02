#include "BossRoomSkeletonSpawnDirector.h"

#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "Soul_and_dungeon.h"
#include "Soul_and_dungeonCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
float GetCharacterCapsuleHalfHeight(TSubclassOf<ACharacter> CharacterClass)
{
	const ACharacter* DefaultCharacter = CharacterClass ? CharacterClass->GetDefaultObject<ACharacter>() : nullptr;
	const UCapsuleComponent* DefaultCapsule = DefaultCharacter ? DefaultCharacter->GetCapsuleComponent() : nullptr;
	return DefaultCapsule ? DefaultCapsule->GetScaledCapsuleHalfHeight() : 90.0f;
}
}

ABossRoomSkeletonSpawnDirector::ABossRoomSkeletonSpawnDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(SceneRoot);
	TriggerBox->SetBoxExtent(FVector(850.0f, 520.0f, 260.0f));
	TriggerBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerBox->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FClassFinder<ACharacter> DefaultEnemyClass(TEXT("/Game/ThirdPerson/Blueprints/BP_Skeleton.BP_Skeleton_C"));
	if (DefaultEnemyClass.Succeeded())
	{
		EnemyClass = DefaultEnemyClass.Class;
	}

}

void ABossRoomSkeletonSpawnDirector::BeginPlay()
{
	Super::BeginPlay();

	if (TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ABossRoomSkeletonSpawnDirector::OnTriggerBeginOverlap);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &ABossRoomSkeletonSpawnDirector::CheckInitialTriggerOverlap);
	}
}

void ABossRoomSkeletonSpawnDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ABossRoomSkeletonSpawnDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	CleanAliveEnemies();
	TickFadeEntries();
}

void ABossRoomSkeletonSpawnDirector::OnTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!bActivated && IsPlayerActor(OtherActor))
	{
		ActivateSpawner(OtherActor);
	}
}

void ABossRoomSkeletonSpawnDirector::CheckInitialTriggerOverlap()
{
	if (bActivated || !TriggerBox)
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	TriggerBox->GetOverlappingActors(OverlappingActors, APawn::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		if (IsPlayerActor(Actor))
		{
			ActivateSpawner(Actor);
			return;
		}
	}
}

void ABossRoomSkeletonSpawnDirector::ActivateSpawner(AActor* ActivationActor)
{
	if (bActivated)
	{
		return;
	}

	bActivated = true;
	LastAliveCount = GetAliveEnemyCount();
	ScheduleNextSpawn(InitialDelay);

	UE_LOG(LogSoul_and_dungeon, Log, TEXT("BossRoomSkeletonSpawnDirector: activated by %s."), *GetNameSafe(ActivationActor));
}

void ABossRoomSkeletonSpawnDirector::ScheduleNextSpawn(float Delay)
{
	if (!GetWorld() || SpawnedCount >= SpawnBudget)
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&ABossRoomSkeletonSpawnDirector::HandleSpawnTimer,
		FMath::Max(0.1f, Delay),
		false);
}

void ABossRoomSkeletonSpawnDirector::HandleSpawnTimer()
{
	CleanAliveEnemies();

	ASoul_and_dungeonCharacter* Player = GetPlayerCharacter();
	if (!bActivated || !Player || Player->bIsDead || SpawnedCount >= SpawnBudget)
	{
		return;
	}

	if (GetAliveEnemyCount() >= MaxAlive)
	{
		ScheduleNextSpawn(ComputeNextSpawnDelay());
		return;
	}

	SpawnSkeleton();

	if (SpawnedCount < SpawnBudget)
	{
		ScheduleNextSpawn(ComputeNextSpawnDelay());
	}
}

void ABossRoomSkeletonSpawnDirector::SpawnSkeleton()
{
	if (!EnemyClass || !GetWorld())
	{
		UE_LOG(LogSoul_and_dungeon, Warning, TEXT("BossRoomSkeletonSpawnDirector: EnemyClass is not assigned."));
		return;
	}

	FTransform SpawnTransform;
	if (!BuildSpawnTransform(SpawnTransform))
	{
		UE_LOG(LogSoul_and_dungeon, Warning, TEXT("BossRoomSkeletonSpawnDirector: could not resolve a nav-safe spawn transform."));
		ScheduleNextSpawn(MaxDelay);
		return;
	}

	const int32 AliveBeforeSpawn = GetAliveEnemyCount();
	if (AliveBeforeSpawn == 0)
	{
		LastSpawnGroupStartTime = GetWorld()->GetTimeSeconds();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ACharacter* SpawnedEnemy = GetWorld()->SpawnActor<ACharacter>(EnemyClass, SpawnTransform, SpawnParams);
	if (!SpawnedEnemy)
	{
		UE_LOG(LogSoul_and_dungeon, Warning, TEXT("BossRoomSkeletonSpawnDirector: failed to spawn skeleton."));
		return;
	}

	++SpawnedCount;
	AliveEnemies.Add(SpawnedEnemy);

	FBossRoomSkeletonFadeEntry FadeEntry;
	FadeEntry.Enemy = SpawnedEnemy;
	FadeEntry.StartTime = GetWorld()->GetTimeSeconds();
	FadeEntry.Duration = FMath::Max(0.01f, FadeDuration);
	FadeEntry.bOriginalCanBeDamaged = SpawnedEnemy->CanBeDamaged();

	if (UCapsuleComponent* Capsule = SpawnedEnemy->GetCapsuleComponent())
	{
		FadeEntry.OriginalCollision = Capsule->GetCollisionEnabled();
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (UCharacterMovementComponent* Movement = SpawnedEnemy->GetCharacterMovement())
	{
		FadeEntry.OriginalMovementMode = Movement->MovementMode;
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	SpawnedEnemy->SetCanBeDamaged(false);

	if (AAIController* ExistingController = Cast<AAIController>(SpawnedEnemy->GetController()))
	{
		ExistingController->StopMovement();
		ExistingController->UnPossess();
		ExistingController->Destroy();
	}

	if (USkeletalMeshComponent* Mesh = SpawnedEnemy->GetMesh())
	{
		const int32 MaterialCount = Mesh->GetNumMaterials();
		FadeEntry.OriginalMaterials.Reserve(MaterialCount);
		FadeEntry.FadeMaterials.Reserve(MaterialCount);

		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			UMaterialInterface* OriginalMaterial = Mesh->GetMaterial(MaterialIndex);
			FadeEntry.OriginalMaterials.Add(OriginalMaterial);

			UMaterialInterface* MaterialSource = SpawnFadeMaterial ? SpawnFadeMaterial.Get() : OriginalMaterial;
			UMaterialInstanceDynamic* DynamicMaterial = MaterialSource
				? UMaterialInstanceDynamic::Create(MaterialSource, Mesh)
				: nullptr;

			if (DynamicMaterial)
			{
				DynamicMaterial->SetScalarParameterValue(SpawnFadeParameterName, 0.0f);
				Mesh->SetMaterial(MaterialIndex, DynamicMaterial);
			}

			FadeEntry.FadeMaterials.Add(DynamicMaterial);
		}
	}

	FadeEntries.Add(MoveTemp(FadeEntry));

	if (bDebugSpawner)
	{
		UE_LOG(LogSoul_and_dungeon, Log, TEXT("BossRoomSkeletonSpawnDirector: spawned %s (%d/%d)."), *GetNameSafe(SpawnedEnemy), SpawnedCount, SpawnBudget);
	}
}

bool ABossRoomSkeletonSpawnDirector::BuildSpawnTransform(FTransform& OutTransform) const
{
	const AActor* SpawnAnchor = FogDoorActor ? FogDoorActor.Get() : this;
	UWorld* World = GetWorld();
	if (!SpawnAnchor || !World)
	{
		return false;
	}

	const FVector Forward = SpawnAnchor->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = SpawnAnchor->GetActorRightVector().GetSafeNormal2D();
	const float ForwardJitter = FMath::FRandRange(0.0f, SpawnJitterRadius);
	const float SideJitter = FMath::FRandRange(-SpawnJitterRadius, SpawnJitterRadius);
	FVector CandidateLocation = SpawnAnchor->GetActorLocation() + (Forward * (SpawnForwardOffset + ForwardJitter)) + (Right * SideJitter);
	FVector FloorLocation = CandidateLocation;
	bool bFoundFloor = false;

	if (UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World))
	{
		FNavLocation NavLocation;
		if (NavSystem->ProjectPointToNavigation(CandidateLocation, NavLocation, NavProjectionExtent))
		{
			FloorLocation = NavLocation.Location;
			bFoundFloor = true;
		}
	}

	if (!bFoundFloor)
	{
		FHitResult FloorHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BossRoomSkeletonSpawnFloorTrace), false, this);
		QueryParams.AddIgnoredActor(this);
		QueryParams.AddIgnoredActor(SpawnAnchor);

		const FVector TraceStart = CandidateLocation + FVector(0.0f, 0.0f, NavProjectionExtent.Z);
		const FVector TraceEnd = CandidateLocation - FVector(0.0f, 0.0f, NavProjectionExtent.Z + 1000.0f);
		if (World->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
		{
			FloorLocation = FloorHit.ImpactPoint;
			bFoundFloor = true;
		}
	}

	if (!bFoundFloor)
	{
		return false;
	}

	const float CapsuleHalfHeight = GetCharacterCapsuleHalfHeight(EnemyClass);
	CandidateLocation = FloorLocation + FVector(0.0f, 0.0f, CapsuleHalfHeight + SpawnFloorClearance);

	ASoul_and_dungeonCharacter* Player = GetPlayerCharacter();
	const FVector FacingTarget = Player ? Player->GetActorLocation() : (CandidateLocation + Forward);
	const FVector FacingDirection = (FacingTarget - CandidateLocation).GetSafeNormal2D();
	const FRotator SpawnRotation = FacingDirection.IsNearlyZero()
		? SpawnAnchor->GetActorRotation()
		: FacingDirection.Rotation();

	OutTransform = FTransform(SpawnRotation, CandidateLocation);
	return true;
}

float ABossRoomSkeletonSpawnDirector::ComputeNextSpawnDelay() const
{
	float Delay = BaseDelay;

	if (const ASoul_and_dungeonCharacter* Player = GetPlayerCharacter())
	{
		const float HealthPercent = Player->MaxHealth > 0.0f ? Player->Health / Player->MaxHealth : 1.0f;
		if (HealthPercent >= HighHealthThreshold)
		{
			Delay -= 1.0f;
		}
		else if (HealthPercent <= LowHealthThreshold)
		{
			Delay += 2.0f;
		}
	}

	const int32 AliveCount = GetAliveEnemyCount();
	if (AliveCount == 0)
	{
		Delay -= 1.0f;
	}
	else if (AliveCount >= MaxAlive)
	{
		Delay += 2.5f;
	}

	if (LastClearDuration >= 0.0f && LastClearDuration <= QuickClearSeconds)
	{
		Delay -= 1.25f;
	}

	return FMath::Clamp(Delay, MinDelay, MaxDelay);
}

void ABossRoomSkeletonSpawnDirector::CleanAliveEnemies()
{
	AliveEnemies.RemoveAll([this](const TObjectPtr<ACharacter>& Enemy)
	{
		return !IsEnemyActive(Enemy.Get());
	});

	const int32 CurrentAliveCount = AliveEnemies.Num();
	if (LastAliveCount > 0 && CurrentAliveCount == 0 && GetWorld())
	{
		LastClearDuration = LastSpawnGroupStartTime >= 0.0f
			? GetWorld()->GetTimeSeconds() - LastSpawnGroupStartTime
			: -1.0f;
	}
	LastAliveCount = CurrentAliveCount;
}

void ABossRoomSkeletonSpawnDirector::TickFadeEntries()
{
	if (!GetWorld())
	{
		return;
	}

	for (int32 Index = FadeEntries.Num() - 1; Index >= 0; --Index)
	{
		FBossRoomSkeletonFadeEntry& FadeEntry = FadeEntries[Index];
		ACharacter* Enemy = FadeEntry.Enemy.Get();
		if (!Enemy || Enemy->ActorHasTag(TEXT("Dead")))
		{
			FadeEntries.RemoveAtSwap(Index);
			continue;
		}

		const float Alpha = FMath::Clamp((GetWorld()->GetTimeSeconds() - FadeEntry.StartTime) / FadeEntry.Duration, 0.0f, 1.0f);
		for (UMaterialInstanceDynamic* FadeMaterial : FadeEntry.FadeMaterials)
		{
			if (FadeMaterial)
			{
				FadeMaterial->SetScalarParameterValue(SpawnFadeParameterName, Alpha);
			}
		}

		if (Alpha >= 1.0f)
		{
			FinishFadeEntry(FadeEntry);
			FadeEntries.RemoveAtSwap(Index);
		}
	}
}

void ABossRoomSkeletonSpawnDirector::FinishFadeEntry(FBossRoomSkeletonFadeEntry& FadeEntry)
{
	ACharacter* Enemy = FadeEntry.Enemy.Get();
	if (!Enemy || Enemy->ActorHasTag(TEXT("Dead")))
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
	{
		for (int32 MaterialIndex = 0; MaterialIndex < FadeEntry.OriginalMaterials.Num(); ++MaterialIndex)
		{
			Mesh->SetMaterial(MaterialIndex, FadeEntry.OriginalMaterials[MaterialIndex]);
		}
	}

	if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(FadeEntry.OriginalCollision);
	}

	if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
	{
		Movement->SetMovementMode(FadeEntry.OriginalMovementMode);
	}

	Enemy->SetCanBeDamaged(FadeEntry.bOriginalCanBeDamaged);
	Enemy->SpawnDefaultController();

	if (bDebugSpawner)
	{
		UE_LOG(LogSoul_and_dungeon, Log, TEXT("BossRoomSkeletonSpawnDirector: released %s after fade."), *GetNameSafe(Enemy));
	}
}

ASoul_and_dungeonCharacter* ABossRoomSkeletonSpawnDirector::GetPlayerCharacter() const
{
	return Cast<ASoul_and_dungeonCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
}

bool ABossRoomSkeletonSpawnDirector::IsPlayerActor(const AActor* Actor) const
{
	const APawn* Pawn = Cast<APawn>(Actor);
	return Pawn && Pawn->IsPlayerControlled();
}

int32 ABossRoomSkeletonSpawnDirector::GetAliveEnemyCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<ACharacter>& Enemy : AliveEnemies)
	{
		if (IsEnemyActive(Enemy.Get()))
		{
			++Count;
		}
	}
	return Count;
}

bool ABossRoomSkeletonSpawnDirector::IsEnemyActive(const ACharacter* Enemy) const
{
	return IsValid(Enemy) && !Enemy->ActorHasTag(TEXT("Dead"));
}
