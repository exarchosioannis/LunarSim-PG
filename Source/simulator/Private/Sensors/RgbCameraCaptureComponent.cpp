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

	SetupRenderTarget();
	ApplyCameraLook();

	// GPU Readback Setup
	GPUReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("RgbCameraCaptureReadback"));
}

void URgbCameraCaptureComponent::SetupRenderTarget()
{
	if (!RGBRenderTarget) 
		RGBRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RGBRenderTarget"));
	
	if (RGBRenderTarget) {
		RGBRenderTarget->RenderTargetFormat = RTF_RGBA8;
		RGBRenderTarget->TargetGamma = bUseGammaCorrection ? OutputGamma : 1.0f;
		RGBRenderTarget->InitAutoFormat(Width, Height);
		RGBRenderTarget->UpdateResourceImmediate(true);
	}
	
	if (SceneCapture && RGBRenderTarget) {
		SceneCapture->TextureTarget = RGBRenderTarget;
		SceneCapture->CaptureScene();
	}
}

void URgbCameraCaptureComponent::ApplyCameraLook()
{
	if (!SceneCapture) return;

	SceneCapture->bAlwaysPersistRenderingState = true;
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
	if (bReadbackInFlight) {
		UE_LOG(LogTemp, Warning, TEXT("RGB capture readback already in flight, cannot start new capture for frame %d"), FrameInfo.FrameIndex);
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

	FTextureRenderTargetResource* Res = RGBRenderTarget->GameThread_GetRenderTargetResource();
	if (!Res) {
		UE_LOG(LogTemp, Warning, TEXT("RenderTarget resource is null, cannot start RGB capture for frame %d"), FrameInfo.FrameIndex);
		return false;
	}

	// Store the frame info for later retrieval
	PendingFrameInfo = FrameInfo;
	
	SceneCapture->CaptureScene();
	ENQUEUE_RENDER_COMMAND(EnqueueRgbCaptureReadback)(
		[Readback = GPUReadback.Get(), RTRes = Res](FRHICommandListImmediate& RHICmdList) {
			if (!Readback || !RTRes) return;
			FRHITexture* Tex = RTRes->GetRenderTargetTexture();
			if (!Tex) return;
			Readback->EnqueueCopy(RHICmdList, Tex);
		});

	bReadbackInFlight = true;
	return true;
}

bool URgbCameraCaptureComponent::PollReadback(TArray<uint8>& OutPixels, FCaptureFrameInfo& OutFrameInfo)
{
	if (!bReadbackInFlight) return false;
	if (!GPUReadback.IsValid()) return false;
	if (!GPUReadback->IsReady()) return false;

	const int32 W = Width;
	const int32 H = Height;
	const int32 BytesPerPixel = 4;

	int32 RowPitchInPixels = 0;
	uint8* Ptr = (uint8*)GPUReadback->Lock(RowPitchInPixels);
	if (!Ptr) {
		bReadbackInFlight = false;
		return false;
	}
	if (RowPitchInPixels < W) {
		GPUReadback->Unlock();
		bReadbackInFlight = false;
		return false;
	}

	// Prepare output pixel array
	const int32 TotalPixels = W * H * BytesPerPixel;
	OutPixels.SetNum(TotalPixels);
	
	const int32 SrcRowBytes = RowPitchInPixels * BytesPerPixel;
	const int32 DstRowBytes = W * BytesPerPixel;
	uint8* Dst = OutPixels.GetData();
	
	for (int32 y = 0; y < H; ++y) {
		FMemory::Memcpy(Dst + (size_t)y * DstRowBytes, Ptr + (size_t)y * SrcRowBytes, DstRowBytes);
	}

	GPUReadback->Unlock();
	bReadbackInFlight = false;

	// Return the frame info that was passed to StartCaptureAsync
	OutFrameInfo = PendingFrameInfo;
	
	return true;
}

bool URgbCameraCaptureComponent::IsCaptureInProgress() const
{
	return bReadbackInFlight;
}

UTextureRenderTarget2D* URgbCameraCaptureComponent::GetRenderTarget() const
{
	return RGBRenderTarget;
}

void URgbCameraCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bReadbackInFlight = false;
	if (GPUReadback.IsValid()) {
		GPUReadback.Reset();
	}
	Super::EndPlay(EndPlayReason);
}