#include "Sensors/RgbCameraCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHICommandList.h"
#include "RenderResource.h"

URgbCameraCaptureComponent::URgbCameraCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URgbCameraCaptureComponent::Initialize(
	USceneCaptureComponent2D* InSceneCapture,
	int32 InWidth,
	int32 InHeight,
	bool bInUseGammaCorrection,
	float InOutputGamma,
	bool bInUseFixedExposure,
	float InExposureCompensation)
{
	SceneCapture = InSceneCapture;
	Width = InWidth;
	Height = InHeight;
	bUseGammaCorrection = bInUseGammaCorrection;
	OutputGamma = InOutputGamma;
	bUseFixedExposure = bInUseFixedExposure;
	ExposureCompensation = InExposureCompensation;

	bCaptureInProgress = false;
	bWaitingToEnqueueReadback = false;

	SetupRenderTarget();
	ApplyCameraLook();

	// GPU Readback Setup
	GPUReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("RgbCameraCaptureReadback"));
}

void URgbCameraCaptureComponent::SetupRenderTarget()
{
	if (!RGBRenderTarget) {
		RGBRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RGBRenderTarget"));
	}

	if (RGBRenderTarget) {
		RGBRenderTarget->RenderTargetFormat = RTF_RGBA8;
		RGBRenderTarget->TargetGamma = bUseGammaCorrection ? OutputGamma : 1.0f;
		RGBRenderTarget->InitAutoFormat(Width, Height);
		RGBRenderTarget->UpdateResourceImmediate(true);
	}

	if (SceneCapture && RGBRenderTarget) {
		SceneCapture->TextureTarget = RGBRenderTarget;
		// Warm-up capture only. Real dataset frames are captured in StartCaptureAsync().
		SceneCapture->CaptureScene();
	}
}

void URgbCameraCaptureComponent::ApplyCameraLook()
{
	if (!SceneCapture) return;

	// Keep this false for dataset capture. We manually capture when FrameInfo is created.
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;

	// Avoid temporal history for sensor-like output.
	SceneCapture->bAlwaysPersistRenderingState = false;
	SceneCapture->ShowFlags.SetTemporalAA(false);
	SceneCapture->ShowFlags.SetMotionBlur(false);
	SceneCapture->ShowFlags.SetAntiAliasing(false);

	SceneCapture->PostProcessBlendWeight = 1.0f;
	FPostProcessSettings& PPS = SceneCapture->PostProcessSettings;

	PPS = FPostProcessSettings();
	if (bUseFixedExposure) {
		PPS.bOverride_AutoExposureMinBrightness = true;
		PPS.bOverride_AutoExposureMaxBrightness = true;
		PPS.bOverride_AutoExposureBias = true;

		PPS.AutoExposureMinBrightness = 1.0f;
		PPS.AutoExposureMaxBrightness = 1.0f;
		PPS.AutoExposureBias = ExposureCompensation;
	}
}

bool URgbCameraCaptureComponent::StartCaptureAsync(const FCaptureFrameInfo& FrameInfo)
{
	if (bCaptureInProgress) {
		UE_LOG(LogTemp, Warning, TEXT("RGB capture already in progress, cannot start new capture for frame %d"), FrameInfo.FrameIndex);
		return false;
	}

	if (!SceneCapture) {
		UE_LOG(LogTemp, Warning, TEXT("SceneCapture component is null, cannot start RGB capture for frame %d"), FrameInfo.FrameIndex);
		return false;
	}

	if (!RGBRenderTarget) {
		UE_LOG(LogTemp, Warning, TEXT("RGBRenderTarget is null, cannot start RGB capture for frame %d"), FrameInfo.FrameIndex);
		return false;
	}

	if (!GPUReadback.IsValid()) {
		UE_LOG(LogTemp, Warning, TEXT("GPUReadback is invalid, cannot start RGB capture for frame %d"), FrameInfo.FrameIndex);
		return false;
	}

	// Store the metadata for THIS capture request.
	PendingFrameInfo = FrameInfo;

	// Important synchronization detail:
	// CaptureScene() queues scene-capture work on the render thread. If we enqueue the GPU
	// readback immediately in the same game-thread function, the readback can observe the
	// previous render target contents on some frames. That gives pixels from visual frame N-1
	// with metadata from frame N.
	//
	// So we do this in two phases:
	//   1. StartCaptureAsync(): request the SceneCapture for FrameInfo.
	//   2. PollReadback() on a later game tick: enqueue the GPU copy, after the capture has
	//      had a chance to update the render target.
	SceneCapture->CaptureScene();

	bCaptureInProgress = true;
	bWaitingToEnqueueReadback = true;

	UE_LOG(LogTemp, Verbose, TEXT("RGB START frame=%d stamp=%.6f"), FrameInfo.FrameIndex, FrameInfo.StampSeconds);

	return true;
}

bool URgbCameraCaptureComponent::PollReadback(TArray<uint8>& OutPixels, FCaptureFrameInfo& OutFrameInfo)
{
	if (!bCaptureInProgress) return false;
	if (!GPUReadback.IsValid()) return false;

	// Phase 2: enqueue the readback one game tick after CaptureScene().
	// No new capture can start while bCaptureInProgress is true, so the render target should
	// still correspond to PendingFrameInfo.
	if (bWaitingToEnqueueReadback) {
		if (!RGBRenderTarget) {
			bCaptureInProgress = false;
			bWaitingToEnqueueReadback = false;
			return false;
		}

		FTextureRenderTargetResource* Res = RGBRenderTarget->GameThread_GetRenderTargetResource();
		if (!Res) {
			bCaptureInProgress = false;
			bWaitingToEnqueueReadback = false;
			return false;
		}

		ENQUEUE_RENDER_COMMAND(EnqueueRgbCaptureReadback)(
			[Readback = GPUReadback.Get(), RTRes = Res, FrameIndex = PendingFrameInfo.FrameIndex](FRHICommandListImmediate& RHICmdList) {
				if (!Readback || !RTRes) return;
				FRHITexture* Tex = RTRes->GetRenderTargetTexture();
				if (!Tex) return;
				Readback->EnqueueCopy(RHICmdList, Tex);
				UE_LOG(LogTemp, Verbose, TEXT("RGB READBACK ENQUEUED frame=%d"), FrameIndex);
			});

		bWaitingToEnqueueReadback = false;
		return false;
	}

	if (!GPUReadback->IsReady()) return false;

	const int32 W = Width;
	const int32 H = Height;
	const int32 BytesPerPixel = 4;

	int32 RowPitchInPixels = 0;
	uint8* Ptr = static_cast<uint8*>(GPUReadback->Lock(RowPitchInPixels));
	if (!Ptr) {
		bCaptureInProgress = false;
		return false;
	}

	if (RowPitchInPixels < W) {
		GPUReadback->Unlock();
		bCaptureInProgress = false;
		return false;
	}

	const int32 TotalBytes = W * H * BytesPerPixel;
	OutPixels.SetNum(TotalBytes);

	const int32 SrcRowBytes = RowPitchInPixels * BytesPerPixel;
	const int32 DstRowBytes = W * BytesPerPixel;
	uint8* Dst = OutPixels.GetData();

	for (int32 y = 0; y < H; ++y) {
		FMemory::Memcpy(Dst + static_cast<size_t>(y) * DstRowBytes, Ptr + static_cast<size_t>(y) * SrcRowBytes, DstRowBytes);
	}

	GPUReadback->Unlock();

	OutFrameInfo = PendingFrameInfo;

	UE_LOG(LogTemp, Verbose, TEXT("RGB READBACK DONE frame=%d stamp=%.6f"), OutFrameInfo.FrameIndex, OutFrameInfo.StampSeconds);

	bCaptureInProgress = false;
	return true;
}

bool URgbCameraCaptureComponent::IsCaptureInProgress() const
{
	return bCaptureInProgress;
}

UTextureRenderTarget2D* URgbCameraCaptureComponent::GetRenderTarget() const
{
	return RGBRenderTarget;
}

void URgbCameraCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCaptureInProgress = false;
	bWaitingToEnqueueReadback = false;
	if (GPUReadback.IsValid()) {
		GPUReadback.Reset();
	}
	Super::EndPlay(EndPlayReason);
}