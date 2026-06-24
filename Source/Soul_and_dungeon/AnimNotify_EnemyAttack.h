#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemyAttack.generated.h"

/**
 * An AnimNotify that executes a forward collision trace to hit the player.
 */
UCLASS()
class SOUL_AND_DUNGEON_API UAnimNotify_EnemyAttack : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack")
	float DamageAmount = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack")
	float TraceRadius = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack")
	float TraceDistance = 150.0f;
};
