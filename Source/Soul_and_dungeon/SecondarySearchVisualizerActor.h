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
class UTextRenderComponent;

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
		float NodeScale);
	void ClearVisualization();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ConfigureComponent(UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh) const;
	void ApplyDepthPriority(bool bXRayEnabled);
	void RebuildInstances(
		const FSecondarySearchResult& Result,
		const FSecondarySearchSettings& Settings,
		int32 ExpandedDrawCount,
		int32 FrontierDrawCount);
	void AddWavePathLayer(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings, float WorldSeconds, bool bTrailsEnabled, int32 PathHistoryCount);
	void UpdateStatusText(const FSecondarySearchResult& Result, const FSecondarySearchSettings& Settings);
	void UpdateFluidAnimation(float DeltaSeconds, float WorldSeconds);
	void UpdateWavePathLayers(float DeltaSeconds, float WorldSeconds);
	void UpdateTargetPulse(float WorldSeconds);
	void ResetRetainedState();
	void ClearWavePathLayers();
	void ReleaseWavePathLayer(int32 LayerIndex);
	void TrimWavePathHistory(int32 PathHistoryCount);

	UMaterialInstanceDynamic* CreateColorMaterial(UMaterialInterface* ParentMaterial, const FLinearColor& Color, const FString& Name);
	USplineMeshComponent* AcquirePathSpline();
	void ReleasePathSpline(USplineMeshComponent* Spline);
	void ConfigurePathSpline(USplineMeshComponent* Spline, UMaterialInterface* Material, float Radius, bool bWaveSpline) const;
	void SetSplineSegment(USplineMeshComponent* Spline, const FVector& Start, const FVector& End, float Radius, bool bVisible) const;
	FTransform MakeDiskTransform(const FVector& Location, float Radius, float Height, float ZOffset) const;
	FTransform MakeSphereTransform(const FVector& Location, float Radius, float ZOffset) const;
	FTransform MakeTubeTransform(const FVector& Start, const FVector& End, float Radius, float ZOffset) const;

	struct FFluidNodeRecord
	{
		UInstancedStaticMeshComponent* Component = nullptr;
		int32 InstanceIndex = INDEX_NONE;
		FVector Location = FVector::ZeroVector;
		float BaseRadius = 0.0f;
		float Height = 0.0f;
		float ZOffset = 0.0f;
		float SpawnSeconds = 0.0f;
		bool bFrontier = false;
	};

	struct FWavePathSegment
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		float StartDistance = 0.0f;
		float Length = 0.0f;
		USplineMeshComponent* BaseSpline = nullptr;
		USplineMeshComponent* WaveSpline = nullptr;
		USplineMeshComponent* WakeSpline = nullptr;
	};

	struct FWavePathLayer
	{
		int32 Generation = 0;
		float CreatedSeconds = 0.0f;
		float SupersededSeconds = -1.0f;
		float WaveDistance = 0.0f;
		float TotalDistance = 0.0f;
		bool bWaveComplete = false;
		TArray<FWavePathSegment> Segments;
		UMaterialInstanceDynamic* BaseMaterial = nullptr;
		UMaterialInstanceDynamic* WaveMaterial = nullptr;
		UMaterialInstanceDynamic* WakeMaterial = nullptr;
	};

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

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
	TArray<TObjectPtr<USplineMeshComponent>> PathSplinePool;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> FreePathSplines;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PathBaseMaterial;

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
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PathLayerMaterials;

	int32 AddWorldInstance(UInstancedStaticMeshComponent* Component, const FTransform& WorldTransform) const;

	FVector CachedTargetLocation = FVector::ZeroVector;
	bool bHasTargetLocation = false;
	bool bLastXRayEnabled = true;
	bool bLastTrailsEnabled = true;
	ESecondarySearchVisualStyle LastVisualStyle = ESecondarySearchVisualStyle::Fluid;
	float CachedVisualSpeed = 1.0f;
	float CachedWaveSpeed = 1.0f;
	float CachedNodeScale = 0.45f;
	float CachedTargetRadius = 44.0f;
	float CachedPathRevealSpeed = 1100.0f;
	float LastNodeBuildSeconds = 0.0f;
	int32 LastVisualizationRevision = -1;
	int32 LastSearchGeneration = -1;
	int32 LastPathLayerGeneration = -1;
	int32 LastExpandedDrawCount = -1;
	int32 LastFrontierDrawCount = -1;
	int32 LastPathPointCount = -1;
	float LastNodeScale = -1.0f;
	TArray<FFluidNodeRecord> FluidNodeRecords;
	TArray<FWavePathLayer> WavePathLayers;
};
