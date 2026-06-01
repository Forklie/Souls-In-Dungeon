#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonChestActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * Dungeon chest actor that notifies ALevelManager when opened.
 * Designed to be spawned by the DungeonGenerator in treasure rooms.
 * Uses overlap detection + lid animation to create an interactive chest.
 */
UCLASS(Blueprintable)
class SOUL_AND_DUNGEON_API ADungeonChestActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ADungeonChestActor();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	/** Called to manually trigger chest opened notification */
	UFUNCTION(BlueprintCallable, Category = "DungeonChest")
	void NotifyOpened();

	/** Player interaction entry point used by the existing E-interaction path. */
	UFUNCTION(BlueprintCallable, Category = "DungeonChest")
	void Interact();

	/** Has this chest already been counted as opened? */
	UFUNCTION(BlueprintPure, Category = "DungeonChest")
	bool HasBeenOpened() const { return bHasBeenOpened; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DungeonChest")
	UStaticMeshComponent* ChestBaseMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DungeonChest")
	USceneComponent* LidPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DungeonChest")
	UStaticMeshComponent* ChestLidMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DungeonChest")
	UBoxComponent* InteractBox;

private:
	UFUNCTION()
	void OnInteractOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	bool bHasBeenOpened = false;
	bool bIsAnimating = false;
	float OpenProgress = 0.0f;

	/** Target rotation for the lid when fully open */
	static constexpr float LidOpenAngle = -100.0f;
	static constexpr float LidOpenSpeed = 2.0f;
};
