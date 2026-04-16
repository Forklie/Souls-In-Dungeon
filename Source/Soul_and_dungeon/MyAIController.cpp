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

    float StopDistance = 150.0f;

    ACharacter* AICharacter = Cast<ACharacter>(AI);
    if (!AICharacter) return;

    UAnimInstance* AnimInstance = AICharacter->GetMesh()->GetAnimInstance();
    if (!AnimInstance) return;

    // 🔥 IMPORTANT LINE (THIS CONTROLS ANIMATION)
    bool bIsAttacking = Distance <= StopDistance;

    // 🟢 Move or Stop
    if (!bIsAttacking)
    {
        MoveToLocation(Player->GetActorLocation());
    }
    else
    {
        StopMovement();
    }

    // 🔴 SET VARIABLE IN ANIM BP
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