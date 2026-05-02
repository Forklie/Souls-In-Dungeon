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
    float AttackDelay = 0.5f;
    float AttackInterval = 1.733f;
    float LastAttackStartTime = 0.0f;
    bool bDamageAppliedThisAttack = false;
};
