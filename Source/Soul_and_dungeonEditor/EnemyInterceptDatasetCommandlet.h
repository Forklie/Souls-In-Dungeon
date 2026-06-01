#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "EnemyInterceptDatasetCommandlet.generated.h"

UCLASS()
class UEnemyInterceptDatasetCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UEnemyInterceptDatasetCommandlet();

	virtual int32 Main(const FString& Params) override;
};
