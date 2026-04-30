#include "Sensors/RobotCamRig.h"

#include "TempoROSTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "RHICommandList.h"
#include "RenderResource.h"
#include "Sensors/GTCamera.h"

#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/camera_info.hpp"

//TempoROS message traits
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::Image);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::CameraInfo);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(std_msgs::msg::Int32);

//Time Helpers
static builtin_interfaces::msg::Time ToRosTime(double Seconds)
{
	builtin_interfaces::msg::Time T;
	if (Seconds < 0.0) Seconds = 0.0;
	const int64 Sec = (int64)Seconds;
	const double Frac = Seconds - (double)Sec;
	T.sec = (int32)Sec;
	T.nanosec = (uint32)FMath::Clamp<int64>((int64)(Frac * 1000000000.0), 0, 999999999);
	return T;
}

//Constructor
ARobotCamRig::ARobotCamRig()
{
	PrimaryActorTick.bCanEverTick = true;

	//Components
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("RobotCamera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

	RGBCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RGBCapture"));
	RGBCapture->SetupAttachment(Camera);
	RGBCapture->SetRelativeLocation(FVector::ZeroVector);
	RGBCapture->SetRelativeRotation(FRotator::ZeroRotator);
	RGBCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RGBCapture->bCaptureEveryFrame = false;
	RGBCapture->bCaptureOnMovement = false;
}

void ARobotCamRig::BeginPlay()
{
	Super::BeginPlay();

	SetupRenderTarget();
	ApplyRoverCameraLook();
	SetupReusableMessages();
	//GPU Readback Setup
	GPUReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("RobotCamGPUReadback"));
	
	//Create and initialize CaptureManager
	CaptureManager = NewObject<UCaptureManager>(this);
	if (CaptureManager)
	{
		CaptureManager->Initialize(CaptureConfig);
	}
	
	SetupRos();
}

void ARobotCamRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ROSNode) ROSNode->Tick(DeltaSeconds);
	if (bReadbackInFlight){
		FinishRgbReadbackAndPublish();
	}
	if (!CaptureManager || !CaptureManager->IsCaptureEnabled()) return;

	UpdatePublishTimer(DeltaSeconds);
}

//Setup Helpers
void ARobotCamRig::SetupRenderTarget()
{
	if (!RGBRenderTarget) RGBRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RGBRenderTarget"));
	if (RGBRenderTarget){
		RGBRenderTarget->RenderTargetFormat = RTF_RGBA8;
		RGBRenderTarget->TargetGamma = bUseGammaCorrection ? OutputGamma : 1.0f;
		RGBRenderTarget->InitAutoFormat(Width, Height);
		RGBRenderTarget->UpdateResourceImmediate(true);
	}
	if (RGBCapture){
		RGBCapture->TextureTarget = RGBRenderTarget;
		RGBCapture->CaptureScene();
	}
}

void ARobotCamRig::ApplyRoverCameraLook()
{
	if (!RGBCapture) return;

	RGBCapture->bAlwaysPersistRenderingState = true;
	RGBCapture->PostProcessBlendWeight = 1.0f;
	FPostProcessSettings& PPS = RGBCapture->PostProcessSettings;

	PPS = FPostProcessSettings();
	if (bUseFixedExposure)
	{
		PPS.bOverride_AutoExposureMinBrightness = true;
		PPS.bOverride_AutoExposureMaxBrightness = true;
		PPS.bOverride_AutoExposureBias = true;

		PPS.AutoExposureMinBrightness = 1.0f;
		PPS.AutoExposureMaxBrightness = 1.0f;
		PPS.AutoExposureBias = ExposureCompensation;
	}
}

void ARobotCamRig::SetupReusableMessages()
{
	ReusableImgMsg.height = Height;
	ReusableImgMsg.width = Width;
	ReusableImgMsg.encoding = "bgra8";
	ReusableImgMsg.is_bigendian = false;
	ReusableImgMsg.step = Width * 4;
	ReusableImgMsg.data.resize((size_t)Width * (size_t)Height * 4);

	ReusableCamInfoMsg.height = Height;
	ReusableCamInfoMsg.width = Width;
	ReusableCamInfoMsg.distortion_model = "plumb_bob";
	ReusableCamInfoMsg.d = {0.0, 0.0, 0.0, 0.0, 0.0};
}

void ARobotCamRig::SetupRos()
{
	ROSNode = UTempoROSNode::Create(TEXT("sim_camera"), this);

	FROSQOSProfile ImageQOS;
	ImageQOS.CustomQueueSize(20).Reliable();

	FROSQOSProfile DefaultQOS;
	DefaultQOS.CustomQueueSize(10).Reliable();

	ROSNode->AddPublisher<sensor_msgs::msg::Image>(
		TEXT("sim_camera/rgb/image_raw"),
		ImageQOS,
		false
	);

	ROSNode->AddPublisher<sensor_msgs::msg::CameraInfo>(
		TEXT("sim_camera/camera_info"),
		DefaultQOS,
		false
	);

	ROSNode->AddPublisher<std_msgs::msg::Int32>(
		TEXT("sim_camera/frame_index"),
		DefaultQOS,
		false
	);

	ROSNode->AddSubscription<std_msgs::msg::Int32>(
		TEXT("sim_camera/control"),
		TROSSubscriptionDelegate<std_msgs::msg::Int32>::CreateUObject(this, &ARobotCamRig::OnCaptureControl)
	);
}

void ARobotCamRig::UpdatePublishTimer(float DeltaSeconds)
{
	PublishAccumulator += DeltaSeconds;
	const float Hz = CaptureManager ? FMath::Max(1.0f, CaptureManager->GetConfig().PublishHz) : 10.0f;
	const float Period = 1.0f / Hz;
	if (PublishAccumulator >= Period) {
		PublishAccumulator -= Period;
		PublishRgb();
	}
}

void ARobotCamRig::PublishRgb()
{
	if (!ROSNode || !RGBRenderTarget || !RGBCapture || !GPUReadback.IsValid()) return;
	if (bReadbackInFlight) return;
	StartRgbReadback();
}

void ARobotCamRig::StartRgbReadback()
{
	if (bReadbackInFlight) return;
	if (!RGBCapture || !RGBRenderTarget || !GPUReadback.IsValid()) return;
	if (!GetWorld()) return;

	FTextureRenderTargetResource* Res = RGBRenderTarget->GameThread_GetRenderTargetResource();
	if (!Res) return;

	const double CaptureTimeSeconds = GetWorld()->GetTimeSeconds();
	PendingStamp = ToRosTime(CaptureTimeSeconds);
	
	// Get the next frame info from CaptureManager
	const FCaptureFrameInfo FrameInfo = CaptureManager ? CaptureManager->NextFrame(CaptureTimeSeconds) : FCaptureFrameInfo();
	CurrentFrameIndex = FrameInfo.FrameIndex;
	CurrentSessionId = FrameInfo.SessionId;
	
	//GT capture only if enabled in config
	if (CaptureManager && CaptureManager->GetConfig().bEnableGt && GroundTruthCamera) {
		if (Camera) GroundTruthCamera->SetActorTransform(Camera->GetComponentTransform());
		GroundTruthCamera->CaptureGroundTruthNow(FrameInfo.FrameIndex, FrameInfo.StampSeconds, FrameInfo.SessionId, CaptureManager);
	}
	
	//ROS RGB capture only if enabled in config
	if (!CaptureManager || !CaptureManager->GetConfig().bEnableRosRgb) {
		return;
	}
	
	RGBCapture->CaptureScene();
	ENQUEUE_RENDER_COMMAND(EnqueueRobotCamReadback)(
		[Readback = GPUReadback.Get(), RTRes = Res](FRHICommandListImmediate& RHICmdList) {
			if (!Readback || !RTRes) return;
			FRHITexture* Tex = RTRes->GetRenderTargetTexture();
			if (!Tex) return;
			Readback->EnqueueCopy(RHICmdList, Tex);
		});

	bReadbackInFlight = true;
}

void ARobotCamRig::FinishRgbReadbackAndPublish()
{
	//Early return if ROS RGB capture is disabled or capture is stopped or session is invalid
	if (!CaptureManager || !CaptureManager->GetConfig().bEnableRosRgb || !CaptureManager->IsSessionValid(CurrentSessionId)) {
		bReadbackInFlight = false;
		return;
	}

	if (!GPUReadback.IsValid()) {
		bReadbackInFlight = false;
		return;
	}

	if (!bReadbackInFlight) return;
	if (!GPUReadback->IsReady()) return;

	const int32 W = Width;
	const int32 H = Height;
	const int32 BytesPerPixel = 4;

	int32 RowPitchInPixels = 0;
	uint8* Ptr = (uint8*)GPUReadback->Lock(RowPitchInPixels);
	if (!Ptr) {
		bReadbackInFlight = false;
		return;
	}
	if (RowPitchInPixels < W){
		GPUReadback->Unlock();
		bReadbackInFlight = false;
		return;
	}

	const int32 SrcRowBytes = RowPitchInPixels * BytesPerPixel;
	const int32 DstRowBytes = W * BytesPerPixel;
	uint8* Dst = ReusableImgMsg.data.data();
	for (int32 y = 0; y < H; ++y){
		FMemory::Memcpy(Dst + (size_t)y * DstRowBytes, Ptr + (size_t)y * SrcRowBytes, DstRowBytes);
	}

	GPUReadback->Unlock();
	bReadbackInFlight = false;

	ReusableImgMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusableImgMsg.header.stamp = PendingStamp;
	ReusableFrameIndexMsg.data = CurrentFrameIndex;

	ROSNode->Publish<sensor_msgs::msg::Image>(TEXT("sim_camera/rgb/image_raw"), ReusableImgMsg);
	ROSNode->Publish<std_msgs::msg::Int32>(TEXT("sim_camera/frame_index"), ReusableFrameIndexMsg);
	PublishCameraInfo(PendingStamp);
}


//Camera Info Publishing
void ARobotCamRig::PublishCameraInfo(const builtin_interfaces::msg::Time& Stamp)
{
	if (!ROSNode || !Camera) return;

	const double Fx = (double)Width / (2.0 * FMath::Tan(FMath::DegreesToRadians((double)Camera->FieldOfView) * 0.5));
	const double Fy = Fx;
	const double Cx = (double)Width * 0.5;
	const double Cy = (double)Height * 0.5;

	ReusableCamInfoMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusableCamInfoMsg.header.stamp = Stamp;

	ReusableCamInfoMsg.k = {
		Fx, 0.0, Cx,
		0.0, Fy, Cy,
		0.0, 0.0, 1.0
	};
	ReusableCamInfoMsg.r = {
		1.0, 0.0, 0.0,
		0.0, 1.0, 0.0,
		0.0, 0.0, 1.0
	};
	ReusableCamInfoMsg.p = {
		Fx, 0.0, Cx, 0.0,
		0.0, Fy, Cy, 0.0,
		0.0, 0.0, 1.0, 0.0
	};
	ROSNode->Publish<sensor_msgs::msg::CameraInfo>(TEXT("sim_camera/camera_info"), ReusableCamInfoMsg);
}

//command helpers
void ARobotCamRig::OnCaptureControl(const std_msgs::msg::Int32& Msg)
{
	if (Msg.data == 1) {
		if (CaptureManager) CaptureManager->StartCapture();
		PublishAccumulator = 0.0f;
	}
	else if (Msg.data == 0) {
		if (CaptureManager) CaptureManager->StopCapture();
	}
}

void ARobotCamRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CaptureManager) {
		CaptureManager->StopCapture();
	}
	
	bReadbackInFlight = false;
	if (GPUReadback.IsValid()){
		GPUReadback.Reset();
	}
	
	Super::EndPlay(EndPlayReason);
}

void ARobotCamRig::SetCaptureConfig(const FCaptureConfig& NewConfig)
{
	CaptureConfig = NewConfig;
}

FCaptureConfig ARobotCamRig::GetCaptureConfig() const
{
	return CaptureConfig;
}