#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsInteractor.h"
#include "EnemyLearningInteractor.generated.h"

UCLASS(BlueprintType, Blueprintable)
class SOUL_AND_DUNGEON_API UEnemyLearningInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	virtual void SpecifyAgentObservation_Implementation(
		FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
		ULearningAgentsObservationSchema* InObservationSchema) override;

	virtual void GatherAgentObservation_Implementation(
		FLearningAgentsObservationObjectElement& OutObservationObjectElement,
		ULearningAgentsObservationObject* InObservationObject,
		const int32 AgentId) override;

	virtual void SpecifyAgentAction_Implementation(
		FLearningAgentsActionSchemaElement& OutActionSchemaElement,
		ULearningAgentsActionSchema* InActionSchema) override;

	virtual void PerformAgentAction_Implementation(
		const ULearningAgentsActionObject* InActionObject,
		const FLearningAgentsActionObjectElement& InActionObjectElement,
		const int32 AgentId) override;

private:
	static constexpr int32 ObservationValueCount = 21;
	static constexpr int32 ActionValueCount = 4;
	static const FName ObservationTag;
	static const FName ActionTag;
};
