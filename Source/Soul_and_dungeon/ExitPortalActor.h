#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExitPortalActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * Exit portal that can be locked/unlocked.
 * Starts locked. When UnlockPortal() is called, enables overlap detection.
 * When the player overlaps while unlocked, triggers "Level Complete".
 */
UCLASS(Blueprintable)
class SOUL_AND_DUNGEON_API AExitPortalActor : public AActor
{
	GENERATED_BODY()
	
public:	
	AExitPortalActor();

protected:
	virtual void BeginPlay() override;

public:
	/** Called by ALevelManager when all objectives are complete */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void UnlockPortal();

	/** Is the portal currently unlocked? */
	UFUNCTION(BlueprintPure, Category = "Portal")
	bool IsUnlocked() const { return bIsUnlocked; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	UStaticMeshComponent* PortalMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	UBoxComponent* TriggerBox;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Portal")
	bool bIsUnlocked = false;

	UPROPERTY(Transient)
	bool bTransitioning = false;

private:
	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
