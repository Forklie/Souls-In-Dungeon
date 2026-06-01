#include "AnimNotify_EnemyAttack.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Soul_and_dungeonCharacter.h"
#include "LevelManager.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

void UAnimNotify_EnemyAttack::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	AActor* OwnerActor = MeshComp->GetOwner();
	UWorld* World = OwnerActor->GetWorld();
	if (!World)
	{
		return;
	}

	if (ALevelManager* LevelManager = ALevelManager::GetActiveLevelManager(OwnerActor))
	{
		if (LevelManager->IsObjectiveComplete())
		{
			return;
		}
	}

	FVector TraceStart = OwnerActor->GetActorLocation() + (OwnerActor->GetActorForwardVector() * -30.0f); // Start slightly behind to catch point-blank
	FVector TraceEnd = OwnerActor->GetActorLocation() + (OwnerActor->GetActorForwardVector() * (TraceDistance + 50.0f)); // Extend distance slightly

	TArray<FHitResult> HitResults;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(TraceRadius);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor);
	QueryParams.bFindInitialOverlaps = true;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// Only query pawns so nearby props or object boxes cannot consume the attack hit.
	bool bHit = World->SweepMultiByObjectType(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ObjectParams,
		Sphere,
		QueryParams
	);

	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			APawn* HitPawn = Cast<APawn>(Hit.GetActor());
			if (HitPawn && HitPawn->IsPlayerControlled())
			{
				if (ASoul_and_dungeonCharacter* PlayerChar = Cast<ASoul_and_dungeonCharacter>(HitPawn))
				{
					if (PlayerChar->bIsDead) continue;
				}

				UGameplayStatics::ApplyDamage(
					HitPawn,
					DamageAmount,
					OwnerActor->GetInstigatorController(),
					OwnerActor,
					nullptr
				);
				break; // Only apply damage once
			}
		}
	}
}
