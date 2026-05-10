#include "EnemyLearningInteractor.h"

#include "LearningAgentsActions.h"
#include "LearningAgentsObservations.h"
#include "MyAIController.h"

const FName UEnemyLearningInteractor::ObservationTag(TEXT("EnemyNavigationObservation"));
const FName UEnemyLearningInteractor::ActionTag(TEXT("EnemySteeringAction"));

void UEnemyLearningInteractor::SpecifyAgentObservation_Implementation(
	FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
	ULearningAgentsObservationSchema* InObservationSchema)
{
	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyContinuousObservation(
		InObservationSchema,
		ObservationValueCount,
		1.0f,
		ObservationTag);
}

void UEnemyLearningInteractor::GatherAgentObservation_Implementation(
	FLearningAgentsObservationObjectElement& OutObservationObjectElement,
	ULearningAgentsObservationObject* InObservationObject,
	const int32 AgentId)
{
	FEnemyLearningObservation Observation;
	if (const AMyAIController* Controller = Cast<AMyAIController>(GetAgent(AgentId)))
	{
		Controller->GetEnemyLearningObservation(Observation);
	}

	const FVector NormalizedVelocity = Observation.Velocity.GetClampedToMaxSize(600.0f) / 600.0f;
	TArray<float> Values;
	Values.Reserve(ObservationValueCount);
	Values.Add(Observation.DirectionToPlayer.X);
	Values.Add(Observation.DirectionToPlayer.Y);
	Values.Add(Observation.DirectionToPath.X);
	Values.Add(Observation.DirectionToPath.Y);
	Values.Add(NormalizedVelocity.X);
	Values.Add(NormalizedVelocity.Y);
	Values.Add(FMath::Clamp(Observation.DistanceToPlayer / 3000.0f, 0.0f, 1.0f));
	Values.Add(FMath::Clamp(Observation.DistanceToPathTarget / 1000.0f, 0.0f, 1.0f));
	Values.Add(FMath::Clamp(Observation.StuckSeconds / 5.0f, 0.0f, 1.0f));
	Values.Add(FMath::Clamp(Observation.PathProgress, 0.0f, 1.0f));
	Values.Add(Observation.bHasLineOfSight ? 1.0f : 0.0f);

	// --- Richer Observations ---
	Values.Add(FMath::Clamp(Observation.RelativeAngleToPlayer / PI, -1.0f, 1.0f));
	for (int32 ProbeIndex = 0; ProbeIndex < 8; ++ProbeIndex)
	{
		const float ProbeDist = Observation.ObstacleProbes.IsValidIndex(ProbeIndex)
			? Observation.ObstacleProbes[ProbeIndex]
			: 1.0f;
		Values.Add(ProbeDist);
	}
	Values.Add(Observation.bIsPathBlocked ? 1.0f : 0.0f);

	ensureMsgf(Values.Num() == ObservationValueCount, TEXT("Enemy learning observation count mismatch: expected %d, got %d"), ObservationValueCount, Values.Num());

	OutObservationObjectElement = ULearningAgentsObservations::MakeContinuousObservation(
		InObservationObject,
		Values,
		ObservationTag);

	// Final safety check for NaN/Infs to prevent engine crash
	for (float& Val : Values)
	{
		if (!FMath::IsFinite(Val))
		{
			Val = 0.0f;
		}
	}
}

void UEnemyLearningInteractor::SpecifyAgentAction_Implementation(
	FLearningAgentsActionSchemaElement& OutActionSchemaElement,
	ULearningAgentsActionSchema* InActionSchema)
{
	OutActionSchemaElement = ULearningAgentsActions::SpecifyContinuousAction(
		InActionSchema,
		ActionValueCount,
		1.0f,
		ActionTag);
}

void UEnemyLearningInteractor::PerformAgentAction_Implementation(
	const ULearningAgentsActionObject* InActionObject,
	const FLearningAgentsActionObjectElement& InActionObjectElement,
	const int32 AgentId)
{
	TArray<float> Values;
	if (!ULearningAgentsActions::GetContinuousAction(Values, InActionObject, InActionObjectElement, ActionTag) ||
		Values.Num() < ActionValueCount)
	{
		return;
	}

	if (AMyAIController* Controller = Cast<AMyAIController>(GetAgent(AgentId)))
	{
		const FVector2D MoveInput(Values[0], Values[1]);
		const float SpeedScale = FMath::Clamp(Values[2], 0.0f, 1.0f);
		const bool bShouldRepath = Values[3] > 0.5f;

		Controller->ApplyLearningSteeringInput(MoveInput, SpeedScale, bShouldRepath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Interactor: Could not find AMyAIController for AgentId %d"), AgentId);
	}
}
