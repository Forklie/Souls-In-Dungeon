#include "MyAIController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "Soul_and_dungeonCharacter.h"

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

    bool bIsAttacking = Distance <= StopDistance;

    float CurrentTime = GetWorld()->GetTimeSeconds();

    if (bIsAttacking)
    {
        StopMovement();

        // 🔴 FACE PLAYER
        FVector Direction = Player->GetActorLocation() - AI->GetActorLocation();
        FRotator LookRotation = Direction.Rotation();
        FRotator TargetRotation(0.0f, LookRotation.Yaw, 0.0f);

        AI->SetActorRotation(TargetRotation);

        // 🧠 START ATTACK TIMER
        if (LastAttackStartTime == 0.0f || CurrentTime - LastAttackStartTime >= AttackInterval)
        {
            LastAttackStartTime = CurrentTime;
            bDamageAppliedThisAttack = false;
        }

        // 💥 APPLY DAMAGE AFTER DELAY
        if (!bDamageAppliedThisAttack && (CurrentTime - LastAttackStartTime) >= AttackDelay)
        {
            ASoul_and_dungeonCharacter* PlayerChar = Cast<ASoul_and_dungeonCharacter>(Player);

            if (PlayerChar)
            {
                PlayerChar->TakeDamageSimple(10.0f, AI);
            }

            bDamageAppliedThisAttack = true;
        }
    }
    else
    {
        MoveToLocation(Player->GetActorLocation());

        // 🔄 RESET ATTACK TIMER
        LastAttackStartTime = 0.0f;
        bDamageAppliedThisAttack = false;
    }

    // 🔴 SET ANIMATION VARIABLE
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
