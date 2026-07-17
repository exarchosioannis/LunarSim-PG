#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "CaptureStatusOverlayComponent.generated.h"

class SBorder;
class STextBlock;
class SWidget;

UCLASS(ClassGroup = (UI))
class SIMULATOR_API UCaptureStatusOverlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCaptureStatusOverlayComponent();

	void SetOverlayEnabled(bool bEnabled);
	void ShowCapturing(const FString& SessionName);
	void ShowFinalizing();
	void ShowReady(float DisplaySeconds = 1.75f);
	void HideOverlay();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EOverlayDisplayState : uint8
	{
		Hidden,
		Capturing,
		Finalizing,
		Ready
	};

	static constexpr float ElapsedUpdateIntervalSeconds = 0.1f;

	void CreateOverlayIfNeeded();
	void RemoveOverlay();
	void CollapseOverlay();
	void RenderCapturing();
	void RenderFinalizing();
	void RenderReady();
	void UpdateElapsedText();
	void StartElapsedUpdates();
	void StopElapsedUpdates();
	void ClearReadyHideTimer();

	bool bOverlayEnabled = false;
	EOverlayDisplayState DisplayState = EOverlayDisplayState::Hidden;
	double CaptureStartedAtSeconds = 0.0;
	int32 LastDisplayedCaptureSecond = INDEX_NONE;
	FString ActiveSessionName;
	FTimerHandle ElapsedUpdateTimerHandle;
	FTimerHandle ReadyHideTimerHandle;

	TSharedPtr<SWidget> OverlayRoot;
	TSharedPtr<SBorder> OverlayPanel;
	TSharedPtr<STextBlock> OverlayPrimaryText;
	TSharedPtr<STextBlock> OverlaySecondaryText;
};
