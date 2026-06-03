#include "PauseMenuWidget.h"

#include "GameMenuConstants.h"
#include "Soul_and_dungeonPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
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
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UPauseMenuWidget::SetMenuOwner(ASoul_and_dungeonPlayerController* InOwner)
{
	MenuOwner = InOwner;
}

void UPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidget();
}

void UPauseMenuWidget::BuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass(), TEXT("PauseMenuWidgetTree"), RF_Transient);
	}
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UImage* Dimmer = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Dimmer"));
	Dimmer->SetBrush(MakeBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f)));
	UCanvasPanelSlot* DimmerSlot = RootCanvas->AddChildToCanvas(Dimmer);
	DimmerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	DimmerSlot->SetOffsets(FMargin(0.0f));

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseFrame"));
	Frame->SetBrush(MakeBrush(FLinearColor(0.018f, 0.015f, 0.014f, 0.92f), 10.0f, 2.0f, FLinearColor(0.70f, 0.42f, 0.16f, 0.72f)));
	Frame->SetPadding(FMargin(34.0f, 30.0f));
	UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetSize(FVector2D(560.0f, 650.0f));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseContent"));
	Frame->AddChild(Content);

	AddLine(Content, TEXT("PAUSED"), 48);
	AddLine(Content, TEXT("Boss Room"), 18);

	ButtonPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonPanel"));
	Content->AddChildToVerticalBox(ButtonPanel);

	UButton* ResumeButton = AddMenuButton(ButtonPanel, TEXT("RESUME"));
	ResumeButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleResume);

	UButton* RestartButton = AddMenuButton(ButtonPanel, TEXT("RESTART"));
	RestartButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleRestart);

	UButton* SettingsButton = AddMenuButton(ButtonPanel, TEXT("SETTINGS"));
	SettingsButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleSettings);

	UButton* MainMenuButton = AddMenuButton(ButtonPanel, TEXT("MAIN MENU"));
	MainMenuButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleMainMenu);

	UButton* QuitButton = AddMenuButton(ButtonPanel, TEXT("QUIT"));
	QuitButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleQuit);

	AddLine(Content, TEXT("Esc resumes gameplay"), 14);

	SettingsPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsPanel"));
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	Content->AddChildToVerticalBox(SettingsPanel);
	AddLine(SettingsPanel, TEXT("SETTINGS"), 30);

	UHorizontalBox* FullscreenRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FullscreenRow"));
	UVerticalBoxSlot* RowSlot = SettingsPanel->AddChildToVerticalBox(FullscreenRow);
	RowSlot->SetPadding(FMargin(80.0f, 22.0f, 80.0f, 16.0f));

	UTextBlock* FullscreenText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FullscreenText"));
	FullscreenText->SetText(FText::FromString(TEXT("FULLSCREEN")));
	FSlateFontInfo TextFont = FullscreenText->GetFont();
	TextFont.Size = 20;
	FullscreenText->SetFont(TextFont);
	FullscreenText->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.86f, 0.70f, 1.0f)));
	UHorizontalBoxSlot* TextSlot = FullscreenRow->AddChildToHorizontalBox(FullscreenText);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UCheckBox* FullscreenCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("FullscreenCheckBox"));
	if (GEngine && GEngine->GetGameUserSettings())
	{
		FullscreenCheckBox->SetIsChecked(GEngine->GetGameUserSettings()->GetFullscreenMode() == EWindowMode::Fullscreen);
	}
	FullscreenCheckBox->OnCheckStateChanged.AddDynamic(this, &UPauseMenuWidget::HandleFullscreenChanged);
	FullscreenRow->AddChildToHorizontalBox(FullscreenCheckBox);

	UButton* BackButton = AddMenuButton(SettingsPanel, TEXT("BACK"));
	BackButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleBack);
}

UButton* UPauseMenuWidget::AddMenuButton(UVerticalBox* Parent, const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("Button_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(MakeBrush(FLinearColor(0.100f, 0.024f, 0.018f, 0.98f), 6.0f, 1.0f, FLinearColor(0.56f, 0.30f, 0.10f, 0.75f)));
	ButtonStyle.SetHovered(MakeBrush(FLinearColor(0.27f, 0.062f, 0.032f, 1.0f), 6.0f, 2.0f, FLinearColor(1.0f, 0.62f, 0.17f, 1.0f)));
	ButtonStyle.SetPressed(MakeBrush(FLinearColor(0.55f, 0.18f, 0.055f, 1.0f), 6.0f, 2.0f, FLinearColor(1.0f, 0.76f, 0.24f, 1.0f)));
	ButtonStyle.SetNormalPadding(FMargin(0.0f));
	ButtonStyle.SetPressedPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	Button->SetStyle(ButtonStyle);
	UVerticalBoxSlot* ButtonSlot = Parent->AddChildToVerticalBox(Button);
	ButtonSlot->SetPadding(FMargin(72.0f, 8.0f));

	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("Size_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	SizeBox->SetHeightOverride(58.0f);
	Button->AddChild(SizeBox);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *FString::Printf(TEXT("Row_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	SizeBox->AddChild(Row);

	USizeBox* AccentBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("AccentBox_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	AccentBox->SetWidthOverride(5.0f);
	UImage* Accent = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("Accent_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	Accent->SetBrush(MakeBrush(FLinearColor(1.0f, 0.55f, 0.14f, 1.0f), 4.0f));
	AccentBox->AddChild(Accent);
	UHorizontalBoxSlot* AccentSlot = Row->AddChildToHorizontalBox(AccentBox);
	AccentSlot->SetPadding(FMargin(10.0f, 9.0f, 12.0f, 9.0f));

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("Text_%s"), *Label.Replace(TEXT(" "), TEXT(""))));
	Text->SetText(FText::FromString(Label));
	Text->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 21;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.88f, 0.70f, 1.0f)));
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetPadding(FMargin(0.0f, 14.0f, 22.0f, 0.0f));
	return Button;
}

UTextBlock* UPauseMenuWidget::AddLine(UVerticalBox* Parent, const FString& Text, int32 FontSize)
{
	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("Line_%d"), Parent->GetChildrenCount()));
	TextBlock->SetText(FText::FromString(Text));
	TextBlock->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FontSize;
	TextBlock->SetFont(Font);
	TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.34f, 1.0f)));
	UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(TextBlock);
	Slot->SetPadding(FMargin(0.0f, 5.0f));
	return TextBlock;
}

void UPauseMenuWidget::ShowSettings(bool bShow)
{
	if (ButtonPanel)
	{
		ButtonPanel->SetVisibility(bShow ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (SettingsPanel)
	{
		SettingsPanel->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

FSlateBrush UPauseMenuWidget::MakeBrush(FLinearColor Color, float Rounding, float OutlineWidth, FLinearColor OutlineColor) const
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

void UPauseMenuWidget::HandleResume()
{
	if (MenuOwner)
	{
		MenuOwner->ClosePauseMenu();
	}
}

void UPauseMenuWidget::HandleRestart()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
	}
	UGameplayStatics::OpenLevel(this, SoulDungeonMenu::BossRoomLevelName);
}

void UPauseMenuWidget::HandleSettings()
{
	ShowSettings(true);
}

void UPauseMenuWidget::HandleMainMenu()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetPause(false);
	}
	UGameplayStatics::OpenLevel(this, SoulDungeonMenu::MainMenuLevelName);
}

void UPauseMenuWidget::HandleQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UPauseMenuWidget::HandleBack()
{
	ShowSettings(false);
}

void UPauseMenuWidget::HandleFullscreenChanged(bool bIsChecked)
{
	if (GEngine && GEngine->GetGameUserSettings())
	{
		UGameUserSettings* Settings = GEngine->GetGameUserSettings();
		Settings->SetFullscreenMode(bIsChecked ? EWindowMode::Fullscreen : EWindowMode::Windowed);
		Settings->ApplySettings(false);
	}
}
