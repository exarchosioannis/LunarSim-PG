#include "Utils/CaptureStatusOverlayComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

UCaptureStatusOverlayComponent::UCaptureStatusOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCaptureStatusOverlayComponent::SetOverlayEnabled(bool bEnabled)
{
	bOverlayEnabled = bEnabled;
	if (!bOverlayEnabled) {
		StopElapsedUpdates();
		CollapseOverlay();
		return;
	}

	CreateOverlayIfNeeded();
	switch (DisplayState) {
	case EOverlayDisplayState::Capturing:
		LastDisplayedCaptureSecond = INDEX_NONE;
		StartElapsedUpdates();
		break;
	case EOverlayDisplayState::Finalizing:
		RenderFinalizing();
		break;
	case EOverlayDisplayState::Ready:
		RenderReady();
		break;
	case EOverlayDisplayState::Hidden:
	default:
		CollapseOverlay();
		break;
	}
}

void UCaptureStatusOverlayComponent::ShowCapturing(const FString& SessionName)
{
	ClearReadyHideTimer();
	StopElapsedUpdates();
	DisplayState = EOverlayDisplayState::Capturing;
	ActiveSessionName = SessionName;
	CaptureStartedAtSeconds = FPlatformTime::Seconds();
	LastDisplayedCaptureSecond = INDEX_NONE;

	if (bOverlayEnabled) {
		StartElapsedUpdates();
	} else {
		CollapseOverlay();
	}
}

void UCaptureStatusOverlayComponent::ShowFinalizing()
{
	StopElapsedUpdates();
	ClearReadyHideTimer();
	DisplayState = EOverlayDisplayState::Finalizing;

	if (bOverlayEnabled) {
		RenderFinalizing();
	} else {
		CollapseOverlay();
	}
}

void UCaptureStatusOverlayComponent::ShowReady(float DisplaySeconds)
{
	StopElapsedUpdates();
	ClearReadyHideTimer();
	DisplayState = EOverlayDisplayState::Ready;

	if (bOverlayEnabled) {
		RenderReady();
	} else {
		CollapseOverlay();
	}

	if (DisplaySeconds <= 0.0f) {
		HideOverlay();
		return;
	}

	if (UWorld* World = GetWorld()) {
		World->GetTimerManager().SetTimer(
			ReadyHideTimerHandle,
			this,
			&UCaptureStatusOverlayComponent::HideOverlay,
			DisplaySeconds,
			false);
	}
}

void UCaptureStatusOverlayComponent::HideOverlay()
{
	StopElapsedUpdates();
	ClearReadyHideTimer();
	DisplayState = EOverlayDisplayState::Hidden;
	ActiveSessionName.Empty();
	CollapseOverlay();
}

void UCaptureStatusOverlayComponent::CreateOverlayIfNeeded()
{
	if (!bOverlayEnabled
		|| OverlayRoot.IsValid()
		|| IsRunningDedicatedServer()
		|| !GEngine
		|| !GEngine->GameViewport) {
		return;
	}

	SAssignNew(OverlayRoot, SOverlay)
		.Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(24.0f, 32.0f, 0.0f, 0.0f))
		[
			SAssignNew(OverlayPanel, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FLinearColor(0.025f, 0.03f, 0.04f, 0.78f))
			.Padding(FMargin(10.0f, 7.0f))
			[
				SNew(SBox)
				.WidthOverride(190.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(OverlayPrimaryText, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Text(FText::GetEmpty())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
					[
						SAssignNew(OverlaySecondaryText, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.81f, 0.86f, 1.0f)))
						.Text(FText::GetEmpty())
					]
				]
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(OverlayRoot.ToSharedRef(), 100);
}

void UCaptureStatusOverlayComponent::RemoveOverlay()
{
	if (OverlayRoot.IsValid() && GEngine && GEngine->GameViewport) {
		GEngine->GameViewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
	}

	OverlaySecondaryText.Reset();
	OverlayPrimaryText.Reset();
	OverlayPanel.Reset();
	OverlayRoot.Reset();
}

void UCaptureStatusOverlayComponent::CollapseOverlay()
{
	if (OverlayPanel.IsValid()) {
		OverlayPanel->SetVisibility(EVisibility::Collapsed);
	}
}

void UCaptureStatusOverlayComponent::RenderCapturing()
{
	CreateOverlayIfNeeded();
	if (!OverlayPanel.IsValid()
		|| !OverlayPrimaryText.IsValid()
		|| !OverlaySecondaryText.IsValid()) {
		return;
	}

	const double ElapsedSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - CaptureStartedAtSeconds);
	const int32 TotalSeconds = FMath::FloorToInt(FMath::Min(ElapsedSeconds, static_cast<double>(MAX_int32)));
	if (TotalSeconds == LastDisplayedCaptureSecond) return;

	LastDisplayedCaptureSecond = TotalSeconds;
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;
	OverlayPrimaryText->SetText(FText::FromString(TEXT("\u25CF CAPTURING")));
	OverlayPrimaryText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.46f, 0.42f, 1.0f)));
	OverlaySecondaryText->SetText(FText::FromString(FString::Printf(
		TEXT("%s  \u00B7  %02d:%02d"),
		*ActiveSessionName,
		Minutes,
		Seconds)));
	OverlaySecondaryText->SetVisibility(EVisibility::Visible);
	OverlayPanel->SetVisibility(EVisibility::Visible);
}

void UCaptureStatusOverlayComponent::RenderFinalizing()
{
	CreateOverlayIfNeeded();
	if (!OverlayPanel.IsValid()
		|| !OverlayPrimaryText.IsValid()
		|| !OverlaySecondaryText.IsValid()) {
		return;
	}

	OverlayPrimaryText->SetText(FText::FromString(TEXT("Finalizing capture\u2026")));
	OverlayPrimaryText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	OverlaySecondaryText->SetText(FText::GetEmpty());
	OverlaySecondaryText->SetVisibility(EVisibility::Collapsed);
	OverlayPanel->SetVisibility(EVisibility::Visible);
}

void UCaptureStatusOverlayComponent::RenderReady()
{
	CreateOverlayIfNeeded();
	if (!OverlayPanel.IsValid()
		|| !OverlayPrimaryText.IsValid()
		|| !OverlaySecondaryText.IsValid()) {
		return;
	}

	OverlayPrimaryText->SetText(FText::FromString(TEXT("Ready for next capture")));
	OverlayPrimaryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.88f, 0.68f, 1.0f)));
	OverlaySecondaryText->SetText(FText::GetEmpty());
	OverlaySecondaryText->SetVisibility(EVisibility::Collapsed);
	OverlayPanel->SetVisibility(EVisibility::Visible);
}

void UCaptureStatusOverlayComponent::UpdateElapsedText()
{
	if (!bOverlayEnabled || DisplayState != EOverlayDisplayState::Capturing) return;
	RenderCapturing();
}

void UCaptureStatusOverlayComponent::StartElapsedUpdates()
{
	StopElapsedUpdates();
	if (!bOverlayEnabled
		|| DisplayState != EOverlayDisplayState::Capturing
		|| IsRunningDedicatedServer()) {
		return;
	}

	UpdateElapsedText();
	if (UWorld* World = GetWorld()) {
		World->GetTimerManager().SetTimer(
			ElapsedUpdateTimerHandle,
			this,
			&UCaptureStatusOverlayComponent::UpdateElapsedText,
			ElapsedUpdateIntervalSeconds,
			true);
	}
}

void UCaptureStatusOverlayComponent::StopElapsedUpdates()
{
	if (UWorld* World = GetWorld()) {
		World->GetTimerManager().ClearTimer(ElapsedUpdateTimerHandle);
	}
}

void UCaptureStatusOverlayComponent::ClearReadyHideTimer()
{
	if (UWorld* World = GetWorld()) {
		World->GetTimerManager().ClearTimer(ReadyHideTimerHandle);
	}
}

void UCaptureStatusOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopElapsedUpdates();
	ClearReadyHideTimer();
	DisplayState = EOverlayDisplayState::Hidden;
	RemoveOverlay();
	Super::EndPlay(EndPlayReason);
}
