#include "HealthBarWidget.h"
#include "Soul_and_dungeon.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"

void UHealthBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	CreateWidgetHierarchy();
}

void UHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UHealthBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateVisuals(InDeltaTime);
}

void UHealthBarWidget::SetHealthState(float InCurrentHealth, float InMaxHealth)
{
	MaxHealth = FMath::Max(1.0f, InMaxHealth);
	CurrentHealth = FMath::Clamp(InCurrentHealth, 0.0f, MaxHealth);
	
	const float NewTarget = CurrentHealth / MaxHealth;
	
	// If we took damage, trigger flash
	if (NewTarget < TargetPercent)
	{
		FlashOpacity = 1.0f;
	}
	
	TargetPercent = NewTarget;
}

void UHealthBarWidget::SetChestCounter(int32 OpenedChests, int32 TotalChests)
{
	ChestsFound = OpenedChests;
	ChestsTotal = TotalChests;
	
	if (ChestCounterText)
	{
		ChestCounterText->SetText(FText::Format(FText::FromString(TEXT("CHESTS {0} / {1}")), FText::AsNumber(ChestsFound), FText::AsNumber(ChestsTotal)));
	}
}

void UHealthBarWidget::CreateWidgetHierarchy()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass(), TEXT("WidgetTree"), RF_Transient);
	}

	UE_LOG(LogSoul_and_dungeon, Log, TEXT("UHealthBarWidget::CreateWidgetHierarchy - Initializing programmatic UI (Tree: %s)"), WidgetTree ? TEXT("Valid") : TEXT("Null"));
	if (!WidgetTree) return;

	// 1. Screen-Space Root Canvas
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// 2. Main Container Size Box (Anchored to Bottom-Left)
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	UCanvasPanelSlot* RootSlot = RootCanvas->AddChildToCanvas(RootSizeBox);
	if (RootSlot)
	{
		RootSlot->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
		RootSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		RootSlot->SetPosition(FVector2D(50.0f, -50.0f));
		RootSlot->SetSize(FVector2D(520.0f, 140.0f));
	}

	// 3. Inner Canvas (for internal layout)
	UCanvasPanel* MainCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MainCanvas"));
	RootSizeBox->AddChild(MainCanvas);

	// 4. Stylized Background (Glassmorphism)
	BackgroundBlur = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BackgroundBlur"));
	BackgroundBlur->SetBrush(MakeStylizedBrush(FLinearColor(0.01f, 0.02f, 0.05f, 0.85f), 15.0f, 2.0f, FLinearColor(0.2f, 0.3f, 0.5f, 0.4f)));
	UCanvasPanelSlot* BgSlot = MainCanvas->AddChildToCanvas(BackgroundBlur);
	BgSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	BgSlot->SetOffsets(FMargin(0));

	// 5. Chest Counter (Top Left)
	ChestCounterText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChestCounterText"));
	ChestCounterText->SetText(FText::FromString("CHESTS 0 / 0"));
	FSlateFontInfo ChestFont = ChestCounterText->GetFont();
	ChestFont.Size = 14;
	ChestCounterText->SetFont(ChestFont);
	ChestCounterText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.2f, 1.0f)));
	
	UCanvasPanelSlot* ChestSlot = MainCanvas->AddChildToCanvas(ChestCounterText);
	ChestSlot->SetAnchors(FAnchors(0, 0, 0, 0));
	ChestSlot->SetPosition(FVector2D(40.0f, 25.0f));
	ChestSlot->SetAutoSize(true);

	// 6. Bar Track (Background)
	UImage* BarTrack = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BarTrack"));
	BarTrack->SetBrush(MakeStylizedBrush(FLinearColor(0.02f, 0.02f, 0.04f, 0.7f), 10.0f));
	UCanvasPanelSlot* TrackSlot = MainCanvas->AddChildToCanvas(BarTrack);
	TrackSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	TrackSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	TrackSlot->SetPosition(FVector2D(0, 0));
	TrackSlot->SetSize(FVector2D(BarBaseWidth, BarBaseHeight + 4.0f));

	// 7. Ghost Fill
	GhostFillBar = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GhostFillBar"));
	GhostFillBar->SetBrush(MakeStylizedBrush(FLinearColor(0.8f, 0.2f, 0.2f, 0.5f), 8.0f));
	GhostFillSlot = MainCanvas->AddChildToCanvas(GhostFillBar);
	GhostFillSlot->SetAnchors(FAnchors(0, 0.5, 0, 0.5));
	GhostFillSlot->SetAlignment(FVector2D(0.0f, 0.5f));
	GhostFillSlot->SetPosition(FVector2D(30.0f, 0.0f));
	GhostFillSlot->SetSize(FVector2D(BarBaseWidth, BarBaseHeight));

	// 8. Main Fill
	MainFillBar = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MainFillBar"));
	MainFillBar->SetBrush(MakeStylizedBrush(FLinearColor(0.2f, 0.8f, 0.4f, 1.0f), 8.0f));
	MainFillSlot = MainCanvas->AddChildToCanvas(MainFillBar);
	MainFillSlot->SetAnchors(FAnchors(0, 0.5, 0, 0.5));
	MainFillSlot->SetAlignment(FVector2D(0.0f, 0.5f));
	MainFillSlot->SetPosition(FVector2D(30.0f, 0.0f));
	MainFillSlot->SetSize(FVector2D(BarBaseWidth, BarBaseHeight));

	// 9. Flash Overlay
	FlashOverlay = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FlashOverlay"));
	FlashOverlay->SetBrush(MakeStylizedBrush(FLinearColor::White, 8.0f));
	FlashOverlay->SetRenderOpacity(0.0f);
	UCanvasPanelSlot* FlashSlot = MainCanvas->AddChildToCanvas(FlashOverlay);
	FlashSlot->SetAnchors(FAnchors(0, 0.5, 0, 0.5));
	FlashSlot->SetAlignment(FVector2D(0.0f, 0.5f));
	FlashSlot->SetPosition(FVector2D(30.0f, 0.0f));
	FlashSlot->SetSize(FVector2D(BarBaseWidth, BarBaseHeight));

	// 10. HP Value Text (Bottom Right)
	HealthValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthValueText"));
	HealthValueText->SetText(FText::FromString("HP 100 / 100"));
	FSlateFontInfo HealthFont = HealthValueText->GetFont();
	HealthFont.Size = 18;
	HealthValueText->SetFont(HealthFont);
	HealthValueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	
	UCanvasPanelSlot* ValueSlot = MainCanvas->AddChildToCanvas(HealthValueText);
	ValueSlot->SetAnchors(FAnchors(1, 1, 1, 1));
	ValueSlot->SetAlignment(FVector2D(1.0f, 1.0f));
	ValueSlot->SetPosition(FVector2D(-40.0f, -25.0f));
	ValueSlot->SetAutoSize(true);

	// 11. Low Health Glow (Pulsing overlay)
	LowHealthGlow = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LowHealthGlow"));
	LowHealthGlow->SetBrush(MakeStylizedBrush(FLinearColor(1.0f, 0.0f, 0.0f, 0.4f), 15.0f));
	LowHealthGlow->SetRenderOpacity(0.0f);
	UCanvasPanelSlot* GlowSlot = MainCanvas->AddChildToCanvas(LowHealthGlow);
	GlowSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	GlowSlot->SetOffsets(FMargin(-12.0f));
}

void UHealthBarWidget::UpdateVisuals(float DeltaTime)
{
	// Smoothly interpolate display percentages
	DisplayPercent = FMath::FInterpTo(DisplayPercent, TargetPercent, DeltaTime, 12.0f);
	GhostPercent = FMath::FInterpTo(GhostPercent, TargetPercent, DeltaTime, 4.0f);
	
	// Update Bar Widths
	if (MainFillSlot)
	{
		MainFillSlot->SetSize(FVector2D(BarBaseWidth * DisplayPercent, BarBaseHeight));
	}
	
	if (GhostFillSlot)
	{
		GhostFillSlot->SetSize(FVector2D(BarBaseWidth * GhostPercent, BarBaseHeight));
	}

	// Update Colors
	FLinearColor HealthColor = GetDynamicHealthColor(DisplayPercent);
	if (MainFillBar)
	{
		MainFillBar->SetBrush(MakeStylizedBrush(HealthColor, 4.0f));
	}

	if (GhostFillBar)
	{
		GhostFillBar->SetBrush(MakeStylizedBrush(HealthColor * 0.3f + FLinearColor(0.1f, 0.0f, 0.0f, 0.5f), 4.0f));
	}
	if (HealthValueText)
	{
		HealthValueText->SetText(FText::Format(FText::FromString(TEXT("HP {0} / {1}")), 
			FText::AsNumber(FMath::RoundToInt(CurrentHealth)), 
			FText::AsNumber(FMath::RoundToInt(MaxHealth))));
		HealthValueText->SetColorAndOpacity(FSlateColor(HealthColor));
	}

	// Flash Logic
	FlashOpacity = FMath::FInterpTo(FlashOpacity, 0.0f, DeltaTime, 8.0f);
	if (FlashOverlay)
	{
		FlashOverlay->SetRenderOpacity(FlashOpacity);
	}

	// Low Health Pulse Logic
	if (DisplayPercent < 0.3f)
	{
		PulseTimer += DeltaTime * 4.0f;
		float PulseVal = (FMath::Sin(PulseTimer) + 1.0f) * 0.5f;
		if (LowHealthGlow)
		{
			LowHealthGlow->SetRenderOpacity(PulseVal * 0.5f);
		}
	}
	else
	{
		if (LowHealthGlow)
		{
			LowHealthGlow->SetRenderOpacity(0.0f);
		}
	}
}

FSlateBrush UHealthBarWidget::MakeStylizedBrush(FLinearColor Color, float Rounding, float OutlineWidth, FLinearColor OutlineColor)
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

FLinearColor UHealthBarWidget::GetDynamicHealthColor(float Percent) const
{
	if (Percent > 0.5f)
	{
		// Green to Yellow
		return FLinearColor::LerpUsingHSV(FLinearColor(1.0f, 0.7f, 0.1f), FLinearColor(0.1f, 0.8f, 0.2f), (Percent - 0.5f) * 2.0f);
	}
	else
	{
		// Yellow to Red
		return FLinearColor::LerpUsingHSV(FLinearColor(0.9f, 0.1f, 0.05f), FLinearColor(1.0f, 0.7f, 0.1f), Percent * 2.0f);
	}
}
