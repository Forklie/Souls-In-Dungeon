#include "EnemyInterceptCsvWriter.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FEnemyInterceptCsvWriter::FEnemyInterceptCsvWriter(const FString& InOutputPath)
	: OutputPath(InOutputPath)
{
}

bool FEnemyInterceptCsvWriter::WriteRow(const FEnemyInterceptCsvRow& Row, FString& OutError)
{
	if (OutputPath.IsEmpty())
	{
		OutError = TEXT("CSV output path is empty");
		return false;
	}

	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(OutputPath) ? FPaths::Combine(FPaths::ProjectDir(), OutputPath) : OutputPath);
	const FString Directory = FPaths::GetPath(AbsolutePath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Failed to create CSV directory: %s"), *Directory);
		return false;
	}

	FString TextToWrite;
	if (!FPaths::FileExists(AbsolutePath))
	{
		TextToWrite += BuildHeader();
		TextToWrite += LINE_TERMINATOR;
	}

	TextToWrite += BuildRow(Row);
	TextToWrite += LINE_TERMINATOR;

	if (!FFileHelper::SaveStringToFile(TextToWrite, *AbsolutePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append))
	{
		OutError = FString::Printf(TEXT("Failed to append CSV row to: %s"), *AbsolutePath);
		return false;
	}

	return true;
}

FString FEnemyInterceptCsvWriter::BuildHeader() const
{
	return TEXT("ScenarioId,Seed,MapName,PlayerMovementSource,PlayerActionSummary,")
		TEXT("EnemyLocationX,EnemyLocationY,EnemyLocationZ,")
		TEXT("EnemyVelocityX,EnemyVelocityY,EnemyVelocityZ,")
		TEXT("PlayerLocationX,PlayerLocationY,PlayerLocationZ,")
		TEXT("PlayerVelocityX,PlayerVelocityY,PlayerVelocityZ,")
		TEXT("PlayerSpeed,EnemySpeed,DistanceToPlayer,ZDelta,LineOfSight,")
		TEXT("DotPlayerMoveWithEnemyDirection,RecentPlayerTurnAmount,TimeSinceLastPlayerDirectionChange,")
		TEXT("BestModeLabel,BestModeScore,")
		TEXT("Score_CurrentLocation,Score_Predict035,Score_Predict075,Score_Predict125,Score_Predict175,")
		TEXT("TimeToAttackRange_CurrentLocation,TimeToAttackRange_Predict035,TimeToAttackRange_Predict075,TimeToAttackRange_Predict125,TimeToAttackRange_Predict175,")
		TEXT("FinalDistance_CurrentLocation,FinalDistance_Predict035,FinalDistance_Predict075,FinalDistance_Predict125,FinalDistance_Predict175,")
		TEXT("StartDistance_CurrentLocation,DistanceReduction_CurrentLocation,")
		TEXT("StartDistance_Predict035,DistanceReduction_Predict035,")
		TEXT("StartDistance_Predict075,DistanceReduction_Predict075,")
		TEXT("StartDistance_Predict125,DistanceReduction_Predict125,")
		TEXT("StartDistance_Predict175,DistanceReduction_Predict175,")
		TEXT("InvalidTargetCount_CurrentLocation,InvalidTargetCount_Predict035,InvalidTargetCount_Predict075,InvalidTargetCount_Predict125,InvalidTargetCount_Predict175,")
		TEXT("RepathCount_CurrentLocation,RepathCount_Predict035,RepathCount_Predict075,RepathCount_Predict125,RepathCount_Predict175,")
		TEXT("FallbackCount_CurrentLocation,FallbackCount_Predict035,FallbackCount_Predict075,FallbackCount_Predict125,FallbackCount_Predict175,")
		TEXT("PathFailureCount_CurrentLocation,PathFailureCount_Predict035,PathFailureCount_Predict075,PathFailureCount_Predict125,PathFailureCount_Predict175");
}

FString FEnemyInterceptCsvWriter::BuildRow(const FEnemyInterceptCsvRow& Row) const
{
	const FEnemyInterceptObservation& O = Row.Observation;
	TArray<FString> Columns;
	Columns.Reserve(55);

	auto AddFloat = [&Columns](float Value)
	{
		Columns.Add(FString::Printf(TEXT("%.6f"), FMath::IsFinite(Value) ? Value : 0.0f));
	};
	auto AddInt = [&Columns](int32 Value)
	{
		Columns.Add(FString::FromInt(Value));
	};

	AddInt(Row.ScenarioId);
	AddInt(Row.Seed);
	Columns.Add(EscapeCsvString(Row.MapName));
	Columns.Add(EscapeCsvString(Row.PlayerMovementSource));
	Columns.Add(EscapeCsvString(Row.PlayerActionSummary));

	AddFloat(O.EnemyLocation.X);
	AddFloat(O.EnemyLocation.Y);
	AddFloat(O.EnemyLocation.Z);
	AddFloat(O.EnemyVelocity.X);
	AddFloat(O.EnemyVelocity.Y);
	AddFloat(O.EnemyVelocity.Z);
	AddFloat(O.PlayerLocation.X);
	AddFloat(O.PlayerLocation.Y);
	AddFloat(O.PlayerLocation.Z);
	AddFloat(O.PlayerVelocity.X);
	AddFloat(O.PlayerVelocity.Y);
	AddFloat(O.PlayerVelocity.Z);
	AddFloat(O.PlayerSpeed);
	AddFloat(O.EnemySpeed);
	AddFloat(O.DistanceToPlayer);
	AddFloat(O.ZDelta);
	AddInt(O.bHasLineOfSight ? 1 : 0);
	AddFloat(O.DotPlayerMoveWithEnemyDirection);
	AddFloat(O.RecentPlayerTurnAmount);
	AddFloat(O.TimeSinceLastPlayerDirectionChange);
	AddInt(ModeToLabel(Row.BestMode));
	AddFloat(Row.BestModeScore);

	AddFloat(Row.CurrentLocation.Score);
	AddFloat(Row.Predict035.Score);
	AddFloat(Row.Predict075.Score);
	AddFloat(Row.Predict125.Score);
	AddFloat(Row.Predict175.Score);
	AddFloat(Row.CurrentLocation.TimeToAttackRange);
	AddFloat(Row.Predict035.TimeToAttackRange);
	AddFloat(Row.Predict075.TimeToAttackRange);
	AddFloat(Row.Predict125.TimeToAttackRange);
	AddFloat(Row.Predict175.TimeToAttackRange);
	AddFloat(Row.CurrentLocation.FinalDistance);
	AddFloat(Row.Predict035.FinalDistance);
	AddFloat(Row.Predict075.FinalDistance);
	AddFloat(Row.Predict125.FinalDistance);
	AddFloat(Row.Predict175.FinalDistance);
	AddFloat(Row.CurrentLocation.StartDistance);
	AddFloat(Row.CurrentLocation.DistanceReduction);
	AddFloat(Row.Predict035.StartDistance);
	AddFloat(Row.Predict035.DistanceReduction);
	AddFloat(Row.Predict075.StartDistance);
	AddFloat(Row.Predict075.DistanceReduction);
	AddFloat(Row.Predict125.StartDistance);
	AddFloat(Row.Predict125.DistanceReduction);
	AddFloat(Row.Predict175.StartDistance);
	AddFloat(Row.Predict175.DistanceReduction);
	AddInt(Row.CurrentLocation.InvalidTargetCount);
	AddInt(Row.Predict035.InvalidTargetCount);
	AddInt(Row.Predict075.InvalidTargetCount);
	AddInt(Row.Predict125.InvalidTargetCount);
	AddInt(Row.Predict175.InvalidTargetCount);
	AddInt(Row.CurrentLocation.RepathCount);
	AddInt(Row.Predict035.RepathCount);
	AddInt(Row.Predict075.RepathCount);
	AddInt(Row.Predict125.RepathCount);
	AddInt(Row.Predict175.RepathCount);
	AddInt(Row.CurrentLocation.FallbackCount);
	AddInt(Row.Predict035.FallbackCount);
	AddInt(Row.Predict075.FallbackCount);
	AddInt(Row.Predict125.FallbackCount);
	AddInt(Row.Predict175.FallbackCount);
	AddInt(Row.CurrentLocation.PathFailureCount);
	AddInt(Row.Predict035.PathFailureCount);
	AddInt(Row.Predict075.PathFailureCount);
	AddInt(Row.Predict125.PathFailureCount);
	AddInt(Row.Predict175.PathFailureCount);

	return FString::Join(Columns, TEXT(","));
}

FString FEnemyInterceptCsvWriter::EscapeCsvString(const FString& Value) const
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

int32 FEnemyInterceptCsvWriter::ModeToLabel(EEnemyInterceptMode Mode) const
{
	return static_cast<int32>(Mode);
}
