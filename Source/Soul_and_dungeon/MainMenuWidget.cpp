#include "MainMenuWidget.h"

#include "GameMenuConstants.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidget();
}

void UMainMenuWidget::BuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass(), TEXT("MainMenuWidgetTree"), RF_Transient);
	}
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DungeonBackdrop"));
	Backdrop->SetBrush(MakeBrush(FLinearColor(0.010f, 0.006f, 0.004f, 1.0f)));
	UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(Backdrop);
	BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	BackdropSlot->SetOffsets(FMargin(0.0f));

	UImage* Vignette = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Vignette"));
	Vignette->SetBrush(MakeBrush(FLinearColor(0.18f, 0.024f, 0.018f, 0.38f), 0.0f, 3.0f, FLinearColor(0.95f, 0.56f, 0.18f, 0.32f)));
	UCanvasPanelSlot* VignetteSlot = RootCanvas->AddChildToCanvas(Vignette);
	VignetteSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	VignetteSlot->SetOffsets(FMargin(44.0f));

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuFrame"));
	Frame->SetBrush(MakeBrush(FLinearColor(0.020f, 0.016f, 0.014f, 0.88f), 10.0f, 2.0f, FLinearColor(0.70f, 0.42f, 0.16f, 0.70f)));
	Frame->SetPadding(FMargin(42.0f, 38.0f));
	UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetPosition(FVector2D(0.0f, -8.0f));
	FrameSlot->SetSize(FVector2D(720.0f, 790.0f));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuContent"));
	Frame->AddChild(Content);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("SOULS IN DUNGEON")));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 64;
	Title->SetFont(TitleFont);
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.25f, 1.0f)));
	UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(Title);
	TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UTextBlock* Subtitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Subtitle"));
	Subtitle->SetText(FText::FromString(TEXT("BOSS ROOM")));
	FSlateFontInfo SubtitleFont = Subtitle->GetFont();
	SubtitleFont.Size = 22;
	Subtitle->SetFont(SubtitleFont);
	Subtitle->SetJustification(ETextJustify::Center);
	Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.83f, 0.72f, 0.58f, 1.0f)));
	UVerticalBoxSlot* SubtitleSlot = Content->AddChildToVerticalBox(Subtitle);
	SubtitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 34.0f));

	USizeBox* DividerBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TitleDividerBox"));
	DividerBox->SetHeightOverride(2.0f);
	UImage* Divider = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TitleDivider"));
	Divider->SetBrush(MakeBrush(FLinearColor(0.82f, 0.45f, 0.13f, 0.86f), 2.0f));
	DividerBox->AddChild(Divider);
	UVerticalBoxSlot* DividerSlot = Content->AddChildToVerticalBox(DividerBox);
	DividerSlot->SetPadding(FMargin(92.0f, 0.0f, 92.0f, 26.0f));

	ButtonPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonPanel"));
	Content->AddChildToVerticalBox(ButtonPanel);

	UButton* NewGameButton = AddMenuButton(ButtonPanel, TEXT("NEW GAME"));
	NewGameButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleNewGame);

	AddMenuButton(ButtonPanel, TEXT("CONTINUE"), false);

	UButton* SettingsButton = AddMenuButton(ButtonPanel, TEXT("SETTINGS"));
	SettingsButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleSettings);

	UButton* CreditsButton = AddMenuButton(ButtonPanel, TEXT("CREDITS"));
	CreditsButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleCredits);

	UButton* QuitButton = AddMenuButton(ButtonPanel, TEXT("QUIT"));
	QuitButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleQuit);

	AddPanelLine(Content, TEXT("NEW GAME loads the boss room encounter"), 14);

	SettingsPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsPanel"));
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	Content->AddChildToVerticalBox(SettingsPanel);
	AddPanelLine(SettingsPanel, TEXT("SETTINGS"), 30);
	AddPanelLine(SettingsPanel, TEXT("Audio and display settings"), 16);

	UHorizontalBox* FullscreenRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FullscreenRow"));
	UVerticalBoxSlot* FullscreenRowSlot = SettingsPanel->AddChildToVerticalBox(FullscreenRow);
	FullscreenRowSlot->SetPadding(FMargin(86.0f, 22.0f, 86.0f, 16.0f));

	UTextBlock* FullscreenText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FullscreenText"));
	FullscreenText->SetText(FText::FromString(TEXT("FULLSCREEN")));
	FSlateFontInfo RowFont = FullscreenText->GetFont();
	RowFont.Size = 20;
	FullscreenText->SetFont(RowFont);
	FullscreenText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.88f, 0.76f, 1.0f)));
	UHorizontalBoxSlot* FullscreenTextSlot = FullscreenRow->AddChildToHorizontalBox(FullscreenText);
	FullscreenTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UCheckBox* FullscreenCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("FullscreenCheckBox"));
	if (GEngine && GEngine->GetGameUserSettings())
	{
		FullscreenCheckBox->SetIsChecked(GEngine->GetGameUserSettings()->GetFullscreenMode() == EWindowMode::Fullscreen);
	}
	FullscreenCheckBox->OnCheckStateChanged.AddDynamic(this, &UMainMenuWidget::HandleFullscreenChanged);
	FullscreenRow->AddChildToHorizontalBox(FullscreenCheckBox);

	UButton* SettingsBackButton = AddMenuButton(SettingsPanel, TEXT("BACK"));
	SettingsBackButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleBack);

	CreditsPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CreditsPanel"));
	CreditsPanel->SetVisibility(ESlateVisibility::Collapsed);
	Content->AddChildToVerticalBox(CreditsPanel);
	AddPanelLine(CreditsPanel, TEXT("CREDITS"), 30);
	AddPanelLine(CreditsPanel, TEXT("Souls In Dungeon"), 22);
	AddPanelLine(CreditsPanel, TEXT("Unreal Engine dungeon combat prototype"), 16);
	UButton* CreditsBackButton = AddMenuButton(CreditsPanel, TEXT("BACK"));
	CreditsBackButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleBack);
}

UButton* UMainMenuWidget::AddMenuButton(UVerticalBox* Parent, const FString& Label, bool bEnabled)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("Button_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	Button->SetIsEnabled(bEnabled);
	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(MakeBrush(FLinearColor(0.115f, 0.024f, 0.018f, 0.98f), 6.0f, 1.0f, FLinearColor(0.62f, 0.32f, 0.10f, 0.75f)));
	ButtonStyle.SetHovered(MakeBrush(FLinearColor(0.30f, 0.070f, 0.034f, 1.0f), 6.0f, 2.0f, FLinearColor(1.0f, 0.63f, 0.18f, 1.0f)));
	ButtonStyle.SetPressed(MakeBrush(FLinearColor(0.58f, 0.20f, 0.055f, 1.0f), 6.0f, 2.0f, FLinearColor(1.0f, 0.78f, 0.28f, 1.0f)));
	ButtonStyle.SetDisabled(MakeBrush(FLinearColor(0.050f, 0.046f, 0.044f, 0.68f), 6.0f, 1.0f, FLinearColor(0.16f, 0.14f, 0.12f, 0.80f)));
	ButtonStyle.SetNormalPadding(FMargin(0.0f));
	ButtonStyle.SetPressedPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	Button->SetStyle(ButtonStyle);
	UVerticalBoxSlot* ButtonSlot = Parent->AddChildToVerticalBox(Button);
	ButtonSlot->SetPadding(FMargin(82.0f, 8.0f));

	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("Size_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	SizeBox->SetHeightOverride(62.0f);
	Button->AddChild(SizeBox);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *FString::Printf(TEXT("Row_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	SizeBox->AddChild(Row);

	USizeBox* AccentBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("AccentBox_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	AccentBox->SetWidthOverride(6.0f);
	UImage* Accent = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("Accent_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	Accent->SetBrush(MakeBrush(bEnabled ? FLinearColor(1.0f, 0.55f, 0.14f, 1.0f) : FLinearColor(0.20f, 0.18f, 0.16f, 1.0f), 4.0f));
	AccentBox->AddChild(Accent);
	UHorizontalBoxSlot* AccentSlot = Row->AddChildToHorizontalBox(AccentBox);
	AccentSlot->SetPadding(FMargin(10.0f, 9.0f, 14.0f, 9.0f));

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("Text_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	Text->SetText(FText::FromString(bEnabled ? Label : FString::Printf(TEXT("%s  -  LOCKED"), *Label)));
	Text->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 24;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(bEnabled ? FLinearColor(0.96f, 0.88f, 0.70f, 1.0f) : FLinearColor(0.45f, 0.43f, 0.40f, 1.0f)));
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetPadding(FMargin(0.0f, 15.0f, 24.0f, 0.0f));
	return Button;
}

UTextBlock* UMainMenuWidget::AddPanelLine(UVerticalBox* Parent, const FString& Text, int32 FontSize)
{
	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("Line_%d"), Parent->GetChildrenCount()));
	TextBlock->SetText(FText::FromString(Text));
	TextBlock->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FontSize;
	TextBlock->SetFont(Font);
	TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.86f, 0.70f, 1.0f)));
	UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(TextBlock);
	Slot->SetPadding(FMargin(0.0f, 6.0f));
	return TextBlock;
}

void UMainMenuWidget::ShowPanel(UVerticalBox* PanelToShow)
{
	if (ButtonPanel)
	{
		ButtonPanel->SetVisibility(PanelToShow ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (SettingsPanel)
	{
		SettingsPanel->SetVisibility(PanelToShow == SettingsPanel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CreditsPanel)
	{
		CreditsPanel->SetVisibility(PanelToShow == CreditsPanel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

FSlateBrush UMainMenuWidget::MakeBrush(FLinearColor Color, float Rounding, float OutlineWidth, FLinearColor OutlineColor) const
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

void UMainMenuWidget::HandleNewGame()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
		PC->bShowMouseCursor = false;
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
	}
	UGameplayStatics::OpenLevel(this, SoulDungeonMenu::BossRoomLevelName);
}

void UMainMenuWidget::HandleSettings()
{
	ShowPanel(SettingsPanel);
}

void UMainMenuWidget::HandleCredits()
{
	ShowPanel(CreditsPanel);
}

void UMainMenuWidget::HandleQuit()
{
	APlayerController* PC = GetOwningPlayer();
	UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
}

void UMainMenuWidget::HandleBack()
{
	ShowPanel(nullptr);
}

void UMainMenuWidget::HandleFullscreenChanged(bool bIsChecked)
{
	if (GEngine && GEngine->GetGameUserSettings())
	{
		UGameUserSettings* Settings = GEngine->GetGameUserSettings();
		Settings->SetFullscreenMode(bIsChecked ? EWindowMode::Fullscreen : EWindowMode::Windowed);
		Settings->ApplySettings(false);
	}
}
