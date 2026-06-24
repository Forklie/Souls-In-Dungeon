// MinimapWidget.cpp — AAA-style minimap with scene capture + icon overlay + elevation
// Uses a shared RotatingCanvas for both the map texture and icons to guarantee
// perfect alignment. The RotatingCanvas rotates by -CameraYaw so camera-forward = up.

#include "MinimapWidget.h"
#include "Soul_and_dungeon.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Engine/TextureRenderTarget2D.h"

// ─────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────

void UMinimapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	CreateWidgetHierarchy();
}

void UMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Apply render target brush once available
	if (RenderTarget && MapImage && !bRenderTargetApplied)
	{
		FSlateBrush RTBrush;
		RTBrush.SetResourceObject(RenderTarget);
		RTBrush.ImageSize = FVector2D(1024, 1024);
		RTBrush.DrawAs = ESlateBrushDrawType::Image;
		RTBrush.TintColor = FSlateColor(FLinearColor::White);
		MapImage->SetBrush(RTBrush);
		bRenderTargetApplied = true;
		UE_LOG(LogSoul_and_dungeon, Log, TEXT("MinimapWidget: Render target applied to map image."));
	}

	// Rotate the shared canvas so both map texture + icons rotate together
	if (RotatingCanvas)
	{
		RotatingCanvas->SetRenderTransformAngle(-CameraYaw);
	}

	if (!DataProvider || !DataProvider->HasData()) return;

	UpdateRadarIcons();
	UpdatePlayerArrow();
	UpdatePortalPulse(InDeltaTime);
	UpdateCompass();
}

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────

void UMinimapWidget::SetDataProvider(UMinimapDataProvider* InProvider)
{
	DataProvider = InProvider;
	UE_LOG(LogSoul_and_dungeon, Log, TEXT("MinimapWidget: DataProvider assigned (%d icons)"),
		InProvider ? InProvider->GetIcons().Num() : 0);
}

#include "LevelManager.h"

void UMinimapWidget::SetRenderTarget(UTextureRenderTarget2D* InRT)
{
	if (RenderTarget == InRT) return;
	RenderTarget = InRT;
	bRenderTargetApplied = false;
	UE_LOG(LogSoul_and_dungeon, Log, TEXT("MinimapWidget: RenderTarget set (%s)"),
		InRT ? TEXT("valid") : TEXT("null"));
}

void UMinimapWidget::UpdatePlayerState(const FVector& WorldPos, float CharacterYawDegrees, float CameraYawDegrees, float WorldRadius)
{
	PlayerWorldPos = WorldPos;
	PlayerYaw = CharacterYawDegrees;
	CameraYaw = CameraYawDegrees;
	ActiveRadarWorldRadius = WorldRadius;
}

void UMinimapWidget::RefreshChestStates(ALevelManager* LevelManager)
{
	if (!DataProvider || !LevelManager) return;

	TArray<FMinimapIconData>& Icons_Mut = DataProvider->GetIconsMutable();

	// Update each chest icon from the specific tracked actor, not from a global count.
	for (FMinimapIconData& Icon : Icons_Mut)
	{
		if (Icon.IconType == EMinimapIconType::Chest || Icon.IconType == EMinimapIconType::ChestOpened)
		{
			bool bIsOpened = Icon.IconType == EMinimapIconType::ChestOpened;

			// If the actor is invalid (e.g., destroyed upon opening), consider it opened.
			if (!Icon.TrackedActor.IsValid())
			{
				bIsOpened = true;
			}
			// Otherwise check whether this exact actor, or its attached parent/child, was opened.
			else if (LevelManager->IsChestOpened(Icon.TrackedActor.Get()))
			{
				bIsOpened = true;
			}
			// Fall back to the chest's actual lid rotation so the minimap stays in sync
			// even if the open notification missed this specific actor.
			else if (LevelManager->IsChestVisuallyOpen(Icon.TrackedActor.Get()))
			{
				bIsOpened = true;
			}

			if (bIsOpened)
			{
				Icon.IconType = EMinimapIconType::ChestOpened;
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────
//  Widget Construction
//
//  Hierarchy:
//    RootCanvas
//      └── ContainerBox (SizeBox, bottom-right anchor)
//            └── MapCanvas (clipped)
//                  ├── BackgroundImage (solid dark)
//                  ├── RotatingCanvas ← ROTATES by -CameraYaw
//                  │     ├── MapImage (render target texture)
//                  │     └── Icon pool (dots + elevation arrows)
//                  ├── PlayerDot (always center, doesn't rotate with map)
//                  ├── Compass labels (N/E/S/W)
//                  ├── MinimapLabel ("MAP")
//                  └── BorderFrame
// ─────────────────────────────────────────────────────────────

void UMinimapWidget::CreateWidgetHierarchy()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass(), TEXT("MinimapWidgetTree"), RF_Transient);
	}
	if (!WidgetTree) return;

	// 1. Screen-space root canvas
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MinimapRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// 2. Container SizeBox — anchored bottom-right
	USizeBox* ContainerBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MinimapContainer"));
	ContainerBox->SetWidthOverride(ContainerSize);
	ContainerBox->SetHeightOverride(ContainerSize);
	UCanvasPanelSlot* ContainerSlot = RootCanvas->AddChildToCanvas(ContainerBox);
	if (ContainerSlot)
	{
		ContainerSlot->SetAnchors(FAnchors(1.0f, 1.0f, 1.0f, 1.0f));
		ContainerSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		ContainerSlot->SetPosition(FVector2D(-ContainerMargin, -ContainerMargin));
		ContainerSlot->SetSize(FVector2D(ContainerSize, ContainerSize));
	}

	// 3. Map canvas (clipped drawing area)
	MapCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MapCanvas"));
	MapCanvas->SetClipping(EWidgetClipping::ClipToBounds);
	ContainerBox->AddChild(MapCanvas);

	// 4. Solid dark background (visible until render target loads, also frames the minimap)
	BackgroundImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapBg"));
	BackgroundImage->SetBrush(MakeRoundedBrush(BgColor, 14.0f, 2.0f, BorderColor));
	UCanvasPanelSlot* BgSlot = MapCanvas->AddChildToCanvas(BackgroundImage);
	BgSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	BgSlot->SetOffsets(FMargin(0));

	// 5. RotatingCanvas — holds BOTH map image AND icons, rotates as a single unit.
	//    Made sqrt(2) larger so corners have content when rotated 45°.
	RotatingCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RotatingCanvas"));
	RotatingCanvas->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	UCanvasPanelSlot* RotSlot = MapCanvas->AddChildToCanvas(RotatingCanvas);
	float RotSize = ContainerSize * 1.42f; // sqrt(2) to fill corners during rotation
	float RotOffset = (RotSize - ContainerSize) * 0.5f;
	RotSlot->SetAnchors(FAnchors(0, 0, 0, 0));
	RotSlot->SetPosition(FVector2D(-RotOffset, -RotOffset));
	RotSlot->SetSize(FVector2D(RotSize, RotSize));

	// 6. Render target map image (the actual dungeon geometry view) — on RotatingCanvas
	MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapRT"));
	FSlateBrush PlaceholderBrush;
	PlaceholderBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	MapImage->SetBrush(PlaceholderBrush);
	MapImage->SetRenderOpacity(0.85f);
	UCanvasPanelSlot* RTSlot = RotatingCanvas->AddChildToCanvas(MapImage);
	RTSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	RTSlot->SetOffsets(FMargin(0));

	// 7. Compass labels (N, E, S, W) — on MapCanvas (don't rotate with map)
	auto MakeCompassLabel = [this](const FString& Letter, FName WidgetName, UTextBlock*& OutText, UCanvasPanelSlot*& OutSlot)
	{
		OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), WidgetName);
		OutText->SetText(FText::FromString(Letter));
		FSlateFontInfo CompassFont = OutText->GetFont();
		CompassFont.Size = 8;
		OutText->SetFont(CompassFont);
		OutText->SetColorAndOpacity(FSlateColor(CompassColor));

		OutSlot = MapCanvas->AddChildToCanvas(OutText);
		OutSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		OutSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		OutSlot->SetAutoSize(true);
		OutSlot->SetPosition(FVector2D(0, 0));
	};

	MakeCompassLabel(TEXT("N"), TEXT("CompassN"), CompassN, CompassNSlot);
	MakeCompassLabel(TEXT("E"), TEXT("CompassE"), CompassE, CompassESlot);
	MakeCompassLabel(TEXT("S"), TEXT("CompassS"), CompassS, CompassSSlot);
	MakeCompassLabel(TEXT("W"), TEXT("CompassW"), CompassW, CompassWSlot);

	if (CompassN)
	{
		CompassN->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.4f, 0.8f)));
	}

	// 8. "MAP" label (top-left inside minimap)
	MinimapLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MinimapLabel"));
	MinimapLabel->SetText(FText::FromString(TEXT("MAP")));
	FSlateFontInfo LabelFont = MinimapLabel->GetFont();
	LabelFont.Size = 9;
	MinimapLabel->SetFont(LabelFont);
	MinimapLabel->SetColorAndOpacity(FSlateColor(LabelColor));

	UCanvasPanelSlot* LabelSlot = MapCanvas->AddChildToCanvas(MinimapLabel);
	LabelSlot->SetAnchors(FAnchors(0, 0, 0, 0));
	LabelSlot->SetPosition(FVector2D(10.0f, 6.0f));
	LabelSlot->SetAutoSize(true);

	// 9. Player dot (always at center of the minimap, on MapCanvas = doesn't rotate)
	PlayerArrow = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PlayerDot"));
	PlayerArrow->SetBrush(MakeCircleBrush(PlayerColor));
	PlayerArrowSlot = MapCanvas->AddChildToCanvas(PlayerArrow);
	PlayerArrowSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	PlayerArrowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PlayerArrowSlot->SetSize(FVector2D(PlayerArrowSize, PlayerArrowSize));
	PlayerArrowSlot->SetPosition(FVector2D(0, 0));

	// 10. Decorative border (top Z, on MapCanvas)
	BorderFrame = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapBorder"));
	BorderFrame->SetBrush(MakeRoundedBrush(FLinearColor::Transparent, 14.0f, 2.0f, BorderColor));
	UCanvasPanelSlot* BorderSlot = MapCanvas->AddChildToCanvas(BorderFrame);
	BorderSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	BorderSlot->SetOffsets(FMargin(0));

	UE_LOG(LogSoul_and_dungeon, Log, TEXT("MinimapWidget: AAA minimap hierarchy built (%.0f x %.0f, rotating=%.0f x %.0f)"),
		ContainerSize, ContainerSize, RotSize, RotSize);
}

// ─────────────────────────────────────────────────────────────
//  Per-Tick: Radar Icon Update with Elevation
//
//  Icons are placed in world-aligned coordinates (no rotation).
//  The RotatingCanvas rotation handles making camera-forward = up.
// ─────────────────────────────────────────────────────────────

void UMinimapWidget::UpdateRadarIcons()
{
	if (!DataProvider || !RotatingCanvas || !WidgetTree) return;

	const TArray<FMinimapIconData>& AllIcons = DataProvider->GetIcons();

	struct FVisibleIcon
	{
		FVector2D RadarPos;
		FLinearColor Color;
		float Size;
		EMinimapIconType Type;
		float DeltaZ;
	};

	TArray<FVisibleIcon> Visible;
	Visible.Reserve(AllIcons.Num());

	for (const FMinimapIconData& Icon : AllIcons)
	{
		FVector IconWorldPos = Icon.WorldLocation;
		if (Icon.TrackedActor.IsValid())
		{
			IconWorldPos = Icon.TrackedActor->GetActorLocation();
		}

		float DistXY = FVector2D::Distance(
			FVector2D(PlayerWorldPos.X, PlayerWorldPos.Y),
			FVector2D(IconWorldPos.X, IconWorldPos.Y));

		const float CurrentRadius = ActiveRadarWorldRadius > 0.0f ? ActiveRadarWorldRadius : RadarWorldRadius;

		// Filter by type-specific radius
		if (Icon.IconType == EMinimapIconType::Chest || Icon.IconType == EMinimapIconType::ChestOpened)
		{
			if (DistXY > CurrentRadius) continue;
		}
		else if (DistXY > CurrentRadius) 
		{
			continue;
		}

		FVisibleIcon VI;
		VI.RadarPos = WorldToRadar(IconWorldPos);
		VI.Type = Icon.IconType;
		VI.DeltaZ = IconWorldPos.Z - PlayerWorldPos.Z;

		switch (Icon.IconType)
		{
		case EMinimapIconType::Chest:
			VI.Color = ChestClosedColor;
			VI.Size = ChestIconSize;
			break;
		case EMinimapIconType::ChestOpened:
			VI.Color = ChestOpenedColor;
			VI.Size = ChestIconSize;
			break;
		case EMinimapIconType::Enemy:
			VI.Color = EnemyColor;
			VI.Size = EnemyIconSize;
			break;
		case EMinimapIconType::Portal:
			VI.Color = PortalColor;
			VI.Size = PortalIconSize;
			break;
		default:
			VI.Color = FLinearColor::White;
			VI.Size = 6.0f;
			break;
		}

		// Fade icons near the edge
		float EdgeFade = 1.0f - FMath::Clamp((DistXY / CurrentRadius - 0.7f) / 0.3f, 0.0f, 1.0f);
		VI.Color.A *= EdgeFade;

		Visible.Add(VI);
	}

	EnsureIconPool(Visible.Num());

	for (int32 i = 0; i < Visible.Num(); ++i)
	{
		UImage* DotImg = IconDots[i];
		UImage* ArrowImg = IconArrows[i];

		if (!DotImg) continue;

		DotImg->SetBrush(MakeCircleBrush(Visible[i].Color));
		// Use Render Scale to change size dynamically (base size is 8x8)
		float Scale = Visible[i].Size / 8.0f;
		DotImg->SetRenderScale(FVector2D(Scale, Scale));
		// Use Render Translation to move it (bypasses layout pass!)
		DotImg->SetRenderTranslation(Visible[i].RadarPos);
		DotImg->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		// Elevation arrow
		if (ArrowImg)
		{
			float DZ = Visible[i].DeltaZ;
			if (FMath::Abs(DZ) > ElevationThreshold)
			{
				bool bAbove = DZ > 0;
				FLinearColor ArrowColor = bAbove ? ElevUpColor : ElevDownColor;
				ArrowColor.A *= Visible[i].Color.A;

				ArrowImg->SetBrush(MakeCircleBrush(ArrowColor));
				
				float ArrowOffsetY = bAbove
					? -(Visible[i].Size * 0.5f + ElevationArrowSize * 0.7f)
					: (Visible[i].Size * 0.5f + ElevationArrowSize * 0.7f);
					
				ArrowImg->SetRenderTranslation(FVector2D(Visible[i].RadarPos.X, Visible[i].RadarPos.Y + ArrowOffsetY));
				// Set fixed scale for arrow
				ArrowImg->SetRenderScale(FVector2D(1.0f, 0.6f));
				ArrowImg->SetRenderTransformAngle(bAbove ? 0.0f : 180.0f);
				ArrowImg->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			else
			{
				ArrowImg->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	for (int32 i = Visible.Num(); i < IconDots.Num(); ++i)
	{
		if (IconDots[i]) IconDots[i]->SetVisibility(ESlateVisibility::Collapsed);
		if (IconArrows.IsValidIndex(i) && IconArrows[i]) IconArrows[i]->SetVisibility(ESlateVisibility::Collapsed);
	}

	ActiveIconCount = Visible.Num();
}

void UMinimapWidget::UpdatePlayerArrow()
{
	if (PlayerArrow)
	{
		// Player dot is on MapCanvas (non-rotating). Show character facing relative to camera.
		PlayerArrow->SetRenderTransformAngle(PlayerYaw - CameraYaw);
	}
}

void UMinimapWidget::UpdatePortalPulse(float DeltaTime)
{
	PortalPulseTimer += DeltaTime * 3.0f;

	for (int32 i = 0; i < ActiveIconCount && i < IconDots.Num(); ++i)
	{
		if (IconDotSlots.IsValidIndex(i) && IconDotSlots[i])
		{
			FVector2D SlotSize = IconDotSlots[i]->GetSize();
			if (FMath::IsNearlyEqual(SlotSize.X, PortalIconSize, 0.5f))
			{
				float PulseAlpha = 0.6f + 0.4f * ((FMath::Sin(PortalPulseTimer) + 1.0f) * 0.5f);
				IconDots[i]->SetRenderOpacity(PulseAlpha);
			}
		}
	}
}

void UMinimapWidget::UpdateCompass()
{
	// Compass markers rotate around the edge based on camera yaw (world-aligned)
	if (!CompassNSlot) return;

	float Radius = ContainerSize * 0.42f;
	float YawRad = FMath::DegreesToRadians(-CameraYaw);

	auto PlaceCompass = [&](UCanvasPanelSlot* CompassSlot, float WorldAngleDeg)
	{
		if (!CompassSlot) return;
		float AngleRad = FMath::DegreesToRadians(WorldAngleDeg) + YawRad;
		float X = FMath::Sin(AngleRad) * Radius;
		float Y = -FMath::Cos(AngleRad) * Radius;
		CompassSlot->SetPosition(FVector2D(X, Y));
	};

	PlaceCompass(CompassNSlot, 0.0f);
	PlaceCompass(CompassESlot, 90.0f);
	PlaceCompass(CompassSSlot, 180.0f);
	PlaceCompass(CompassWSlot, 270.0f);
}

// ─────────────────────────────────────────────────────────────
//  Coordinate Mapping — World-Aligned (no rotation)
//
//  The RotatingCanvas handles rotation visually. Here we only
//  convert world offset to pixel offset with no yaw rotation.
//  Icons are placed relative to the center of the RotatingCanvas.
// ─────────────────────────────────────────────────────────────

FVector2D UMinimapWidget::WorldToRadar(const FVector& WorldPos) const
{
	float DX = WorldPos.X - PlayerWorldPos.X;
	float DY = WorldPos.Y - PlayerWorldPos.Y;

	// The RotatingCanvas is 1.42x the visible size. Icons are anchored at center (0.5, 0.5) of RotatingCanvas.
	// The scene capture's OrthoWidth (which maps to the full width of RotatingCanvas) is RadarWorldRadius * 2.
	// Therefore, the edge of the RotatingCanvas corresponds to RadarWorldRadius.
	float RotSize = ContainerSize * 1.42f;
	float HalfSize = RotSize * 0.5f;
	float CurrentRadius = ActiveRadarWorldRadius > 0.0f ? ActiveRadarWorldRadius : RadarWorldRadius;
	float ScaleFactor = HalfSize / CurrentRadius;

	// UE: X = forward, Y = right
	// Minimap pixels: +X = right, +Y = down
	// So: world forward (+X) = minimap up (-Y pixel), world right (+Y) = minimap right (+X pixel)
	return FVector2D(DY * ScaleFactor, -DX * ScaleFactor);
}

// ─────────────────────────────────────────────────────────────
//  Icon Pool Management — icons go on RotatingCanvas
// ─────────────────────────────────────────────────────────────

void UMinimapWidget::EnsureIconPool(int32 NeededCount)
{
	if (!RotatingCanvas || !WidgetTree) return;

	while (IconDots.Num() < NeededCount)
	{
		int32 Idx = IconDots.Num();

		// Create dot on RotatingCanvas
		FName DotName = *FString::Printf(TEXT("RadarDot_%d"), Idx);
		UImage* Dot = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), DotName);
		Dot->SetBrush(MakeCircleBrush(FLinearColor::White));
		UCanvasPanelSlot* DotSlot = RotatingCanvas->AddChildToCanvas(Dot);
		DotSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		DotSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		DotSlot->SetSize(FVector2D(8, 8));
		DotSlot->SetPosition(FVector2D(0, 0));
		Dot->SetVisibility(ESlateVisibility::Collapsed);

		// Create elevation arrow on RotatingCanvas
		FName ArrowName = *FString::Printf(TEXT("RadarArrow_%d"), Idx);
		UImage* Arrow = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), ArrowName);
		Arrow->SetBrush(MakeCircleBrush(ElevUpColor));
		UCanvasPanelSlot* ArrowSlot = RotatingCanvas->AddChildToCanvas(Arrow);
		ArrowSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		ArrowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ArrowSlot->SetSize(FVector2D(ElevationArrowSize, ElevationArrowSize * 0.6f));
		ArrowSlot->SetPosition(FVector2D(0, 0));
		Arrow->SetVisibility(ESlateVisibility::Collapsed);

		IconDots.Add(Dot);
		IconDotSlots.Add(DotSlot);
		IconArrows.Add(Arrow);
		IconArrowSlots.Add(ArrowSlot);
	}
}

// ─────────────────────────────────────────────────────────────
//  Brush Helpers
// ─────────────────────────────────────────────────────────────

FSlateBrush UMinimapWidget::MakeRoundedBrush(FLinearColor Color, float Rounding, float OutlineWidth, FLinearColor OutlineColor)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Color);

	FSlateBrushOutlineSettings Outline;
	Outline.Width = OutlineWidth;
	Outline.Color = FSlateColor(OutlineColor);
	Outline.CornerRadii = FVector4(Rounding, Rounding, Rounding, Rounding);

	Brush.OutlineSettings = Outline;
	return Brush;
}

FSlateBrush UMinimapWidget::MakeCircleBrush(FLinearColor Color)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Color);

	FSlateBrushOutlineSettings Outline;
	Outline.CornerRadii = FVector4(50.0f, 50.0f, 50.0f, 50.0f);
	Outline.Width = 0.0f;
	Outline.Color = FSlateColor(FLinearColor::Transparent);

	Brush.OutlineSettings = Outline;
	return Brush;
}

FSlateBrush UMinimapWidget::MakeTriangleBrush(FLinearColor Color)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Color);

	FSlateBrushOutlineSettings Outline;
	Outline.CornerRadii = FVector4(2.0f, 50.0f, 2.0f, 50.0f);
	Outline.Width = 0.0f;
	Outline.Color = FSlateColor(FLinearColor::Transparent);

	Brush.OutlineSettings = Outline;
	return Brush;
}
