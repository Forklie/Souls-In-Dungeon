#include "MyAIController.h"
#include "Kismet/GameplayStatics.h"

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

    if (Distance > StopDistance)
    {
        // 🟢 Follow
        MoveToLocation(Player->GetActorLocation());
    }
    else
    {
        // 🔴 Stop
        StopMovement();
    }
}