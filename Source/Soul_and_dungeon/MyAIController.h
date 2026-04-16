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

    float AttackCooldown = 1.5f;
    float LastAttackTime = 0.0f;
    float AttackDuration = 1.0f;
};