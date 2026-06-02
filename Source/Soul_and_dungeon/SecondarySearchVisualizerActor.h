#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SecondarySearchSolver.h"
#include "SecondarySearchVisualizerActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USplineMeshComponent;
class UStaticMesh;

UCLASS(NotPlaceable, Transient)
class SOUL_AND_DUNGEON_API ASecondarySearchVisualizerActor : public AActor
{
	GENERATED_BODY()

public:
	ASecondarySearchVisualizerActor();

	void UpdateVisualization(
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
		float NodeSoftness);
	void ClearVisualization();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class ERetainedNodeVisualState : uint8
	{
		Base,
		Frontier,
		Expanded,
		PathTouched
	};

	struct FRetainedNodeRecord
	{
		FVector Location = FVector::ZeroVector;
		int32 BaseInstanceIndex = INDEX_NONE;
		int32 ExpandedInstanceIndex = INDEX_NONE;
		int32 FrontierInstanceIndex = INDEX_NONE;
		float BaseRadius = 0.0f;
		float BaseHeight = 0.0f;
		float BaseZOffset = 0.0f;
		float ExpandedRadius = 0.0f;
		float FrontierRadius = 0.0f;
		float ExpandedHeight = 0.0f;
		float FrontierHeight = 0.0f;
		float ExpandedZOffset = 0.0f;
		float FrontierZOffset = 0.0f;
		float StateChangeSeconds = 0.0f;
		float LastTouchedSeconds = 0.0f;
		float VisualBlend = 0.0f;
		float TargetVisualBlend = 0.0f;
		float RippleStrength = 0.0f;
		float LastRippleSeconds = 0.0f;
		int32 LastTouchedGeneration = -1;
		int32 SequenceIndex = 0;
		bool bWasAtomAnimated = false;
		FVector AtomOffset = FVector::ZeroVector;
		ERetainedNodeVisualState State = ERetainedNodeVisualState::Base;
	};

	struct FPathSegmentRecord
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		float StartDistance = 0.0f;
		float Length = 0.0f;
		USplineMeshComponent* BaseSpline = nullptr;
		USplineMeshComponent* WaveSpline = nullptr;
		USplineMeshComponent* WakeSpline = nullptr;
	};

	struct FPathLayer
	{
		int32 Generation = 0;
		float CreatedSeconds = 0.0f;
		float SupersededSeconds = -1.0f;
		float WaveDistance = 0.0f;
		float TotalDistance = 0.0f;
		bool bWaveComplete = false;
		bool bPreview = false;
		TArray<FPathSegmentRecord> Segments;
		UMaterialInstanceDynamic* BaseMaterial = nullptr;
		UMaterialInstanceDynamic* WaveMaterial = nullptr;
		UMaterialInstanceDynamic* WakeMaterial = nullptr;
	};

	void ConfigureComponent(UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh) const;
	void ApplyDepthPriority(bool bXRayEnabled);
	void UpdateBaseGridInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, int32 NodeCap);
	void UpdateRetainedNodeInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, int32 NodeCap, float WorldSeconds);
	void UpdateEndpointMarkers(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings);
	void UpdateSimplePathInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings);
	void UpdatePreviewPathInstances(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, float WorldSeconds, bool bTrailsEnabled);
	void UpdateStatusHud(const FSecondarySearchResult& Result) const;
	void UpdateFluidAnimation(float DeltaSeconds, float WorldSeconds);
	void UpdatePathLayers(float DeltaSeconds, float WorldSeconds);
	void UpdateTargetPulse(float DeltaSeconds, float WorldSeconds);
	void ResetRetainedState();
	void ClearPathLayers();
	void ReleasePathLayer(FPathLayer& Layer);
	void TrimPathHistory(int32 PathHistoryCount);
	void AddPathLayer(
		const TArray<FVector>& Path,
		int32 Generation,
		const FSecondarySearchSettings& Settings,
		float WorldSeconds,
		bool bPreview);
	void TriggerRippleAt(const FVector& Location, float Intensity, float Radius);

	UMaterialInstanceDynamic* CreateColorMaterial(UMaterialInterface* ParentMaterial, const FLinearColor& Color, const FString& Name);
	USplineMeshComponent* AcquirePathSpline();
	void ReleasePathSpline(USplineMeshComponent* Spline);
	void ConfigurePathSpline(USplineMeshComponent* Spline, UMaterialInterface* Material, float Radius) const;
	void SetSplineSegment(USplineMeshComponent* Spline, const FVector& Start, const FVector& End, float Radius, bool bVisible) const;
	FTransform MakeDiskTransform(const FVector& Location, float Radius, float Height, float ZOffset) const;
	FTransform MakeSphereTransform(const FVector& Location, float Radius, float ZOffset) const;
	FTransform MakeTubeTransform(const FVector& Start, const FVector& End, float Radius, float ZOffset) const;
	FTransform MakeHiddenTransform() const;
	int32 AddWorldInstance(UInstancedStaticMeshComponent* Component, const FTransform& WorldTransform) const;
	FIntPoint MakeNodeKey(const FVector& Location, float CellSize) const;
	FIntPoint FindRetainedNodeKeyNear(const FVector& Location, float CellSize) const;
	bool ArePathsEquivalent(const TArray<FVector>& A, const TArray<FVector>& B) const;
	float SmoothStep01(float Value) const;
	float EaseOutCubic(float Value) const;
	float EaseInOutSine(float Value) const;
	float GetQualityScale() const;
	bool ShouldAnimateRetainedNode(const FRetainedNodeRecord& Record, float WorldSeconds) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BaseGridNodes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExpandedNodes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> FrontierNodes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> StartMarker;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> GoalMarker;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TargetMarker;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> PathSegments;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> PreviewPathSegments;
	
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BFSPathSegments;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> UCSPathSegments;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> AStarPathSegments;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> PathSplinePool;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> FreePathSplines;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PathBaseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BaseGridMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ExpandedMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FrontierMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> StartMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GoalMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TargetMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PathMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewPathMaterial;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BFSMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> UCSMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AStarMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PathLayerMaterials;

	TMap<FIntPoint, FRetainedNodeRecord> RetainedNodes;
	TArray<FIntPoint> RetainedNodeKeys;
	TSet<FIntPoint> AnimatedAtomKeys;
	TArray<FPathLayer> PathLayers;
	TArray<FVector> LastSuccessfulPath;
	TArray<FVector> LastPreviewPath;
	TArray<FVector> LastSampledNodes;

	FVector DesiredTargetLocation = FVector::ZeroVector;
	FVector SmoothedTargetLocation = FVector::ZeroVector;
	FVector DesiredStartLocation = FVector::ZeroVector;
	FVector SmoothedStartLocation = FVector::ZeroVector;
	FVector DesiredGoalLocation = FVector::ZeroVector;
	FVector SmoothedGoalLocation = FVector::ZeroVector;
	bool bHasSmoothedTargetLocation = false;
	bool bHasSmoothedStartLocation = false;
	bool bHasSmoothedGoalLocation = false;
	bool bHasTargetLocation = false;
	bool bHasStartLocation = false;
	bool bHasGoalLocation = false;
	bool bLastXRayEnabled = true;
	bool bLastTrailsEnabled = true;
	bool bLastShowBaseGrid = true;
	bool bCachedLastPathFallback = true;
	ESecondarySearchVisualStyle LastVisualStyle = ESecondarySearchVisualStyle::Fluid;
	ESecondarySearchVisualQuality CachedVisualQuality = ESecondarySearchVisualQuality::High;
	float CachedVisualSpeed = 1.0f;
	float CachedWaveSpeed = 1.0f;
	float CachedNodeScale = 0.45f;
	float CachedTargetRadius = 44.0f;
	float CachedPathRevealSpeed = 1100.0f;
	float CachedNodePulse = 1.0f;
	float CachedNodeFadeTime = 1.2f;
	float CachedPathFadeTime = 4.0f;
	float CachedGlowIntensity = 1.0f;
	float CachedFlowBandWidth = 0.18f;
	float CachedNodeSoftness = 0.75f;
	float CachedTargetSmoothing = 18.0f;
	float CachedTargetZOffset = 80.0f;
	float LastNodeUpdateSeconds = 0.0f;
	int32 LastVisualizationRevision = -1;
	int32 LastSearchGeneration = -1;
	int32 LastPathLayerGeneration = -1;
	int32 LastPreviewLayerGeneration = -1;
	int32 LastSampledDrawCount = -1;
	int32 LastMaxVisibleNodes = -1;
	int32 LastPathPointCount = -1;
	int32 LastPreviewPathPointCount = -1;
	float LastNodeScale = -1.0f;
	float LastCellSize = -1.0f;
};
