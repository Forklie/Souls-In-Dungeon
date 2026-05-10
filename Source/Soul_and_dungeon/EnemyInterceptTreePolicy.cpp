#include "EnemyInterceptTreePolicy.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr int32 EnemyInterceptClassCount = 5;

bool TryReadIntField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int32& OutValue)
{
	if (!Object.IsValid())
	{
		return false;
	}

	double NumberValue = 0.0;
	if (!Object->TryGetNumberField(FieldName, NumberValue))
	{
		return false;
	}

	OutValue = FMath::RoundToInt(NumberValue);
	return FMath::IsNearlyEqual(NumberValue, static_cast<double>(OutValue), KINDA_SMALL_NUMBER);
}

bool TryReadFloatField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, float& OutValue)
{
	if (!Object.IsValid())
	{
		return false;
	}

	double NumberValue = 0.0;
	if (!Object->TryGetNumberField(FieldName, NumberValue) || !FMath::IsFinite(NumberValue))
	{
		return false;
	}

	OutValue = static_cast<float>(NumberValue);
	return FMath::IsFinite(OutValue);
}

FString ModeLabelToString(int32 ModeLabel)
{
	switch (ModeLabel)
	{
	case 0:
		return TEXT("CurrentLocation");
	case 1:
		return TEXT("Predict035");
	case 2:
		return TEXT("Predict075");
	case 3:
		return TEXT("Predict125");
	case 4:
		return TEXT("Predict175");
	default:
		return FString::Printf(TEXT("Unknown(%d)"), ModeLabel);
	}
}
}

bool FEnemyInterceptTreePolicy::LoadFromFile(const FString& Path, FString& OutError)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		OutError = FString::Printf(TEXT("could not read policy file: %s"), *Path);
		Reset();
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		OutError = FString::Printf(TEXT("invalid policy JSON: %s"), *Path);
		Reset();
		return false;
	}

	int32 FormatVersion = 0;
	if (!TryReadIntField(RootObject, TEXT("format_version"), FormatVersion) || FormatVersion != 1)
	{
		OutError = TEXT("unsupported or missing policy format_version");
		Reset();
		return false;
	}

	FString ModelType;
	if (!RootObject->TryGetStringField(TEXT("model_type"), ModelType) || ModelType != TEXT("RandomForestClassifier"))
	{
		OutError = TEXT("policy model_type must be RandomForestClassifier");
		Reset();
		return false;
	}

	FString RuntimeFeatureSet;
	if (!RootObject->TryGetStringField(TEXT("runtime_feature_set"), RuntimeFeatureSet) || RuntimeFeatureSet != TEXT("EnemyInterceptObservationV1"))
	{
		OutError = TEXT("policy runtime_feature_set must be EnemyInterceptObservationV1");
		Reset();
		return false;
	}

	const TSharedPtr<FJsonObject>* NormalizationObject = nullptr;
	FString NormalizationType;
	if (!RootObject->TryGetObjectField(TEXT("normalization"), NormalizationObject)
		|| !NormalizationObject
		|| !NormalizationObject->IsValid()
		|| !(*NormalizationObject)->TryGetStringField(TEXT("type"), NormalizationType)
		|| NormalizationType != TEXT("none"))
	{
		OutError = TEXT("policy normalization must be {\"type\":\"none\"}");
		Reset();
		return false;
	}

	TArray<FString> ParsedFeatureNames;
	TArray<EEnemyInterceptPolicyFeature> ParsedFeatureMap;
	const TArray<TSharedPtr<FJsonValue>>* FeatureValues = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("feature_names"), FeatureValues) || !FeatureValues || FeatureValues->IsEmpty())
	{
		OutError = TEXT("policy feature_names is missing or empty");
		Reset();
		return false;
	}

	for (const TSharedPtr<FJsonValue>& FeatureValue : *FeatureValues)
	{
		FString FeatureName;
		if (!FeatureValue.IsValid() || !FeatureValue->TryGetString(FeatureName))
		{
			OutError = TEXT("policy feature_names contains a non-string entry");
			Reset();
			return false;
		}

		EEnemyInterceptPolicyFeature ParsedFeature = EEnemyInterceptPolicyFeature::DistanceToPlayer;
		if (!ParseFeatureName(FeatureName, ParsedFeature))
		{
			OutError = FString::Printf(TEXT("unsupported runtime feature: %s"), *FeatureName);
			Reset();
			return false;
		}

		ParsedFeatureNames.Add(FeatureName);
		ParsedFeatureMap.Add(ParsedFeature);
	}

	TArray<int32> ParsedClassLabels;
	const TArray<TSharedPtr<FJsonValue>>* ClassValues = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("class_labels"), ClassValues) || !ClassValues)
	{
		OutError = TEXT("policy class_labels is missing");
		Reset();
		return false;
	}

	for (const TSharedPtr<FJsonValue>& ClassValue : *ClassValues)
	{
		if (!ClassValue.IsValid())
		{
			OutError = TEXT("policy class_labels contains an invalid entry");
			Reset();
			return false;
		}

		const double ClassNumber = ClassValue->AsNumber();
		const int32 ClassLabel = FMath::RoundToInt(ClassNumber);
		if (!FMath::IsNearlyEqual(ClassNumber, static_cast<double>(ClassLabel), KINDA_SMALL_NUMBER))
		{
			OutError = TEXT("policy class_labels must be integers");
			Reset();
			return false;
		}

		ParsedClassLabels.Add(ClassLabel);
	}

	if (ParsedClassLabels.Num() != EnemyInterceptClassCount)
	{
		OutError = TEXT("policy class_labels must contain exactly five labels");
		Reset();
		return false;
	}

	for (int32 LabelIndex = 0; LabelIndex < EnemyInterceptClassCount; ++LabelIndex)
	{
		if (ParsedClassLabels[LabelIndex] != LabelIndex)
		{
			OutError = TEXT("policy class_labels must be [0,1,2,3,4]");
			Reset();
			return false;
		}
	}

	TArray<FEnemyInterceptTree> ParsedTrees;
	const TArray<TSharedPtr<FJsonValue>>* TreeValues = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("trees"), TreeValues) || !TreeValues || TreeValues->IsEmpty())
	{
		OutError = TEXT("policy trees is missing or empty");
		Reset();
		return false;
	}

	for (int32 TreeIndex = 0; TreeIndex < TreeValues->Num(); ++TreeIndex)
	{
		const TSharedPtr<FJsonObject> TreeObject = (*TreeValues)[TreeIndex].IsValid()
			? (*TreeValues)[TreeIndex]->AsObject()
			: nullptr;
		if (!TreeObject.IsValid())
		{
			OutError = FString::Printf(TEXT("policy tree %d is not an object"), TreeIndex);
			Reset();
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
		if (!TreeObject->TryGetArrayField(TEXT("nodes"), NodeValues) || !NodeValues || NodeValues->IsEmpty())
		{
			OutError = FString::Printf(TEXT("policy tree %d has no nodes"), TreeIndex);
			Reset();
			return false;
		}

		FEnemyInterceptTree ParsedTree;
		for (int32 NodeIndex = 0; NodeIndex < NodeValues->Num(); ++NodeIndex)
		{
			const TSharedPtr<FJsonObject> NodeObject = (*NodeValues)[NodeIndex].IsValid()
				? (*NodeValues)[NodeIndex]->AsObject()
				: nullptr;
			if (!NodeObject.IsValid())
			{
				OutError = FString::Printf(TEXT("policy tree %d node %d is not an object"), TreeIndex, NodeIndex);
				Reset();
				return false;
			}

			FEnemyInterceptTreeNode Node;
			if (!ReadNode(NodeObject, ParsedFeatureNames.Num(), Node, OutError))
			{
				OutError = FString::Printf(TEXT("policy tree %d node %d invalid: %s"), TreeIndex, NodeIndex, *OutError);
				Reset();
				return false;
			}

			ParsedTree.Nodes.Add(MoveTemp(Node));
		}

		if (!ValidateTree(ParsedTree, OutError))
		{
			OutError = FString::Printf(TEXT("policy tree %d invalid: %s"), TreeIndex, *OutError);
			Reset();
			return false;
		}

		ParsedTrees.Add(MoveTemp(ParsedTree));
	}

	FeatureNames = MoveTemp(ParsedFeatureNames);
	FeatureMap = MoveTemp(ParsedFeatureMap);
	ClassLabels = MoveTemp(ParsedClassLabels);
	Trees = MoveTemp(ParsedTrees);
	LoadedPath = Path;
	ModelSummary = FString::Printf(
		TEXT("%d-tree RandomForestClassifier, %d features"),
		Trees.Num(),
		FeatureNames.Num());
	OutError.Reset();
	return true;
}

bool FEnemyInterceptTreePolicy::IsLoaded() const
{
	return Trees.Num() > 0 && FeatureMap.Num() > 0 && ClassLabels.Num() == EnemyInterceptClassCount;
}

void FEnemyInterceptTreePolicy::Reset()
{
	FeatureNames.Reset();
	FeatureMap.Reset();
	ClassLabels.Reset();
	Trees.Reset();
	LoadedPath.Reset();
	ModelSummary.Reset();
}

const FString& FEnemyInterceptTreePolicy::GetLoadedPath() const
{
	return LoadedPath;
}

const FString& FEnemyInterceptTreePolicy::GetModelSummary() const
{
	return ModelSummary;
}

const TArray<FString>& FEnemyInterceptTreePolicy::GetFeatureNames() const
{
	return FeatureNames;
}

FEnemyInterceptPolicyResult FEnemyInterceptTreePolicy::ChooseMode(const FEnemyInterceptObservation& Observation) const
{
	FEnemyInterceptPolicyResult Result;
	if (!IsLoaded())
	{
		Result.Reason = TEXT("learned policy is not loaded");
		return Result;
	}

	TArray<float> Features;
	FString FeatureError;
	if (!BuildFeatureVector(Observation, Features, FeatureError))
	{
		Result.Reason = FString::Printf(TEXT("feature build failed: %s"), *FeatureError);
		return Result;
	}

	TArray<float> Scores;
	Scores.Init(0.0f, ClassLabels.Num());

	for (int32 TreeIndex = 0; TreeIndex < Trees.Num(); ++TreeIndex)
	{
		const FEnemyInterceptTree& Tree = Trees[TreeIndex];
		int32 NodeIndex = 0;
		for (int32 StepCount = 0; StepCount <= Tree.Nodes.Num(); ++StepCount)
		{
			if (!Tree.Nodes.IsValidIndex(NodeIndex))
			{
				Result.Reason = FString::Printf(TEXT("tree %d visited invalid node %d"), TreeIndex, NodeIndex);
				return Result;
			}

			const FEnemyInterceptTreeNode& Node = Tree.Nodes[NodeIndex];
			if (Node.bIsLeaf)
			{
				for (int32 ClassIndex = 0; ClassIndex < Scores.Num(); ++ClassIndex)
				{
					Scores[ClassIndex] += Node.ClassProbabilities.IsValidIndex(ClassIndex)
						? Node.ClassProbabilities[ClassIndex]
						: 0.0f;
				}
				break;
			}

			if (!Features.IsValidIndex(Node.FeatureIndex))
			{
				Result.Reason = FString::Printf(TEXT("tree %d requested invalid feature %d"), TreeIndex, Node.FeatureIndex);
				return Result;
			}

			NodeIndex = Features[Node.FeatureIndex] <= Node.Threshold
				? Node.LeftIndex
				: Node.RightIndex;

			if (StepCount == Tree.Nodes.Num())
			{
				Result.Reason = FString::Printf(TEXT("tree %d exceeded traversal guard"), TreeIndex);
				return Result;
			}
		}
	}

	int32 BestClassIndex = INDEX_NONE;
	float BestScore = -FLT_MAX;
	for (int32 ClassIndex = 0; ClassIndex < Scores.Num(); ++ClassIndex)
	{
		if (Scores[ClassIndex] > BestScore)
		{
			BestScore = Scores[ClassIndex];
			BestClassIndex = ClassIndex;
		}
	}

	if (!ClassLabels.IsValidIndex(BestClassIndex) || ClassLabels[BestClassIndex] < 0 || ClassLabels[BestClassIndex] > 4)
	{
		Result.Reason = TEXT("learned policy returned an invalid class label");
		return Result;
	}

	Result.bSuccess = true;
	Result.Mode = static_cast<EEnemyInterceptMode>(ClassLabels[BestClassIndex]);
	Result.Confidence = Trees.Num() > 0 ? BestScore / static_cast<float>(Trees.Num()) : 0.0f;
	Result.Reason = FString::Printf(
		TEXT("learned policy %s confidence=%.3f"),
		*ModeLabelToString(ClassLabels[BestClassIndex]),
		Result.Confidence);
	return Result;
}

bool FEnemyInterceptTreePolicy::BuildFeatureVector(const FEnemyInterceptObservation& Observation, TArray<float>& OutFeatures, FString& OutError) const
{
	if (FeatureMap.IsEmpty())
	{
		OutError = TEXT("policy has no feature map");
		return false;
	}

	OutFeatures.Reset(FeatureMap.Num());
	for (EEnemyInterceptPolicyFeature Feature : FeatureMap)
	{
		const float Value = GetFeatureValue(Feature, Observation);
		if (!FMath::IsFinite(Value))
		{
			OutError = TEXT("feature value was non-finite");
			OutFeatures.Reset();
			return false;
		}

		OutFeatures.Add(Value);
	}

	OutError.Reset();
	return true;
}

bool FEnemyInterceptTreePolicy::ParseFeatureName(const FString& FeatureName, EEnemyInterceptPolicyFeature& OutFeature) const
{
	if (FeatureName == TEXT("PlayerSpeed")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerSpeed; return true; }
	if (FeatureName == TEXT("EnemySpeed")) { OutFeature = EEnemyInterceptPolicyFeature::EnemySpeed; return true; }
	if (FeatureName == TEXT("DistanceToPlayer")) { OutFeature = EEnemyInterceptPolicyFeature::DistanceToPlayer; return true; }
	if (FeatureName == TEXT("ZDelta")) { OutFeature = EEnemyInterceptPolicyFeature::ZDelta; return true; }
	if (FeatureName == TEXT("LineOfSight")) { OutFeature = EEnemyInterceptPolicyFeature::LineOfSight; return true; }
	if (FeatureName == TEXT("DotPlayerMoveWithEnemyDirection")) { OutFeature = EEnemyInterceptPolicyFeature::DotPlayerMoveWithEnemyDirection; return true; }
	if (FeatureName == TEXT("RecentPlayerTurnAmount")) { OutFeature = EEnemyInterceptPolicyFeature::RecentPlayerTurnAmount; return true; }
	if (FeatureName == TEXT("TimeSinceLastPlayerDirectionChange")) { OutFeature = EEnemyInterceptPolicyFeature::TimeSinceLastPlayerDirectionChange; return true; }
	if (FeatureName == TEXT("PlayerVelocityX")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerVelocityX; return true; }
	if (FeatureName == TEXT("PlayerVelocityY")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerVelocityY; return true; }
	if (FeatureName == TEXT("PlayerVelocityZ")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerVelocityZ; return true; }
	if (FeatureName == TEXT("EnemyVelocityX")) { OutFeature = EEnemyInterceptPolicyFeature::EnemyVelocityX; return true; }
	if (FeatureName == TEXT("EnemyVelocityY")) { OutFeature = EEnemyInterceptPolicyFeature::EnemyVelocityY; return true; }
	if (FeatureName == TEXT("EnemyVelocityZ")) { OutFeature = EEnemyInterceptPolicyFeature::EnemyVelocityZ; return true; }
	if (FeatureName == TEXT("EnemyLocationX")) { OutFeature = EEnemyInterceptPolicyFeature::EnemyLocationX; return true; }
	if (FeatureName == TEXT("EnemyLocationY")) { OutFeature = EEnemyInterceptPolicyFeature::EnemyLocationY; return true; }
	if (FeatureName == TEXT("EnemyLocationZ")) { OutFeature = EEnemyInterceptPolicyFeature::EnemyLocationZ; return true; }
	if (FeatureName == TEXT("PlayerLocationX")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerLocationX; return true; }
	if (FeatureName == TEXT("PlayerLocationY")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerLocationY; return true; }
	if (FeatureName == TEXT("PlayerLocationZ")) { OutFeature = EEnemyInterceptPolicyFeature::PlayerLocationZ; return true; }

	return false;
}

bool FEnemyInterceptTreePolicy::ReadNode(const TSharedPtr<FJsonObject>& NodeObject, int32 FeatureCount, FEnemyInterceptTreeNode& OutNode, FString& OutError) const
{
	const TArray<TSharedPtr<FJsonValue>>* ValueArray = nullptr;
	if (NodeObject->TryGetArrayField(TEXT("value"), ValueArray))
	{
		if (!ValueArray || ValueArray->Num() != EnemyInterceptClassCount)
		{
			OutError = TEXT("leaf value must contain five class probabilities");
			return false;
		}

		OutNode.bIsLeaf = true;
		OutNode.ClassProbabilities.Reset(EnemyInterceptClassCount);
		for (const TSharedPtr<FJsonValue>& JsonValue : *ValueArray)
		{
			if (!JsonValue.IsValid())
			{
				OutError = TEXT("leaf value contains an invalid probability");
				return false;
			}

			const float Probability = static_cast<float>(JsonValue->AsNumber());
			if (!FMath::IsFinite(Probability) || Probability < 0.0f)
			{
				OutError = TEXT("leaf value contains a negative or non-finite probability");
				return false;
			}
			OutNode.ClassProbabilities.Add(Probability);
		}
		return true;
	}

	int32 FeatureIndex = INDEX_NONE;
	int32 LeftIndex = INDEX_NONE;
	int32 RightIndex = INDEX_NONE;
	float Threshold = 0.0f;
	if (!TryReadIntField(NodeObject, TEXT("feature"), FeatureIndex)
		|| !TryReadFloatField(NodeObject, TEXT("threshold"), Threshold)
		|| !TryReadIntField(NodeObject, TEXT("left"), LeftIndex)
		|| !TryReadIntField(NodeObject, TEXT("right"), RightIndex))
	{
		OutError = TEXT("decision node must include feature, threshold, left, and right");
		return false;
	}

	if (FeatureIndex < 0 || FeatureIndex >= FeatureCount)
	{
		OutError = FString::Printf(TEXT("feature index %d is outside feature count %d"), FeatureIndex, FeatureCount);
		return false;
	}

	OutNode.bIsLeaf = false;
	OutNode.FeatureIndex = FeatureIndex;
	OutNode.Threshold = Threshold;
	OutNode.LeftIndex = LeftIndex;
	OutNode.RightIndex = RightIndex;
	return true;
}

bool FEnemyInterceptTreePolicy::ValidateTree(const FEnemyInterceptTree& Tree, FString& OutError) const
{
	if (Tree.Nodes.IsEmpty())
	{
		OutError = TEXT("tree has no nodes");
		return false;
	}

	for (int32 NodeIndex = 0; NodeIndex < Tree.Nodes.Num(); ++NodeIndex)
	{
		const FEnemyInterceptTreeNode& Node = Tree.Nodes[NodeIndex];
		if (Node.bIsLeaf)
		{
			if (Node.ClassProbabilities.Num() != EnemyInterceptClassCount)
			{
				OutError = FString::Printf(TEXT("leaf node %d has the wrong class probability count"), NodeIndex);
				return false;
			}
			continue;
		}

		if (!Tree.Nodes.IsValidIndex(Node.LeftIndex) || !Tree.Nodes.IsValidIndex(Node.RightIndex))
		{
			OutError = FString::Printf(TEXT("node %d has an invalid child index"), NodeIndex);
			return false;
		}

		if (Node.LeftIndex == NodeIndex || Node.RightIndex == NodeIndex)
		{
			OutError = FString::Printf(TEXT("node %d points to itself"), NodeIndex);
			return false;
		}
	}

	OutError.Reset();
	return true;
}

float FEnemyInterceptTreePolicy::GetFeatureValue(EEnemyInterceptPolicyFeature Feature, const FEnemyInterceptObservation& Observation) const
{
	switch (Feature)
	{
	case EEnemyInterceptPolicyFeature::PlayerSpeed:
		return Observation.PlayerSpeed;
	case EEnemyInterceptPolicyFeature::EnemySpeed:
		return Observation.EnemySpeed;
	case EEnemyInterceptPolicyFeature::DistanceToPlayer:
		return Observation.DistanceToPlayer;
	case EEnemyInterceptPolicyFeature::ZDelta:
		return Observation.ZDelta;
	case EEnemyInterceptPolicyFeature::LineOfSight:
		return Observation.bHasLineOfSight ? 1.0f : 0.0f;
	case EEnemyInterceptPolicyFeature::DotPlayerMoveWithEnemyDirection:
		return Observation.DotPlayerMoveWithEnemyDirection;
	case EEnemyInterceptPolicyFeature::RecentPlayerTurnAmount:
		return Observation.RecentPlayerTurnAmount;
	case EEnemyInterceptPolicyFeature::TimeSinceLastPlayerDirectionChange:
		return Observation.TimeSinceLastPlayerDirectionChange;
	case EEnemyInterceptPolicyFeature::PlayerVelocityX:
		return Observation.PlayerVelocity.X;
	case EEnemyInterceptPolicyFeature::PlayerVelocityY:
		return Observation.PlayerVelocity.Y;
	case EEnemyInterceptPolicyFeature::PlayerVelocityZ:
		return Observation.PlayerVelocity.Z;
	case EEnemyInterceptPolicyFeature::EnemyVelocityX:
		return Observation.EnemyVelocity.X;
	case EEnemyInterceptPolicyFeature::EnemyVelocityY:
		return Observation.EnemyVelocity.Y;
	case EEnemyInterceptPolicyFeature::EnemyVelocityZ:
		return Observation.EnemyVelocity.Z;
	case EEnemyInterceptPolicyFeature::EnemyLocationX:
		return Observation.EnemyLocation.X;
	case EEnemyInterceptPolicyFeature::EnemyLocationY:
		return Observation.EnemyLocation.Y;
	case EEnemyInterceptPolicyFeature::EnemyLocationZ:
		return Observation.EnemyLocation.Z;
	case EEnemyInterceptPolicyFeature::PlayerLocationX:
		return Observation.PlayerLocation.X;
	case EEnemyInterceptPolicyFeature::PlayerLocationY:
		return Observation.PlayerLocation.Y;
	case EEnemyInterceptPolicyFeature::PlayerLocationZ:
		return Observation.PlayerLocation.Z;
	default:
		return 0.0f;
	}
}
