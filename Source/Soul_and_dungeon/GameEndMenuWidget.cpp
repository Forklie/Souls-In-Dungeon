#include "GameEndMenuWidget.h"

#include "GameMenuConstants.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/GameplayStatics.h"

void UGameEndMenuWidget::ConfigureForDeath()
{
	bVictory = false;
	UpdateText();
}

void UGameEndMenuWidget::ConfigureForVictory()
{
	bVictory = true;
	UpdateText();
}

void UGameEndMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidget();
	UpdateText();
}

void UGameEndMenuWidget::BuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass(), TEXT("GameEndMenuWidgetTree"), RF_Transient);
	}
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UImage* Dimmer = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Dimmer"));
	Dimmer->SetBrush(MakeBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.80f)));
	UCanvasPanelSlot* DimmerSlot = RootCanvas->AddChildToCanvas(Dimmer);
	DimmerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	DimmerSlot->SetOffsets(FMargin(0.0f));

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EndFrame"));
	Frame->SetBrush(MakeBrush(FLinearColor(0.018f, 0.014f, 0.012f, 0.93f), 10.0f, 2.0f, FLinearColor(0.76f, 0.40f, 0.12f, 0.75f)));
	Frame->SetPadding(FMargin(42.0f, 36.0f));
	UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetSize(FVector2D(660.0f, 500.0f));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EndContent"));
	Frame->AddChild(Content);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetJustification(ETextJustify::Center);
	FSlateFontInfo TitleFont = TitleText->GetFont();
	TitleFont.Size = 54;
	TitleText->SetFont(TitleFont);
	Content->AddChildToVerticalBox(TitleText);

	SubtitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SubtitleText"));
	SubtitleText->SetJustification(ETextJustify::Center);
	FSlateFontInfo SubtitleFont = SubtitleText->GetFont();
	SubtitleFont.Size = 20;
	SubtitleText->SetFont(SubtitleFont);
	SubtitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.84f, 0.76f, 0.64f, 1.0f)));
	UVerticalBoxSlot* SubtitleSlot = Content->AddChildToVerticalBox(SubtitleText);
	SubtitleSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 34.0f));

	UButton* PrimaryButton = AddButton(Content, TEXT(""));
	PrimaryButton->OnClicked.AddDynamic(this, &UGameEndMenuWidget::HandlePrimaryAction);

	UButton* MainMenuButton = AddButton(Content, TEXT("MAIN MENU"));
	MainMenuButton->OnClicked.AddDynamic(this, &UGameEndMenuWidget::HandleMainMenu);

	UTextBlock* FooterText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterText"));
	FooterText->SetText(FText::FromString(TEXT("Return stronger. The boss room waits.")));
	FooterText->SetJustification(ETextJustify::Center);
	FSlateFontInfo FooterFont = FooterText->GetFont();
	FooterFont.Size = 14;
	FooterText->SetFont(FooterFont);
	FooterText->SetColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.50f, 0.40f, 1.0f)));
	UVerticalBoxSlot* FooterSlot = Content->AddChildToVerticalBox(FooterText);
	FooterSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
}

UButton* UGameEndMenuWidget::AddButton(UVerticalBox* Parent, const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("Button_%d"), Parent->GetChildrenCount()));
	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(MakeBrush(FLinearColor(0.105f, 0.024f, 0.018f, 0.98f), 6.0f, 1.0f, FLinearColor(0.58f, 0.30f, 0.10f, 0.75f)));
	ButtonStyle.SetHovered(MakeBrush(FLinearColor(0.29f, 0.065f, 0.034f, 1.0f), 6.0f, 2.0f, FLinearColor(1.0f, 0.62f, 0.17f, 1.0f)));
	ButtonStyle.SetPressed(MakeBrush(FLinearColor(0.56f, 0.19f, 0.055f, 1.0f), 6.0f, 2.0f, FLinearColor(1.0f, 0.76f, 0.24f, 1.0f)));
	ButtonStyle.SetNormalPadding(FMargin(0.0f));
	ButtonStyle.SetPressedPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	Button->SetStyle(ButtonStyle);
	UVerticalBoxSlot* ButtonSlot = Parent->AddChildToVerticalBox(Button);
	ButtonSlot->SetPadding(FMargin(108.0f, 8.0f));

	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("ButtonSize_%d"), Parent->GetChildrenCount()));
	SizeBox->SetHeightOverride(56.0f);
	Button->AddChild(SizeBox);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *FString::Printf(TEXT("ButtonRow_%d"), Parent->GetChildrenCount()));
	SizeBox->AddChild(Row);

	USizeBox* AccentBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("AccentBox_%d"), Parent->GetChildrenCount()));
	AccentBox->SetWidthOverride(5.0f);
	UImage* Accent = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("Accent_%d"), Parent->GetChildrenCount()));
	Accent->SetBrush(MakeBrush(FLinearColor(1.0f, 0.55f, 0.14f, 1.0f), 4.0f));
	AccentBox->AddChild(Accent);
	UHorizontalBoxSlot* AccentSlot = Row->AddChildToHorizontalBox(AccentBox);
	AccentSlot->SetPadding(FMargin(10.0f, 9.0f, 12.0f, 9.0f));

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("ButtonText_%d"), Parent->GetChildrenCount()));
	Text->SetText(FText::FromString(Label));
	Text->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 22;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.88f, 0.70f, 1.0f)));
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetPadding(FMargin(0.0f, 13.0f, 22.0f, 0.0f));

	if (Label.IsEmpty())
	{
		PrimaryButtonText = Text;
	}
	return Button;
}

void UGameEndMenuWidget::UpdateText()
{
	if (TitleText)
	{
		TitleText->SetText(FText::FromString(bVictory ? TEXT("VICTORY") : TEXT("YOU DIED")));
		TitleText->SetColorAndOpacity(FSlateColor(bVictory ? FLinearColor(1.0f, 0.78f, 0.28f, 1.0f) : FLinearColor(0.82f, 0.06f, 0.04f, 1.0f)));
	}
	if (SubtitleText)
	{
		SubtitleText->SetText(FText::FromString(bVictory ? TEXT("The boss room is conquered.") : TEXT("The dungeon claims another soul.")));
	}
	if (PrimaryButtonText)
	{
		PrimaryButtonText->SetText(FText::FromString(bVictory ? TEXT("PLAY AGAIN") : TEXT("RESTART")));
	}
}

FSlateBrush UGameEndMenuWidget::MakeBrush(FLinearColor Color, float Rounding, float OutlineWidth, FLinearColor OutlineColor) const
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

void UGameEndMenuWidget::HandlePrimaryAction()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
	}
	UGameplayStatics::OpenLevel(this, SoulDungeonMenu::BossRoomLevelName);
}

void UGameEndMenuWidget::HandleMainMenu()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
	}
	UGameplayStatics::OpenLevel(this, SoulDungeonMenu::MainMenuLevelName);
}
