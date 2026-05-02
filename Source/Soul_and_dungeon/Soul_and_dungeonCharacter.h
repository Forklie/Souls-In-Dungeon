#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Soul_and_dungeonCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UAnimSequenceBase;
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

protected:

	virtual void BeginPlay() override;   // ✅ ADD THIS
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Health = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Hit Reaction")
	UAnimSequenceBase* HitReactionAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Hit Reaction", meta = (ClampMin = 0.1, ClampMax = 3.0))
	float HitReactionPlayRate = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Hit Reaction", meta = (ClampMin = -1.0, ClampMax = 0.0))
	float BackHitDotThreshold = -0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Hit Reaction", meta = (ClampMin = 0.01, ClampMax = 0.5))
	float HitReactionBlendInTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Hit Reaction", meta = (ClampMin = 0.01, ClampMax = 0.5))
	float HitReactionBlendOutTime = 0.18f;

	// 💥 DAMAGE FUNCTION
	UFUNCTION(BlueprintCallable)
	void TakeDamageSimple(float DamageAmount, AActor* DamageCauser = nullptr);

public:

	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

private:

	void PlayHitReaction();
	void EndHitReaction();
	bool IsBackHit(AActor* DamageCauser) const;
	bool SetAnimInstanceBool(FName VariableName, bool bValue) const;
	bool SetAnimInstanceFloat(FName VariableName, float Value) const;

	FTimerHandle HitReactionTimer;
	bool bHitReactionActive = false;
	float HitReactionOverlayWeightCurrent = 0.0f;
	float HitReactionOverlayWeightTarget = 0.0f;
};
