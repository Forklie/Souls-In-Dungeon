#pragma once

#include "CoreMinimal.h"
#include "EnemyInterceptTypes.h"

struct FEnemyInterceptCsvEvaluationColumns
{
	float Score = 0.0f;
	float TimeToAttackRange = 0.0f;
	float StartDistance = 0.0f;
	float FinalDistance = 0.0f;
	float DistanceReduction = 0.0f;
	int32 InvalidTargetCount = 0;
	int32 RepathCount = 0;
	int32 FallbackCount = 0;
	int32 PathFailureCount = 0;
};

struct FEnemyInterceptCsvRow
{
	int32 ScenarioId = 0;
	int32 Seed = 0;
	FString MapName;
	FString PlayerMovementSource;
	FString PlayerActionSummary;
	FEnemyInterceptObservation Observation;
	EEnemyInterceptMode BestMode = EEnemyInterceptMode::CurrentLocation;
	float BestModeScore = 0.0f;
	FEnemyInterceptCsvEvaluationColumns CurrentLocation;
	FEnemyInterceptCsvEvaluationColumns Predict035;
	FEnemyInterceptCsvEvaluationColumns Predict075;
	FEnemyInterceptCsvEvaluationColumns Predict125;
	FEnemyInterceptCsvEvaluationColumns Predict175;
};

class FEnemyInterceptCsvWriter
{
public:
	explicit FEnemyInterceptCsvWriter(const FString& InOutputPath);

	bool WriteRow(const FEnemyInterceptCsvRow& Row, FString& OutError);

private:
	FString BuildHeader() const;
	FString BuildRow(const FEnemyInterceptCsvRow& Row) const;
	FString EscapeCsvString(const FString& Value) const;
	int32 ModeToLabel(EEnemyInterceptMode Mode) const;

	FString OutputPath;
};
