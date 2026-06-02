#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/EngineTypes.h"
#include "BossRoomSkeletonSpawnDirector.generated.h"

class ACharacter;
class ASoul_and_dungeonCharacter;
class UBoxComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;

USTRUCT()
struct FBossRoomSkeletonFadeEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ACharacter> Enemy = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FadeMaterials;

	ECollisionEnabled::Type OriginalCollision = ECollisionEnabled::QueryAndPhysics;
	TEnumAsByte<EMovementMode> OriginalMovementMode = MOVE_Walking;
	bool bOriginalCanBeDamaged = true;
	float StartTime = 0.0f;
	float Duration = 1.2f;
};

UCLASS()
class SOUL_AND_DUNGEON_API ABossRoomSkeletonSpawnDirector : public AActor
{
	GENERATED_BODY()

public:
	ABossRoomSkeletonSpawnDirector();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner")
	TSubclassOf<ACharacter> EnemyClass;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Bossroom Spawner")
	TObjectPtr<AActor> FogDoorActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Fade")
	TObjectPtr<UMaterialInterface> SpawnFadeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Fade")
	FName SpawnFadeParameterName = TEXT("SpawnFade");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner", meta = (ClampMin = "1"))
	int32 SpawnBudget = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner", meta = (ClampMin = "1"))
	int32 MaxAlive = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.0"))
	float InitialDelay = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.1"))
	float BaseDelay = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.1"))
	float MinDelay = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.1"))
	float MaxDelay = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.1"))
	float QuickClearSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthThreshold = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Timing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HighHealthThreshold = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Fade", meta = (ClampMin = "0.0"))
	float FadeDuration = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Spawn", meta = (ClampMin = "0.0"))
	float SpawnForwardOffset = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Spawn", meta = (ClampMin = "0.0"))
	float SpawnJitterRadius = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Spawn")
	FVector NavProjectionExtent = FVector(250.0f, 250.0f, 500.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Spawn", meta = (ClampMin = "0.0"))
	float SpawnFloorClearance = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bossroom Spawner|Debug")
	bool bDebugSpawner = false;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void CheckInitialTriggerOverlap();
	void ActivateSpawner(AActor* ActivationActor);
	void ScheduleNextSpawn(float Delay);
	void HandleSpawnTimer();
	void SpawnSkeleton();
	bool BuildSpawnTransform(FTransform& OutTransform) const;
	float ComputeNextSpawnDelay() const;
	void CleanAliveEnemies();
	void TickFadeEntries();
	void FinishFadeEntry(FBossRoomSkeletonFadeEntry& FadeEntry);
	ASoul_and_dungeonCharacter* GetPlayerCharacter() const;
	bool IsPlayerActor(const AActor* Actor) const;
	int32 GetAliveEnemyCount() const;
	bool IsEnemyActive(const ACharacter* Enemy) const;

	UPROPERTY()
	TArray<TObjectPtr<ACharacter>> AliveEnemies;

	UPROPERTY()
	TArray<FBossRoomSkeletonFadeEntry> FadeEntries;

	FTimerHandle SpawnTimerHandle;
	bool bActivated = false;
	int32 SpawnedCount = 0;
	int32 LastAliveCount = 0;
	float LastSpawnGroupStartTime = -1000.0f;
	float LastClearDuration = -1.0f;
};
