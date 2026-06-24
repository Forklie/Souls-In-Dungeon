// MinimapWidget.h — AAA-style minimap with scene capture + icon overlay + elevation
// Shows actual dungeon geometry via SceneCaptureComponent2D render target,
// with game object icons (chests, enemies, portals) overlaid, including elevation arrows.
// Pure C++ programmatic UMG — no Blueprint assets required.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MinimapDataProvider.h"
#include "MinimapWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UTextBlock;
class UTextureRenderTarget2D;

UCLASS()
class UMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ── Public API ────────────────────────────────────────────
	void SetDataProvider(UMinimapDataProvider* InProvider);
	void SetRenderTarget(UTextureRenderTarget2D* InRT);
	void UpdatePlayerState(const FVector& WorldPos, float CharacterYawDegrees, float CameraYawDegrees, float WorldRadius = 2500.0f);
	void RefreshChestStates(class ALevelManager* LevelManager);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// ── Construction ──────────────────────────────────────────
	void CreateWidgetHierarchy();

	// ── Per-Tick ──────────────────────────────────────────────
	void UpdateRadarIcons();
	void UpdatePlayerArrow();
	void UpdatePortalPulse(float DeltaTime);
	void UpdateCompass();

	// ── Coordinate Mapping ───────────────────────────────────
	/** Convert a world position to minimap pixel position relative to the player */
	FVector2D WorldToRadar(const FVector& WorldPos) const;

	// ── Brush Helpers ─────────────────────────────────────────
	FSlateBrush MakeRoundedBrush(FLinearColor Color, float Rounding = 4.0f,
		float OutlineWidth = 0.0f, FLinearColor OutlineColor = FLinearColor::Transparent);
	FSlateBrush MakeCircleBrush(FLinearColor Color);
	FSlateBrush MakeTriangleBrush(FLinearColor Color);

	// ── Layout Helpers ────────────────────────────────────────
	void EnsureIconPool(int32 NeededCount);

	// ── Data ──────────────────────────────────────────────────
	UPROPERTY(Transient)
	UMinimapDataProvider* DataProvider = nullptr;

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RenderTarget = nullptr;
	bool bRenderTargetApplied = false;

	FVector PlayerWorldPos = FVector::ZeroVector;
	float PlayerYaw = 0.0f;  // Character facing
	float CameraYaw = 0.0f;  // Camera view direction
	float ActiveRadarWorldRadius = 2500.0f;
	float PortalPulseTimer = 0.0f;
	bool bPopulated = false;

	// ── Widget Hierarchy ──────────────────────────────────────
	UPROPERTY() UCanvasPanel* MapCanvas = nullptr;
	UPROPERTY() UCanvasPanel* RotatingCanvas = nullptr;  // Holds map image + icons, rotates together
	UPROPERTY() UImage* BackgroundImage = nullptr;  // Solid background behind RT
	UPROPERTY() UImage* MapImage = nullptr;          // The render target display
	UPROPERTY() UImage* PlayerArrow = nullptr;
	UPROPERTY() UImage* BorderFrame = nullptr;
	UPROPERTY() UTextBlock* MinimapLabel = nullptr;
	UPROPERTY() UCanvasPanelSlot* PlayerArrowSlot = nullptr;

	// Compass labels (N, E, S, W)
	UPROPERTY() UTextBlock* CompassN = nullptr;
	UPROPERTY() UTextBlock* CompassE = nullptr;
	UPROPERTY() UTextBlock* CompassS = nullptr;
	UPROPERTY() UTextBlock* CompassW = nullptr;
	UPROPERTY() UCanvasPanelSlot* CompassNSlot = nullptr;
	UPROPERTY() UCanvasPanelSlot* CompassESlot = nullptr;
	UPROPERTY() UCanvasPanelSlot* CompassSSlot = nullptr;
	UPROPERTY() UCanvasPanelSlot* CompassWSlot = nullptr;

	// Icon pool — reused each tick instead of creating/destroying
	// Each icon has a main dot + an elevation arrow
	struct FMinimapIconPool
	{
		UPROPERTY() UImage* DotImage = nullptr;
		UPROPERTY() UCanvasPanelSlot* DotSlot = nullptr;
		UPROPERTY() UImage* ElevationArrow = nullptr;
		UPROPERTY() UCanvasPanelSlot* ElevationSlot = nullptr;
	};

	UPROPERTY() TArray<UImage*> IconDots;
	UPROPERTY() TArray<UCanvasPanelSlot*> IconDotSlots;
	UPROPERTY() TArray<UImage*> IconArrows;
	UPROPERTY() TArray<UCanvasPanelSlot*> IconArrowSlots;
	int32 ActiveIconCount = 0;

	// FOV Wedge (shows camera look direction)
	UPROPERTY() UImage* FovWedge = nullptr;
	UPROPERTY() UCanvasPanelSlot* FovWedgeSlot = nullptr;

	// ── Visual Tuning Constants ───────────────────────────────
	// Container
	const float ContainerSize = 260.0f;  // px (slightly larger for premium feel)
	const float ContainerMargin = 24.0f; // px from bottom-right corner

	// Radar
	const float RadarWorldRadius = 2500.0f; // World units visible on radar
	const float ChestDetectionRadius = 1500.0f; // Specifically for chests
	const float PlayerArrowSize = 14.0f;    // px

	// Icon sizes
	const float ChestIconSize = 10.0f;
	const float EnemyIconSize = 10.0f;
	const float PortalIconSize = 14.0f;
	const float ElevationArrowSize = 8.0f;

	// Elevation threshold (world units = ~1.5 meters)
	const float ElevationThreshold = 150.0f;

	// Compass radius (distance from center)
	const float CompassRadius = 110.0f;

	// Color palette (AAA style)
	const FLinearColor BgColor        = FLinearColor(0.005f, 0.005f, 0.015f, 0.95f); // Deeper dark
	const FLinearColor BorderColor    = FLinearColor(0.4f, 0.5f, 0.8f, 0.85f);       // Brighter, softer border
	const FLinearColor LabelColor     = FLinearColor(0.6f, 0.75f, 0.95f, 0.8f);
	const FLinearColor PlayerColor    = FLinearColor(0.0f, 1.0f, 0.7f, 1.0f);        // Electric cyan
	const FLinearColor ChestClosedColor = FLinearColor(1.0f, 0.8f, 0.1f, 1.0f);      // Pure gold
	const FLinearColor ChestOpenedColor = FLinearColor(0.2f, 0.25f, 0.3f, 0.4f);     // Very dim
	const FLinearColor EnemyColor     = FLinearColor(1.0f, 0.05f, 0.15f, 1.0f);      // Danger red
	const FLinearColor PortalColor    = FLinearColor(0.8f, 0.2f, 1.0f, 1.0f);        // Neon purple
	const FLinearColor CompassColor   = FLinearColor(0.5f, 0.65f, 0.85f, 0.7f);
	const FLinearColor ElevUpColor    = FLinearColor(0.4f, 0.9f, 1.0f, 1.0f);        // Cyan up
	const FLinearColor ElevDownColor  = FLinearColor(1.0f, 0.4f, 0.1f, 1.0f);        // Orange down
	const FLinearColor FovWedgeColor  = FLinearColor(0.8f, 1.0f, 0.9f, 0.18f);       // More visible FOV
};
