#pragma once

#include "CoreMinimal.h"
#include "TrainingAutoPlayerComponent.h"

struct FLMStudioAutoPlayerSettings
{
	FString BaseUrl = TEXT("http://localhost:1234/v1");
	FString Model = TEXT("google/gemma-3-270m");
	float TimeoutSeconds = 1.0f;
	float MinDurationSeconds = 0.5f;
	float MaxDurationSeconds = 2.0f;
};

class FLMStudioAutoPlayerDriver
{
public:
	explicit FLMStudioAutoPlayerDriver(const FLMStudioAutoPlayerSettings& InSettings);

	bool IsAvailable() const;
	bool RequestAction(const FAutoPlayerMovementObservation& Observation, FTrainingPlayerActionDecision& OutDecision) const;

private:
	FString BuildPrompt(const FAutoPlayerMovementObservation& Observation) const;
	FString BuildRequestBody(const FString& Prompt) const;
	bool RunCurlPost(const FString& Url, const FString& Body, FString& OutResponse, FString& OutError) const;
	bool RunCurlGet(const FString& Url, FString& OutResponse, FString& OutError) const;
	bool ParseResponse(const FString& ResponseJson, FTrainingPlayerActionDecision& OutDecision) const;
	bool ExtractFirstJsonObject(const FString& Text, FString& OutJsonObject) const;

	FLMStudioAutoPlayerSettings Settings;
};
