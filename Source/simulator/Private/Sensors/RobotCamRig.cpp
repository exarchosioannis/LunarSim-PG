#include "Sensors/RobotCamRig.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Capture/CaptureManager.h"
#include "Engine/World.h"
#include "Sensors/GTCamera.h"
#include "Sensors/RgbCameraCaptureComponent.h"
#include "Sensors/CameraRosPublisherComponent.h"
#include "Robots/RoverRobot.h"

//TempoROS message traits - moved to CameraRosPublisherComponent

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

	RgbCaptureComponent = CreateDefaultSubobject<URgbCameraCaptureComponent>(TEXT("RgbCaptureComponent"));
	RosPublisherComponent = CreateDefaultSubobject<UCameraRosPublisherComponent>(TEXT("RosPublisherComponent"));
}

void ARobotCamRig::BeginPlay()
{
	Super::BeginPlay();

	// Initialize RGB capture component
	if (RgbCaptureComponent && RGBCapture) {
		RgbCaptureComponent->Initialize(
			RGBCapture,
			Width,
			Height,
			bUseGammaCorrection,
			OutputGamma,
			bUseFixedExposure,
			ExposureCompensation
		);
	}
	
	// Initialize ROS publisher component
	if (RosPublisherComponent && Camera) {
		RosPublisherComponent->Initialize(
			Width,
			Height,
			FrameId,
			TEXT("/left_camera"),
			Camera
		);
		// Bind to capture control delegate
		RosPublisherComponent->OnCaptureControlReceived.AddUObject(this, &ARobotCamRig::OnCaptureControl);
	}
	
	//Create and initialize CaptureManager
	CaptureManager = NewObject<UCaptureManager>(this);
	if (CaptureManager)
	{
		CaptureManager->Initialize(CaptureConfig);
		CaptureManager->SetLeftCameraPoseSource(Camera);
	}
}

void ARobotCamRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	// Tick ROS through component
	if (RosPublisherComponent) {
		RosPublisherComponent->TickRos(DeltaSeconds);
	}
	
	// Poll for completed RGB captures and publish
	PollRgbCaptureAndPublish();
	
	if (!CaptureManager || !CaptureManager->IsCaptureEnabled()) return;

	UpdatePublishTimer(DeltaSeconds);
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
	if (!CaptureManager || !RgbCaptureComponent) return;
	
	const FCaptureConfig& Config = CaptureManager->GetConfig();
	if (!Config.bEnableGt && !Config.bEnableRosRgb) return;
	
	if (Config.bEnableRosRgb && RgbCaptureComponent->IsCaptureInProgress()) return;
	
	StartRgbCaptureAndPublish();
}

void ARobotCamRig::StartRgbCaptureAndPublish()
{
	if (!CaptureManager || !RgbCaptureComponent || !GetWorld()) return;

	const FCaptureConfig& Config = CaptureManager->GetConfig();
	if (!Config.bEnableGt && !Config.bEnableRosRgb) return;

	const double CaptureTimeSeconds = GetWorld()->GetTimeSeconds();
	const FCaptureFrameInfo FrameInfo = CaptureManager->NextFrame(CaptureTimeSeconds);

	if (RoverRobot) {
		RoverRobot->PublishGroundTruthPose(FrameInfo);
	}
	
	//GT capture only if enabled in config
	if (Config.bEnableGt && GroundTruthCamera) {
		if (Camera) GroundTruthCamera->SetActorTransform(Camera->GetComponentTransform());
		GroundTruthCamera->CaptureGroundTruthNow(FrameInfo.FrameIndex, FrameInfo.StampSeconds, FrameInfo.SessionId, CaptureManager);
	}
	
	//ROS RGB capture only if enabled in config
	if (Config.bEnableRosRgb) {
		//Start async RGB capture
		if (!RgbCaptureComponent->StartCaptureAsync(FrameInfo)) {
			UE_LOG(LogTemp, Warning, TEXT("RGB capture readback could not start for frame %d"), FrameInfo.FrameIndex);
		}
	}
}

void ARobotCamRig::PollRgbCaptureAndPublish()
{
	if (!RgbCaptureComponent) return;
	
	TArray<uint8> PixelData;
	FCaptureFrameInfo FrameInfo;
	
	if (RgbCaptureComponent->PollReadback(PixelData, FrameInfo)) {
		// Early return if ROS RGB capture is disabled or capture is stopped or session is invalid
		if (!CaptureManager || !CaptureManager->GetConfig().bEnableRosRgb || !CaptureManager->IsSessionValid(FrameInfo.SessionId)) {
			return;
		}
		
		// Publish through ROS component
		if (RosPublisherComponent && RosPublisherComponent->IsReady()) {
			RosPublisherComponent->PublishFrame(FrameInfo, PixelData);
		}
	}
}

//command helper
void ARobotCamRig::OnCaptureControl(int32 ControlValue)
{
	if (ControlValue == 1) {
		if (CaptureManager) CaptureManager->StartCapture();
		PublishAccumulator = 0.0f;
	}
	else if (ControlValue == 0) {
		if (CaptureManager) CaptureManager->StopCapture();
	}
}

void ARobotCamRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CaptureManager) {
		CaptureManager->StopCapture();
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