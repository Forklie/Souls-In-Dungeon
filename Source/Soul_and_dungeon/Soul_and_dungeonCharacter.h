#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Soul_and_dungeonCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class USceneComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UInputAction;
class UInteractPromptWidget;
class UHealthBarWidget;
class UMinimapWidget;
class ALevelManager;
class USoundBase;
class UAnimMontage;
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

	// 🗺️ MINIMAP SCENE CAPTURE — top-down orthographic view
	// Uses a plain USceneComponent (NOT a spring arm) to avoid rotation override from spring arm tick.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (AllowPrivateAccess = "true"))
	USceneComponent* MinimapBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (AllowPrivateAccess = "true"))
	USceneCaptureComponent2D* MinimapCapture;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

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

	// 🔊 HIT SOUND - assign SC_Kino_Hit SoundCue in Blueprint defaults
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	USoundBase* HitSound = nullptr;

	FTimerHandle HitReactionTimerHandle;
	FTimerHandle DeathUITimerHandle;
	void OnHitReactionFinished();

	// 💀 DEATH SYSTEM
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	UAnimMontage* DeathMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	USoundBase* DeathSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float DeathSoundStartTime = 0.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsDead = false;

	void PlayDeathSequence();

	void OnDeathMontageEnded();

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



	/** Cycles through the available search algorithms (BFS, UCS, AStar) */
	void CycleSearchAlgorithm();

	// 🖼️ INTERACTION UI  (pure C++ — no blueprint widget needed)
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	UInteractPromptWidget* InteractPromptWidget;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	UHealthBarWidget* HealthBarWidget;

	UPROPERTY(BlueprintReadOnly, Category = "UI")
	UMinimapWidget* MinimapWidget = nullptr;

	// 🗺️ MINIMAP RENDER TARGET (created at runtime)
	UPROPERTY(Transient)
	UTextureRenderTarget2D* MinimapRenderTarget = nullptr;

	/** World-space radius visible on the minimap (half of OrthoWidth) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	float MinimapWorldRadius = 2500.0f;

	/** Height above the player for the minimap camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	float MinimapCaptureHeight = 3000.0f;

	/** How often to capture the scene for the minimap (seconds, 0.0 = every frame) */
	float MinimapCaptureInterval = 0.0f;
	float MinimapCaptureTimer = 0.0f;

private:
	bool IsInteractableActor(AActor* Actor) const;
	bool IsDoorActor(AActor* Actor) const;
	bool IsChestActor(AActor* Actor) const;
	bool IsChestOpen(AActor* Actor) const;
	FString GetInteractPromptText(AActor* Actor) const;
	void EnsurePlayerHudWidgets();
	void UpdateHealthBarWidget();
	void UpdateChestCounterWidget();
	
	float LastChestUpdateTime = 0.0f;
	const float ChestUpdateInterval = 0.3f; // Update every 300ms

	ALevelManager* GetLevelManager() const;

	bool bHealthBarLoadWarningLogged = false;
	mutable TWeakObjectPtr<ALevelManager> CachedLevelManager;

public:

	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};
