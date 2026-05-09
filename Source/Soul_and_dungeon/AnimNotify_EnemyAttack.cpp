#include "AnimNotify_EnemyAttack.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Soul_and_dungeonCharacter.h"
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

	FVector TraceStart = OwnerActor->GetActorLocation() + (OwnerActor->GetActorForwardVector() * -30.0f); // Start slightly behind to catch point-blank
	FVector TraceEnd = OwnerActor->GetActorLocation() + (OwnerActor->GetActorForwardVector() * (TraceDistance + 50.0f)); // Extend distance slightly

	TArray<FHitResult> HitResults;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(TraceRadius);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor);

	// Sweep for players (Pawn trace channel)
	bool bHit = World->SweepMultiByChannel(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		Sphere,
		QueryParams
	);

	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			if (Hit.GetActor())
			{
				UGameplayStatics::ApplyDamage(
					Hit.GetActor(),
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
