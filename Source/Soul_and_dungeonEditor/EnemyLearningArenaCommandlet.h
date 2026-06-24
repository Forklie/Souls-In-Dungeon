#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "EnemyLearningArenaCommandlet.generated.h"

UCLASS()
class UEnemyLearningArenaCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UEnemyLearningArenaCommandlet();

	virtual int32 Main(const FString& Params) override;
};
