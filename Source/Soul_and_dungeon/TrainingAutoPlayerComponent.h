#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TrainingAutoPlayerComponent.generated.h"

UENUM(BlueprintType)
enum class ETrainingPlayerAction : uint8
{
	RunAwayFromEnemy UMETA(DisplayName = "Run Away From Enemy"),
	RunStraight UMETA(DisplayName = "Run Straight"),
	StrafeLeft UMETA(DisplayName = "Strafe Left"),
	StrafeRight UMETA(DisplayName = "Strafe Right"),
	ZigZag UMETA(DisplayName = "Zig Zag"),
	CircleEnemy UMETA(DisplayName = "Circle Enemy"),
	StopAndTurn UMETA(DisplayName = "Stop And Turn"),
	DodgeLeft UMETA(DisplayName = "Dodge Left"),
	DodgeRight UMETA(DisplayName = "Dodge Right"),
	RandomExplore UMETA(DisplayName = "Random Explore")
};

USTRUCT(BlueprintType)
struct SOUL_AND_DUNGEON_API FTrainingPlayerActionDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Training Auto Player")
	ETrainingPlayerAction Action = ETrainingPlayerAction::RunAwayFromEnemy;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Training Auto Player")
	float DurationSeconds = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Training Auto Player")
	float SpeedScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Training Auto Player")
	FVector Direction = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Training Auto Player")
	FString Source = TEXT("ScriptedRandom");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Training Auto Player")
	FString Reason;
};

USTRUCT(BlueprintType)
struct SOUL_AND_DUNGEON_API FAutoPlayerMovementObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	FVector PlayerVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	FVector EnemyLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	FVector EnemyVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	float DistanceToEnemy = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	float PlayerSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	float EnemySpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	float ZDelta = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	bool bHasLineOfSight = false;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	bool bNearWall = false;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	bool bNearDoor = false;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	bool bNearObstacle = false;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	float EnemyAngleDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Training Auto Player")
	float TimeSinceLastAction = 0.0f;
};

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class SOUL_AND_DUNGEON_API UTrainingAutoPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrainingAutoPlayerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void StartAutoMovement(APawn* InTrainingPlayer, APawn* InEnemy);
	void StopAutoMovement();
	void SetActionPlan(const TArray<FTrainingPlayerActionDecision>& InActionPlan);
	void GenerateSeededActionPlan(int32 Seed, float TotalDurationSeconds, TArray<FTrainingPlayerActionDecision>& OutActionPlan) const;
	void TickAutoMovement(float DeltaSeconds);

	FAutoPlayerMovementObservation BuildAutoPlayerObservation(APawn* InTrainingPlayer, APawn* InEnemy) const;
	void ExecuteTrainingPlayerAction(const FTrainingPlayerActionDecision& Decision, float DeltaSeconds);
	FVector ComputeMovementDirectionForAction(ETrainingPlayerAction Action, float DeltaSeconds) const;

	static const TCHAR* TrainingPlayerActionToString(ETrainingPlayerAction Action);
	static bool TryTrainingPlayerActionFromString(const FString& ActionName, ETrainingPlayerAction& OutAction);
	static void GetAllowedActionNames(TArray<FString>& OutActionNames);
	static FTrainingPlayerActionDecision ChooseScriptedPlayerAction(
		FRandomStream& RandomStream,
		float MinDuration,
		float MaxDuration,
		float SpeedScaleMin,
		float SpeedScaleMax);

private:
	bool ShouldRunAutoPlayer() const;
	void AdvanceActionIfNeeded();
	void ApplyMovementDirection(const FVector& Direction, float DeltaSeconds, float SpeedScale);

	UPROPERTY()
	TObjectPtr<APawn> TrainingPlayer;

	UPROPERTY()
	TObjectPtr<APawn> EnemyPawn;

	TArray<FTrainingPlayerActionDecision> ActionPlan;
	int32 CurrentActionIndex = 0;
	float CurrentActionElapsed = 0.0f;
	float TimeSinceLastAction = 0.0f;
	float ZigZagElapsed = 0.0f;
	float FallbackMoveSpeed = 420.0f;
	bool bAutoMovementActive = false;
};
