// InteractPromptWidget.h — Pure C++ widget, no blueprint asset needed.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractPromptWidget.generated.h"

class UTextBlock;

UCLASS()
class SOUL_AND_DUNGEON_API UInteractPromptWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

public:
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetPromptText(const FString& NewText);

	/** Update the widget position to track a world location */
	void UpdateWorldPosition(APlayerController* PC, const FVector& WorldLocation);

private:
	UPROPERTY()
	UTextBlock* PromptTextBlock = nullptr;
};
