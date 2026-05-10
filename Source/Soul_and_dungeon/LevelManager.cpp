#include "LevelManager.h"
#include "Kismet/KismetSystemLibrary.h"

ALevelManager::ALevelManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ALevelManager::NotifyChestOpened()
{
	OpenedChests++;
	UE_LOG(LogTemp, Log, TEXT("LevelManager: Chest opened. Progress: %d/%d"), OpenedChests, TotalRequiredChests);
	
	// Optional: Debug screen message
	UKismetSystemLibrary::PrintString(this, FString::Printf(TEXT("Objective Progress: %d / %d"), OpenedChests, TotalRequiredChests), true, true, FLinearColor::Blue, 5.0f);
	
	CheckObjectiveComplete();
}

void ALevelManager::RegisterChest()
{
	TotalRequiredChests++;
	UE_LOG(LogTemp, Log, TEXT("LevelManager: Required chest registered. Total: %d"), TotalRequiredChests);
}

void ALevelManager::CheckObjectiveComplete()
{
	if (OpenedChests >= TotalRequiredChests && TotalRequiredChests > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("LevelManager: All objectives complete! Unlocking portal."));
		
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
