#include "LMStudioPlayerDriver.h"

#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MyAIController.h"
#include "NavigationSystem.h"

namespace
{
static FString ExtractJsonObjectText(const FString& Text)
{
	FString Trimmed = Text;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.StartsWith(TEXT("{")) && Trimmed.EndsWith(TEXT("}")))
	{
		return Trimmed;
	}

	int32 StartIndex = INDEX_NONE;
	int32 EndIndex = INDEX_NONE;
	if (Trimmed.FindChar(TEXT('{'), StartIndex) && Trimmed.FindLastChar(TEXT('}'), EndIndex) && EndIndex > StartIndex)
	{
		return Trimmed.Mid(StartIndex, EndIndex - StartIndex + 1);
	}

	return Trimmed;
}
}

void FLMStudioPlayerDriver::Configure(const FLMStudioPlayerDriverSettings& InSettings)
{
	Settings = InSettings;
	Settings.DecisionInterval = FMath::Max(0.1f, Settings.DecisionInterval);
	Settings.TimeoutSeconds = FMath::Max(0.1f, Settings.TimeoutSeconds);
	Settings.CandidateCount = FMath::Clamp(Settings.CandidateCount, 4, 16);
	RandomStream.Initialize(Settings.Seed);
}

void FLMStudioPlayerDriver::Reset(APawn* InPlayerPawn, AMyAIController* InEnemyController, ELMStudioPlayerBehavior InBehavior, const FVector& StartLocation)
{
	PlayerPawn = InPlayerPawn;
	EnemyController = InEnemyController;
	Behavior = InBehavior;
	CandidateGoals.Reset();
	CurrentGoal = StartLocation;
	EpisodeStartLocation = StartLocation;
	LastDecisionTime = -1000000.0f;
	PendingRequestStartTime = 0.0f;
	bRequestPending = false;
}

void FLMStudioPlayerDriver::Tick(UWorld* World, float DeltaSeconds, float CurrentTime)
{
	APawn* Player = PlayerPawn.Get();
	AMyAIController* Enemy = EnemyController.Get();
	APawn* EnemyPawn = Enemy ? Enemy->GetPawn() : nullptr;
	if (!World || !Player || !EnemyPawn || Behavior == ELMStudioPlayerBehavior::Static)
	{
		return;
	}

	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector EnemyLocation = EnemyPawn->GetActorLocation();
	const float DistanceToEnemy = FVector::Dist2D(PlayerLocation, EnemyLocation);

	FHttpModule::Get().GetHttpManager().Tick(FMath::Max(DeltaSeconds, 0.0f));

	if (bRequestPending && CurrentTime - PendingRequestStartTime > Settings.TimeoutSeconds)
	{
		bRequestPending = false;
		Metrics.DecisionTimeouts++;
		Metrics.FallbackDecisions++;
		ChooseDeterministicGoal(PlayerLocation, EnemyLocation, CurrentTime);
	}

	if ((CurrentTime - LastDecisionTime) >= Settings.DecisionInterval && !bRequestPending)
	{
		LastDecisionTime = CurrentTime;
		BuildCandidateGoals(World, PlayerLocation, EnemyLocation);

		if (Behavior == ELMStudioPlayerBehavior::LMStudioEvasive && Settings.bUseLMStudio)
		{
			RequestLMStudioDecision(PlayerLocation, EnemyLocation, DistanceToEnemy);
		}
		else
		{
			ChooseDeterministicGoal(PlayerLocation, EnemyLocation, CurrentTime);
		}
	}

	MoveTowardCurrentGoal(World, DeltaSeconds);
}

const FLMStudioPlayerDriverMetrics& FLMStudioPlayerDriver::GetMetrics() const
{
	return Metrics;
}

void FLMStudioPlayerDriver::BuildCandidateGoals(UWorld* World, const FVector& PlayerLocation, const FVector& EnemyLocation)
{
	CandidateGoals.Reset();
	const FVector Away = (PlayerLocation - EnemyLocation).GetSafeNormal2D();
	const FVector Right(-Away.Y, Away.X, 0.0f);
	const float Radius = 650.0f;

	for (int32 Index = 0; Index < Settings.CandidateCount; ++Index)
	{
		const float Angle = (2.0f * PI * static_cast<float>(Index)) / static_cast<float>(Settings.CandidateCount);
		const FVector Direction = (Away * FMath::Cos(Angle) + Right * FMath::Sin(Angle)).GetSafeNormal2D();
		FVector Projected;
		if (ProjectGoalToNavigation(World, PlayerLocation + Direction * Radius, Projected))
		{
			CandidateGoals.Add(Projected);
		}
	}

	if (CandidateGoals.Num() == 0)
	{
		CandidateGoals.Add(PlayerLocation);
	}
}

void FLMStudioPlayerDriver::RequestLMStudioDecision(const FVector& PlayerLocation, const FVector& EnemyLocation, float DistanceToEnemy)
{
	if (Settings.Endpoint.IsEmpty() || CandidateGoals.Num() == 0)
	{
		Metrics.FallbackDecisions++;
		ChooseDeterministicGoal(PlayerLocation, EnemyLocation, 0.0f);
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings.Model.IsEmpty() ? TEXT("local-model") : Settings.Model);
	Root->SetNumberField(TEXT("temperature"), 0.1);

	TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("text"));
	Root->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedRef<FJsonObject> System = MakeShared<FJsonObject>();
	System->SetStringField(TEXT("role"), TEXT("system"));
	System->SetStringField(TEXT("content"), TEXT("Choose one candidate goal for an evasive player in a dungeon chase. Return only JSON with goal_index, speed, look_yaw_delta, intent."));
	Messages.Add(MakeShared<FJsonValueObject>(System));

	FString CandidateText;
	for (int32 Index = 0; Index < CandidateGoals.Num(); ++Index)
	{
		CandidateText += FString::Printf(TEXT("%d:(%.0f,%.0f) "), Index, CandidateGoals[Index].X, CandidateGoals[Index].Y);
	}

	TSharedRef<FJsonObject> User = MakeShared<FJsonObject>();
	User->SetStringField(TEXT("role"), TEXT("user"));
	User->SetStringField(TEXT("content"), FString::Printf(
		TEXT("Player=(%.0f,%.0f), Enemy=(%.0f,%.0f), Distance=%.0f. Candidate goals: %s. Prefer goals farther from enemy but still reachable."),
		PlayerLocation.X,
		PlayerLocation.Y,
		EnemyLocation.X,
		EnemyLocation.Y,
		DistanceToEnemy,
		*CandidateText));
	Messages.Add(MakeShared<FJsonValueObject>(User));
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root, Writer);

	if (IsRunningCommandlet())
	{
		if (!RequestLMStudioDecisionSynchronous(Body))
		{
			Metrics.FallbackDecisions++;
			ChooseDeterministicGoal(PlayerLocation, EnemyLocation, 0.0f);
		}
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings.Endpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindRaw(this, &FLMStudioPlayerDriver::HandleLMStudioResponse);

	bRequestPending = Request->ProcessRequest();
	PendingRequestStartTime = PlayerPawn.IsValid() && PlayerPawn->GetWorld() ? PlayerPawn->GetWorld()->GetTimeSeconds() : 0.0f;
	Metrics.DecisionRequests++;

	if (!bRequestPending)
	{
		Metrics.FallbackDecisions++;
		ChooseDeterministicGoal(PlayerLocation, EnemyLocation, 0.0f);
	}
}

bool FLMStudioPlayerDriver::RequestLMStudioDecisionSynchronous(const FString& Body)
{
	const FString TempBodyPath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("LMStudioRequest_"), TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(Body, *TempBodyPath))
	{
		return false;
	}

	const FString CurlParams = FString::Printf(
		TEXT("-sS -m %.2f -X POST %s -H \"Content-Type: application/json\" --data-binary @%s"),
		Settings.TimeoutSeconds,
		*Settings.Endpoint,
		*TempBodyPath);

	int32 ReturnCode = 1;
	FString StdOut;
	FString StdErr;
	const bool bStarted = FPlatformProcess::ExecProcess(TEXT("/usr/bin/curl"), *CurlParams, &ReturnCode, &StdOut, &StdErr);
	IFileManager::Get().Delete(*TempBodyPath, false, true);
	Metrics.DecisionRequests++;

	if (!bStarted || ReturnCode != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LMStudioPlayerDriver: curl failed started=%s return_code=%d stderr=%s stdout=%s"), bStarted ? TEXT("true") : TEXT("false"), ReturnCode, *StdErr, *StdOut);
		if (ReturnCode == 28)
		{
			Metrics.DecisionTimeouts++;
		}
		return false;
	}

	return ApplyLMStudioResponse(StdOut);
}

bool FLMStudioPlayerDriver::ApplyLMStudioResponse(const FString& ResponseBody)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Metrics.InvalidResponses++;
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
	{
		Metrics.InvalidResponses++;
		return false;
	}

	const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
	const TSharedPtr<FJsonObject>* MessageObject = nullptr;
	FString Content;
	if (!(*Choices)[0]->TryGetObject(ChoiceObject) ||
		!(*ChoiceObject)->TryGetObjectField(TEXT("message"), MessageObject) ||
		!(*MessageObject)->TryGetStringField(TEXT("content"), Content))
	{
		Metrics.InvalidResponses++;
		return false;
	}

	TSharedPtr<FJsonObject> Decision;
	const FString DecisionText = ExtractJsonObjectText(Content);
	TSharedRef<TJsonReader<>> DecisionReader = TJsonReaderFactory<>::Create(DecisionText);
	if (!FJsonSerializer::Deserialize(DecisionReader, Decision) || !Decision.IsValid())
	{
		Metrics.InvalidResponses++;
		return false;
	}

	double GoalIndexNumber = -1.0;
	if (!Decision->TryGetNumberField(TEXT("goal_index"), GoalIndexNumber))
	{
		Metrics.InvalidResponses++;
		return false;
	}

	const int32 GoalIndex = FMath::RoundToInt(GoalIndexNumber);
	if (!CandidateGoals.IsValidIndex(GoalIndex))
	{
		Metrics.InvalidResponses++;
		return false;
	}

	CurrentGoal = CandidateGoals[GoalIndex];
	Metrics.DecisionResponses++;
	return true;
}

void FLMStudioPlayerDriver::HandleLMStudioResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	bRequestPending = false;
	if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		Metrics.FallbackDecisions++;
		return;
	}

	if (!ApplyLMStudioResponse(Response->GetContentAsString()))
	{
		Metrics.FallbackDecisions++;
	}
}

void FLMStudioPlayerDriver::ChooseDeterministicGoal(const FVector& PlayerLocation, const FVector& EnemyLocation, float CurrentTime)
{
	if (CandidateGoals.Num() == 0)
	{
		CurrentGoal = PlayerLocation;
		return;
	}

	float BestScore = -BIG_NUMBER;
	FVector BestGoal = CandidateGoals[0];
	for (const FVector& Candidate : CandidateGoals)
	{
		const float DistanceFromEnemy = FVector::DistSquared2D(Candidate, EnemyLocation);
		const float DistanceFromStart = FVector::DistSquared2D(Candidate, EpisodeStartLocation) * 0.05f;
		const float Score = DistanceFromEnemy + DistanceFromStart + RandomStream.FRandRange(-2500.0f, 2500.0f);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestGoal = Candidate;
		}
	}

	CurrentGoal = BestGoal;
}

void FLMStudioPlayerDriver::MoveTowardCurrentGoal(UWorld* World, float DeltaSeconds)
{
	APawn* Player = PlayerPawn.Get();
	if (!World || !Player || CurrentGoal.IsNearlyZero())
	{
		return;
	}

	const FVector CurrentLocation = Player->GetActorLocation();
	const FVector Direction = (CurrentGoal - CurrentLocation).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	FVector NewLocation = CurrentLocation + Direction * Settings.MoveSpeed * FMath::Max(DeltaSeconds, 0.0f);
	FVector ProjectedLocation;
	if (ProjectGoalToNavigation(World, NewLocation, ProjectedLocation))
	{
		Player->SetActorLocation(ProjectedLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

bool FLMStudioPlayerDriver::ProjectGoalToNavigation(UWorld* World, const FVector& Candidate, FVector& OutLocation) const
{
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem)
	{
		OutLocation = Candidate;
		return true;
	}

	FNavLocation Projected;
	if (!NavSystem->ProjectPointToNavigation(Candidate, Projected, FVector(500.0f, 500.0f, 500.0f)))
	{
		return false;
	}

	OutLocation = Projected.Location;
	return true;
}
