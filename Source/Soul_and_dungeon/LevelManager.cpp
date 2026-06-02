#include "LevelManager.h"
#include "DungeonChestActor.h"
#include "DungeonProgressionState.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float ChestOpenRotationThreshold = 5.0f;

	bool IsChestLidOpen(const USceneComponent* LidComponent)
	{
		if (!LidComponent)
		{
			return false;
		}

		const FRotator Rot = LidComponent->GetRelativeRotation();
		return FMath::Abs(Rot.Roll) > ChestOpenRotationThreshold ||
			FMath::Abs(Rot.Pitch) > ChestOpenRotationThreshold ||
			FMath::Abs(Rot.Yaw) > ChestOpenRotationThreshold;
	}

	bool ChestActorsMatch(const AActor* CachedActor, const AActor* QueryActor)
	{
		if (!CachedActor || !QueryActor)
		{
			return false;
		}

		if (CachedActor == QueryActor)
		{
			return true;
		}

		if (const AActor* CachedParent = CachedActor->GetAttachParentActor())
		{
			if (CachedParent == QueryActor)
			{
				return true;
			}
		}

		if (const AActor* QueryParent = QueryActor->GetAttachParentActor())
		{
			if (QueryParent == CachedActor)
			{
				return true;
			}
		}

		TArray<AActor*> CachedAttachedActors;
		CachedActor->GetAttachedActors(CachedAttachedActors);
		if (CachedAttachedActors.Contains(QueryActor))
		{
			return true;
		}

		TArray<AActor*> QueryAttachedActors;
		QueryActor->GetAttachedActors(QueryAttachedActors);
		return QueryAttachedActors.Contains(CachedActor);
	}

	USceneComponent* FindChestLidComponent(AActor* ChestActor)
	{
		if (!ChestActor)
		{
			return nullptr;
		}

		TArray<USceneComponent*> Components;
		ChestActor->GetComponents(Components);
		for (USceneComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const FString ComponentName = Component->GetName().ToLower();
			if (ComponentName.Contains(TEXT("lid")) || ComponentName.Contains(TEXT("top")))
			{
				return Component;
			}
		}

		return nullptr;
	}

	bool IsTrackedChestActor(AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		if (Cast<ADungeonChestActor>(Actor))
		{
			return true;
		}

		const FString Name = Actor->GetName().ToLower();
		const FString ClassName = Actor->GetClass()->GetName().ToLower();
		return Name.Contains(TEXT("bp_dungeonchest")) ||
			ClassName.Contains(TEXT("bp_dungeonchest")) ||
			Name.Contains(TEXT("dungeonchest")) ||
			ClassName.Contains(TEXT("dungeonchest")) ||
			Name.Contains(TEXT("bp_prop_chest_interactive")) ||
			ClassName.Contains(TEXT("bp_prop_chest_interactive"));
	}
	}

ALevelManager::ALevelManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

ALevelManager* ALevelManager::GetActiveLevelManager(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	ALevelManager* BestManager = nullptr;
	int32 BestScore = MIN_int32;

	for (TActorIterator<ALevelManager> It(World); It; ++It)
	{
		ALevelManager* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		int32 Score = 0;
		if (Candidate->TotalRequiredChests > 0)
		{
			Score += 1000;
		}
		if (Candidate->ExitPortalRef)
		{
			Score += 100;
		}
		if (Candidate->bObjectivesComplete)
		{
			Score += 50;
		}
		Score += FMath::Clamp(Candidate->OpenedChests, 0, 999);

		if (!BestManager || Score > BestScore)
		{
			BestManager = Candidate;
			BestScore = Score;
		}
	}

	return BestManager;
}

void ALevelManager::BeginPlay()
{
	Super::BeginPlay();

	RefreshChestCache();
}

int32 ALevelManager::GetOpenChestCount() const
{
	const_cast<ALevelManager*>(this)->RefreshChestCache();

	int32 OpenCount = 0;
	TSet<AActor*> CountedChests;

	for (const FCachedChest& Cached : CachedChests)
	{
		AActor* ChestActor = Cached.ChestActor.Get();
		if (!ChestActor || CountedChests.Contains(ChestActor))
		{
			continue;
		}

		const ADungeonChestActor* DungeonChest = Cast<ADungeonChestActor>(ChestActor);
		if ((DungeonChest && DungeonChest->HasBeenOpened()) ||
			OpenedChestsSet.Contains(ChestActor) ||
			IsChestLidOpen(Cached.LidComponent.Get()))
		{
			OpenCount++;
			CountedChests.Add(ChestActor);
		}
	}

	return FMath::Max(OpenCount, OpenedChests);
}

void ALevelManager::SyncObjectiveStateFromVisualCount(int32 VisualOpenCount)
{
	OpenedChests = FMath::Max(VisualOpenCount, 0);
	CheckObjectiveComplete();
}

bool ALevelManager::IsObjectiveComplete() const
{
	return bObjectivesComplete;
}

void ALevelManager::ResetObjectives()
{
	TotalRequiredChests = 0;
	OpenedChests = 0;
	ExitPortalRef = nullptr;
	AllChestActors.Reset();
	CachedChests.Reset();
	OpenedChestsSet.Reset();
	bObjectivesComplete = false;

	UE_LOG(LogTemp, Log, TEXT("LevelManager: Objective state reset for procedural generation."));
}

void ALevelManager::NotifyChestOpened(AActor* ChestActor)
{
	// Prevent double-counting the same chest
	if (ChestActor && OpenedChestsSet.Contains(ChestActor))
	{
		return;
	}

	if (ChestActor)
	{
		OpenedChestsSet.Add(ChestActor);
	}

	OpenedChests++;
	UE_LOG(LogTemp, Log, TEXT("LevelManager: Chest opened (%s). Progress: %d/%d"),
		ChestActor ? *ChestActor->GetName() : TEXT("Unknown"), OpenedChests, TotalRequiredChests);
	
	// Optional: Debug screen message
	UKismetSystemLibrary::PrintString(this, FString::Printf(TEXT("Objective Progress: %d / %d"), OpenedChests, TotalRequiredChests), true, true, FLinearColor::Blue, 5.0f);
	
	CheckObjectiveComplete();
}

bool ALevelManager::IsChestOpened(AActor* ChestActor) const
{
	if (!ChestActor) return false;
	if (OpenedChestsSet.Contains(ChestActor)) return true;

	if (const ADungeonChestActor* DungeonChest = Cast<ADungeonChestActor>(ChestActor))
	{
		if (DungeonChest->HasBeenOpened())
		{
			return true;
		}
	}

	// Sometimes the Minimap tracks the Root Actor, but the player's interact trace hits a Child Actor.
	// Check if the actor's parent is in the set.
	if (AActor* Parent = ChestActor->GetAttachParentActor())
	{
		if (OpenedChestsSet.Contains(Parent)) return true;
	}

	// Sometimes the Minimap tracks the Child Actor, but the LevelManager recorded the Root Actor.
	// Check if any attached child actors are in the set.
	TArray<AActor*> AttachedActors;
	ChestActor->GetAttachedActors(AttachedActors);
	for (AActor* Child : AttachedActors)
	{
		if (OpenedChestsSet.Contains(Child)) return true;
		if (const ADungeonChestActor* DungeonChest = Cast<ADungeonChestActor>(Child))
		{
			if (DungeonChest->HasBeenOpened())
			{
				return true;
			}
		}
	}

	return false;
}

bool ALevelManager::IsChestVisuallyOpen(AActor* ChestActor) const
{
	if (!ChestActor)
	{
		return false;
	}

	const_cast<ALevelManager*>(this)->RefreshChestCache();

	if (const ADungeonChestActor* DungeonChest = Cast<ADungeonChestActor>(ChestActor))
	{
		return DungeonChest->HasBeenOpened();
	}

	if (const FCachedChest* Cached = FindCachedChest(ChestActor))
	{
		if (const ADungeonChestActor* DungeonChest = Cast<ADungeonChestActor>(Cached->ChestActor.Get()))
		{
			return DungeonChest->HasBeenOpened();
		}

		return IsChestLidOpen(Cached->LidComponent.Get());
	}

	TArray<USceneComponent*> SceneComponents;
	ChestActor->GetComponents(SceneComponents);
	for (const USceneComponent* Component : SceneComponents)
	{
		if (!Component)
		{
			continue;
		}

		const FString ComponentName = Component->GetName().ToLower();
		if ((ComponentName.Contains(TEXT("lid")) || ComponentName.Contains(TEXT("top"))) && IsChestLidOpen(Component))
		{
			return true;
		}
	}

	return false;
}

const ALevelManager::FCachedChest* ALevelManager::FindCachedChest(AActor* ChestActor) const
{
	if (!ChestActor)
	{
		return nullptr;
	}

	for (const FCachedChest& Cached : CachedChests)
	{
		if (ChestActorsMatch(Cached.ChestActor.Get(), ChestActor))
		{
			return &Cached;
		}
	}

	return nullptr;
}

void ALevelManager::RegisterChest(AActor* ChestActor)
{
	if (ChestActor && !FindCachedChest(ChestActor))
	{
		FCachedChest Cached;
		Cached.ChestActor = ChestActor;
		Cached.LidComponent = FindChestLidComponent(ChestActor);
		CachedChests.Add(Cached);
		AllChestActors.AddUnique(ChestActor);
	}

	TotalRequiredChests++;
	UE_LOG(LogTemp, Log, TEXT("LevelManager: Required chest registered (%s). Total: %d"),
		ChestActor ? *ChestActor->GetName() : TEXT("Unknown"),
		TotalRequiredChests);
}

void ALevelManager::CheckObjectiveComplete()
{
	if (bObjectivesComplete)
	{
		return;
	}

	if (OpenedChests >= TotalRequiredChests && TotalRequiredChests > 0)
	{
		bObjectivesComplete = true;
		UE_LOG(LogTemp, Log, TEXT("LevelManager: All objectives complete! Unlocking portal."));

		if (bCompleteGameWhenObjectivesComplete)
		{
			UE_LOG(LogTemp, Log, TEXT("LevelManager: Objective completion is configured to complete the game immediately."));

			ADungeonProgressionState* Progression = ADungeonProgressionState::Get(this);
			if (!Progression && GetWorld())
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Progression = GetWorld()->SpawnActor<ADungeonProgressionState>(ADungeonProgressionState::StaticClass(), FTransform::Identity, SpawnParams);
			}

			if (Progression)
			{
				Progression->CompleteGame();
			}
			else
			{
				UKismetSystemLibrary::PrintString(this, TEXT("ALL CHESTS OPENED! LEVEL COMPLETE!"), true, true, FLinearColor::Green, 10.0f);
			}
			return;
		}
		
		// Optional: Screen message
		UKismetSystemLibrary::PrintString(this, TEXT("ALL CHESTS OPENED! PORTAL UNLOCKED!"), true, true, FLinearColor::Green, 10.0f);

		if (ExitPortalRef)
		{
			// Call UnlockPortal function on the portal actor if it exists
			UFunction* UnlockFunc = ExitPortalRef->FindFunction(TEXT("UnlockPortal"));
			if (UnlockFunc)
			{
				ExitPortalRef->ProcessEvent(UnlockFunc, nullptr);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("LevelManager: ExitPortalRef found but does not have UnlockPortal function!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("LevelManager: All objectives complete but ExitPortalRef is NULL!"));
		}
	}
}

void ALevelManager::RefreshChestCache()
{
	if (!GetWorld())
	{
		return;
	}

	if (TotalRequiredChests > 0 && CachedChests.Num() >= TotalRequiredChests)
	{
		return;
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), AllActors);

	TArray<FCachedChest> FoundCachedChests;
	FoundCachedChests.Reserve(AllActors.Num());

	for (AActor* Actor : AllActors)
	{
		if (!IsTrackedChestActor(Actor))
		{
			continue;
		}

		FCachedChest NewCached;
		NewCached.ChestActor = Actor;
		NewCached.LidComponent = FindChestLidComponent(Actor);
		FoundCachedChests.Add(NewCached);
	}

	if (FoundCachedChests.Num() > 0)
	{
		for (const FCachedChest& FoundChest : FoundCachedChests)
		{
			if (AActor* ChestActor = FoundChest.ChestActor.Get())
			{
				if (!FindCachedChest(ChestActor))
				{
					CachedChests.Add(FoundChest);
				}
				AllChestActors.AddUnique(ChestActor);
			}
		}

		if (TotalRequiredChests <= 0)
		{
			TotalRequiredChests = CachedChests.Num();
			UE_LOG(LogTemp, Log, TEXT("LevelManager: Auto-scanned map and found %d objective chests."), TotalRequiredChests);
		}
	}
}
