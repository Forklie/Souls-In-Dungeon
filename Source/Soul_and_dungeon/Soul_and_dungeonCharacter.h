#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Soul_and_dungeonCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInteractPromptWidget;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(abstract)
class SOUL_AND_DUNGEON_API ASoul_and_dungeonCharacter : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

protected:

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

public:

	ASoul_and_dungeonCharacter();

	virtual void Tick(float DeltaTime) override;

protected:

	virtual void BeginPlay() override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

public:

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoLook(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

	// ❤️ HEALTH
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Health = 100.0f;

	// 💥 NATIVE DAMAGE OVERRIDE
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// 🎬 HIT REACTION OVERLAY (drives ABP_Kino LayeredBoneBlend)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float HitReactionOverlayWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float HitReactionOverlayWeightTarget = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bHitReactionOverlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float HitReactionBlendInSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float HitReactionBlendOutSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float HitReactionDuration = 0.5f;

	void StartBackHitReaction();

	FTimerHandle HitReactionTimerHandle;
	void OnHitReactionFinished();

	// 🔍 INTERACTION LOGIC
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractTraceDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	UInputAction* InteractAction;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	AActor* CurrentInteractable;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void TraceForInteractables();

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void DoInteract();

	/** Toggles the enemy navigation mode between Learning (2) and Smoothed AStar (1) */
	void ToggleEnemyLearningMode();

	// 🖼️ INTERACTION UI  (pure C++ — no blueprint widget needed)
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	UInteractPromptWidget* InteractPromptWidget;

private:
	bool IsInteractableActor(AActor* Actor) const;
	bool IsDoorActor(AActor* Actor) const;
	bool IsChestActor(AActor* Actor) const;
	bool IsChestOpen(AActor* Actor) const;
	FString GetInteractPromptText(AActor* Actor) const;

public:

	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};
