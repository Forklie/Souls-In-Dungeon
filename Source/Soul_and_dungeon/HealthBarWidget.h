#pragma once

#include "Blueprint/UserWidget.h"
#include "Styling/SlateColor.h"
#include "HealthBarWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UTextBlock;
class USizeBox;
class UProgressBar;
class UOverlay;
class UBorder;

/**
 * A premium, purely programmatic Health Bar widget.
 * Rebuilt from scratch to provide a high-end visual experience.
 */
UCLASS()
class SOUL_AND_DUNGEON_API UHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Updates the current health values and triggers animations */
	UFUNCTION(BlueprintCallable, Category = "Souls|UI")
	void SetHealthState(float CurrentHealth, float MaxHealth);

	/** Updates the chest counter display */
	UFUNCTION(BlueprintCallable, Category = "Souls|UI")
	void SetChestCounter(int32 OpenedChests, int32 TotalChests);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** Constructs the entire UI hierarchy dynamically */
	void CreateWidgetHierarchy();
	
	/** Updates sizes, colors, and animations each frame */
	void UpdateVisuals(float DeltaTime);

	/** Helper to create stylized brushes */
	FSlateBrush MakeStylizedBrush(FLinearColor Color, float Rounding = 4.0f, float OutlineWidth = 0.0f, FLinearColor OutlineColor = FLinearColor::Transparent);

	/** Helper to calculate health color based on percentage */
	FLinearColor GetDynamicHealthColor(float Percent) const;

	// --- Widget Components ---
	UPROPERTY(Transient)
	USizeBox* RootSizeBox = nullptr;

	UPROPERTY(Transient)
	UImage* BackgroundBlur = nullptr;

	UPROPERTY(Transient)
	UImage* MainFillBar = nullptr;

	UPROPERTY(Transient)
	UImage* GhostFillBar = nullptr;

	UPROPERTY(Transient)
	UImage* FlashOverlay = nullptr;

	UPROPERTY(Transient)
	UImage* LowHealthGlow = nullptr;

	UPROPERTY(Transient)
	UTextBlock* HealthLabel = nullptr;

	UPROPERTY(Transient)
	UTextBlock* HealthValueText = nullptr;

	UPROPERTY(Transient)
	UTextBlock* ChestCounterText = nullptr;

	UPROPERTY(Transient)
	UCanvasPanelSlot* MainFillSlot = nullptr;

	UPROPERTY(Transient)
	UCanvasPanelSlot* GhostFillSlot = nullptr;

	// --- Animation State ---
	float CurrentHealth = 100.0f;
	float MaxHealth = 100.0f;
	float TargetPercent = 1.0f;
	float DisplayPercent = 1.0f;
	float GhostPercent = 1.0f;
	
	float FlashOpacity = 0.0f;
	float LowHealthPulse = 0.0f;
	float PulseTimer = 0.0f;

	int32 ChestsFound = 0;
	int32 ChestsTotal = 0;

	// Constants
	const float BarBaseWidth = 460.0f;
	const float BarBaseHeight = 28.0f;
};
