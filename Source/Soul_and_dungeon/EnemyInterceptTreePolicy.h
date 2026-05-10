#pragma once

#include "CoreMinimal.h"
#include "EnemyInterceptTypes.h"

enum class EEnemyInterceptPolicyFeature : uint8
{
	PlayerSpeed,
	EnemySpeed,
	DistanceToPlayer,
	ZDelta,
	LineOfSight,
	DotPlayerMoveWithEnemyDirection,
	RecentPlayerTurnAmount,
	TimeSinceLastPlayerDirectionChange,
	PlayerVelocityX,
	PlayerVelocityY,
	PlayerVelocityZ,
	EnemyVelocityX,
	EnemyVelocityY,
	EnemyVelocityZ,
	EnemyLocationX,
	EnemyLocationY,
	EnemyLocationZ,
	PlayerLocationX,
	PlayerLocationY,
	PlayerLocationZ
};

struct FEnemyInterceptTreeNode
{
	bool bIsLeaf = false;
	int32 FeatureIndex = INDEX_NONE;
	float Threshold = 0.0f;
	int32 LeftIndex = INDEX_NONE;
	int32 RightIndex = INDEX_NONE;
	TArray<float> ClassProbabilities;
};

struct FEnemyInterceptTree
{
	TArray<FEnemyInterceptTreeNode> Nodes;
};

struct FEnemyInterceptPolicyResult
{
	bool bSuccess = false;
	EEnemyInterceptMode Mode = EEnemyInterceptMode::CurrentLocation;
	float Confidence = 0.0f;
	FString Reason;
};

class SOUL_AND_DUNGEON_API FEnemyInterceptTreePolicy
{
public:
	bool LoadFromFile(const FString& Path, FString& OutError);
	bool IsLoaded() const;
	void Reset();

	const FString& GetLoadedPath() const;
	const FString& GetModelSummary() const;
	const TArray<FString>& GetFeatureNames() const;

	FEnemyInterceptPolicyResult ChooseMode(const FEnemyInterceptObservation& Observation) const;
	bool BuildFeatureVector(const FEnemyInterceptObservation& Observation, TArray<float>& OutFeatures, FString& OutError) const;

private:
	bool ParseFeatureName(const FString& FeatureName, EEnemyInterceptPolicyFeature& OutFeature) const;
	bool ReadNode(const TSharedPtr<class FJsonObject>& NodeObject, int32 FeatureCount, FEnemyInterceptTreeNode& OutNode, FString& OutError) const;
	bool ValidateTree(const FEnemyInterceptTree& Tree, FString& OutError) const;
	float GetFeatureValue(EEnemyInterceptPolicyFeature Feature, const FEnemyInterceptObservation& Observation) const;

	TArray<FString> FeatureNames;
	TArray<EEnemyInterceptPolicyFeature> FeatureMap;
	TArray<int32> ClassLabels;
	TArray<FEnemyInterceptTree> Trees;
	FString LoadedPath;
	FString ModelSummary;
};
