#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "EnemyLearningTrainCommandlet.generated.h"

UCLASS()
class UEnemyLearningTrainCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UEnemyLearningTrainCommandlet();

	virtual int32 Main(const FString& Params) override;
};
