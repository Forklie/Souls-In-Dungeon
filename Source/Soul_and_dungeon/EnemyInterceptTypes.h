#pragma once

#include "CoreMinimal.h"
#include "EnemyInterceptTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyInterceptMode : uint8
{
	CurrentLocation UMETA(DisplayName = "Current Player Location"),
	Predict035 UMETA(DisplayName = "Predict 0.35s"),
	Predict075 UMETA(DisplayName = "Predict 0.75s"),
	Predict125 UMETA(DisplayName = "Predict 1.25s"),
	Predict175 UMETA(DisplayName = "Predict 1.75s")
};

USTRUCT(BlueprintType)
struct SOUL_AND_DUNGEON_API FEnemyInterceptObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	FVector EnemyLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	FVector EnemyVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	FVector PlayerVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float PlayerSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float EnemySpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float DistanceToPlayer = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float ZDelta = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	bool bHasLineOfSight = false;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float DotPlayerMoveWithEnemyDirection = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float RecentPlayerTurnAmount = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float TimeSinceLastPlayerDirectionChange = 0.0f;
};

USTRUCT(BlueprintType)
struct SOUL_AND_DUNGEON_API FEnemyInterceptDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	EEnemyInterceptMode Mode = EEnemyInterceptMode::CurrentLocation;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	FVector ChosenGoal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	float PredictionTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	bool bWasPredicted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	bool bWasValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy Intercept")
	FString Reason;
};
