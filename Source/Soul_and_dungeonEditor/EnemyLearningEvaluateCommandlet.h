#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "EnemyLearningEvaluateCommandlet.generated.h"

UCLASS()
class UEnemyLearningEvaluateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UEnemyLearningEvaluateCommandlet();
	virtual int32 Main(const FString& Params) override;
};
