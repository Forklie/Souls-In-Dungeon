#include "MyAIController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"

AMyAIController::AMyAIController()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AMyAIController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    APawn* AI = GetPawn();

    if (!Player || !AI) return;

    float Distance = FVector::Dist(AI->GetActorLocation(), Player->GetActorLocation());

    ACharacter* AICharacter = Cast<ACharacter>(AI);
    if (!AICharacter) return;

    UAnimInstance* AnimInstance = AICharacter->GetMesh()->GetAnimInstance();
    if (!AnimInstance) return;

    // ✅ SIMPLE ATTACK CONDITION (LOOP)
    bool bIsAttacking = Distance <= StopDistance;

    if (bIsAttacking)
    {
        StopMovement();

        // 🔴 SMOOTH ROTATION TOWARD PLAYER
        FVector Direction = Player->GetActorLocation() - AI->GetActorLocation();
        FRotator LookRotation = Direction.Rotation();

        // Only rotate Yaw (left/right)
        FRotator TargetRotation(0.0f, LookRotation.Yaw, 0.0f);

        // 🔴 FORCE LOOK AT PLAYER (NO SMOOTH)
        AI->SetActorRotation(TargetRotation);
    }
    else
    {
        // 🟢 FOLLOW PLAYER
        MoveToLocation(Player->GetActorLocation());
    }

    // 🔴 SET ANIMATION VARIABLE (IMPORTANT)
    FName VarName = "IsAttacking";
    FProperty* Prop = AnimInstance->GetClass()->FindPropertyByName(VarName);

    if (Prop)
    {
        FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop);
        if (BoolProp)
        {
            BoolProp->SetPropertyValue_InContainer(AnimInstance, bIsAttacking);
        }
    }
}