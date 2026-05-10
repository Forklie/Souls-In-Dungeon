// InteractPromptWidget.cpp
#include "InteractPromptWidget.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/PlayerController.h"

void UInteractPromptWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (PromptTextBlock) return;
	if (!WidgetTree) return;

	// Root = compact dark background panel
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), FName("PromptBG"));
	Background->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
	Background->SetPadding(FMargin(20.0f, 10.0f, 20.0f, 10.0f));
	WidgetTree->RootWidget = Background;

	// The prompt text
	PromptTextBlock = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), FName("PromptText"));
	PromptTextBlock->SetText(FText::FromString(TEXT("Press  [ E ]  to Interact")));

	FSlateFontInfo FontInfo = PromptTextBlock->GetFont();
	FontInfo.Size = 18;
	PromptTextBlock->SetFont(FontInfo);
	PromptTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	PromptTextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	PromptTextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	PromptTextBlock->SetJustification(ETextJustify::Center);

	Background->AddChild(PromptTextBlock);
}

void UInteractPromptWidget::SetPromptText(const FString& NewText)
{
	if (PromptTextBlock)
	{
		PromptTextBlock->SetText(FText::FromString(NewText));
	}
}

void UInteractPromptWidget::UpdateWorldPosition(APlayerController* PC, const FVector& WorldLocation)
{
	if (!PC) return;

	FVector2D ScreenPos;
	bool bOnScreen = PC->ProjectWorldLocationToScreen(WorldLocation, ScreenPos, false);

	if (bOnScreen)
	{
		// SetPositionInViewport already handles DPI scaling internally
		// (bRemoveDPIScale defaults to true), so pass raw screen coords
		SetPositionInViewport(ScreenPos);
		// Bottom-center alignment: text floats ABOVE the projected point
		SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
	}
}
