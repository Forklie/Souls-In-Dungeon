#include "LMStudioAutoPlayerDriver.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FLMStudioAutoPlayerDriver::FLMStudioAutoPlayerDriver(const FLMStudioAutoPlayerSettings& InSettings)
	: Settings(InSettings)
{
	Settings.BaseUrl.RemoveFromEnd(TEXT("/"));
	Settings.TimeoutSeconds = FMath::Max(0.1f, Settings.TimeoutSeconds);
	Settings.MinDurationSeconds = FMath::Max(0.05f, Settings.MinDurationSeconds);
	Settings.MaxDurationSeconds = FMath::Max(Settings.MinDurationSeconds, Settings.MaxDurationSeconds);
}

bool FLMStudioAutoPlayerDriver::IsAvailable() const
{
	FString Response;
	FString Error;
	return RunCurlGet(Settings.BaseUrl / TEXT("models"), Response, Error);
}

bool FLMStudioAutoPlayerDriver::RequestAction(const FAutoPlayerMovementObservation& Observation, FTrainingPlayerActionDecision& OutDecision) const
{
	const FString Prompt = BuildPrompt(Observation);
	const FString Body = BuildRequestBody(Prompt);
	FString Response;
	FString Error;
	if (!RunCurlPost(Settings.BaseUrl / TEXT("chat/completions"), Body, Response, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("LMStudioAutoPlayer: request failed: %s"), *Error);
		return false;
	}

	if (!ParseResponse(Response, OutDecision))
	{
		UE_LOG(LogTemp, Warning, TEXT("LMStudioAutoPlayer: invalid response: %s"), *Response.Left(512));
		return false;
	}

	OutDecision.Source = TEXT("LMStudio");
	OutDecision.Reason = TEXT("LM Studio high-level action selection");
	return true;
}

FString FLMStudioAutoPlayerDriver::BuildPrompt(const FAutoPlayerMovementObservation& Observation) const
{
	TArray<FString> AllowedActionNames;
	UTrainingAutoPlayerComponent::GetAllowedActionNames(AllowedActionNames);

	const FString ObservationJson = FString::Printf(
		TEXT("{\"enemy_distance\":%.1f,\"player_speed\":%.1f,\"enemy_speed\":%.1f,\"line_of_sight\":%s,\"near_wall\":%s,\"near_door\":%s,\"near_obstacle\":%s,\"enemy_angle_degrees\":%.1f,\"z_delta\":%.1f}"),
		Observation.DistanceToEnemy,
		Observation.PlayerSpeed,
		Observation.EnemySpeed,
		Observation.bHasLineOfSight ? TEXT("true") : TEXT("false"),
		Observation.bNearWall ? TEXT("true") : TEXT("false"),
		Observation.bNearDoor ? TEXT("true") : TEXT("false"),
		Observation.bNearObstacle ? TEXT("true") : TEXT("false"),
		Observation.EnemyAngleDegrees,
		Observation.ZDelta);

	TArray<FString> QuotedActions;
	for (const FString& ActionName : AllowedActionNames)
	{
		QuotedActions.Add(FString::Printf(TEXT("\"%s\""), *ActionName));
	}

	return FString::Printf(
		TEXT("You control a training player in a dungeon chase simulation.\n")
		TEXT("Choose exactly one action from the allowed list.\n")
		TEXT("Return only valid JSON.\n")
		TEXT("No prose.\n\n")
		TEXT("Observation:\n%s\n\n")
		TEXT("Allowed actions:\n[%s]\n\n")
		TEXT("Return:\n{\"action\":\"ZigZag\",\"duration\":1.0}"),
		*ObservationJson,
		*FString::Join(QuotedActions, TEXT(",")));
}

FString FLMStudioAutoPlayerDriver::BuildRequestBody(const FString& Prompt) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings.Model);
	Root->SetNumberField(TEXT("temperature"), 0.35);
	Root->SetNumberField(TEXT("max_tokens"), 64);
	TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	Root->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(TEXT("content"), TEXT("Return only a compact JSON object. Do not include prose."));
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(TEXT("content"), Prompt);
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Body;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Root, Writer);
	return Body;
}

bool FLMStudioAutoPlayerDriver::RunCurlPost(const FString& Url, const FString& Body, FString& OutResponse, FString& OutError) const
{
	const FString TempBodyPath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("lmstudio_auto_player_"), TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(Body, *TempBodyPath))
	{
		OutError = FString::Printf(TEXT("failed to write temp body file %s"), *TempBodyPath);
		return false;
	}

	FString StdErr;
	int32 ReturnCode = -1;
	const FString Args = FString::Printf(
		TEXT("-sS -m %.2f -H \"Content-Type: application/json\" -X POST --data-binary @%s \"%s\""),
		Settings.TimeoutSeconds,
		*TempBodyPath,
		*Url);

	const bool bLaunched = FPlatformProcess::ExecProcess(TEXT("/usr/bin/curl"), *Args, &ReturnCode, &OutResponse, &StdErr);
	IFileManager::Get().Delete(*TempBodyPath, false, true);
	if (!bLaunched || ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("curl failed return_code=%d stderr=%s"), ReturnCode, *StdErr);
		return false;
	}

	return true;
}

bool FLMStudioAutoPlayerDriver::RunCurlGet(const FString& Url, FString& OutResponse, FString& OutError) const
{
	FString StdErr;
	int32 ReturnCode = -1;
	const FString Args = FString::Printf(TEXT("-sS -m %.2f \"%s\""), Settings.TimeoutSeconds, *Url);
	const bool bLaunched = FPlatformProcess::ExecProcess(TEXT("/usr/bin/curl"), *Args, &ReturnCode, &OutResponse, &StdErr);
	if (!bLaunched || ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("curl failed return_code=%d stderr=%s"), ReturnCode, *StdErr);
		return false;
	}

	return true;
}

bool FLMStudioAutoPlayerDriver::ParseResponse(const FString& ResponseJson, FTrainingPlayerActionDecision& OutDecision) const
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ChoiceObject = (*Choices)[0]->AsObject();
	if (!ChoiceObject.IsValid())
	{
		return false;
	}

	FString Content;
	const TSharedPtr<FJsonObject>* MessageObject = nullptr;
	if (ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject && MessageObject->IsValid())
	{
		(*MessageObject)->TryGetStringField(TEXT("content"), Content);
	}

	if (Content.IsEmpty())
	{
		return false;
	}

	FString ActionJson;
	if (!ExtractFirstJsonObject(Content, ActionJson))
	{
		return false;
	}

	TSharedPtr<FJsonObject> ActionRoot;
	TSharedRef<TJsonReader<>> ActionReader = TJsonReaderFactory<>::Create(ActionJson);
	if (!FJsonSerializer::Deserialize(ActionReader, ActionRoot) || !ActionRoot.IsValid())
	{
		return false;
	}

	FString ActionName;
	if (!ActionRoot->TryGetStringField(TEXT("action"), ActionName))
	{
		return false;
	}

	ETrainingPlayerAction Action = ETrainingPlayerAction::RunAwayFromEnemy;
	if (!UTrainingAutoPlayerComponent::TryTrainingPlayerActionFromString(ActionName, Action))
	{
		return false;
	}

	double Duration = 1.0;
	if (!ActionRoot->TryGetNumberField(TEXT("duration"), Duration) || !FMath::IsFinite(Duration))
	{
		Duration = 1.0;
	}

	OutDecision.Action = Action;
	OutDecision.DurationSeconds = FMath::Clamp(static_cast<float>(Duration), Settings.MinDurationSeconds, Settings.MaxDurationSeconds);
	return true;
}

bool FLMStudioAutoPlayerDriver::ExtractFirstJsonObject(const FString& Text, FString& OutJsonObject) const
{
	const int32 StartIndex = Text.Find(TEXT("{"), ESearchCase::CaseSensitive);
	if (StartIndex == INDEX_NONE)
	{
		return false;
	}

	int32 Depth = 0;
	bool bInString = false;
	bool bEscaped = false;
	for (int32 Index = StartIndex; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		if (bEscaped)
		{
			bEscaped = false;
			continue;
		}

		if (Char == TEXT('\\') && bInString)
		{
			bEscaped = true;
			continue;
		}

		if (Char == TEXT('"'))
		{
			bInString = !bInString;
			continue;
		}

		if (bInString)
		{
			continue;
		}

		if (Char == TEXT('{'))
		{
			Depth++;
		}
		else if (Char == TEXT('}'))
		{
			Depth--;
			if (Depth == 0)
			{
				OutJsonObject = Text.Mid(StartIndex, Index - StartIndex + 1);
				return true;
			}
		}
	}

	return false;
}
