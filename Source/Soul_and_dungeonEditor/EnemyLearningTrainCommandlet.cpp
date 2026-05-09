#include "EnemyLearningTrainCommandlet.h"

#include "AIController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EnemyLearningInteractor.h"
#include "EnemyLearningTrainingEnvironment.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsPPOTrainer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "MyAIController.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
static FString GetParamValue(const FString& Params, const TCHAR* Name, const FString& DefaultValue)
{
	FString Value;
	return FParse::Value(*Params, Name, Value) ? Value : DefaultValue;
}

static int32 GetIntParamValue(const FString& Params, const TCHAR* Name, int32 DefaultValue)
{
	int32 Value = DefaultValue;
	FParse::Value(*Params, Name, Value);
	return Value;
}

static bool SaveNetworkAsset(ULearningAgentsNeuralNetwork* Network, const FString& AssetPath)
{
	if (!Network || AssetPath.IsEmpty())
	{
		return false;
	}

	FString PackageName = AssetPath;
	FString AssetName;
	if (!PackageName.Split(TEXT("."), &PackageName, &AssetName))
	{
		AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return false;
	}

	Network->Rename(*AssetName, Package, REN_DontCreateRedirectors | REN_NonTransactional);
	Network->SetFlags(RF_Public | RF_Standalone);
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Network);

	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, Network, *Filename, SaveArgs);
}

static void TickWorld(UWorld* World, float DeltaSeconds)
{
	if (!World)
	{
		return;
	}

	World->Tick(LEVELTICK_All, DeltaSeconds);
	if (GEngine)
	{
		GEngine->Tick(DeltaSeconds, false);
	}
}

static void RegisterLearningPythonPathsForCommandlet()
{
	const FString ExperimentalPluginsPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Experimental")));

	TArray<FString> PluginPythonPaths;
	PluginPythonPaths.Add(FPaths::Combine(ExperimentalPluginsPath, TEXT("LearningAgents/Content/Python")));
	PluginPythonPaths.Add(FPaths::Combine(ExperimentalPluginsPath, TEXT("NNERuntimeBasicCpu/Content/Python")));

	for (FString& PluginPythonPath : PluginPythonPaths)
	{
		PluginPythonPath = FPaths::ConvertRelativePathToFull(PluginPythonPath);
	}

	const FString ExistingPythonPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PYTHONPATH"));
	TArray<FString> PythonPathEntries = PluginPythonPaths;
	if (!ExistingPythonPath.IsEmpty())
	{
		PythonPathEntries.Add(ExistingPythonPath);
	}
	FPlatformMisc::SetEnvironmentVar(TEXT("PYTHONPATH"), *FString::Join(PythonPathEntries, TEXT(":")));

	TArray<FString> SitePackagePaths;
	SitePackagePaths.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("PipInstall") / TEXT("Lib") / TEXT("site-packages")));
	SitePackagePaths.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("PipInstall") / TEXT("lib") / TEXT("python3.11") / TEXT("site-packages")));

	for (const FString& SitePackagePath : SitePackagePaths)
	{
		IFileManager::Get().MakeDirectory(*SitePackagePath, true);
		FFileHelper::SaveStringArrayToFile(
			PluginPythonPaths,
			*(SitePackagePath / TEXT("learning_agents.pth")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
}

UEnemyLearningTrainCommandlet::UEnemyLearningTrainCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UEnemyLearningTrainCommandlet::Main(const FString& Params)
{
	const FString MapPath = GetParamValue(Params, TEXT("Map="), TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"));
	const FString OutputPolicyPath = GetParamValue(Params, TEXT("OutputPolicy="), TEXT("/Game/AI/Learning/NN_EnemySteering"));
	const int32 Steps = FMath::Max(1, GetIntParamValue(Params, TEXT("Steps="), 500));
	const int32 MaxEpisodeSteps = FMath::Max(1, GetIntParamValue(Params, TEXT("MaxEpisodeSteps="), 1200));
	const int32 TrainingIterations = FMath::Max(1, GetIntParamValue(Params, TEXT("Iterations="), FMath::Max(1, Steps / 512)));
	const float FixedDeltaSeconds = 1.0f / 60.0f;

	RegisterLearningPythonPathsForCommandlet();

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: loading map %s"), *MapPath);
	if (!UEditorLoadingAndSavingUtils::LoadMap(MapPath))
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: failed to load map %s"), *MapPath);
		return 1;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: no editor world after map load"));
		return 1;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	AMyAIController* EnemyController = nullptr;
	for (TActorIterator<AMyAIController> It(World); It; ++It)
	{
		if (It->GetPawn())
		{
			EnemyController = *It;
			break;
		}
	}

	if (!PlayerPawn)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = TEXT("EnemyLearningTrainingPlayer");
		PlayerPawn = World->SpawnActor<APawn>(APawn::StaticClass(), FVector(0.0f, 0.0f, 100.0f), FRotator::ZeroRotator, SpawnParams);
	}

	if (!EnemyController)
	{
		ACharacter* EnemyCharacter = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(700.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
		EnemyController = World->SpawnActor<AMyAIController>(AMyAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (EnemyController && EnemyCharacter)
		{
			EnemyController->Possess(EnemyCharacter);
		}
	}

	if (!PlayerPawn || !EnemyController)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: missing player pawn or AMyAIController enemy in %s"), *MapPath);
		return 1;
	}

	EnemyController->SetLearningTrainingPlayer(PlayerPawn);

	if (IConsoleVariable* ModeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sd.EnemyNavigation.Mode")))
	{
		ModeCVar->Set(2, ECVF_SetByCode);
	}

	AActor* ManagerOwner = World->SpawnActor<AActor>();
	ULearningAgentsManager* Manager = NewObject<ULearningAgentsManager>(ManagerOwner, TEXT("EnemyLearningAgentsManager"));
	Manager->SetMaxAgentNum(1);
	Manager->RegisterComponent();

	ULearningAgentsManager* ManagerRef = Manager;
	ULearningAgentsInteractor* Interactor = ULearningAgentsInteractor::MakeInteractor(ManagerRef, UEnemyLearningInteractor::StaticClass(), TEXT("EnemyLearningInteractor"));
	ULearningAgentsTrainingEnvironment* TrainingEnvironmentBase = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(ManagerRef, UEnemyLearningTrainingEnvironment::StaticClass(), TEXT("EnemyLearningTrainingEnvironment"));
	UEnemyLearningTrainingEnvironment* TrainingEnvironment = CastChecked<UEnemyLearningTrainingEnvironment>(TrainingEnvironmentBase);
	TrainingEnvironment->Configure(PlayerPawn, MaxEpisodeSteps, 150.0f);

	Manager->AddAgent(EnemyController);

	ULearningAgentsPolicy* Policy = ULearningAgentsPolicy::MakePolicy(ManagerRef, Interactor, ULearningAgentsPolicy::StaticClass(), TEXT("EnemySteeringPolicy"));
	ULearningAgentsPolicy* PolicyRef = Policy;
	ULearningAgentsCritic* Critic = ULearningAgentsCritic::MakeCritic(ManagerRef, Interactor, PolicyRef, ULearningAgentsCritic::StaticClass(), TEXT("EnemySteeringCritic"));

	FLearningAgentsTrainerProcessSettings ProcessSettings;
	ProcessSettings.TaskName = TEXT("EnemyLearningTrain");
	FLearningAgentsSharedMemoryCommunicatorSettings SharedMemorySettings;
	SharedMemorySettings.Timeout = 30.0f;
	const FLearningAgentsCommunicator Communicator = ULearningAgentsCommunicatorLibrary::MakeSharedMemoryTrainingProcess(ProcessSettings, SharedMemorySettings);
	if (!Communicator.Trainer.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: failed to create Learning Agents training communicator"));
		return 1;
	}

	ULearningAgentsTrainingEnvironment* TrainingEnvironmentRef = TrainingEnvironment;
	ULearningAgentsCritic* CriticRef = Critic;
	ULearningAgentsPPOTrainer* Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(
		ManagerRef,
		Interactor,
		TrainingEnvironmentRef,
		PolicyRef,
		CriticRef,
		Communicator,
		ULearningAgentsPPOTrainer::StaticClass(),
		TEXT("EnemyPPOTrainer"));

	FLearningAgentsPPOTrainingSettings TrainingSettings;
	TrainingSettings.NumberOfIterations = TrainingIterations;
	TrainingSettings.Device = ELearningAgentsTrainingDevice::CPU;
	TrainingSettings.bSaveSnapshots = true;
	TrainingSettings.IterationsPerSnapshot = FMath::Max(1, TrainingIterations);

	FLearningAgentsTrainingGameSettings GameSettings;
	GameSettings.bUseFixedTimeStep = true;
	GameSettings.FixedTimeStepFrequency = 60.0f;

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: running %d steps (%d PPO iterations)"), Steps, TrainingIterations);
	for (int32 StepIndex = 0; StepIndex < Steps; ++StepIndex)
	{
		Trainer->RunTraining(TrainingSettings, GameSettings, StepIndex == 0, true);
		TickWorld(World, FixedDeltaSeconds);

		if (Trainer->HasTrainingFailed())
		{
			UE_LOG(LogTemp, Error, TEXT("EnemyLearningTrain: PPO trainer reported communication failure at step %d"), StepIndex);
			Trainer->EndTraining();
			return 1;
		}
	}

	Trainer->EndTraining();

	const bool bSavedPolicy = SaveNetworkAsset(Policy->GetPolicyNetworkAsset(), OutputPolicyPath);
	const FString SummaryPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EnemyLearning"), TEXT("TrainingSummary.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SummaryPath), true);

	const float AverageReward = TrainingEnvironment->GetRewardSampleCount() > 0
		? TrainingEnvironment->GetRewardSum() / static_cast<float>(TrainingEnvironment->GetRewardSampleCount())
		: 0.0f;

	const FString Summary = FString::Printf(
		TEXT("{\n  \"steps\": %d,\n  \"ppo_iterations\": %d,\n  \"average_reward\": %.6f,\n  \"completed_episodes\": %d,\n  \"truncated_episodes\": %d,\n  \"stuck_episodes\": %d,\n  \"policy_path\": \"%s\",\n  \"policy_saved\": %s\n}\n"),
		Steps,
		TrainingIterations,
		AverageReward,
		TrainingEnvironment->GetCompletedEpisodeCount(),
		TrainingEnvironment->GetTruncatedEpisodeCount(),
		TrainingEnvironment->GetStuckEpisodeCount(),
		*OutputPolicyPath,
		bSavedPolicy ? TEXT("true") : TEXT("false"));
	FFileHelper::SaveStringToFile(Summary, *SummaryPath);

	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: wrote summary %s"), *SummaryPath);
	UE_LOG(LogTemp, Display, TEXT("EnemyLearningTrain: policy save %s at %s"), bSavedPolicy ? TEXT("succeeded") : TEXT("failed"), *OutputPolicyPath);

	return bSavedPolicy ? 0 : 1;
}
