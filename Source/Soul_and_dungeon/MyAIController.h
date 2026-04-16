#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MyAIController.generated.h"

UCLASS()
class SOUL_AND_DUNGEON_API AMyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AMyAIController();

protected:
    virtual void Tick(float DeltaTime) override;

private:
    float StopDistance = 150.0f;

    // 💥 DAMAGE SYSTEM
    float DamageCooldown = 3.0f;
    float LastDamageTime = 0.0f;
    float AttackDelay = 0.5f;
    float LastAttackStartTime = 0.0f;
};