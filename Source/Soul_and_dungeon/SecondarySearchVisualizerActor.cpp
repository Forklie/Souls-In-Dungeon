#include "SecondarySearchVisualizerActor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
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
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	BaseGridNodes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BaseGridNodes"));
	ExpandedNodes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ExpandedNodes"));
	FrontierNodes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FrontierNodes"));
	StartMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StartMarker"));
	GoalMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GoalMarker"));
	TargetMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TargetMarker"));
	PathSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathSegments"));
	PreviewPathSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PreviewPathSegments"));
	BFSPathSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BFSPathSegments"));
	UCSPathSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("UCSPathSegments"));
	AStarPathSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AStarPathSegments"));

	BaseGridNodes->SetupAttachment(SceneRoot);
	ExpandedNodes->SetupAttachment(SceneRoot);
	FrontierNodes->SetupAttachment(SceneRoot);
	StartMarker->SetupAttachment(SceneRoot);
	GoalMarker->SetupAttachment(SceneRoot);
	TargetMarker->SetupAttachment(SceneRoot);
	PathSegments->SetupAttachment(SceneRoot);
	PreviewPathSegments->SetupAttachment(SceneRoot);
	BFSPathSegments->SetupAttachment(SceneRoot);
	UCSPathSegments->SetupAttachment(SceneRoot);
	AStarPathSegments->SetupAttachment(SceneRoot);

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

	ConfigureComponent(BaseGridNodes, CylinderMesh);
	ConfigureComponent(ExpandedNodes, CylinderMesh);
	ConfigureComponent(FrontierNodes, CylinderMesh);
	ConfigureComponent(StartMarker, SphereMesh);
	ConfigureComponent(GoalMarker, SphereMesh);
	ConfigureComponent(TargetMarker, CylinderMesh);
	ConfigureComponent(PathSegments, CylinderMesh);
	ConfigureComponent(PreviewPathSegments, CylinderMesh);
	ConfigureComponent(BFSPathSegments, CylinderMesh);
	ConfigureComponent(UCSPathSegments, CylinderMesh);
	ConfigureComponent(AStarPathSegments, CylinderMesh);

	SetActorHiddenInGame(true);
}

void ASecondarySearchVisualizerActor::BeginPlay()
{
	Super::BeginPlay();

	BaseGridMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.15f, 0.68f, 1.0f, 0.82f), TEXT("BaseGridMaterial"));
	ExpandedMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.04f, 0.18f, 0.85f, 0.65f), TEXT("ExpandedMaterial"));
	FrontierMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.0f, 0.95f, 1.0f, 0.92f), TEXT("FrontierMaterial"));
	StartMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.2f, 1.0f, 0.4f, 1.0f), TEXT("StartMaterial"));
	GoalMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(1.0f, 0.95f, 0.05f, 1.0f), TEXT("GoalMaterial"));
	TargetMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.5f, 0.5f, 0.5f, 1.0f), TEXT("TargetMaterial"));
	PathMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.08f, 0.45f, 0.95f, 0.32f), TEXT("PathMaterial"));
	PreviewPathMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.0f, 1.0f, 0.15f, 1.0f), TEXT("PreviewPathMaterial"));
	BFSMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(1.0f, 0.0f, 1.0f, 0.6f), TEXT("BFSMaterial"));
	UCSMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(1.0f, 1.0f, 0.0f, 0.6f), TEXT("UCSMaterial"));
	AStarMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.0f, 1.0f, 1.0f, 0.6f), TEXT("AStarMaterial"));

	BaseGridNodes->SetMaterial(0, BaseGridMaterial);
	ExpandedNodes->SetMaterial(0, ExpandedMaterial);
	FrontierNodes->SetMaterial(0, FrontierMaterial);
	StartMarker->SetMaterial(0, StartMaterial);
	GoalMarker->SetMaterial(0, GoalMaterial);
	TargetMarker->SetMaterial(0, TargetMaterial);
	PathSegments->SetMaterial(0, PathMaterial);
	PreviewPathSegments->SetMaterial(0, PreviewPathMaterial);
	BFSPathSegments->SetMaterial(0, BFSMaterial);
	UCSPathSegments->SetMaterial(0, UCSMaterial);
	AStarPathSegments->SetMaterial(0, AStarMaterial);

	if (BaseGridMaterial)
	{
		BaseGridMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), 1.8f);
		BaseGridMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), 0.55f);
	}
}

void ASecondarySearchVisualizerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsHidden())
	{
		return;
	}

	const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	UpdateFluidAnimation(DeltaSeconds, WorldSeconds);
	UpdatePathLayers(DeltaSeconds, WorldSeconds);
	UpdateTargetPulse(DeltaSeconds, WorldSeconds);
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
	float NodeScale,
	bool bShowBaseGrid,
	float TargetSmoothing,
	float NodePulse,
	float NodeFadeTime,
	float PathFadeTime,
	bool bLastPathFallback,
	ESecondarySearchVisualQuality VisualQuality,
	float GlowIntensity,
	float FlowBandWidth,
	float NodeSoftness)
{
#if !UE_BUILD_SHIPPING
	if (Result.StartLocation.IsNearlyZero() && Result.GoalLocation.IsNearlyZero() &&
		Result.Path.Num() == 0 && Result.ExpandedNodes.Num() == 0 && Result.FrontierNodes.Num() == 0 &&
		Result.SampledNodes.Num() == 0)
	{
		return;
	}

	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);

	const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : Result.DebugStartSeconds;
	CachedVisualSpeed = FMath::Max(0.1f, VisualSpeed);
	CachedWaveSpeed = FMath::Max(0.1f, WaveSpeed);
	CachedNodeScale = FMath::Clamp(NodeScale, 0.25f, 1.25f);
	CachedTargetRadius = FMath::Max(1.0f, Settings.DebugTargetRadius * 0.6f);
	CachedPathRevealSpeed = FMath::Max(100.0f, Settings.FluidPathRevealSpeed);
	CachedNodePulse = FMath::Clamp(NodePulse, 0.0f, 3.0f);
	CachedNodeFadeTime = FMath::Clamp(NodeFadeTime, 0.2f, 5.0f);
	CachedPathFadeTime = FMath::Clamp(PathFadeTime, 0.5f, 10.0f);
	CachedGlowIntensity = FMath::Clamp(GlowIntensity, 0.0f, 3.0f);
	CachedFlowBandWidth = FMath::Clamp(FlowBandWidth, 0.05f, 0.5f);
	CachedNodeSoftness = FMath::Clamp(NodeSoftness, 0.1f, 1.0f);
	CachedTargetSmoothing = FMath::Clamp(TargetSmoothing, 1.0f, 40.0f);
	CachedTargetZOffset = Settings.DebugPointZOffset;
	CachedPathTubeRadius = Settings.PathTubeRadius;
	CachedVisualQuality = ESecondarySearchVisualQuality::High;
	bCachedLastPathFallback = bLastPathFallback;

	const int32 NodeCap = FMath::Max(0, MaxVisibleNodes > 0 ? MaxVisibleNodes : Settings.MaxDebugDrawNodes);
	const bool bNeedsStructuralRebuild =
		LastMaxVisibleNodes != NodeCap ||
		LastSampledDrawCount != Result.SampledNodes.Num() ||
		LastVisualStyle != VisualStyle ||
		bLastShowBaseGrid != bShowBaseGrid ||
		bLastXRayEnabled != bXRayEnabled ||
		!FMath::IsNearlyEqual(LastNodeScale, CachedNodeScale) ||
		!FMath::IsNearlyEqual(LastCellSize, Settings.CellSize);

	if (bNeedsStructuralRebuild)
	{
		LastVisualStyle = VisualStyle;
		bLastShowBaseGrid = bShowBaseGrid;
		bLastXRayEnabled = bXRayEnabled;
		ApplyDepthPriority(bXRayEnabled);
		UpdateBaseGridInstances(Result, Settings, NodeCap);
		LastMaxVisibleNodes = NodeCap;
		LastSampledDrawCount = Result.SampledNodes.Num();
		LastNodeScale = CachedNodeScale;
		LastCellSize = Settings.CellSize;
	}

	if (LastVisualizationRevision != Result.VisualizationRevision || LastSearchGeneration != Result.SearchGeneration)
	{
		UpdateRetainedNodeInstances(Result, Settings, NodeCap, WorldSeconds);
		LastVisualizationRevision = Result.VisualizationRevision;
		LastSearchGeneration = Result.SearchGeneration;
		LastNodeUpdateSeconds = WorldSeconds;
	}

	if (!Result.StartLocation.IsNearlyZero())
	{
		DesiredStartLocation = Result.StartLocation;
		if (!bHasSmoothedStartLocation)
		{
			SmoothedStartLocation = DesiredStartLocation;
			bHasSmoothedStartLocation = true;
		}
		bHasStartLocation = true;
	}
	if (!Result.CurrentTarget.IsNearlyZero())
	{
		DesiredTargetLocation = Result.CurrentTarget;
		if (!bHasSmoothedTargetLocation)
		{
			SmoothedTargetLocation = DesiredTargetLocation;
			bHasSmoothedTargetLocation = true;
		}
		bHasTargetLocation = true;
	}
	if (!Result.GoalLocation.IsNearlyZero())
	{
		DesiredGoalLocation = Result.GoalLocation;
		if (!bHasSmoothedGoalLocation)
		{
			SmoothedGoalLocation = DesiredGoalLocation;
			bHasSmoothedGoalLocation = true;
		}
		bHasGoalLocation = true;
	}

	UpdateEndpointMarkers(Result, Settings);
	if (VisualStyle == ESecondarySearchVisualStyle::Simple)
	{
		ClearPathLayers();
		UpdateSimplePathInstances(Result, Settings);
	}
	else
	{
		PathSegments->ClearInstances();
		const TArray<FVector>& PathToShow = (Result.Path.Num() > 1 || !bLastPathFallback) ? Result.Path : LastSuccessfulPath;
		if (PathToShow.Num() > 1 && (Result.bSuccess || bLastPathFallback) &&
			(LastPathLayerGeneration != Result.SearchGeneration || !ArePathsEquivalent(PathToShow, LastSuccessfulPath)))
		{
			AddPathLayer(PathToShow, Result.SearchGeneration, Settings, WorldSeconds, false);
			LastPathLayerGeneration = Result.SearchGeneration;
			LastSuccessfulPath = PathToShow;
		}
		TrimPathHistory(PathHistoryCount);
	}

	UpdatePreviewPathInstances(Result, Settings, WorldSeconds, bTrailsEnabled);
	
	// Update algorithm paths with normal debug-line weight.
	BFSPathSegments->ClearInstances();
	for (int32 i = 1; i < Result.BFSPath.Num(); ++i) {
		AddWorldInstance(BFSPathSegments, MakeTubeTransform(Result.BFSPath[i-1], Result.BFSPath[i], Settings.PathTubeRadius * 0.15f, Settings.DebugPointZOffset + 32.0f));
	}
	
	UCSPathSegments->ClearInstances();
	for (int32 i = 1; i < Result.UCSPath.Num(); ++i) {
		AddWorldInstance(UCSPathSegments, MakeTubeTransform(Result.UCSPath[i-1], Result.UCSPath[i], Settings.PathTubeRadius * 0.15f, Settings.DebugPointZOffset + 38.0f));
	}
	
	AStarPathSegments->ClearInstances();
	for (int32 i = 1; i < Result.AStarPath.Num(); ++i) {
		AddWorldInstance(AStarPathSegments, MakeTubeTransform(Result.AStarPath[i-1], Result.AStarPath[i], Settings.PathTubeRadius * 0.15f, Settings.DebugPointZOffset + 44.0f));
	}

	bLastTrailsEnabled = bTrailsEnabled;
	if (!bTrailsEnabled)
	{
		TrimPathHistory(1);
	}

	UpdateStatusHud(Result);
#endif
}

void ASecondarySearchVisualizerActor::ClearVisualization()
{
	BaseGridNodes->ClearInstances();
	ExpandedNodes->ClearInstances();
	FrontierNodes->ClearInstances();
	StartMarker->ClearInstances();
	GoalMarker->ClearInstances();
	TargetMarker->ClearInstances();
	PathSegments->ClearInstances();
	PreviewPathSegments->ClearInstances();
	BFSPathSegments->ClearInstances();
	UCSPathSegments->ClearInstances();
	AStarPathSegments->ClearInstances();
	ClearPathLayers();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(913702, 0.01f, FColor::White, FString());
	}
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	bHasTargetLocation = false;
	bHasSmoothedTargetLocation = false;
	bHasStartLocation = false;
	bHasSmoothedStartLocation = false;
	bHasGoalLocation = false;
	bHasSmoothedGoalLocation = false;
	ResetRetainedState();
	LastSuccessfulPath.Reset();
	LastPreviewPath.Reset();
	LastSampledNodes.Reset();
	LastVisualizationRevision = -1;
	LastSearchGeneration = -1;
	LastPathLayerGeneration = -1;
	LastPreviewLayerGeneration = -1;
	LastSampledDrawCount = -1;
	LastMaxVisibleNodes = -1;
	LastPathPointCount = -1;
	LastPreviewPathPointCount = -1;
	LastNodeScale = -1.0f;
	LastCellSize = -1.0f;
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
		BaseGridNodes,
		ExpandedNodes,
		FrontierNodes,
		StartMarker,
		GoalMarker,
		TargetMarker,
		PathSegments,
		PreviewPathSegments,
		BFSPathSegments,
		UCSPathSegments,
		AStarPathSegments,
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

void ASecondarySearchVisualizerActor::UpdateBaseGridInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, int32 NodeCap)
{
	BaseGridNodes->ClearInstances();
	ExpandedNodes->ClearInstances();
	FrontierNodes->ClearInstances();
	RetainedNodes.Reset();
	RetainedNodeKeys.Reset();
	AnimatedAtomKeys.Reset();

	const int32 DrawCount = FMath::Min(Result.SampledNodes.Num(), NodeCap);
	RetainedNodes.Reserve(DrawCount);
	RetainedNodeKeys.Reserve(DrawCount);

	const float BaseRadius = FMath::Max(4.2f, Settings.DebugExpandedNodeRadius * 0.58f * CachedNodeScale);
	const float ExpandedRadius = FMath::Max(5.0f, Settings.DebugExpandedNodeRadius * CachedNodeScale);
	const float FrontierRadius = FMath::Max(6.0f, Settings.DebugFrontierNodeRadius * CachedNodeScale);

	for (int32 Index = 0; Index < DrawCount; ++Index)
	{
		const FVector& Location = Result.SampledNodes[Index];
		const FIntPoint Key = MakeNodeKey(Location, Settings.CellSize);
		if (RetainedNodes.Contains(Key))
		{
			continue;
		}

		FRetainedNodeRecord Record;
		Record.Location = Location;
		Record.BaseRadius = BaseRadius;
		Record.BaseHeight = 2.2f;
		Record.BaseZOffset = Settings.DebugPointZOffset * 0.55f;
		Record.ExpandedRadius = ExpandedRadius;
		Record.FrontierRadius = FrontierRadius;
		Record.ExpandedHeight = Settings.DebugNodeHeight;
		Record.FrontierHeight = Settings.DebugFrontierNodeHeight;
		Record.ExpandedZOffset = Settings.DebugPointZOffset;
		Record.FrontierZOffset = Settings.DebugPointZOffset + 5.0f;
		Record.SequenceIndex = Index;
		Record.BaseInstanceIndex = AddWorldInstance(BaseGridNodes, bLastShowBaseGrid
			? MakeDiskTransform(Location, Record.BaseRadius, Record.BaseHeight, Record.BaseZOffset)
			: MakeHiddenTransform());
		Record.ExpandedInstanceIndex = AddWorldInstance(ExpandedNodes, MakeHiddenTransform());
		Record.FrontierInstanceIndex = AddWorldInstance(FrontierNodes, MakeHiddenTransform());
		RetainedNodeKeys.Add(Key);
		RetainedNodes.Add(Key, Record);
	}

	LastSampledNodes = Result.SampledNodes;
}

void ASecondarySearchVisualizerActor::UpdateRetainedNodeInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, int32 NodeCap, float WorldSeconds)
{
	const int32 ExpandedDrawCount = FMath::Min(Result.ExpandedNodes.Num(), NodeCap);
	for (int32 Index = 0; Index < ExpandedDrawCount; ++Index)
	{
		const FIntPoint Key = FindRetainedNodeKeyNear(Result.ExpandedNodes[Index], Settings.CellSize);
		FRetainedNodeRecord* Record = RetainedNodes.Find(Key);
		if (!Record)
		{
			continue;
		}

		const bool bStateChanged = Record->State != ERetainedNodeVisualState::Expanded;
		Record->State = ERetainedNodeVisualState::Expanded;
		Record->LastTouchedGeneration = Result.SearchGeneration;
		Record->LastTouchedSeconds = WorldSeconds;
		Record->StateChangeSeconds = bStateChanged ? WorldSeconds - Index * 0.002f : Record->StateChangeSeconds;
		Record->TargetVisualBlend = 1.0f;
		if (bStateChanged)
		{
			// Do NOT fire an immediate ripple pop — let the S-curve blend handle the rise smoothly
		}
		Record->SequenceIndex = Index;

		BaseGridNodes->UpdateInstanceTransform(
			Record->BaseInstanceIndex,
			bLastShowBaseGrid ? MakeDiskTransform(Record->Location, Record->BaseRadius, Record->BaseHeight, Record->BaseZOffset) : MakeHiddenTransform(),
			true,
			false,
			true);
		ExpandedNodes->UpdateInstanceTransform(
			Record->ExpandedInstanceIndex,
			MakeDiskTransform(Record->Location, FMath::Max(1.0f, Record->ExpandedRadius * FMath::Max(0.2f, Record->VisualBlend)), Record->ExpandedHeight, Record->ExpandedZOffset),
			true,
			false,
			true);
		FrontierNodes->UpdateInstanceTransform(Record->FrontierInstanceIndex, MakeHiddenTransform(), true, false, true);
	}

	const int32 FrontierDrawCount = FMath::Min(Result.FrontierNodes.Num(), NodeCap);
	for (int32 Index = 0; Index < FrontierDrawCount; ++Index)
	{
		const FIntPoint Key = FindRetainedNodeKeyNear(Result.FrontierNodes[Index], Settings.CellSize);
		FRetainedNodeRecord* Record = RetainedNodes.Find(Key);
		if (!Record)
		{
			continue;
		}

		const bool bStateChanged = Record->State != ERetainedNodeVisualState::Frontier;
		Record->State = ERetainedNodeVisualState::Frontier;
		Record->LastTouchedGeneration = Result.SearchGeneration;
		Record->LastTouchedSeconds = WorldSeconds;
		Record->StateChangeSeconds = bStateChanged ? WorldSeconds - Index * 0.003f : Record->StateChangeSeconds;
		Record->TargetVisualBlend = 1.0f;
		if (bStateChanged)
		{
			// Do NOT fire an immediate ripple pop — let the S-curve blend handle the rise smoothly
		}
		Record->SequenceIndex = Index;

		BaseGridNodes->UpdateInstanceTransform(
			Record->BaseInstanceIndex,
			bLastShowBaseGrid ? MakeDiskTransform(Record->Location, Record->BaseRadius, Record->BaseHeight, Record->BaseZOffset) : MakeHiddenTransform(),
			true,
			false,
			true);
		ExpandedNodes->UpdateInstanceTransform(Record->ExpandedInstanceIndex, MakeHiddenTransform(), true, false, true);
		FrontierNodes->UpdateInstanceTransform(
			Record->FrontierInstanceIndex,
			MakeDiskTransform(Record->Location, FMath::Max(1.0f, Record->FrontierRadius * FMath::Max(0.2f, Record->VisualBlend)), Record->FrontierHeight, Record->FrontierZOffset),
			true,
			false,
			true);
	}

	BaseGridNodes->MarkRenderStateDirty();
	ExpandedNodes->MarkRenderStateDirty();
	FrontierNodes->MarkRenderStateDirty();
}

void ASecondarySearchVisualizerActor::UpdateEndpointMarkers(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings)
{
	StartMarker->ClearInstances();
	GoalMarker->ClearInstances();
	if (!Result.StartLocation.IsNearlyZero())
	{
		const FVector StartLocation = bHasSmoothedStartLocation ? SmoothedStartLocation : Result.StartLocation;
		AddWorldInstance(StartMarker, MakeSphereTransform(StartLocation, Settings.DebugEndpointRadius * 0.45f * CachedNodeScale, Settings.DebugPointZOffset + 18.0f));
	}
	if (!Result.GoalLocation.IsNearlyZero())
	{
		const FVector GoalLocation = bHasSmoothedGoalLocation ? SmoothedGoalLocation : Result.GoalLocation;
		AddWorldInstance(GoalMarker, MakeSphereTransform(GoalLocation, Settings.DebugEndpointRadius * 0.45f * CachedNodeScale, Settings.DebugPointZOffset + 18.0f));
	}
}

void ASecondarySearchVisualizerActor::UpdateSimplePathInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings)
{
	PathSegments->ClearInstances();
	const TArray<FVector>& PathToShow = Result.Path.Num() > 1 ? Result.Path : LastSuccessfulPath;
	for (int32 Index = 1; Index < PathToShow.Num(); ++Index)
	{
		AddWorldInstance(PathSegments, MakeTubeTransform(PathToShow[Index - 1], PathToShow[Index], Settings.PathTubeRadius * 0.22f, Settings.DebugPointZOffset + 18.0f));
	}
}

void ASecondarySearchVisualizerActor::UpdatePreviewPathInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, float WorldSeconds, bool bTrailsEnabled)
{
	const TArray<FVector>& PreviewToShow = Result.PreviewPath.Num() > 1 ? Result.PreviewPath : LastPreviewPath;
	if (PreviewToShow.Num() > 1 && !ArePathsEquivalent(PreviewToShow, LastPreviewPath))
	{
		if (!bTrailsEnabled)
		{
			for (int32 LayerIndex = PathLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
			{
				if (PathLayers[LayerIndex].bPreview)
				{
					ReleasePathLayer(PathLayers[LayerIndex]);
					PathLayers.RemoveAtSwap(LayerIndex, 1, EAllowShrinking::No);
				}
			}
		}
		AddPathLayer(PreviewToShow, Result.SearchGeneration, Settings, WorldSeconds, true);
		LastPreviewPath = PreviewToShow;
		LastPreviewLayerGeneration = Result.SearchGeneration;
	}
}

void ASecondarySearchVisualizerActor::UpdateStatusHud(const FSecondarySearchResult& Result) const
{
	if (!GEngine)
	{
		return;
	}

	const FString Phase = Result.bSuccess ? TEXT("stable") : (Result.FailureReason.IsEmpty() ? TEXT("searching") : TEXT("fallback"));
	const FString Message = FString::Printf(
		TEXT("Secondary Search: %s | Gen %d | %s | Base %d"),
		*FSecondarySearchDebug::GetModeName(Result.Mode),
		Result.SearchGeneration,
		*Phase,
		RetainedNodes.Num());
	
	// Main status line
	GEngine->AddOnScreenDebugMessage(913702, 0.18f, FColor(180, 220, 255), Message);

	// Legend lines with metrics
	const float LegendDuration = 0.18f;
	GEngine->AddOnScreenDebugMessage(913703, LegendDuration, FColor(100, 100, 115), TEXT("  [Grey] CURRENT PATH"));
	GEngine->AddOnScreenDebugMessage(913704, LegendDuration, FColor(50, 255, 100), TEXT("  [Green] CURVED PREVIEW"));
	
	GEngine->AddOnScreenDebugMessage(913705, LegendDuration, FColor::Cyan, FString::Printf(
		TEXT("  [Cyan] A* SEARCH | Exp: %d | Time: %.2fms | Cost: %.0f"), 
		Result.AStarCount, Result.AStarMs, Result.AStarCost));
	
	GEngine->AddOnScreenDebugMessage(913706, LegendDuration, FColor::Yellow, FString::Printf(
		TEXT("  [Yellow] UCS SEARCH | Exp: %d | Time: %.2fms | Cost: %.0f"), 
		Result.UCSCount, Result.UCSMs, Result.UCSCost));
	
	GEngine->AddOnScreenDebugMessage(913707, LegendDuration, FColor::Magenta, FString::Printf(
		TEXT("  [Magenta] BFS SEARCH | Exp: %d | Time: %.2fms | Cost: %.0f"), 
		Result.BFSCount, Result.BFSMs, Result.BFSCost));
}

void ASecondarySearchVisualizerActor::UpdateFluidAnimation(float DeltaSeconds, float WorldSeconds)
{
	const float QualityScale = GetQualityScale();
	if (BaseGridMaterial)
	{
		BaseGridMaterial->SetVectorParameterValue(TEXT("DebugColor"), FLinearColor(0.05f, 0.05f, 0.1f, 1.0f));
		BaseGridMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.46f + CachedGlowIntensity * 0.04f);
		BaseGridMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.36f);
		BaseGridMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), CachedNodeSoftness);
		BaseGridMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), 0.55f * CachedGlowIntensity);
		BaseGridMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * 0.08f);
	}
	if (FrontierMaterial)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(WorldSeconds * 2.0f * CachedVisualSpeed);
		FrontierMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse * CachedNodePulse);
		FrontierMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.78f);
		FrontierMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.86f);
		FrontierMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), CachedNodeSoftness);
		FrontierMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), 0.95f * CachedGlowIntensity);
		FrontierMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed * 0.55f);
	}
	if (ExpandedMaterial)
	{
		ExpandedMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.48f);
		ExpandedMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.42f);
		ExpandedMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), CachedNodeSoftness);
		ExpandedMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), 0.5f * CachedGlowIntensity);
		ExpandedMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * 0.12f);
	}

	if (CachedVisualQuality == ESecondarySearchVisualQuality::Low)
	{
		return;
	}

	// --- Player Water Interaction: apply atom displacement ONLY to Base state nodes ---
	bool bDirty = false;
	if (bLastShowBaseGrid && bHasSmoothedTargetLocation)
	{
		const float InteractionRadius = 240.0f;
		const float InteractionRadiusSq = FMath::Square(InteractionRadius);
		
		// Reset nodes that were displaced last frame but are no longer in range
		for (const FIntPoint& PreviousKey : AnimatedAtomKeys)
		{
			if (FRetainedNodeRecord* Record = RetainedNodes.Find(PreviousKey))
			{
				const float DistSq = FVector::DistSquared2D(Record->Location, SmoothedTargetLocation);
				if (DistSq >= InteractionRadiusSq)
				{
					Record->AtomOffset = FMath::VInterpTo(Record->AtomOffset, FVector::ZeroVector, DeltaSeconds, 7.0f);
					if (Record->State == ERetainedNodeVisualState::Base)
					{
						BaseGridNodes->UpdateInstanceTransform(
							Record->BaseInstanceIndex,
							MakeDiskTransform(Record->Location + Record->AtomOffset, Record->BaseRadius, Record->BaseHeight, Record->BaseZOffset),
							true, false, true);
						bDirty = true;
					}
					if (Record->AtomOffset.IsNearlyZero(0.5f))
					{
						Record->AtomOffset = FVector::ZeroVector;
						Record->bWasAtomAnimated = false;
					}
				}
			}
		}
		AnimatedAtomKeys.Reset();

		// Apply water displacement to Base nodes near the player
		for (auto& Pair : RetainedNodes)
		{
			FRetainedNodeRecord& Rec = Pair.Value;
			if (Rec.State != ERetainedNodeVisualState::Base)
			{
				continue;
			}
			const float DistSq = FVector::DistSquared2D(Rec.Location, SmoothedTargetLocation);
			if (DistSq >= InteractionRadiusSq && Rec.AtomOffset.IsNearlyZero(0.5f))
			{
				continue;
			}
			FVector TargetAtomOffset = FVector::ZeroVector;
			if (DistSq < InteractionRadiusSq)
			{
				const float Dist = FMath::Sqrt(DistSq);
				const float Alpha = 1.0f - (Dist / InteractionRadius);
				const float PushStrength = Alpha * Alpha * 30.0f; // quadratic falloff for natural feel
				const FVector PushDir = (Rec.Location - SmoothedTargetLocation).GetSafeNormal2D();
				TargetAtomOffset = PushDir * PushStrength + FVector(0.0f, 0.0f, PushStrength * 0.5f);
			}
			Rec.AtomOffset = FMath::VInterpTo(Rec.AtomOffset, TargetAtomOffset, DeltaSeconds, 8.0f);
			Rec.bWasAtomAnimated = true;
			AnimatedAtomKeys.Add(Pair.Key);
			BaseGridNodes->UpdateInstanceTransform(
				Rec.BaseInstanceIndex,
				MakeDiskTransform(Rec.Location + Rec.AtomOffset, Rec.BaseRadius, Rec.BaseHeight, Rec.BaseZOffset),
				true, false, true);
			bDirty = true;
		}
	}
	else
	{
		AnimatedAtomKeys.Reset();
	}

	for (const FIntPoint& Key : RetainedNodeKeys)
	{
		FRetainedNodeRecord* Record = RetainedNodes.Find(Key);
		if (!Record)
		{
			continue;
		}

		// Skip entirely if this is a base node not near the player and not fading
		if (Record->State == ERetainedNodeVisualState::Base)
		{
			continue;
		}

		const float Age = FMath::Max(0.0f, WorldSeconds - Record->StateChangeSeconds);
		const float FadeAlpha = SmoothStep01(Age / CachedNodeFadeTime);
		// Use a constant-rate progression for the underlying blend state.
		// Combined with EaseInOutSine below, this creates a smooth S-curve transition in time (Rise and Fall).
		const float TransitionSpeed = 2.8f * CachedVisualSpeed;
		if (Record->VisualBlend < Record->TargetVisualBlend)
		{
			Record->VisualBlend = FMath::Min(Record->TargetVisualBlend, Record->VisualBlend + DeltaSeconds * TransitionSpeed);
		}
		else
		{
			Record->VisualBlend = FMath::Max(Record->TargetVisualBlend, Record->VisualBlend - DeltaSeconds * TransitionSpeed);
		}

		const float TransitionAlpha = EaseInOutSine(Record->VisualBlend);
		const float RippleAge = FMath::Max(0.0f, WorldSeconds - Record->LastRippleSeconds);
		const float RippleFalloff = (1.0f - SmoothStep01(RippleAge / 0.45f)) * Record->RippleStrength;
		if (RippleAge > 0.65f)
		{
			Record->RippleStrength = 0.0f;
		}
		const float BaseFreq = 2.1f * CachedVisualSpeed;
		const float Phase = (Record->SequenceIndex % 17) * 0.37f;
		
		// Fluid multi-octave breathing for more organic feel
		const float SineA = FMath::Sin(WorldSeconds * BaseFreq + Phase);
		const float SineB = FMath::Sin(WorldSeconds * BaseFreq * 0.45f + Phase * 0.5f);
		const float Breathing = 1.0f + 0.045f * CachedNodePulse * QualityScale * (SineA * 0.75f + SineB * 0.25f);
		
		const float RipplePulse = 0.35f * RippleFalloff * FMath::Sin(RippleAge * 12.0f + Phase);
		
		// Vertical fluid wobble (up and down motion) - Ultra-smooth timing
		const float BobFreq = 0.75f * CachedVisualSpeed;
		const float RawSineA = FMath::Sin(WorldSeconds * BobFreq + Phase);
		const float RawSineB = FMath::Sin(WorldSeconds * BobFreq * 0.48f + Phase * 1.3f);
		
		// Apply SmoothStep logic to the sine waves to flatten peaks and smoothen the 'lower to upper' transition
		const float SmoothSineA = RawSineA * RawSineA * (3.0f - 2.0f * FMath::Abs(RawSineA)) * FMath::Sign(RawSineA);
		const float SmoothSineB = RawSineB * RawSineB * (3.0f - 2.0f * FMath::Abs(RawSineB)) * FMath::Sign(RawSineB);
		
		const float VerticalWobble = (SmoothSineA * 3.0f + SmoothSineB * 1.2f) * CachedNodePulse * QualityScale * TransitionAlpha;

		if (bLastShowBaseGrid)
		{
			BaseGridNodes->UpdateInstanceTransform(
				Record->BaseInstanceIndex,
				MakeDiskTransform(Record->Location + Record->AtomOffset, Record->BaseRadius * (1.0f + RipplePulse * 0.15f), Record->BaseHeight, Record->BaseZOffset + RipplePulse * 15.0f),
				true,
				false,
				true);
			bDirty = true;
		}

		if (Record->State == ERetainedNodeVisualState::Frontier && ShouldAnimateRetainedNode(*Record, WorldSeconds))
		{
			const float Pop = 1.0f + 0.08f * (1.0f - EaseOutCubic(FMath::Min(1.0f, Age * 4.0f)));
			const float Radius = FMath::Lerp(Record->BaseRadius * 0.85f, Record->FrontierRadius * Breathing * Pop, TransitionAlpha);
			FrontierNodes->UpdateInstanceTransform(
				Record->FrontierInstanceIndex,
				MakeDiskTransform(Record->Location, Radius, Record->FrontierHeight, FMath::Lerp(Record->BaseZOffset, Record->FrontierZOffset * 0.75f, TransitionAlpha) + VerticalWobble),
				true,
				false,
				true);
			bDirty = true;
		}
		else if (Record->State == ERetainedNodeVisualState::Expanded && ShouldAnimateRetainedNode(*Record, WorldSeconds))
		{
			const float SettledRadius = FMath::Lerp(Record->ExpandedRadius * 1.08f, Record->ExpandedRadius * 0.9f, FadeAlpha);
			const float Radius = FMath::Lerp(Record->BaseRadius * 0.8f, SettledRadius, TransitionAlpha);
			ExpandedNodes->UpdateInstanceTransform(
				Record->ExpandedInstanceIndex,
				MakeDiskTransform(Record->Location, Radius, Record->ExpandedHeight, FMath::Lerp(Record->BaseZOffset, Record->ExpandedZOffset * 0.65f, TransitionAlpha) + VerticalWobble),
				true,
				false,
				true);
			bDirty = true;
		}

		if (Age > CachedNodeFadeTime + 0.45f && Record->LastTouchedGeneration != LastSearchGeneration)
		{
			Record->TargetVisualBlend = 0.0f;
		}

		if (Record->TargetVisualBlend <= 0.0f && Record->VisualBlend <= 0.03f)
		{
			ExpandedNodes->UpdateInstanceTransform(Record->ExpandedInstanceIndex, MakeHiddenTransform(), true, false, true);
			FrontierNodes->UpdateInstanceTransform(Record->FrontierInstanceIndex, MakeHiddenTransform(), true, false, true);
			BaseGridNodes->UpdateInstanceTransform(
				Record->BaseInstanceIndex,
				bLastShowBaseGrid ? MakeDiskTransform(Record->Location, Record->BaseRadius, Record->BaseHeight, Record->BaseZOffset) : MakeHiddenTransform(),
				true,
				false,
				true);
			Record->State = ERetainedNodeVisualState::Base;
			bDirty = true;
		}
	}

	if (bDirty)
	{
		BaseGridNodes->MarkRenderStateDirty();
		ExpandedNodes->MarkRenderStateDirty();
		FrontierNodes->MarkRenderStateDirty();
	}
}

void ASecondarySearchVisualizerActor::UpdatePathLayers(float DeltaSeconds, float WorldSeconds)
{
	for (int32 LayerIndex = PathLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		FPathLayer& Layer = PathLayers[LayerIndex];
		const float TravelSeconds = FMath::Max(0.15f, 0.8f / CachedWaveSpeed);
		const float WaveSpeedUnits = Layer.TotalDistance / TravelSeconds;
		if (!Layer.bWaveComplete)
		{
			Layer.WaveDistance = FMath::Min(Layer.TotalDistance, Layer.WaveDistance + WaveSpeedUnits * FMath::Max(DeltaSeconds, 0.016f));
			Layer.bWaveComplete = Layer.WaveDistance >= Layer.TotalDistance - KINDA_SMALL_NUMBER;
		}

		// --- Age-based fade-in on creation for a smooth reveal ---
		float LayerAlpha = 1.0f;
		if (Layer.SupersededSeconds >= 0.0f)
		{
			LayerAlpha = 1.0f - SmoothStep01((WorldSeconds - Layer.SupersededSeconds) / CachedPathFadeTime);
		}
		const float CreationAge = WorldSeconds - Layer.CreatedSeconds;
		const float FadeInAlpha = SmoothStep01(CreationAge / 0.35f);
		const float EffectiveAlpha = LayerAlpha * FadeInAlpha;

		// --- Travelling crest pulse: breathes in size as it moves ---
		const float CrestPulseFreq = 3.5f * CachedVisualSpeed;
		const float CrestPulse = 1.0f + 0.18f * FMath::Sin(WorldSeconds * CrestPulseFreq);
		const float CrestRadiusScale = 1.4f * CrestPulse;
		const float WakeRadiusScale = 1.05f;

			// --- Wave windows ---
			const float WaveWindow = Layer.bPreview
				? FMath::Max(80.0f, Layer.TotalDistance * 0.08f)
				: FMath::Max(120.0f, Layer.TotalDistance * CachedFlowBandWidth);
			const float WakeWindow = WaveWindow * 2.8f;

			// --- Update materials ---
			const FLinearColor PathColor = Layer.bPreview ? FLinearColor(0.0f, 1.0f, 0.15f, 1.0f) : FLinearColor(0.08f, 0.45f, 0.95f, 1.0f);

		// --- Compute the peak crest glow across the whole layer (used for shared BaseMaterial) ---
		float PeakCrestGlow = 0.0f;
		for (const FPathSegmentRecord& Seg : Layer.Segments)
		{
			const float Mid = Seg.StartDistance + Seg.Length * 0.5f;
			const float Influence = FMath::Max(0.0f, 1.0f - FMath::Abs(Layer.WaveDistance - Mid) / WaveWindow);
			PeakCrestGlow = FMath::Max(PeakCrestGlow, Influence * Influence);
		}

			// --- Set BaseMaterial once for the whole layer ---
			if (Layer.BaseMaterial)
			{
				const FLinearColor BaseColor = Layer.bPreview
					? FLinearColor(0.005f, 0.15f, 0.03f, 1.0f)
					: FLinearColor(0.03f, 0.12f, 0.22f, 1.0f);
				const float BaseOpacity = (Layer.bPreview ? 0.38f : 0.14f + PeakCrestGlow * 0.08f) * EffectiveAlpha;
				Layer.BaseMaterial->SetVectorParameterValue(TEXT("DebugColor"), BaseColor);
				Layer.BaseMaterial->SetScalarParameterValue(TEXT("Opacity"), BaseOpacity);
				Layer.BaseMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), BaseOpacity);
				Layer.BaseMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), (Layer.bPreview ? 1.8f : 0.14f) * CachedGlowIntensity + PeakCrestGlow * CachedGlowIntensity * 0.45f);
				Layer.BaseMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
				Layer.BaseMaterial->SetScalarParameterValue(TEXT("FlowBandWidth"), CachedFlowBandWidth);
			}

			// --- Set WaveMaterial once (crest is one travelling window) ---
			if (Layer.WaveMaterial)
			{
				const FLinearColor WaveColor = Layer.bPreview
					? FLinearColor(0.0f, 1.0f, 0.15f, 1.0f)
					: PathColor;
				Layer.WaveMaterial->SetVectorParameterValue(TEXT("DebugColor"), WaveColor);
				Layer.WaveMaterial->SetScalarParameterValue(TEXT("Opacity"), Layer.bPreview ? 0.98f * EffectiveAlpha : 0.5f * EffectiveAlpha);
				Layer.WaveMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), Layer.bPreview ? 0.98f * EffectiveAlpha : 0.5f * EffectiveAlpha);
				Layer.WaveMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity * (Layer.bPreview ? 3.8f : 0.5f + 0.18f * CrestPulse));
				Layer.WaveMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed * 1.4f);
				Layer.WaveMaterial->SetScalarParameterValue(TEXT("FlowBandWidth"), CachedFlowBandWidth);
			}

			// --- Set WakeMaterial once at the leading edge strength ---
			if (Layer.WakeMaterial)
			{
				const FLinearColor WakeColor = Layer.bPreview
					? FLinearColor(0.0f, 0.85f, 0.2f, 1.0f)
					: PathColor;
				Layer.WakeMaterial->SetVectorParameterValue(TEXT("DebugColor"), WakeColor);
				Layer.WakeMaterial->SetScalarParameterValue(TEXT("Opacity"), Layer.bPreview ? 0.78f * EffectiveAlpha : 0.18f * EffectiveAlpha);
				Layer.WakeMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), Layer.bPreview ? 0.78f * EffectiveAlpha : 0.18f * EffectiveAlpha);
				Layer.WakeMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), Layer.bPreview ? CachedGlowIntensity * 2.2f : CachedGlowIntensity * 0.35f);
				Layer.WakeMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
				Layer.WakeMaterial->SetScalarParameterValue(TEXT("FlowBandWidth"), CachedFlowBandWidth);
		}

		// --- Per-segment: only geometry (visibility + spline positions/sizes) ---
		for (FPathSegmentRecord& Segment : Layer.Segments)
		{
			const float SegmentEnd = Segment.StartDistance + Segment.Length;
			const float CrestStartDistance = Layer.WaveDistance - WaveWindow * 0.45f;
			const float CrestEndDistance   = Layer.WaveDistance + WaveWindow * 0.35f;
			const float WakeStartDistance  = Layer.WaveDistance - WakeWindow;
			const float WakeEndDistance    = Layer.WaveDistance - WaveWindow * 0.15f;
			const bool bCrestVisible = CrestEndDistance >= Segment.StartDistance && CrestStartDistance <= SegmentEnd;
			const bool bWakeVisible  = WakeEndDistance  >= Segment.StartDistance && WakeStartDistance  <= SegmentEnd;

			if (Segment.BaseSpline)
			{
				Segment.BaseSpline->SetHiddenInGame(EffectiveAlpha <= 0.01f);
			}
			if (Segment.WaveSpline)
			{
				Segment.WaveSpline->SetHiddenInGame(!bCrestVisible || EffectiveAlpha <= 0.01f);
				if (bCrestVisible)
				{
					const float ClampedStart = FMath::Max(CrestStartDistance, Segment.StartDistance);
					const float ClampedEnd   = FMath::Min(CrestEndDistance,   SegmentEnd);
					const float LocalStartAlpha = FMath::Clamp((ClampedStart - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					const float LocalEndAlpha   = FMath::Clamp((ClampedEnd   - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					SetSplineSegment(Segment.WaveSpline,
						FMath::Lerp(Segment.Start, Segment.End, LocalStartAlpha),
						FMath::Lerp(Segment.Start, Segment.End, LocalEndAlpha),
						(Layer.bPreview ? CachedPathTubeRadius * 0.95f : 3.0f) * CrestRadiusScale,
						LocalEndAlpha > LocalStartAlpha,
						Layer.bPreview ? 12.0f : 0.0f);
				}
			}
			if (Segment.WakeSpline)
			{
				Segment.WakeSpline->SetHiddenInGame(!bWakeVisible || EffectiveAlpha <= 0.01f);
				if (bWakeVisible)
				{
					const float LocalStartAlpha = FMath::Clamp((FMath::Max(WakeStartDistance, Segment.StartDistance) - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					const float LocalEndAlpha   = FMath::Clamp((FMath::Min(WakeEndDistance,   SegmentEnd)             - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					SetSplineSegment(Segment.WakeSpline,
						FMath::Lerp(Segment.Start, Segment.End, LocalStartAlpha),
						FMath::Lerp(Segment.Start, Segment.End, LocalEndAlpha),
						(Layer.bPreview ? CachedPathTubeRadius * 0.85f : 2.5f) * WakeRadiusScale,
						LocalEndAlpha > LocalStartAlpha,
						Layer.bPreview ? 12.0f : 0.0f);
				}
			}
		}

		if (LayerAlpha <= 0.01f)
		{
			ReleasePathLayer(Layer);
			PathLayers.RemoveAtSwap(LayerIndex, 1, EAllowShrinking::No);
		}
	}
}

void ASecondarySearchVisualizerActor::UpdateTargetPulse(float DeltaSeconds, float WorldSeconds)
{
	const float SmoothAlpha = 1.0f - FMath::Exp(-FMath::Max(DeltaSeconds, 0.016f) * CachedTargetSmoothing);
	if (bHasTargetLocation && bHasSmoothedTargetLocation)
	{
		SmoothedTargetLocation = FMath::Lerp(SmoothedTargetLocation, DesiredTargetLocation, SmoothAlpha);
	}
	if (bHasGoalLocation && bHasSmoothedGoalLocation)
	{
		const float MotionDist = FVector::Dist(SmoothedGoalLocation, DesiredGoalLocation);
		if (MotionDist > 15.0f)
		{
			TriggerRippleAt(SmoothedGoalLocation, FMath::Min(1.0f, MotionDist / 100.0f), 450.0f);
		}
	}

	if (bHasStartLocation && bHasSmoothedStartLocation)
	{
		SmoothedStartLocation = FMath::Lerp(SmoothedStartLocation, DesiredStartLocation, SmoothAlpha);
	}
	if (bHasGoalLocation && bHasSmoothedGoalLocation)
	{
		SmoothedGoalLocation = FMath::Lerp(SmoothedGoalLocation, DesiredGoalLocation, SmoothAlpha);
	}

	if (!bHasTargetLocation && !bHasStartLocation && !bHasGoalLocation)
	{
		return;
	}

	const float BaseFreq = 3.6f * CachedVisualSpeed;
	const float SineA = FMath::Sin(WorldSeconds * BaseFreq);
	const float SineB = FMath::Sin(WorldSeconds * BaseFreq * 0.35f);
	const float Pulse = 1.0f + 0.14f * CachedNodePulse * (SineA * 0.8f + SineB * 0.2f);
	
	const float BobFreq = 1.1f * CachedVisualSpeed;
	const float RawBobA = FMath::Sin(WorldSeconds * BobFreq);
	const float RawBobB = FMath::Sin(WorldSeconds * BobFreq * 0.62f);
	const float SmoothBobA = RawBobA * RawBobA * (3.0f - 2.0f * FMath::Abs(RawBobA)) * FMath::Sign(RawBobA);
	const float SmoothBobB = RawBobB * RawBobB * (3.0f - 2.0f * FMath::Abs(RawBobB)) * FMath::Sign(RawBobB);
	const float VerticalWobble = (SmoothBobA * 4.0f + SmoothBobB * 2.0f) * CachedNodePulse;
	if (bHasStartLocation && bHasSmoothedStartLocation)
	{
		StartMarker->ClearInstances();
		AddWorldInstance(StartMarker, MakeSphereTransform(SmoothedStartLocation, CachedTargetRadius * CachedNodeScale * 0.82f, CachedTargetZOffset + 18.0f + VerticalWobble));
	}
	if (bHasTargetLocation && bHasSmoothedTargetLocation)
	{
		TargetMarker->ClearInstances();
		AddWorldInstance(TargetMarker, MakeDiskTransform(SmoothedTargetLocation, CachedTargetRadius * CachedNodeScale * Pulse, 2.0f, CachedTargetZOffset + 8.0f + VerticalWobble));
	}
	if (bHasGoalLocation && bHasSmoothedGoalLocation)
	{
		GoalMarker->ClearInstances();
		AddWorldInstance(GoalMarker, MakeSphereTransform(SmoothedGoalLocation, CachedTargetRadius * CachedNodeScale * 0.9f, CachedTargetZOffset + 18.0f + VerticalWobble));
	}
	if (TargetMaterial)
	{
		TargetMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.85f);
		TargetMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse);
		TargetMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity);
		TargetMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
	}
	if (StartMaterial)
	{
		StartMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.88f);
		StartMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse * 0.85f);
		StartMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity * 1.1f);
		StartMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed * 0.8f);
	}
	if (GoalMaterial)
	{
		GoalMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.9f);
		GoalMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse * 0.9f);
		GoalMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity * 1.15f);
		GoalMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed * 0.9f);
	}
}

void ASecondarySearchVisualizerActor::ResetRetainedState()
{
	RetainedNodes.Reset();
	RetainedNodeKeys.Reset();
	AnimatedAtomKeys.Reset();
}

void ASecondarySearchVisualizerActor::ClearPathLayers()
{
	for (FPathLayer& Layer : PathLayers)
	{
		ReleasePathLayer(Layer);
	}
	PathLayers.Reset();
	PathLayerMaterials.Reset();
}

void ASecondarySearchVisualizerActor::ReleasePathLayer(FPathLayer& Layer)
{
	for (FPathSegmentRecord& Segment : Layer.Segments)
	{
		ReleasePathSpline(Segment.BaseSpline);
		ReleasePathSpline(Segment.WaveSpline);
		ReleasePathSpline(Segment.WakeSpline);
		Segment.BaseSpline = nullptr;
		Segment.WaveSpline = nullptr;
		Segment.WakeSpline = nullptr;
	}
	Layer.Segments.Reset();
}

void ASecondarySearchVisualizerActor::TrimPathHistory(int32 PathHistoryCount)
{
	const int32 MaxLayers = FMath::Clamp(PathHistoryCount, 1, 8);
	const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	int32 NonPreviewCount = 0;
	for (const FPathLayer& Layer : PathLayers)
	{
		if (!Layer.bPreview)
		{
			++NonPreviewCount;
		}
	}

	for (FPathLayer& Layer : PathLayers)
	{
		if (!Layer.bPreview && NonPreviewCount > MaxLayers && Layer.SupersededSeconds < 0.0f)
		{
			Layer.SupersededSeconds = WorldSeconds;
			--NonPreviewCount;
		}
	}
}

void ASecondarySearchVisualizerActor::AddPathLayer(
	const TArray<FVector>& Path,
	int32 Generation,
	const FSecondarySearchSettings& Settings,
	float WorldSeconds,
	bool bPreview)
{
	if (Path.Num() < 2)
	{
		return;
	}

	for (FPathLayer& ExistingLayer : PathLayers)
	{
		if (ExistingLayer.bPreview == bPreview && ExistingLayer.SupersededSeconds < 0.0f)
		{
			ExistingLayer.SupersededSeconds = WorldSeconds;
		}
	}

	FPathLayer NewLayer;
	NewLayer.Generation = Generation;
	NewLayer.CreatedSeconds = WorldSeconds;
	NewLayer.bPreview = bPreview;
	NewLayer.WaveDistance = 0.0f;
	NewLayer.bWaveComplete = false;
	NewLayer.BaseMaterial = CreateColorMaterial(
		PathBaseMaterial,
		bPreview ? FLinearColor(0.005f, 0.15f, 0.03f, 1.0f) : FLinearColor(0.03f, 0.12f, 0.22f, 1.0f),
		TEXT("PathLayerBase"));
	NewLayer.WaveMaterial = CreateColorMaterial(
		PathBaseMaterial,
		bPreview ? FLinearColor(0.0f, 1.0f, 0.15f, 1.0f) : FLinearColor(0.08f, 0.45f, 0.95f, 1.0f),
		TEXT("PathLayerWave"));
	NewLayer.WakeMaterial = CreateColorMaterial(
		PathBaseMaterial,
		bPreview ? FLinearColor(0.0f, 0.85f, 0.2f, 1.0f) : FLinearColor(0.04f, 0.32f, 0.75f, 1.0f),
		TEXT("PathLayerWake"));

	float RunningDistance = 0.0f;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector Start = Path[Index - 1];
		const FVector End = Path[Index];
		const float Length = FVector::Dist(Start, End);
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		FPathSegmentRecord Segment;
		Segment.Start = Start;
		Segment.End = End;
		Segment.StartDistance = RunningDistance;
		Segment.Length = Length;
		Segment.BaseSpline = AcquirePathSpline();
		Segment.WaveSpline = AcquirePathSpline();
		Segment.WakeSpline = AcquirePathSpline();
		ConfigurePathSpline(Segment.BaseSpline, NewLayer.BaseMaterial, bPreview ? Settings.PathTubeRadius * 1.15f : Settings.PathTubeRadius * 0.28f);
		ConfigurePathSpline(Segment.WaveSpline, NewLayer.WaveMaterial, bPreview ? Settings.PathTubeRadius * 0.90f : Settings.PathTubeRadius * 0.32f);
		ConfigurePathSpline(Segment.WakeSpline, NewLayer.WakeMaterial, bPreview ? Settings.PathTubeRadius * 0.80f : Settings.PathTubeRadius * 0.22f);
		SetSplineSegment(Segment.BaseSpline, Start, End, bPreview ? Settings.PathTubeRadius * 1.15f : Settings.PathTubeRadius * 0.28f, true, bPreview ? 12.0f : 0.0f);
		SetSplineSegment(Segment.WaveSpline, Start, End, bPreview ? Settings.PathTubeRadius * 0.90f : Settings.PathTubeRadius * 0.32f, false, bPreview ? 12.0f : 0.0f);
		SetSplineSegment(Segment.WakeSpline, Start, End, bPreview ? Settings.PathTubeRadius * 0.80f : Settings.PathTubeRadius * 0.22f, false, bPreview ? 12.0f : 0.0f);
		NewLayer.Segments.Add(Segment);
		RunningDistance += Length;
	}

	NewLayer.TotalDistance = RunningDistance;
	if (bPreview)
	{
		NewLayer.WaveDistance = RunningDistance;
		NewLayer.bWaveComplete = true;
	}
	if (NewLayer.Segments.Num() > 0)
	{
		PathLayers.Add(NewLayer);
	}
	else
	{
		ReleasePathLayer(NewLayer);
	}
}

UMaterialInstanceDynamic* ASecondarySearchVisualizerActor::CreateColorMaterial(UMaterialInterface* ParentMaterial, const FLinearColor& Color, const FString& Name)
{
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ParentMaterial, this, *Name);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("DebugColor"), Color);
		Material->SetScalarParameterValue(TEXT("Opacity"), Color.A);
		Material->SetScalarParameterValue(TEXT("CoreOpacity"), Color.A);
		Material->SetScalarParameterValue(TEXT("EdgeGlow"), 1.0f);
		Material->SetScalarParameterValue(TEXT("SoftFalloff"), 0.75f);
		Material->SetScalarParameterValue(TEXT("FlowBandWidth"), 0.18f);
		PathLayerMaterials.Add(Material);
	}
	return Material;
}

USplineMeshComponent* ASecondarySearchVisualizerActor::AcquirePathSpline()
{
	USplineMeshComponent* Spline = nullptr;
	if (FreePathSplines.Num() > 0)
	{
		Spline = FreePathSplines.Pop(EAllowShrinking::No);
	}
	else
	{
		Spline = NewObject<USplineMeshComponent>(this);
		Spline->SetMobility(EComponentMobility::Movable);
		Spline->SetupAttachment(SceneRoot);
		Spline->RegisterComponent();
		Spline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Spline->SetGenerateOverlapEvents(false);
		Spline->SetCanEverAffectNavigation(false);
		Spline->SetCastShadow(false);
		Spline->bCastDynamicShadow = false;
		Spline->bCastStaticShadow = false;
		PathSplinePool.Add(Spline);
	}

	Spline->SetHiddenInGame(false);
	Spline->SetVisibility(true);
	Spline->SetMobility(EComponentMobility::Movable);
	ApplyDepthPriority(bLastXRayEnabled);
	return Spline;
}

void ASecondarySearchVisualizerActor::ReleasePathSpline(USplineMeshComponent* Spline)
{
	if (!Spline)
	{
		return;
	}
	Spline->SetHiddenInGame(true);
	Spline->SetVisibility(false);
	FreePathSplines.Add(Spline);
}

void ASecondarySearchVisualizerActor::ConfigurePathSpline(USplineMeshComponent* Spline, UMaterialInterface* Material, float Radius) const
{
	if (!Spline)
	{
		return;
	}
	Spline->SetStaticMesh(CylinderMesh);
	Spline->SetMobility(EComponentMobility::Movable);
	Spline->SetForwardAxis(ESplineMeshAxis::Z);
	Spline->SetStartScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetEndScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetMaterial(0, Material);
	Spline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Spline->SetGenerateOverlapEvents(false);
	Spline->SetCanEverAffectNavigation(false);
}

void ASecondarySearchVisualizerActor::SetSplineSegment(USplineMeshComponent* Spline, const FVector& Start, const FVector& End, float Radius, bool bVisible, float ExtraZOffset) const
{
	if (!Spline)
	{
		return;
	}
	const FVector LocalStart = GetActorTransform().InverseTransformPosition(Start + FVector(0.0f, 0.0f, CachedTargetZOffset + 16.0f + ExtraZOffset));
	const FVector LocalEnd = GetActorTransform().InverseTransformPosition(End + FVector(0.0f, 0.0f, CachedTargetZOffset + 16.0f + ExtraZOffset));
	const FVector Direction = (LocalEnd - LocalStart).GetSafeNormal();
	const FVector Tangent = Direction * FVector::Dist(LocalStart, LocalEnd) * 0.5f;
	Spline->SetStartScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetEndScale(FVector2D(Radius / 50.0f, Radius / 50.0f));
	Spline->SetStartAndEnd(LocalStart, Tangent, LocalEnd, Tangent, true);
	Spline->SetHiddenInGame(!bVisible);
	Spline->SetVisibility(bVisible);
}

FTransform ASecondarySearchVisualizerActor::MakeDiskTransform(const FVector& Location, float Radius, float Height, float ZOffset) const
{
	return FTransform(
		FRotator::ZeroRotator,
		Location + FVector(0.0f, 0.0f, ZOffset),
		FVector(Radius / 50.0f, Radius / 50.0f, Height / 100.0f));
}

FTransform ASecondarySearchVisualizerActor::MakeSphereTransform(const FVector& Location, float Radius, float ZOffset) const
{
	return FTransform(
		FRotator::ZeroRotator,
		Location + FVector(0.0f, 0.0f, ZOffset),
		FVector(Radius / 50.0f));
}

FTransform ASecondarySearchVisualizerActor::MakeTubeTransform(const FVector& Start, const FVector& End, float Radius, float ZOffset) const
{
	const FVector Segment = End - Start;
	const float Length = Segment.Size();
	const FVector Midpoint = (Start + End) * 0.5f + FVector(0.0f, 0.0f, ZOffset);
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Segment.GetSafeNormal());
	return FTransform(Rotation, Midpoint, FVector(Radius / 50.0f, Radius / 50.0f, Length / 100.0f));
}

FTransform ASecondarySearchVisualizerActor::MakeHiddenTransform() const
{
	return FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector::ZeroVector);
}

int32 ASecondarySearchVisualizerActor::AddWorldInstance(UInstancedStaticMeshComponent* Component, const FTransform& WorldTransform) const
{
	return Component ? Component->AddInstance(WorldTransform, true) : INDEX_NONE;
}

FIntPoint ASecondarySearchVisualizerActor::MakeNodeKey(const FVector& Location, float CellSize) const
{
	const float SafeCellSize = FMath::Max(1.0f, CellSize);
	return FIntPoint(
		FMath::RoundToInt(Location.X / SafeCellSize),
		FMath::RoundToInt(Location.Y / SafeCellSize));
}

FIntPoint ASecondarySearchVisualizerActor::FindRetainedNodeKeyNear(const FVector& Location, float CellSize) const
{
	const FIntPoint DirectKey = MakeNodeKey(Location, CellSize);
	if (RetainedNodes.Contains(DirectKey))
	{
		return DirectKey;
	}

	FIntPoint BestKey = DirectKey;
	float BestDistanceSquared = FMath::Square(CellSize * 0.85f);
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			const FIntPoint CandidateKey(DirectKey.X + X, DirectKey.Y + Y);
			if (const FRetainedNodeRecord* Record = RetainedNodes.Find(CandidateKey))
			{
				const float DistanceSquared = FVector::DistSquared2D(Location, Record->Location);
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					BestKey = CandidateKey;
				}
			}
		}
	}
	return BestKey;
}

bool ASecondarySearchVisualizerActor::ArePathsEquivalent(const TArray<FVector>& A, const TArray<FVector>& B) const
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (!A[Index].Equals(B[Index], 5.0f))
		{
			return false;
		}
	}
	return true;
}

float ASecondarySearchVisualizerActor::SmoothStep01(float Value) const
{
	const float X = FMath::Clamp(Value, 0.0f, 1.0f);
	return X * X * (3.0f - 2.0f * X);
}

float ASecondarySearchVisualizerActor::EaseOutCubic(float Value) const
{
	const float X = 1.0f - FMath::Clamp(Value, 0.0f, 1.0f);
	return 1.0f - X * X * X;
}

float ASecondarySearchVisualizerActor::EaseInOutSine(float Value) const
{
	const float X = FMath::Clamp(Value, 0.0f, 1.0f);
	return 0.5f - 0.5f * FMath::Cos(PI * X);
}

float ASecondarySearchVisualizerActor::GetQualityScale() const
{
	if (CachedVisualQuality == ESecondarySearchVisualQuality::Low)
	{
		return 0.0f;
	}
	if (CachedVisualQuality == ESecondarySearchVisualQuality::Medium)
	{
		return 0.55f;
	}
	return 1.0f;
}

bool ASecondarySearchVisualizerActor::ShouldAnimateRetainedNode(const FRetainedNodeRecord& Record, float WorldSeconds) const
{
	if (CachedVisualQuality == ESecondarySearchVisualQuality::Low)
	{
		return false;
	}
	return Record.State == ERetainedNodeVisualState::Frontier ||
		(WorldSeconds - Record.StateChangeSeconds) <= CachedNodeFadeTime ||
		(WorldSeconds - Record.LastRippleSeconds) < 0.65f ||
		!Record.AtomOffset.IsNearlyZero();
}

void ASecondarySearchVisualizerActor::TriggerRippleAt(const FVector& Location, float Intensity, float Radius)
{
	const float WorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float RadiusSq = FMath::Square(Radius);

	for (auto& Pair : RetainedNodes)
	{
		FRetainedNodeRecord& Record = Pair.Value;
		const float DistSq = FVector::DistSquared2D(Record.Location, Location);
		if (DistSq < RadiusSq)
		{
			const float Dist = FMath::Sqrt(DistSq);
			Record.LastRippleSeconds = WorldSeconds;
			Record.RippleStrength = FMath::Max(Record.RippleStrength, Intensity * (1.0f - Dist / Radius));
		}
	}
}
