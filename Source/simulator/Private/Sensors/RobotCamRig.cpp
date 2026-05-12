#include "Sensors/RobotCamRig.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/ChildActorComponent.h"
#include "Capture/CaptureManager.h"
#include "Engine/World.h"
#include "Sensors/GTCamera.h"
#include "Sensors/RgbCameraCaptureComponent.h"
#include "Sensors/CameraRosPublisherComponent.h"
#include "Robots/RoverGroundTruthPublisherComponent.h"
#include "Capture/CapturePoseSourceComponent.h"
#include "EngineUtils.h"

ARobotCamRig::ARobotCamRig()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	LeftCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftCameraRoot"));
	LeftCameraRoot->SetupAttachment(Root);
	LeftCameraRoot->SetRelativeLocation(FVector::ZeroVector);
	LeftCameraRoot->SetRelativeRotation(FRotator::ZeroRotator);

	RightCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RightCameraRoot"));
	RightCameraRoot->SetupAttachment(Root);
	RightCameraRoot->SetRelativeLocation(FVector(0.0f, StereoBaselineCm, 0.0f));
	RightCameraRoot->SetRelativeRotation(FRotator::ZeroRotator);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("RobotCamera"));
	Camera->SetupAttachment(LeftCameraRoot);
	Camera->SetRelativeLocation(FVector::ZeroVector);
	Camera->SetRelativeRotation(FRotator::ZeroRotator);

	RightCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("RightRobotCamera"));
	RightCamera->SetupAttachment(RightCameraRoot);
	RightCamera->SetRelativeLocation(FVector::ZeroVector);
	RightCamera->SetRelativeRotation(FRotator::ZeroRotator);

	RGBCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RGBCapture"));
	RGBCapture->SetupAttachment(Camera);
	RGBCapture->SetRelativeLocation(FVector::ZeroVector);
	RGBCapture->SetRelativeRotation(FRotator::ZeroRotator);
	RGBCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RGBCapture->bCaptureEveryFrame = false;
	RGBCapture->bCaptureOnMovement = false;

	RightRGBCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RightRGBCapture"));
	RightRGBCapture->SetupAttachment(RightCamera);
	RightRGBCapture->SetRelativeLocation(FVector::ZeroVector);
	RightRGBCapture->SetRelativeRotation(FRotator::ZeroRotator);
	RightRGBCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RightRGBCapture->bCaptureEveryFrame = false;
	RightRGBCapture->bCaptureOnMovement = false;

	// BP_UnrealGT_Camera lives inside this ChildActorComponent.
	// It is attached to the left/reference camera, so GT output stays aligned with the left ROS camera.
	GroundTruthCameraChild = CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundTruthCameraChild"));
	GroundTruthCameraChild->SetupAttachment(Camera);
	GroundTruthCameraChild->SetRelativeLocation(FVector::ZeroVector);
	GroundTruthCameraChild->SetRelativeRotation(FRotator::ZeroRotator);

	RgbCaptureComponent = CreateDefaultSubobject<URgbCameraCaptureComponent>(TEXT("RgbCaptureComponent"));
	RightRgbCaptureComponent = CreateDefaultSubobject<URgbCameraCaptureComponent>(TEXT("RightRgbCaptureComponent"));

	RosPublisherComponent = CreateDefaultSubobject<UCameraRosPublisherComponent>(TEXT("RosPublisherComponent"));
	RightRosPublisherComponent = CreateDefaultSubobject<UCameraRosPublisherComponent>(TEXT("RightRosPublisherComponent"));
}

void ARobotCamRig::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyStereoBaseline();
}

void ARobotCamRig::BeginPlay()
{
	Super::BeginPlay();

	ApplyStereoBaseline();
	ResolveGroundTruthCameraChild();

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

	if (RightRgbCaptureComponent && RightRGBCapture) {
		RightRgbCaptureComponent->Initialize(
			RightRGBCapture,
			Width,
			Height,
			bUseGammaCorrection,
			OutputGamma,
			bUseFixedExposure,
			ExposureCompensation
		);
	}
	
	// Left publisher owns the /control subscription for the whole capture pipeline.
	if (RosPublisherComponent && Camera) {
		RosPublisherComponent->Initialize(
			Width,
			Height,
			FrameId,
			TEXT("/left_camera"),
			Camera,
			true,
			false,
			0.0
		);
		RosPublisherComponent->OnCaptureControlReceived.AddUObject(this, &ARobotCamRig::OnCaptureControl);
	}

	// Right publisher publishes only. It does not subscribe to /control.
	if (RightRosPublisherComponent && RightCamera) {
		RightRosPublisherComponent->Initialize(
			Width,
			Height,
			RightFrameId,
			TEXT("/right_camera"),
			RightCamera,
			false,
			true,
			(double)StereoBaselineCm / 100.0
		);
	}
	
	CaptureManager = NewObject<UCaptureManager>(this);
	if (CaptureManager)
	{
		CaptureManager->Initialize(CaptureConfig);
		CaptureManager->SetLeftCameraPoseSource(Camera);
		CaptureManager->SetRightCameraPoseSource(RightCamera);
	}

	ResolveRoverGroundTruthComponents();
	if (CaptureManager && RoverPoseSource) {
		CaptureManager->SetRoverPoseSource(RoverPoseSource);
	}
}

void ARobotCamRig::ApplyStereoBaseline()
{
	const float SafeBaselineCm = FMath::Clamp(StereoBaselineCm, 1.0f, 200.0f);

	// Important architecture rule:
	// RobotCamRig actor transform is the left/reference camera pose.
	// Therefore left camera and GT camera stay at local (0,0,0).
	// Only the right camera moves, and in this project the correct stereo-right direction is local +Y.
	if (LeftCameraRoot) {
		LeftCameraRoot->SetRelativeLocation(FVector::ZeroVector);
		LeftCameraRoot->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (Camera) {
		Camera->SetRelativeLocation(FVector::ZeroVector);
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (RGBCapture) {
		RGBCapture->SetRelativeLocation(FVector::ZeroVector);
		RGBCapture->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (RightCameraRoot) {
		RightCameraRoot->SetRelativeLocation(FVector(0.0f, SafeBaselineCm, 0.0f));
		RightCameraRoot->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (RightCamera) {
		RightCamera->SetRelativeLocation(FVector::ZeroVector);
		RightCamera->SetRelativeRotation(FRotator::ZeroRotator);

		if (Camera) {
			RightCamera->SetFieldOfView(Camera->FieldOfView);
			RightCamera->SetAspectRatio(Camera->AspectRatio);
			RightCamera->SetConstraintAspectRatio(Camera->bConstrainAspectRatio);
		}
	}

	if (RightRGBCapture) {
		RightRGBCapture->SetRelativeLocation(FVector::ZeroVector);
		RightRGBCapture->SetRelativeRotation(FRotator::ZeroRotator);

		if (RGBCapture) {
			RightRGBCapture->FOVAngle = RGBCapture->FOVAngle;
			RightRGBCapture->ProjectionType = RGBCapture->ProjectionType;
			RightRGBCapture->OrthoWidth = RGBCapture->OrthoWidth;
		}
	}

	if (GroundTruthCameraChild) {
		GroundTruthCameraChild->SetRelativeLocation(FVector::ZeroVector);
		GroundTruthCameraChild->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void ARobotCamRig::ResolveGroundTruthCameraChild()
{
	GroundTruthCamera = nullptr;

	if (!GroundTruthCameraChild || !GroundTruthCameraClass) {
		return;
	}

	GroundTruthCameraChild->SetChildActorClass(GroundTruthCameraClass);

	AActor* ChildActor = GroundTruthCameraChild->GetChildActor();
	GroundTruthCamera = Cast<AGTCamera>(ChildActor);

	if (GroundTruthCamera && Camera) {
		GroundTruthCamera->SetActorTransform(Camera->GetComponentTransform());
	}
}


void ARobotCamRig::ResolveRoverGroundTruthComponents()
{
	RoverGroundTruthPublisher = nullptr;
	RoverPoseSource = nullptr;

	if (RoverActor) {
		RoverGroundTruthPublisher = RoverActor->FindComponentByClass<URoverGroundTruthPublisherComponent>();
		RoverPoseSource = RoverActor->FindComponentByClass<UCapturePoseSourceComponent>();
	}

	// If RoverActor is not assigned, auto-find the first reusable GT publisher.
	// Explicit assignment is still preferred when more than one robot exists.
	if (!RoverGroundTruthPublisher) {
		UWorld* World = GetWorld();
		if (World) {
			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt) {
				AActor* Actor = *ActorIt;
				if (!Actor) continue;

				URoverGroundTruthPublisherComponent* Candidate = Actor->FindComponentByClass<URoverGroundTruthPublisherComponent>();
				if (Candidate) {
					RoverActor = Actor;
					RoverGroundTruthPublisher = Candidate;
					RoverPoseSource = Candidate;
					break;
				}
			}
		}
	}

	// Fallback: if the actor has only the old CapturePoseSourceComponent, CaptureManager can still
	// write synchronized CSV trajectory rows, even though ROS /gt topics need the new publisher.
	if (!RoverPoseSource && RoverActor) {
		RoverPoseSource = RoverActor->FindComponentByClass<UCapturePoseSourceComponent>();
	}

	if (RoverActor && !RoverGroundTruthPublisher) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: RoverActor '%s' has no RoverGroundTruthPublisherComponent. CSV pose may work if it has CapturePoseSourceComponent, but /rover/gt/pose, /gt/odom, /tf and /gt/path will not publish."), *RoverActor->GetName());
	}
}

void ARobotCamRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	if (RosPublisherComponent) {
		RosPublisherComponent->TickRos(DeltaSeconds);
	}

	if (RightRosPublisherComponent) {
		RightRosPublisherComponent->TickRos(DeltaSeconds);
	}

	if (RoverGroundTruthPublisher) {
		RoverGroundTruthPublisher->TickRos(DeltaSeconds);
	}
	
	PollRgbCaptureAndPublish();
	
	if (!CaptureManager || !CaptureManager->IsCaptureEnabled()) return;

	UpdatePublishTimer(DeltaSeconds);
}

void ARobotCamRig::UpdatePublishTimer(float DeltaSeconds)
{
	PublishAccumulator += DeltaSeconds;
	const float Hz = CaptureManager ? FMath::Clamp((float)CaptureManager->GetConfig().PublishHz, 1.0f, 24.0f) : 10.0f;
	const float Period = 1.0f / Hz;
	if (PublishAccumulator >= Period) {
		PublishAccumulator -= Period;
		PublishRgb();
	}
}

void ARobotCamRig::PublishRgb()
{
	if (!CaptureManager) return;
	
	const FCaptureConfig& Config = CaptureManager->GetConfig();
	if (!Config.HasAnyCaptureOutput()) return;
	
	if (Config.IsLeftRosCameraEnabled() && RgbCaptureComponent && RgbCaptureComponent->IsCaptureInProgress()) return;
	if (Config.IsRightRosCameraEnabled() && RightRgbCaptureComponent && RightRgbCaptureComponent->IsCaptureInProgress()) return;
	
	StartRgbCaptureAndPublish();
}

void ARobotCamRig::StartRgbCaptureAndPublish()
{
	if (!CaptureManager || !GetWorld()) return;

	const FCaptureConfig& Config = CaptureManager->GetConfig();
	if (!Config.HasAnyCaptureOutput()) return;

	const double CaptureTimeSeconds = GetWorld()->GetTimeSeconds();
	const FCaptureFrameInfo FrameInfo = CaptureManager->NextFrame(CaptureTimeSeconds);

	if (Config.bEnableRosRoverGtPose && RoverGroundTruthPublisher) {
		RoverGroundTruthPublisher->PublishGroundTruth(FrameInfo);
	}
	
	if (Config.IsGroundTruthEnabled()) {
		if (GroundTruthCamera) {
			if (Camera) GroundTruthCamera->SetActorTransform(Camera->GetComponentTransform());
			GroundTruthCamera->CaptureGroundTruthNow(FrameInfo.FrameIndex, FrameInfo.StampSeconds, FrameInfo.SessionId, CaptureManager);
		}
		else if (!bWarnedMissingGroundTruthCamera) {
			bWarnedMissingGroundTruthCamera = true;
			UE_LOG(LogTemp, Warning, TEXT("Ground truth capture is enabled, but GroundTruthCameraClass is not set or is not based on GTCamera."));
		}
	}
	
	if (Config.IsLeftRosCameraEnabled() && RgbCaptureComponent) {
		if (!RgbCaptureComponent->StartCaptureAsync(FrameInfo)) {
			UE_LOG(LogTemp, Warning, TEXT("Left RGB capture readback could not start for frame %d"), FrameInfo.FrameIndex);
		}
	}

	if (Config.IsRightRosCameraEnabled() && RightRgbCaptureComponent) {
		if (!RightRgbCaptureComponent->StartCaptureAsync(FrameInfo)) {
			UE_LOG(LogTemp, Warning, TEXT("Right RGB capture readback could not start for frame %d"), FrameInfo.FrameIndex);
		}
	}
}

void ARobotCamRig::PollRgbCaptureAndPublish()
{
	const FCaptureConfig Config = CaptureManager ? CaptureManager->GetConfig() : FCaptureConfig();

	PollOneRgbCaptureAndPublish(
		RgbCaptureComponent,
		RosPublisherComponent,
		CaptureManager && Config.IsLeftRosCameraEnabled()
	);

	PollOneRgbCaptureAndPublish(
		RightRgbCaptureComponent,
		RightRosPublisherComponent,
		CaptureManager && Config.IsRightRosCameraEnabled()
	);
}

void ARobotCamRig::PollOneRgbCaptureAndPublish(
	URgbCameraCaptureComponent* CaptureComponent,
	UCameraRosPublisherComponent* PublisherComponent,
	bool bShouldPublish)
{
	if (!CaptureComponent) return;
	
	TArray<uint8> PixelData;
	FCaptureFrameInfo FrameInfo;
	
	if (CaptureComponent->PollReadback(PixelData, FrameInfo)) {
		if (!bShouldPublish || !CaptureManager || !CaptureManager->IsSessionValid(FrameInfo.SessionId)) {
			return;
		}
		
		if (PublisherComponent && PublisherComponent->IsReady()) {
			PublisherComponent->PublishFrame(FrameInfo, PixelData);
		}
	}
}

void ARobotCamRig::OnCaptureControl(int32 ControlValue)
{
	if (ControlValue == 1) {
		if (RoverGroundTruthPublisher) {
			RoverGroundTruthPublisher->ResetPath();
		}
		if (CaptureManager) CaptureManager->StartCapture();
		PublishAccumulator = 0.0f;
		bWarnedMissingGroundTruthCamera = false;
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

void ARobotCamRig::SetStereoBaselineCm(float NewBaselineCm)
{
	StereoBaselineCm = FMath::Clamp(NewBaselineCm, 1.0f, 200.0f);
	ApplyStereoBaseline();

	if (RightRosPublisherComponent) {
		RightRosPublisherComponent->SetStereoCalibration(true, (double)StereoBaselineCm / 100.0);
	}
}


float ARobotCamRig::GetStereoBaselineCm() const
{
	return StereoBaselineCm;
}