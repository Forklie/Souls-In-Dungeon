#include "SecondarySearchVisualizerActor.h"

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

	BaseGridNodes->SetupAttachment(SceneRoot);
	ExpandedNodes->SetupAttachment(SceneRoot);
	FrontierNodes->SetupAttachment(SceneRoot);
	StartMarker->SetupAttachment(SceneRoot);
	GoalMarker->SetupAttachment(SceneRoot);
	TargetMarker->SetupAttachment(SceneRoot);
	PathSegments->SetupAttachment(SceneRoot);
	PreviewPathSegments->SetupAttachment(SceneRoot);

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

	SetActorHiddenInGame(true);
}

void ASecondarySearchVisualizerActor::BeginPlay()
{
	Super::BeginPlay();

	BaseGridMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.03f, 0.34f, 0.95f, 0.5f), TEXT("BaseGridMaterial"));
	ExpandedMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.04f, 0.18f, 0.85f, 0.65f), TEXT("ExpandedMaterial"));
	FrontierMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.0f, 0.95f, 1.0f, 0.92f), TEXT("FrontierMaterial"));
	StartMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(0.0f, 1.0f, 0.15f, 1.0f), TEXT("StartMaterial"));
	GoalMaterial = CreateColorMaterial(BaseMaterial, FLinearColor(1.0f, 0.05f, 0.02f, 1.0f), TEXT("GoalMaterial"));
	TargetMaterial = CreateColorMaterial(BaseMaterial, FLinearColor::White, TEXT("TargetMaterial"));
	PathMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(1.0f, 0.55f, 0.0f, 1.0f), TEXT("PathMaterial"));
	PreviewPathMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.85f, 0.18f, 1.0f, 0.55f), TEXT("PreviewPathMaterial"));

	BaseGridNodes->SetMaterial(0, BaseGridMaterial);
	ExpandedNodes->SetMaterial(0, ExpandedMaterial);
	FrontierNodes->SetMaterial(0, FrontierMaterial);
	StartMarker->SetMaterial(0, StartMaterial);
	GoalMarker->SetMaterial(0, GoalMaterial);
	TargetMarker->SetMaterial(0, TargetMaterial);
	PathSegments->SetMaterial(0, PathMaterial);
	PreviewPathSegments->SetMaterial(0, PreviewPathMaterial);
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
	CachedVisualQuality = VisualQuality;
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

	if (Result.PreviewPath.Num() > 1 && !ArePathsEquivalent(Result.PreviewPath, LastPreviewPath))
	{
		LastPreviewPath = Result.PreviewPath;
		LastPreviewLayerGeneration = Result.SearchGeneration;
	}
	UpdatePreviewPathInstances(Result, Settings, WorldSeconds);

	bLastTrailsEnabled = bTrailsEnabled;
	if (!bTrailsEnabled)
	{
		TrimPathHistory(1);
	}

	if (!Result.CurrentTarget.IsNearlyZero())
	{
		if (!bHasSmoothedTargetLocation)
		{
			SmoothedTargetLocation = Result.CurrentTarget;
			bHasSmoothedTargetLocation = true;
		}
		else
		{
			const float Alpha = 1.0f - FMath::Exp(-0.05f * CachedTargetSmoothing);
			SmoothedTargetLocation = FMath::Lerp(SmoothedTargetLocation, Result.CurrentTarget, Alpha);
		}
		bHasTargetLocation = true;
	}

	UpdateStatusHud(Result);
	UpdateFluidAnimation(0.0f, WorldSeconds);
	UpdatePathLayers(0.0f, WorldSeconds);
	UpdateTargetPulse(0.0f, WorldSeconds);
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
	ClearPathLayers();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(913702, 0.01f, FColor::White, FString());
	}
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	bHasTargetLocation = false;
	bHasSmoothedTargetLocation = false;
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

	const int32 DrawCount = FMath::Min(Result.SampledNodes.Num(), NodeCap);
	RetainedNodes.Reserve(DrawCount);
	RetainedNodeKeys.Reserve(DrawCount);

	const float BaseRadius = FMath::Max(5.5f, Settings.DebugExpandedNodeRadius * 0.58f * CachedNodeScale);
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
		Record.BaseHeight = 1.4f;
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

		Record->State = ERetainedNodeVisualState::Expanded;
		Record->LastTouchedGeneration = Result.SearchGeneration;
		Record->LastTouchedSeconds = WorldSeconds;
		Record->StateChangeSeconds = WorldSeconds - Index * 0.002f;
		Record->SequenceIndex = Index;

		BaseGridNodes->UpdateInstanceTransform(Record->BaseInstanceIndex, MakeHiddenTransform(), true, false, true);
		ExpandedNodes->UpdateInstanceTransform(
			Record->ExpandedInstanceIndex,
			MakeDiskTransform(Record->Location, Record->ExpandedRadius, Record->ExpandedHeight, Record->ExpandedZOffset),
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

		Record->State = ERetainedNodeVisualState::Frontier;
		Record->LastTouchedGeneration = Result.SearchGeneration;
		Record->LastTouchedSeconds = WorldSeconds;
		Record->StateChangeSeconds = WorldSeconds - Index * 0.003f;
		Record->SequenceIndex = Index;

		BaseGridNodes->UpdateInstanceTransform(Record->BaseInstanceIndex, MakeHiddenTransform(), true, false, true);
		ExpandedNodes->UpdateInstanceTransform(Record->ExpandedInstanceIndex, MakeHiddenTransform(), true, false, true);
		FrontierNodes->UpdateInstanceTransform(
			Record->FrontierInstanceIndex,
			MakeDiskTransform(Record->Location, Record->FrontierRadius, Record->FrontierHeight, Record->FrontierZOffset),
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
		AddWorldInstance(StartMarker, MakeSphereTransform(Result.StartLocation, Settings.DebugEndpointRadius * 0.45f * CachedNodeScale, Settings.DebugPointZOffset + 18.0f));
	}
	if (!Result.GoalLocation.IsNearlyZero())
	{
		AddWorldInstance(GoalMarker, MakeSphereTransform(Result.GoalLocation, Settings.DebugEndpointRadius * 0.45f * CachedNodeScale, Settings.DebugPointZOffset + 18.0f));
	}
}

void ASecondarySearchVisualizerActor::UpdateSimplePathInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings)
{
	PathSegments->ClearInstances();
	const TArray<FVector>& PathToShow = Result.Path.Num() > 1 ? Result.Path : LastSuccessfulPath;
	for (int32 Index = 1; Index < PathToShow.Num(); ++Index)
	{
		AddWorldInstance(PathSegments, MakeTubeTransform(PathToShow[Index - 1], PathToShow[Index], Settings.PathTubeRadius, Settings.DebugPointZOffset + 18.0f));
	}
}

void ASecondarySearchVisualizerActor::UpdatePreviewPathInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, float WorldSeconds)
{
	const TArray<FVector>& PreviewToShow = Result.PreviewPath.Num() > 1 ? Result.PreviewPath : LastPreviewPath;
	if (LastPreviewPathPointCount == PreviewToShow.Num() && PreviewPathSegments->GetInstanceCount() > 0)
	{
		return;
	}

	PreviewPathSegments->ClearInstances();
	LastPreviewPathPointCount = PreviewToShow.Num();
	for (int32 Index = 1; Index < PreviewToShow.Num(); ++Index)
	{
		AddWorldInstance(PreviewPathSegments, MakeTubeTransform(PreviewToShow[Index - 1], PreviewToShow[Index], Settings.PathTubeRadius * 0.55f, Settings.DebugPointZOffset + 24.0f));
	}

	if (PreviewPathMaterial)
	{
		PreviewPathMaterial->SetScalarParameterValue(TEXT("FlowSpeed"), 0.35f * CachedVisualSpeed);
		PreviewPathMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * 0.35f);
		PreviewPathMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.42f);
		PreviewPathMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.42f);
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
		TEXT("Secondary Search: %s | Gen %d | %s | Expanded %d | Base %d"),
		*FSecondarySearchDebug::GetModeName(Result.Mode),
		Result.SearchGeneration,
		*Phase,
		Result.ExpandedCount,
		RetainedNodes.Num());
	GEngine->AddOnScreenDebugMessage(913702, 0.18f, FColor(180, 220, 255), Message);
}

void ASecondarySearchVisualizerActor::UpdateFluidAnimation(float DeltaSeconds, float WorldSeconds)
{
	const float QualityScale = GetQualityScale();
	if (BaseGridMaterial)
	{
		BaseGridMaterial->SetVectorParameterValue(TEXT("DebugColor"), FLinearColor(0.03f, 0.34f, 0.95f, 1.0f));
		BaseGridMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.42f + CachedGlowIntensity * 0.06f);
		BaseGridMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.34f);
		BaseGridMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), CachedNodeSoftness);
		BaseGridMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), 0.7f * CachedGlowIntensity);
		BaseGridMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * 0.2f);
	}
	if (FrontierMaterial)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(WorldSeconds * 4.0f * CachedVisualSpeed);
		FrontierMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse * CachedNodePulse);
		FrontierMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.8f);
		FrontierMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.9f);
		FrontierMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), CachedNodeSoftness);
		FrontierMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity);
		FrontierMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
	}
	if (ExpandedMaterial)
	{
		ExpandedMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.46f);
		ExpandedMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), 0.46f);
		ExpandedMaterial->SetScalarParameterValue(TEXT("SoftFalloff"), CachedNodeSoftness);
		ExpandedMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), 0.55f * CachedGlowIntensity);
		ExpandedMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * 0.35f);
	}

	if (CachedVisualQuality == ESecondarySearchVisualQuality::Low)
	{
		return;
	}

	bool bDirty = false;
	for (const FIntPoint& Key : RetainedNodeKeys)
	{
		FRetainedNodeRecord* Record = RetainedNodes.Find(Key);
		if (!Record || Record->State == ERetainedNodeVisualState::Base)
		{
			continue;
		}

		const float Age = FMath::Max(0.0f, WorldSeconds - Record->StateChangeSeconds);
		const float FadeAlpha = SmoothStep01(Age / CachedNodeFadeTime);
		const float Phase = (Record->SequenceIndex % 17) * 0.37f;
		const float Breathing = 1.0f + 0.14f * CachedNodePulse * QualityScale * FMath::Sin(WorldSeconds * 3.0f * CachedVisualSpeed + Phase);

		if (Record->State == ERetainedNodeVisualState::Frontier && ShouldAnimateRetainedNode(*Record, WorldSeconds))
		{
			const float Pop = 1.0f + 0.35f * (1.0f - EaseOutCubic(FMath::Min(1.0f, Age * 4.0f)));
			FrontierNodes->UpdateInstanceTransform(
				Record->FrontierInstanceIndex,
				MakeDiskTransform(Record->Location, Record->FrontierRadius * Breathing * Pop, Record->FrontierHeight, Record->FrontierZOffset),
				true,
				false,
				true);
			bDirty = true;
		}
		else if (Record->State == ERetainedNodeVisualState::Expanded && ShouldAnimateRetainedNode(*Record, WorldSeconds))
		{
			const float Radius = FMath::Lerp(Record->ExpandedRadius * 1.08f, Record->ExpandedRadius * 0.92f, FadeAlpha);
			ExpandedNodes->UpdateInstanceTransform(
				Record->ExpandedInstanceIndex,
				MakeDiskTransform(Record->Location, Radius, Record->ExpandedHeight, Record->ExpandedZOffset),
				true,
				false,
				true);
			bDirty = true;
		}

		if (Age > CachedNodeFadeTime + 0.45f && Record->LastTouchedGeneration != LastSearchGeneration)
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

		float LayerAlpha = 1.0f;
		if (Layer.SupersededSeconds >= 0.0f)
		{
			LayerAlpha = 1.0f - SmoothStep01((WorldSeconds - Layer.SupersededSeconds) / CachedPathFadeTime);
		}

		const FLinearColor PathColor = Layer.bPreview ? FLinearColor(0.75f, 0.05f, 1.0f, 1.0f) : FLinearColor(1.0f, 0.52f, 0.0f, 1.0f);
		if (Layer.BaseMaterial)
		{
			Layer.BaseMaterial->SetVectorParameterValue(TEXT("DebugColor"), PathColor);
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("Opacity"), (Layer.bPreview ? 0.22f : 0.32f) * LayerAlpha);
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), (Layer.bPreview ? 0.22f : 0.32f) * LayerAlpha);
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity * 0.45f);
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
			Layer.BaseMaterial->SetScalarParameterValue(TEXT("FlowBandWidth"), CachedFlowBandWidth);
		}
		if (Layer.WaveMaterial)
		{
			Layer.WaveMaterial->SetVectorParameterValue(TEXT("DebugColor"), PathColor);
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("Opacity"), (Layer.bPreview ? 0.0f : 1.0f) * LayerAlpha);
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), (Layer.bPreview ? 0.0f : 1.0f) * LayerAlpha);
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity * 1.5f);
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed * 1.4f);
			Layer.WaveMaterial->SetScalarParameterValue(TEXT("FlowBandWidth"), CachedFlowBandWidth);
		}
		if (Layer.WakeMaterial)
		{
			Layer.WakeMaterial->SetVectorParameterValue(TEXT("DebugColor"), PathColor);
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("Opacity"), (Layer.bPreview ? 0.0f : 0.45f) * LayerAlpha);
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("CoreOpacity"), (Layer.bPreview ? 0.0f : 0.45f) * LayerAlpha);
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity);
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
			Layer.WakeMaterial->SetScalarParameterValue(TEXT("FlowBandWidth"), CachedFlowBandWidth);
		}

		const float CrestRadiusScale = 1.35f;
		const float WakeRadiusScale = 1.05f;
		const float WaveWindow = FMath::Max(120.0f, Layer.TotalDistance * CachedFlowBandWidth);
		const float WakeWindow = WaveWindow * 2.8f;

		for (FPathSegmentRecord& Segment : Layer.Segments)
		{
			const float SegmentEnd = Segment.StartDistance + Segment.Length;
			const float CrestStartDistance = Layer.WaveDistance - WaveWindow * 0.45f;
			const float CrestEndDistance = Layer.WaveDistance + WaveWindow * 0.35f;
			const float WakeStartDistance = Layer.WaveDistance - WakeWindow;
			const float WakeEndDistance = Layer.WaveDistance - WaveWindow * 0.15f;
			const bool bCrestVisible = !Layer.bPreview && CrestEndDistance >= Segment.StartDistance && CrestStartDistance <= SegmentEnd;
			const bool bWakeVisible = !Layer.bPreview && WakeEndDistance >= Segment.StartDistance && WakeStartDistance <= SegmentEnd;
			if (Segment.BaseSpline)
			{
				Segment.BaseSpline->SetHiddenInGame(LayerAlpha <= 0.01f);
			}
			if (Segment.WaveSpline)
			{
				Segment.WaveSpline->SetHiddenInGame(!bCrestVisible || LayerAlpha <= 0.01f);
				if (bCrestVisible)
				{
					const float LocalStartAlpha = FMath::Clamp((FMath::Max(CrestStartDistance, Segment.StartDistance) - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					const float LocalEndAlpha = FMath::Clamp((FMath::Min(CrestEndDistance, SegmentEnd) - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					SetSplineSegment(
						Segment.WaveSpline,
						FMath::Lerp(Segment.Start, Segment.End, LocalStartAlpha),
						FMath::Lerp(Segment.Start, Segment.End, LocalEndAlpha),
						5.0f * CrestRadiusScale,
						LocalEndAlpha > LocalStartAlpha);
				}
			}
			if (Segment.WakeSpline)
			{
				Segment.WakeSpline->SetHiddenInGame(!bWakeVisible || LayerAlpha <= 0.01f);
				if (bWakeVisible)
				{
					const float LocalStartAlpha = FMath::Clamp((FMath::Max(WakeStartDistance, Segment.StartDistance) - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					const float LocalEndAlpha = FMath::Clamp((FMath::Min(WakeEndDistance, SegmentEnd) - Segment.StartDistance) / Segment.Length, 0.0f, 1.0f);
					SetSplineSegment(
						Segment.WakeSpline,
						FMath::Lerp(Segment.Start, Segment.End, LocalStartAlpha),
						FMath::Lerp(Segment.Start, Segment.End, LocalEndAlpha),
						4.5f * WakeRadiusScale,
						LocalEndAlpha > LocalStartAlpha);
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
	if (!bHasTargetLocation || !bHasSmoothedTargetLocation)
	{
		return;
	}

	const float Pulse = 1.0f + 0.14f * CachedNodePulse * FMath::Sin(WorldSeconds * 5.0f * CachedVisualSpeed);
	TargetMarker->ClearInstances();
	AddWorldInstance(TargetMarker, MakeDiskTransform(SmoothedTargetLocation, CachedTargetRadius * CachedNodeScale * Pulse, 2.0f, CachedTargetZOffset + 8.0f));
	if (TargetMaterial)
	{
		TargetMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.85f);
		TargetMaterial->SetScalarParameterValue(TEXT("Pulse"), Pulse);
		TargetMaterial->SetScalarParameterValue(TEXT("EdgeGlow"), CachedGlowIntensity);
		TargetMaterial->SetScalarParameterValue(TEXT("WavePhase"), WorldSeconds * CachedVisualSpeed);
	}
}

void ASecondarySearchVisualizerActor::ResetRetainedState()
{
	RetainedNodes.Reset();
	RetainedNodeKeys.Reset();
}

void ASecondarySearchVisualizerActor::ClearPathLayers()
{
	for (FPathLayer& Layer : PathLayers)
	{
		ReleasePathLayer(Layer);
	}
	PathLayers.Reset();
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
	NewLayer.bWaveComplete = bPreview;
	NewLayer.BaseMaterial = CreateColorMaterial(PathBaseMaterial, bPreview ? FLinearColor(0.82f, 0.1f, 1.0f, 1.0f) : FLinearColor(1.0f, 0.52f, 0.0f, 1.0f), TEXT("PathLayerBase"));
	NewLayer.WaveMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(1.0f, 0.85f, 0.25f, 1.0f), TEXT("PathLayerWave"));
	NewLayer.WakeMaterial = CreateColorMaterial(PathBaseMaterial, FLinearColor(0.3f, 0.78f, 1.0f, 1.0f), TEXT("PathLayerWake"));

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
		ConfigurePathSpline(Segment.BaseSpline, NewLayer.BaseMaterial, bPreview ? Settings.PathTubeRadius * 0.5f : Settings.PathTubeRadius * 0.85f);
		ConfigurePathSpline(Segment.WaveSpline, NewLayer.WaveMaterial, Settings.PathTubeRadius * 1.25f);
		ConfigurePathSpline(Segment.WakeSpline, NewLayer.WakeMaterial, Settings.PathTubeRadius * 1.05f);
		SetSplineSegment(Segment.BaseSpline, Start, End, bPreview ? Settings.PathTubeRadius * 0.5f : Settings.PathTubeRadius * 0.85f, true);
		SetSplineSegment(Segment.WaveSpline, Start, End, Settings.PathTubeRadius * 1.25f, false);
		SetSplineSegment(Segment.WakeSpline, Start, End, Settings.PathTubeRadius * 1.05f, false);
		NewLayer.Segments.Add(Segment);
		RunningDistance += Length;
	}

	NewLayer.TotalDistance = RunningDistance;
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

void ASecondarySearchVisualizerActor::SetSplineSegment(USplineMeshComponent* Spline, const FVector& Start, const FVector& End, float Radius, bool bVisible) const
{
	if (!Spline)
	{
		return;
	}
	const FVector LocalStart = GetActorTransform().InverseTransformPosition(Start + FVector(0.0f, 0.0f, 96.0f));
	const FVector LocalEnd = GetActorTransform().InverseTransformPosition(End + FVector(0.0f, 0.0f, 96.0f));
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
		(WorldSeconds - Record.StateChangeSeconds) <= CachedNodeFadeTime;
}
