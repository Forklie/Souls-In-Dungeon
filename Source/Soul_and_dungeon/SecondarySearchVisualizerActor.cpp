#include "SecondarySearchVisualizerActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASecondarySearchVisualizerActor::ASecondarySearchVisualizerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ExpandedNodes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ExpandedNodes"));
	FrontierNodes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FrontierNodes"));
	StartMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StartMarker"));
	GoalMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GoalMarker"));
	TargetMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TargetMarker"));
	PathSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathSegments"));
	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));

	ExpandedNodes->SetupAttachment(SceneRoot);
	FrontierNodes->SetupAttachment(SceneRoot);
	StartMarker->SetupAttachment(SceneRoot);
	GoalMarker->SetupAttachment(SceneRoot);
	TargetMarker->SetupAttachment(SceneRoot);
	PathSegments->SetupAttachment(SceneRoot);
	StatusText->SetupAttachment(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialAsset(TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));

	CylinderMesh = CylinderAsset.Object;
	SphereMesh = SphereAsset.Object;
	BaseMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/Debug/M_SecondarySearch_NodeFluid.M_SecondarySearch_NodeFluid")));
	PathBaseMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/Debug/M_SecondarySearch_PathFluid.M_SecondarySearch_PathFluid")));
	if (!BaseMaterial)
	{
		BaseMaterial = MaterialAsset.Object;
	}
	if (!PathBaseMaterial)
	{
		PathBaseMaterial = BaseMaterial;
	}

	ConfigureComponent(ExpandedNodes, CylinderMesh);
	ConfigureComponent(FrontierNodes, CylinderMesh);
	ConfigureComponent(StartMarker, SphereMesh);
	ConfigureComponent(GoalMarker, SphereMesh);
	ConfigureComponent(TargetMarker, SphereMesh);
	ConfigureComponent(PathSegments, CylinderMesh);

	StatusText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StatusText->SetGenerateOverlapEvents(false);
	StatusText->SetCanEverAffectNavigation(false);
	StatusText->SetMobility(EComponentMobility::Movable);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetWorldSize(34.0f);
	StatusText->SetTextRenderColor(FColor::White);
	StatusText->SetHiddenInGame(true);

	SetActorHiddenInGame(true);
}

void ASecondarySearchVisualizerActor::BeginPlay()
{
	Super::BeginPlay();

	ExpandedMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.04f, 0.18f, 0.85f, 1.0f), TEXT("ExpandedMaterial"));
	FrontierMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.0f, 0.95f, 1.0f, 1.0f), TEXT("FrontierMaterial"));
	StartMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.0f, 1.0f, 0.15f, 1.0f), TEXT("StartMaterial"));
	GoalMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(1.0f, 0.05f, 0.02f, 1.0f), TEXT("GoalMaterial"));
	TargetMaterial = CreateColorMaterial(BaseMaterial, FLinearColor::White, TEXT("TargetMaterial"));
	PathMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(1.0f, 0.55f, 0.0f, 1.0f), TEXT("PathMaterial"));

	ExpandedNodes->SetMaterial(0, ExpandedMaterial);
	FrontierNodes->SetMaterial(0, FrontierMaterial);
	StartMarker->SetMaterial(0, StartMaterial);
	GoalMarker->SetMaterial(0, GoalMaterial);
	TargetMarker->SetMaterial(0, TargetMaterial);
	PathSegments->SetMaterial(0, PathMaterial);
}

void ASecondarySearchVisualizerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsHidden())
	{
		const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		UpdateFluidAnimation(DeltaSeconds, WorldSeconds);
		UpdateWavePathLayers(DeltaSeconds, WorldSeconds);
		if (bHasTargetLocation)
		{
			UpdateTargetPulse(WorldSeconds);
		}
	}
}

void ASecondarySearchVisualizerActor::UpdateVisualization(
	const FSecondarySearchResult& Result,
	const FSecondarySearchSettings& Settings,
	bool bXRayEnabled,
	int32 MaxVisibleNodes,
	ESecondarySearchVisualStyle VisualStyle,
	float VisualSpeed,
	bool bTrailsEnabled,
	float WaveSpeed,
	int32 PathHistoryCount,
	float NodeScale)
{
#if !UE_BUILD_SHIPPING
	if (Result.StartLocation.IsNearlyZero() && Result.GoalLocation.IsNearlyZero() &&
		Result.Path.Num() == 0 && Result.ExpandedNodes.Num() == 0 && Result.FrontierNodes.Num() == 0)
	{
		ClearVisualization();
		return;
	}

	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
	StatusText->SetHiddenInGame(true);

	const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : Result.DebugStartSeconds;
	CachedVisualSpeed = FMath::Max(0.1f, VisualSpeed);
	CachedWaveSpeed = FMath::Max(0.1f, WaveSpeed);
	CachedNodeScale = FMath::Clamp(NodeScale, 0.25f, 1.25f);
	CachedTargetRadius = FMath::Max(1.0f, Settings.DebugTargetRadius);
	CachedPathRevealSpeed = FMath::Max(100.0f, Settings.FluidPathRevealSpeed);
	const float RevealSeconds = FMath::Max(0.0f, WorldSeconds - Result.DebugStartSeconds) * CachedVisualSpeed;
	const int32 AnimatedNodeLimit = FMath::Max(12, FMath::CeilToInt(RevealSeconds * Settings.VisualizationRevealRate));
	const int32 NodeCap = FMath::Max(0, MaxVisibleNodes > 0 ? MaxVisibleNodes : Settings.MaxDebugDrawNodes);
	const bool bFluidStyle = VisualStyle == ESecondarySearchVisualStyle::Fluid;
	const int32 ExpandedDrawCount = bFluidStyle
		? FMath::Min3(Result.ExpandedNodes.Num(), NodeCap, AnimatedNodeLimit)
		: FMath::Min(Result.ExpandedNodes.Num(), NodeCap);
	const int32 FrontierDrawCount = bFluidStyle
		? FMath::Min3(Result.FrontierNodes.Num(), NodeCap, AnimatedNodeLimit)
		: FMath::Min(Result.FrontierNodes.Num(), NodeCap);

	const bool bNeedsRebuild =
		LastVisualizationRevision != Result.VisualizationRevision ||
		LastSearchGeneration != Result.SearchGeneration ||
		LastExpandedDrawCount != ExpandedDrawCount ||
		LastFrontierDrawCount != FrontierDrawCount ||
		LastPathPointCount != Result.Path.Num() ||
		LastVisualStyle != VisualStyle ||
		bLastTrailsEnabled != bTrailsEnabled ||
		bLastXRayEnabled != bXRayEnabled ||
		!FMath::IsNearlyEqual(LastNodeScale, CachedNodeScale);

	if (bNeedsRebuild)
	{
		LastVisualStyle = VisualStyle;
		bLastTrailsEnabled = bTrailsEnabled;
		ApplyDepthPriority(bXRayEnabled);
		RebuildInstances(Result, Settings, ExpandedDrawCount, FrontierDrawCount);
		LastVisualizationRevision = Result.VisualizationRevision;
		LastSearchGeneration = Result.SearchGeneration;
		LastExpandedDrawCount = ExpandedDrawCount;
		LastFrontierDrawCount = FrontierDrawCount;
		LastPathPointCount = Result.Path.Num();
		bLastXRayEnabled = bXRayEnabled;
		LastNodeScale = CachedNodeScale;
		LastNodeBuildSeconds = WorldSeconds;
	}

	if (VisualStyle == ESecondarySearchVisualStyle::Fluid)
	{
		AddWavePathLayer(Result, Settings, WorldSeconds, bTrailsEnabled, PathHistoryCount);
	}
	else
	{
		ClearWavePathLayers();
	}

	TrimWavePathHistory(PathHistoryCount);
	UpdateStatusText(Result, Settings);
	CachedTargetLocation = Result.CurrentTarget;
	bHasTargetLocation = !Result.CurrentTarget.IsNearlyZero();
	UpdateFluidAnimation(0.0f, WorldSeconds);
	UpdateWavePathLayers(0.0f, WorldSeconds);
	UpdateTargetPulse(WorldSeconds);
#endif
}

void ASecondarySearchVisualizerActor::ClearVisualization()
{
	ExpandedNodes->ClearInstances();
	FrontierNodes->ClearInstances();
	StartMarker->ClearInstances();
	GoalMarker->ClearInstances();
	TargetMarker->ClearInstances();
	PathSegments->ClearInstances();
	ClearWavePathLayers();
	StatusText->SetText(FText::GetEmpty());
	StatusText->SetHiddenInGame(true);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(913702, 0.01f, FColor::White, FString());
	}
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	bHasTargetLocation = false;
	ResetRetainedState();
	LastVisualizationRevision = -1;
	LastSearchGeneration = -1;
	LastPathLayerGeneration = -1;
	LastExpandedDrawCount = -1;
	LastFrontierDrawCount = -1;
	LastPathPointCount = -1;
	LastNodeScale = -1.0f;
}

void ASecondarySearchVisualizerActor::ConfigureComponent(UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh) const
{
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCastShadow(false);
	Component->bCastDynamicShadow = false;
	Component->bCastStaticShadow = false;
	Component->SetHiddenInGame(false);
}

void ASecondarySearchVisualizerActor::ApplyDepthPriority(bool bXRayEnabled)
{
	const ESceneDepthPriorityGroup DepthGroup = bXRayEnabled ? SDPG_Foreground : SDPG_World;
	UPrimitiveComponent* Components[] = {
		ExpandedNodes,
		FrontierNodes,
		StartMarker,
		GoalMarker,
		TargetMarker,
		PathSegments,
	};

	for (UPrimitiveComponent* Component : Components)
	{
		if (Component)
		{
			Component->SetDepthPriorityGroup(DepthGroup);
			Component->SetRenderCustomDepth(bXRayEnabled);
		}
	}

	for (USplineMeshComponent* Spline : PathSplinePool)
	{
		if (Spline)
		{
			Spline->SetDepthPriorityGroup(DepthGroup);
			Spline->SetRenderCustomDepth(bXRayEnabled);
		}
	}
}

void ASecondarySearchVisualizerActor::RebuildInstances(
	const FSecondarySearchResult& Result,
	const FSecondarySearchSettings& Settings,
	int32 ExpandedDrawCount,
	int32 FrontierDrawCount)
{
	ExpandedNodes->ClearInstances();
	FrontierNodes->ClearInstances();
	StartMarker->ClearInstances();
	GoalMarker->ClearInstances();
	PathSegments->ClearInstances();
	FluidNodeRecords.Reset();

	const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : Result.DebugStartSeconds;

	for (int32 Index = 0; Index < ExpandedDrawCount; ++Index)
	{
		const float Radius = Settings.DebugExpandedNodeRadius * CachedNodeScale;
		const float Height = Settings.DebugNodeHeight;
		const int32 InstanceIndex = AddWorldInstance(ExpandedNodes, MakeDiskTransform(Result.ExpandedNodes[Index], Radius, Height, Settings.DebugPointZOffset));
		FluidNodeRecords.Add({ ExpandedNodes, InstanceIndex, Result.ExpandedNodes[Index], Radius, Height, Settings.DebugPointZOffset, WorldSeconds - (ExpandedDrawCount - Index) * 0.015f, false });
	}

	for (int32 Index = 0; Index < FrontierDrawCount; ++Index)
	{
		const float Radius = Settings.DebugFrontierNodeRadius * CachedNodeScale;
		const float Height = Settings.DebugFrontierNodeHeight;
		const int32 InstanceIndex = AddWorldInstance(FrontierNodes, MakeDiskTransform(Result.FrontierNodes[Index], Radius, Height, Settings.DebugPointZOffset + 5.0f));
		FluidNodeRecords.Add({ FrontierNodes, InstanceIndex, Result.FrontierNodes[Index], Radius, Height, Settings.DebugPointZOffset + 5.0f, WorldSeconds - (FrontierDrawCount - Index) * 0.01f, true });
	}

	AddWorldInstance(StartMarker, MakeSphereTransform(Result.StartLocation, Settings.DebugEndpointRadius * CachedNodeScale, Settings.DebugPointZOffset + 20.0f));
	AddWorldInstance(GoalMarker, MakeSphereTransform(Result.GoalLocation, Settings.DebugEndpointRadius * CachedNodeScale, Settings.DebugPointZOffset + 20.0f));

	if (LastVisualStyle == ESecondarySearchVisualStyle::Simple)
	{
		for (int32 Index = 1; Index < Result.Path.Num(); ++Index)
		{
			AddWorldInstance(PathSegments, MakeTubeTransform(
				Result.Path[Index - 1],
				Result.Path[Index],
				Settings.PathTubeRadius,
				Settings.DebugPointZOffset + 18.0f));
		}
	}
}

void ASecondarySearchVisualizerActor::AddWavePathLayer(
	const FSecondarySearchResult& Result,
	const FSecondarySearchSettings& Settings,
	float WorldSeconds,
	bool bTrailsEnabled,
	int32 PathHistoryCount)
{
	if (!Result.bSuccess || Result.Path.Num() < 2 || LastPathLayerGeneration == Result.SearchGeneration)
	{
		return;
	}

	if (!bTrailsEnabled)
	{
		ClearWavePathLayers();
	}
	else
	{
		for (FWavePathLayer& Layer : WavePathLayers)
		{
			if (Layer.SupersededSeconds < 0.0f)
			{
				Layer.SupersededSeconds = WorldSeconds;
			}
		}
	}

	FWavePathLayer NewLayer;
	NewLayer.Generation = Result.SearchGeneration;
	NewLayer.CreatedSeconds = WorldSeconds;
	NewLayer.WaveDistance = 0.0f;
	NewLayer.BaseMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(1.0f, 0.45f, 0.04f, 0.30f), TEXT("WavePathBaseMaterial"));
	NewLayer.WaveMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.35f, 0.95f, 1.0f, 1.0f), TEXT("WavePathCrestMaterial"));
	NewLayer.WakeMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.08f, 0.55f, 1.0f, 0.45f), TEXT("WavePathWakeMaterial"));
	if (NewLayer.BaseMaterial)
	{
		PathLayerMaterials.Add(NewLayer.BaseMaterial);
	}
	if (NewLayer.WaveMaterial)
	{
		PathLayerMaterials.Add(NewLayer.WaveMaterial);
	}
	if (NewLayer.WakeMaterial)
	{
		PathLayerMaterials.Add(NewLayer.WakeMaterial);
	}

	float RunningDistance = 0.0f;
	for (int32 Index = 1; Index < Result.Path.Num(); ++Index)
	{
		const FVector Start = Result.Path[Index - 1] + FVector(0.0f, 0.0f, Settings.DebugPointZOffset + 18.0f);
		const FVector End = Result.Path[Index] + FVector(0.0f, 0.0f, Settings.DebugPointZOffset + 18.0f);
		const float Length = FVector::Distance(Start, End);
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		FWavePathSegment Segment;
		Segment.Start = Start;
		Segment.End = End;
		Segment.StartDistance = RunningDistance;
		Segment.Length = Length;
		Segment.BaseSpline = AcquirePathSpline();
		Segment.WakeSpline = AcquirePathSpline();
		Segment.WaveSpline = AcquirePathSpline();
		if (!Segment.BaseSpline || !Segment.WakeSpline || !Segment.WaveSpline)
		{
			ReleasePathSpline(Segment.BaseSpline);
			ReleasePathSpline(Segment.WakeSpline);
			ReleasePathSpline(Segment.WaveSpline);
			continue;
		}

		ConfigurePathSpline(Segment.BaseSpline, NewLayer.BaseMaterial, Settings.PathTubeRadius, false);
		ConfigurePathSpline(Segment.WakeSpline, NewLayer.WakeMaterial, Settings.PathTubeRadius * 1.2f, true);
		ConfigurePathSpline(Segment.WaveSpline, NewLayer.WaveMaterial, Settings.PathTubeRadius * 1.65f, true);
		SetSplineSegment(Segment.BaseSpline, Start, End, Settings.PathTubeRadius, true);
		SetSplineSegment(Segment.WakeSpline, Start, End, Settings.PathTubeRadius * 1.2f, false);
		SetSplineSegment(Segment.WaveSpline, Start, End, Settings.PathTubeRadius * 1.65f, false);

		NewLayer.Segments.Add(Segment);
		RunningDistance += Length;
	}

	if (NewLayer.Segments.Num() == 0)
	{
		PathLayerMaterials.RemoveSingleSwap(NewLayer.BaseMaterial, EAllowShrinking::No);
		PathLayerMaterials.RemoveSingleSwap(NewLayer.WaveMaterial, EAllowShrinking::No);
		PathLayerMaterials.RemoveSingleSwap(NewLayer.WakeMaterial, EAllowShrinking::No);
		return;
	}

	NewLayer.TotalDistance = RunningDistance;
	WavePathLayers.Add(MoveTemp(NewLayer));
	LastPathLayerGeneration = Result.SearchGeneration;
	TrimWavePathHistory(PathHistoryCount);
}

void ASecondarySearchVisualizerActor::UpdateStatusText(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings)
{
	FString PathState = TEXT("searching");
	if (Result.bSuccess)
	{
		PathState = TEXT("stable");
		for (const FWavePathLayer& Layer : WavePathLayers)
		{
			if (Layer.Generation == Result.SearchGeneration)
			{
				PathState = Layer.bWaveComplete ? TEXT("stable") : TEXT("wave");
				break;
			}
		}
	}

	const FString StatusTextValue = Result.bSuccess
		? FString::Printf(TEXT("%s %s | gen %d | expanded %d | frontier %d | path %d | %.2f ms"),
			*FSecondarySearchDebug::GetModeName(Result.Mode),
			*PathState,
			Result.SearchGeneration,
			Result.ExpandedCount,
			Result.FrontierNodes.Num(),
			Result.Path.Num(),
			Result.ElapsedMs)
		: FString::Printf(TEXT("%s %s | gen %d | expanded %d | frontier %d"),
			*FSecondarySearchDebug::GetModeName(Result.Mode),
			Result.FailureReason.IsEmpty() ? TEXT("searching") : *Result.FailureReason,
			Result.SearchGeneration,
			Result.ExpandedCount,
			Result.FrontierNodes.Num());

	StatusText->SetHiddenInGame(true);
	StatusText->SetText(FText::GetEmpty());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(913702, 0.12f, FColor::Cyan, StatusTextValue);
	}
}

void ASecondarySearchVisualizerActor::UpdateFluidAnimation(float DeltaSeconds, float WorldSeconds)
{
	const bool bFluidStyle = LastVisualStyle == ESecondarySearchVisualStyle::Fluid;
	const float Pulse = bFluidStyle ? FMath::Sin(WorldSeconds * 7.5f * CachedVisualSpeed) * 0.5f + 0.5f : 0.0f;

	if (FrontierMaterial)
	{
		FrontierMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse);
		FrontierMaterial->SetScalarParameterValue(TEXT("Opacity"), bFluidStyle ? 0.85f : 1.0f);
	}
	if (ExpandedMaterial)
	{
		ExpandedMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse * 0.35f);
		ExpandedMaterial->SetScalarParameterValue(TEXT("Opacity"), bFluidStyle ? 0.42f : 1.0f);
	}
	if (PathMaterial)
	{
		PathMaterial->SetScalarParameterValue(TEXT("FlowSpeed"), CachedVisualSpeed);
		PathMaterial->SetScalarParameterValue(TEXT("RevealProgress"), 1.0f);
		PathMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.95f);
	}

	if (bFluidStyle)
	{
		for (const FFluidNodeRecord& Record : FluidNodeRecords)
		{
			if (!Record.Component || Record.InstanceIndex == INDEX_NONE)
			{
				continue;
			}

			const float Age = FMath::Max(0.0f, WorldSeconds - Record.SpawnSeconds);
			const float Intro = FMath::Clamp(Age * 5.0f, 0.0f, 1.0f);
			const float NodePulse = Record.bFrontier ? (1.0f + Pulse * 0.22f) : 1.0f;
			const float Radius = Record.BaseRadius * FMath::Lerp(0.35f, NodePulse, Intro);
			Record.Component->UpdateInstanceTransform(
				Record.InstanceIndex,
				MakeDiskTransform(Record.Location, Radius, Record.Height, Record.ZOffset),
				true,
				false,
				true);
		}

	}
}

void ASecondarySearchVisualizerActor::UpdateWavePathLayers(float DeltaSeconds, float WorldSeconds)
{
	const float RetentionSeconds = 4.0f;
	for (int32 LayerIndex = WavePathLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		FWavePathLayer& Layer = WavePathLayers[LayerIndex];
		const bool bNewest = Layer.Generation == LastPathLayerGeneration;
		const float FadeAge = Layer.SupersededSeconds >= 0.0f ? WorldSeconds - Layer.SupersededSeconds : 0.0f;
		const float HistoryAlpha = Layer.SupersededSeconds >= 0.0f
			? FMath::Clamp(1.0f - FadeAge / RetentionSeconds, 0.0f, 1.0f)
			: 1.0f;

		if (!bNewest && (!bLastTrailsEnabled || HistoryAlpha <= 0.0f))
		{
			ReleaseWavePathLayer(LayerIndex);
			continue;
		}

		if (!Layer.bWaveComplete && DeltaSeconds > 0.0f)
		{
			const float TravelSeconds = FMath::Max(0.1f, 0.8f / CachedWaveSpeed);
			const float WaveSpeed = Layer.TotalDistance / TravelSeconds;
			Layer.WaveDistance = FMath::Min(Layer.TotalDistance, Layer.WaveDistance + DeltaSeconds * WaveSpeed);
			Layer.bWaveComplete = Layer.WaveDistance >= Layer.TotalDistance - KINDA_SMALL_NUMBER;
		}

		const float StablePulse = FMath::Sin((WorldSeconds - Layer.CreatedSeconds) * 3.25f + Layer.Generation * 0.61f) * 0.5f + 0.5f;
		const float BaseOpacity = bNewest ? (Layer.bWaveComplete ? 0.72f + StablePulse * 0.16f : 0.30f + StablePulse * 0.08f) : 0.16f * HistoryAlpha;
		const float WaveOpacity = (!Layer.bWaveComplete && bNewest) ? 1.0f : 0.0f;
		const float WakeOpacity = (!Layer.bWaveComplete && bNewest) ? 0.46f : 0.0f;
		if (Layer.BaseMaterial)
		{
			Layer.BaseMaterial->SetVectorParameterValue(TEXT("DebugColor"), bNewest
				? FLinearColor(1.0f, 0.42f + StablePulse * 0.12f, 0.04f, BaseOpacity)
				: FLinearColor(0.20f, 0.45f, 1.0f, BaseOpacity));
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("Opacity"), BaseOpacity);
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("RevealProgress"), 1.0f);
		}
		if (Layer.WaveMaterial)
		{
			const float Pulse = FMath::Sin(WorldSeconds * 12.0f * CachedWaveSpeed) * 0.5f + 0.5f;
			Layer.WaveMaterial->SetVectorParameterValue(TEXT("DebugColor"), FLinearColor(0.35f + Pulse * 0.25f, 0.95f, 1.0f, WaveOpacity));
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("Opacity"), WaveOpacity);
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("FlowSpeed"), CachedWaveSpeed);
		}
		if (Layer.WakeMaterial)
		{
			const float WakePulse = FMath::Sin(WorldSeconds * 8.0f * CachedWaveSpeed + Layer.Generation) * 0.5f + 0.5f;
			Layer.WakeMaterial->SetVectorParameterValue(TEXT("DebugColor"), FLinearColor(0.05f, 0.38f + WakePulse * 0.18f, 1.0f, WakeOpacity));
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("Opacity"), WakeOpacity);
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("FlowSpeed"), CachedWaveSpeed * 0.75f);
		}

		const float WaveWindow = FMath::Clamp(Layer.TotalDistance * 0.12f, 140.0f, 320.0f);
		const float WakeWindow = FMath::Clamp(Layer.TotalDistance * 0.28f, 280.0f, 760.0f);
		const float WaveStartDistance = Layer.WaveDistance - WaveWindow;
		const float WaveEndDistance = Layer.WaveDistance;
		const float WakeStartDistance = Layer.WaveDistance - WakeWindow;
		const float WakeEndDistance = Layer.WaveDistance - WaveWindow * 0.20f;
		for (FWavePathSegment& Segment : Layer.Segments)
		{
			if (Segment.BaseSpline)
			{
				Segment.BaseSpline->SetHiddenInGame(false);
			}

			const float SegmentStart = Segment.StartDistance;
			const float SegmentEnd = Segment.StartDistance + Segment.Length;
			const float WakeOverlapStart = FMath::Max(SegmentStart, WakeStartDistance);
			const float WakeOverlapEnd = FMath::Min(SegmentEnd, WakeEndDistance);
			const bool bShowWake = !Layer.bWaveComplete && bNewest && WakeOverlapEnd > WakeOverlapStart;
			if (bShowWake)
			{
				const float WakeStartAlpha = FMath::Clamp((WakeOverlapStart - SegmentStart) / Segment.Length, 0.0f, 1.0f);
				const float WakeEndAlpha = FMath::Clamp((WakeOverlapEnd - SegmentStart) / Segment.Length, 0.0f, 1.0f);
				SetSplineSegment(
					Segment.WakeSpline,
					FMath::Lerp(Segment.Start, Segment.End, WakeStartAlpha),
					FMath::Lerp(Segment.Start, Segment.End, WakeEndAlpha),
					6.0f,
					true);
			}
			else if (Segment.WakeSpline)
			{
				Segment.WakeSpline->SetHiddenInGame(true);
			}

			const float OverlapStart = FMath::Max(SegmentStart, WaveStartDistance);
			const float OverlapEnd = FMath::Min(SegmentEnd, WaveEndDistance);
			const bool bShowWave = !Layer.bWaveComplete && bNewest && OverlapEnd > OverlapStart;
			if (!bShowWave)
			{
				if (Segment.WaveSpline)
				{
					Segment.WaveSpline->SetHiddenInGame(true);
				}
				continue;
			}

			const float StartAlpha = FMath::Clamp((OverlapStart - SegmentStart) / Segment.Length, 0.0f, 1.0f);
			const float EndAlpha = FMath::Clamp((OverlapEnd - SegmentStart) / Segment.Length, 0.0f, 1.0f);
			SetSplineSegment(
				Segment.WaveSpline,
				FMath::Lerp(Segment.Start, Segment.End, StartAlpha),
				FMath::Lerp(Segment.Start, Segment.End, EndAlpha),
				8.0f,
				true);
		}
	}
}

void ASecondarySearchVisualizerActor::UpdateTargetPulse(float WorldSeconds)
{
	TargetMarker->ClearInstances();

	if (!bHasTargetLocation)
	{
		return;
	}

	const float BaseRadius = CachedTargetRadius * CachedNodeScale;
	const float Radius = BaseRadius + FMath::Sin(WorldSeconds * 6.0f) * FMath::Max(2.0f, BaseRadius * 0.18f);
	AddWorldInstance(TargetMarker, MakeSphereTransform(CachedTargetLocation, Radius, 120.0f));
}

void ASecondarySearchVisualizerActor::ResetRetainedState()
{
	FluidNodeRecords.Reset();
	ClearWavePathLayers();
	LastNodeBuildSeconds = 0.0f;
}

void ASecondarySearchVisualizerActor::ClearWavePathLayers()
{
	for (int32 LayerIndex = WavePathLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		ReleaseWavePathLayer(LayerIndex);
	}
	LastPathLayerGeneration = -1;
}

void ASecondarySearchVisualizerActor::ReleaseWavePathLayer(int32 LayerIndex)
{
	if (!WavePathLayers.IsValidIndex(LayerIndex))
	{
		return;
	}

	FWavePathLayer& Layer = WavePathLayers[LayerIndex];
	for (FWavePathSegment& Segment : Layer.Segments)
	{
		ReleasePathSpline(Segment.BaseSpline);
		ReleasePathSpline(Segment.WakeSpline);
		ReleasePathSpline(Segment.WaveSpline);
		Segment.BaseSpline = nullptr;
		Segment.WakeSpline = nullptr;
		Segment.WaveSpline = nullptr;
	}
	PathLayerMaterials.RemoveSingleSwap(Layer.BaseMaterial, EAllowShrinking::No);
	PathLayerMaterials.RemoveSingleSwap(Layer.WaveMaterial, EAllowShrinking::No);
	PathLayerMaterials.RemoveSingleSwap(Layer.WakeMaterial, EAllowShrinking::No);
	WavePathLayers.RemoveAt(LayerIndex, 1, EAllowShrinking::No);
}

void ASecondarySearchVisualizerActor::TrimWavePathHistory(int32 PathHistoryCount)
{
	const int32 MaxLayers = FMath::Clamp(PathHistoryCount, 1, 8);
	while (WavePathLayers.Num() > MaxLayers)
	{
		int32 OldestLayerIndex = 0;
		float OldestSeconds = WavePathLayers[0].CreatedSeconds;
		for (int32 Index = 1; Index < WavePathLayers.Num(); ++Index)
		{
			if (WavePathLayers[Index].CreatedSeconds < OldestSeconds)
			{
				OldestLayerIndex = Index;
				OldestSeconds = WavePathLayers[Index].CreatedSeconds;
			}
		}
		ReleaseWavePathLayer(OldestLayerIndex);
	}
}

UMaterialInstanceDynamic* ASecondarySearchVisualizerActor::CreateColorMaterial(UMaterialInterface* ParentMaterial, const FLinearColor& Color, const FString& Name)
{
	UMaterialInterface* ResolvedParent = ParentMaterial ? ParentMaterial : BaseMaterial.Get();
	UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(ResolvedParent, this, *Name);
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		DynamicMaterial->SetVectorParameterValue(TEXT("DebugColor"), Color);
		DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), Color.A);
		DynamicMaterial->SetScalarParameterValue(TEXT("Pulse"), 0.0f);
		DynamicMaterial->SetScalarParameterValue(TEXT("Age"), 0.0f);
		DynamicMaterial->SetScalarParameterValue(TEXT("RevealProgress"), 1.0f);
		DynamicMaterial->SetScalarParameterValue(TEXT("FlowSpeed"), 1.0f);
	}

	return DynamicMaterial;
}

USplineMeshComponent* ASecondarySearchVisualizerActor::AcquirePathSpline()
{
	if (FreePathSplines.Num() > 0)
	{
		USplineMeshComponent* Spline = FreePathSplines.Pop(EAllowShrinking::No);
		if (Spline)
		{
			Spline->SetHiddenInGame(true);
			return Spline;
		}
	}

	USplineMeshComponent* Spline = NewObject<USplineMeshComponent>(this, USplineMeshComponent::StaticClass());
	if (!Spline)
	{
		return nullptr;
	}

	Spline->SetMobility(EComponentMobility::Movable);
	Spline->SetupAttachment(SceneRoot);
	Spline->RegisterComponent();
	Spline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Spline->SetGenerateOverlapEvents(false);
	Spline->SetCanEverAffectNavigation(false);
	Spline->SetCastShadow(false);
	Spline->SetDepthPriorityGroup(bLastXRayEnabled ? SDPG_Foreground : SDPG_World);
	Spline->SetRenderCustomDepth(bLastXRayEnabled);
	Spline->SetHiddenInGame(true);

	PathSplinePool.Add(Spline);
	return Spline;
}

void ASecondarySearchVisualizerActor::ReleasePathSpline(USplineMeshComponent* Spline)
{
	if (!Spline)
	{
		return;
	}

	Spline->SetHiddenInGame(true);
	Spline->SetStartAndEnd(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, false);
	FreePathSplines.AddUnique(Spline);
}

void ASecondarySearchVisualizerActor::ConfigurePathSpline(USplineMeshComponent* Spline, UMaterialInterface* Material, float Radius, bool bWaveSpline) const
{
	if (!Spline)
	{
		return;
	}

	Spline->SetStaticMesh(CylinderMesh);
	Spline->SetMaterial(0, Material);
	Spline->SetForwardAxis(ESplineMeshAxis::Z);
	Spline->SetStartScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetEndScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Spline->SetGenerateOverlapEvents(false);
	Spline->SetCanEverAffectNavigation(false);
	Spline->SetCastShadow(false);
	Spline->SetDepthPriorityGroup(bLastXRayEnabled ? SDPG_Foreground : SDPG_World);
	Spline->SetRenderCustomDepth(bLastXRayEnabled);
	if (bWaveSpline)
	{
		Spline->SetBoundsScale(1.25f);
	}
}

void ASecondarySearchVisualizerActor::SetSplineSegment(USplineMeshComponent* Spline, const FVector& Start, const FVector& End, float Radius, bool bVisible) const
{
	if (!Spline)
	{
		return;
	}

	const FVector Segment = End - Start;
	const float Length = Segment.Size();
	if (!bVisible || Length <= KINDA_SMALL_NUMBER)
	{
		Spline->SetHiddenInGame(true);
		return;
	}

	const FVector Tangent = Segment.GetSafeNormal() * FMath::Min(Length * 0.45f, 120.0f);
	Spline->SetStartScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetEndScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetStartAndEnd(Start, Tangent, End, Tangent, true);
	Spline->SetHiddenInGame(false);
}

int32 ASecondarySearchVisualizerActor::AddWorldInstance(UInstancedStaticMeshComponent* Component, const FTransform& WorldTransform) const
{
	if (!Component)
	{
		return INDEX_NONE;
	}

	return Component->AddInstance(WorldTransform, true);
}

FTransform ASecondarySearchVisualizerActor::MakeDiskTransform(const FVector& Location, float Radius, float Height, float ZOffset) const
{
	const FVector Scale(Radius / 50.0f, Radius / 50.0f, Height / 100.0f);
	return FTransform(FRotator::ZeroRotator, Location + FVector(0.0f, 0.0f, ZOffset), Scale);
}

FTransform ASecondarySearchVisualizerActor::MakeSphereTransform(const FVector& Location, float Radius, float ZOffset) const
{
	const FVector Scale(Radius / 50.0f);
	return FTransform(FRotator::ZeroRotator, Location + FVector(0.0f, 0.0f, ZOffset), Scale);
}

FTransform ASecondarySearchVisualizerActor::MakeTubeTransform(const FVector& Start, const FVector& End, float Radius, float ZOffset) const
{
	const FVector RaisedStart = Start + FVector(0.0f, 0.0f, ZOffset);
	const FVector RaisedEnd = End + FVector(0.0f, 0.0f, ZOffset);
	const FVector Segment = RaisedEnd - RaisedStart;
	const float Length = Segment.Size();

	if (Length <= KINDA_SMALL_NUMBER)
	{
		return FTransform(FRotator::ZeroRotator, RaisedStart, FVector::ZeroVector);
	}

	const FVector Direction = Segment / Length;
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);
	const FVector Scale(Radius / 50.0f, Radius / 50.0f, Length / 100.0f);

	return FTransform(Rotation, (RaisedStart + RaisedEnd) * 0.5f, Scale);
}
