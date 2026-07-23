#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RHIGPUReadback.h"
#include "Capture/CaptureTypes.h"
#include "RgbCameraCaptureComponent.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API URgbCameraCaptureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URgbCameraCaptureComponent();

	void Initialize(
		USceneCaptureComponent2D* InSceneCapture,
		const USceneCaptureComponent2D* InAppearanceSource,
		const FResolvedCameraCalibration& InCalibration,
		bool bInUseGammaCorrection,
		float InOutputGamma,
		bool bInUseFixedExposure,
		float InExposureCompensation
	);

	bool StartCaptureAsync(const FCaptureFrameInfo& FrameInfo);
	bool PollReadback(TArray<uint8>& OutPixels, FCaptureFrameInfo& OutFrameInfo);

	bool IsCaptureInProgress() const;
	UTextureRenderTarget2D* GetRenderTarget() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetupRenderTarget(const USceneCaptureComponent2D* AppearanceSource);
	void ApplyCameraLook(const USceneCaptureComponent2D* AppearanceSource);

	UPROPERTY()
	USceneCaptureComponent2D* SceneCapture = nullptr;

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RGBRenderTarget = nullptr;

	TUniquePtr<FRHIGPUTextureReadback> GPUReadback;

	// True from CaptureScene() request until the matching GPU readback is returned.
	bool bCaptureInProgress = false;

	// True only during the one-tick gap between CaptureScene() and EnqueueCopy().
	bool bWaitingToEnqueueReadback = false;

	FCaptureFrameInfo PendingFrameInfo;
	FResolvedCameraCalibration Calibration;

	int32 Width = 1280;
	int32 Height = 720;
	bool bUseGammaCorrection = true;
	float OutputGamma = 2.2f;
	bool bUseFixedExposure = true;
	float ExposureCompensation = 1.5f;
};
