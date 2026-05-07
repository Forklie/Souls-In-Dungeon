#include "MyAIController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "SecondarySearchVisualizerActor.h"
#include "Soul_and_dungeonCharacter.h"
#include "Soul_and_dungeon.h"

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

    const bool bIsAttacking = Distance <= StopDistance;

    const float CurrentTime = GetWorld()->GetTimeSeconds();

    if (bIsAttacking)
    {
        StopMovement();

        const FVector Direction = Player->GetActorLocation() - AI->GetActorLocation();
        const FRotator LookRotation = Direction.Rotation();
        const FRotator TargetRotation(0.0f, LookRotation.Yaw, 0.0f);

        AI->SetActorRotation(TargetRotation);

        if (LastAttackStartTime == 0.0f)
        {
            LastAttackStartTime = CurrentTime;
        }

        if ((CurrentTime - LastAttackStartTime) >= AttackDelay)
        {
            if (CurrentTime - LastDamageTime > DamageCooldown)
            {
                ASoul_and_dungeonCharacter* PlayerChar = Cast<ASoul_and_dungeonCharacter>(Player);

                if (PlayerChar)
                {
                    PlayerChar->TakeDamageSimple(10.0f);
                }

                LastDamageTime = CurrentTime;
            }
        }
    }
    else
    {
        LastAttackStartTime = 0.0f;
        MoveToLocation(Player->GetActorLocation());
    }

    UpdateSecondarySearchDebug(AI, Player, CurrentTime);
    SetAttackAnimationState(AnimInstance, bIsAttacking);
}

void AMyAIController::UpdateSecondarySearchDebug(APawn* AI, APawn* Player, float CurrentTime)
{
    if (!AI || !Player)
    {
        return;
    }

    if (!FSecondarySearchDebug::IsEnabled())
    {
        if (bDebugSearchWasEnabled || SearchTask.IsActive() || bHasSearchResult)
        {
            SearchTask.Reset();
            bHasSearchResult = false;
            LastSearchResult = FSecondarySearchResult();
            bDebugSearchWasEnabled = false;
            HideSecondarySearchVisualizer();
        }
        return;
    }

    bDebugSearchWasEnabled = true;
    EnsureSecondarySearchVisualizer(AI);

    const FSecondarySearchSettings EffectiveSearchSettings = BuildSecondarySearchSettings();

    if (FSecondarySearchDebug::ConsumeClearRequested())
    {
        SearchTask.Reset();
        bHasSearchResult = false;
        LastSearchResult = FSecondarySearchResult();
        LastSearchFailureReason.Empty();
        ActiveSecondarySearchSettings = EffectiveSearchSettings;
        LastVisualizerUpdateTime = -1000000.0f;

        if (IsValid(SearchVisualizer))
        {
            SearchVisualizer->ClearVisualization();
        }
        return;
    }

    const ESecondarySearchMode SearchMode = FSecondarySearchDebug::GetMode();
    if (!SearchTask.IsActive() && ShouldRefreshSearchDebug(Player, CurrentTime, SearchMode, EffectiveSearchSettings))
    {
        LastSearchMode = SearchMode;
        LastSearchGoal = Player->GetActorLocation();
        LastSearchTime = CurrentTime;
        LastDebugRevision = FSecondarySearchDebug::GetRevision();
        ActiveSecondarySearchSettings = EffectiveSearchSettings;

        SearchTask.Start(
            GetWorld(),
            AI->GetActorLocation(),
            Player->GetActorLocation(),
            SearchMode,
            ActiveSecondarySearchSettings);
    }

    const FSecondarySearchSettings& StepSettings = SearchTask.IsActive() ? ActiveSecondarySearchSettings : EffectiveSearchSettings;
    SearchTask.Step(GetWorld(), StepSettings, StepSettings.MaxDebugSearchStepsPerTick);
    LastSearchResult = SearchTask.BuildDebugResult();
    LastSearchResult.CurrentTarget = Player->GetActorLocation();

    if (SearchTask.HasResult())
    {
        bHasSearchResult = true;

        if (LastSearchResult.bSuccess)
        {
            LastSearchFailureReason.Empty();
        }
        else if (LastSearchFailureReason != LastSearchResult.FailureReason || (CurrentTime - LastFailureLogTime) > 2.0f)
        {
            LastSearchFailureReason = LastSearchResult.FailureReason;
            LastFailureLogTime = CurrentTime;
            UE_LOG(LogSoul_and_dungeon, Warning, TEXT("Secondary search failed: %s"), *LastSearchFailureReason);
        }
    }

    if (IsValid(SearchVisualizer) && (CurrentTime - LastVisualizerUpdateTime) >= SecondarySearchSettings.DebugVisualizerUpdateInterval)
    {
        SearchVisualizer->UpdateVisualization(
            LastSearchResult,
            EffectiveSearchSettings,
            FSecondarySearchDebug::IsXRayEnabled(),
            FSecondarySearchDebug::GetNodeDensity(),
            FSecondarySearchDebug::GetVisualStyle(),
            FSecondarySearchDebug::GetVisualSpeed(),
            FSecondarySearchDebug::AreTrailsEnabled(),
            FSecondarySearchDebug::GetWaveSpeed(),
            FSecondarySearchDebug::GetPathHistoryCount(),
            FSecondarySearchDebug::GetNodeScale());
        LastVisualizerUpdateTime = CurrentTime;
    }
}

FSecondarySearchSettings AMyAIController::BuildSecondarySearchSettings() const
{
    FSecondarySearchSettings Settings = SecondarySearchSettings;
    Settings.CellSize = FMath::Clamp(FSecondarySearchDebug::GetCellSize(), 70.0f, 200.0f);
    Settings.ProjectionExtent = FVector(Settings.CellSize * 0.5f, Settings.CellSize * 0.5f, 250.0f);
    Settings.GoalAcceptanceRadius = FMath::Max(60.0f, Settings.CellSize * 0.7f);
    Settings.PathPointReachRadius = FMath::Max(35.0f, Settings.CellSize * 0.5f);
    Settings.MaxDebugDrawNodes = FSecondarySearchDebug::GetNodeDensity();
    Settings.MaxExpandedNodes = FMath::Max(Settings.MaxExpandedNodes, Settings.MaxDebugDrawNodes);
    return Settings;
}

bool AMyAIController::ShouldRefreshSearchDebug(const APawn* Player, float CurrentTime, ESecondarySearchMode SearchMode, const FSecondarySearchSettings& Settings) const
{
    if (!Player)
    {
        return false;
    }

    if (!bHasSearchResult || LastSearchMode != SearchMode || LastDebugRevision != FSecondarySearchDebug::GetRevision())
    {
        return true;
    }

    if ((CurrentTime - LastSearchTime) < SearchRefreshInterval)
    {
        return false;
    }

    const float GoalMoveThreshold = Settings.CellSize * 0.5f;
    return FVector::DistSquared2D(Player->GetActorLocation(), LastSearchGoal) > FMath::Square(GoalMoveThreshold);
}

void AMyAIController::EnsureSecondarySearchVisualizer(APawn* AI)
{
#if !UE_BUILD_SHIPPING
    if (IsValid(SearchVisualizer) || !GetWorld())
    {
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = this;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParameters.ObjectFlags |= RF_Transient;

    SearchVisualizer = GetWorld()->SpawnActor<ASecondarySearchVisualizerActor>(
        ASecondarySearchVisualizerActor::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParameters);
#endif
}

void AMyAIController::HideSecondarySearchVisualizer()
{
#if !UE_BUILD_SHIPPING
    if (IsValid(SearchVisualizer))
    {
        SearchVisualizer->ClearVisualization();
    }
#endif
}

void AMyAIController::SetAttackAnimationState(UAnimInstance* AnimInstance, bool bIsAttacking) const
{
    if (!AnimInstance)
    {
        return;
    }

    const FName VarName = "IsAttacking";
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
