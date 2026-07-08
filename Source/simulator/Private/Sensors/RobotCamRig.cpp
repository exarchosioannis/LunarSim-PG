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
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "TempoROSNode.h"
#include "EngineUtils.h"

ARobotCamRig::ARobotCamRig()
{
	PrimaryActorTick.bCanEverTick = true;
	ResolveCaptureSettings();

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	LeftCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftCameraRoot"));
	LeftCameraRoot->SetupAttachment(Root);
	LeftCameraRoot->SetRelativeLocation(FVector::ZeroVector);
	LeftCameraRoot->SetRelativeRotation(FRotator::ZeroRotator);

	RightCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RightCameraRoot"));
	RightCameraRoot->SetupAttachment(Root);
	RightCameraRoot->SetRelativeLocation(FVector(0.0f, ResolvedStereoBaselineMeters * 100.0, 0.0f));
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
	ResolveCaptureSettings();
	ApplyStereoBaseline();
}

AActor* ARobotCamRig::ResolveRoverActor() const
{
	if (IsValid(RoverActor) && RoverActor != this) {
		return RoverActor;
	}

	//RobotCamRig rig is attached under the rover hierarchy.
	if (AActor* ParentActor = GetAttachParentActor()) {
		if (ParentActor != this) {
			return ParentActor;
		}
	}

	if (AActor* OwnerActor = GetOwner()) {
		if (OwnerActor != this) {
			return OwnerActor;
		}
	}
	//fallback
	if (const USceneComponent* RootComponent = GetRootComponent()) {
		if (const USceneComponent* ParentComponent = RootComponent->GetAttachParent()) {
			if (AActor* ParentOwner = ParentComponent->GetOwner()) {
				if (ParentOwner != this) {
					return ParentOwner;
				}
			}
		}
	}

	return nullptr;
}

void ARobotCamRig::BeginPlay()
{
	Super::BeginPlay();
	EnforceCameraFrameIds();
	ResolveCaptureSettings();
	if (!IsValid(RoverActor)) {
		RoverActor = ResolveRoverActor();
	}
	if (CaptureConfig.IsRos2LiveMode() && CaptureConfig.IsGroundTruthEnabled()) {
		UE_LOG(LogTemp, Warning, TEXT("Ground Truth Images are enabled in ROS2 Live mode. This may reduce live experiment performance."));
	}

	//Attach RobotCamRig to the rover sensor mount first.
	AActor* ResolvedRoverActor = GetRoverActor();
	if (bAttachToRoverSensorMountOnBeginPlay && ResolvedRoverActor) {
		USceneComponent* MountComponent = nullptr;
		TArray<USceneComponent*> SceneComponents;
		ResolvedRoverActor->GetComponents<USceneComponent>(SceneComponents);
		for (USceneComponent* SceneComponent : SceneComponents) {
			if (SceneComponent && SceneComponent->GetFName() == RoverSensorMountComponentName) {
				MountComponent = SceneComponent;
				break;
			}
		}

		if (MountComponent) {
			AttachToComponent(MountComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale );
			SetActorRelativeLocation(FVector::ZeroVector);
			SetActorRelativeRotation(FRotator::ZeroRotator);
			SetActorRelativeScale3D(FVector::OneVector);
			UE_LOG(LogTemp, Log, TEXT("RobotCamRig attached to rover sensor mount: %s"), *RoverSensorMountComponentName.ToString());
		} else {
			UE_LOG(LogTemp, Warning, TEXT("RobotCamRig could not find rover sensor mount component: %s"), *RoverSensorMountComponentName.ToString());
		}
	}

	//Finalize camera component transforms before publishing TF.
	ApplyStereoBaseline();

	//Resolve actor/component references.
	ResolveGroundTruthCameraChild();
	ApplyGroundTruthConfig();
	ResolveRoverGroundTruthComponents();

	//Create TempoROS TF node and publish static camera transforms.
	SetupCameraTfNode();
	PublishStaticCameraTransforms();

	//Initialize RGB capture components.
	if (RgbCaptureComponent && RGBCapture) {
		RgbCaptureComponent->Initialize(RGBCapture, ResolvedWidth, ResolvedHeight, bUseGammaCorrection, OutputGamma, bUseFixedExposure, ExposureCompensation);
	}

	if (RightRgbCaptureComponent && RightRGBCapture) {
		RightRgbCaptureComponent->Initialize(RightRGBCapture, ResolvedWidth, ResolvedHeight, bUseGammaCorrection, OutputGamma, bUseFixedExposure, ExposureCompensation);
	}

	//Initialize ROS image/camera_info publishers.
	//Image and camera_info messages should use optical frames.
	if (RosPublisherComponent && Camera) {
		RosPublisherComponent->Initialize(
			ResolvedWidth, ResolvedHeight,
			LeftCameraOpticalFrameId, TEXT("/left_camera"), Camera,
			true, false, 0.0, CaptureConfig.RunMode
		);
		RosPublisherComponent->OnCaptureControlReceived.AddUObject(this, &ARobotCamRig::OnCaptureControl );
	}

	if (RightRosPublisherComponent && RightCamera) {
		RightRosPublisherComponent->Initialize(
			ResolvedWidth, ResolvedHeight,
			RightCameraOpticalFrameId, TEXT("/right_camera"), RightCamera, false, true,
			ResolvedStereoBaselineMeters, CaptureConfig.RunMode
		);
	}
	//Initialize CaptureManager after camera components exist and are configured.
	CaptureManager = NewObject<UCaptureManager>(this);

	if (CaptureManager) {
		CaptureManager->Initialize(CaptureConfig);
		CaptureManager->SetLeftCameraPoseSource(Camera);
		CaptureManager->SetRightCameraPoseSource(RightCamera);
		if (RoverPoseSource) {
			CaptureManager->SetRoverPoseSource(RoverPoseSource);
		}
	}
	// Enable keyboard input for this actor during gameplay.
	// This allows pressing C to toggle capture
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController()) {
		EnableInput(PlayerController);
		if (InputComponent) {
			InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ARobotCamRig::ToggleCaptureFromKeyboard);
			UE_LOG(LogTemp, Log, TEXT("RobotCamRig: keyboard capture toggle bound to C."));
		}
	}
}

void ARobotCamRig::ApplyStereoBaseline()
{
	const double SafeBaselineCm = FMath::Clamp(ResolvedStereoBaselineMeters * 100.0, 1.0, 200.0);
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
		ApplyGroundTruthConfig();
	}
}

void ARobotCamRig::ApplyGroundTruthConfig()
{
	if (!GroundTruthCamera) {
		return;
	}

	GroundTruthCamera->SetGroundTruthResolution(ResolvedWidth, ResolvedHeight);
	GroundTruthCamera->SetGroundTruthOutputs(
		CaptureConfig.IsGroundTruthRgbEnabled(),
		CaptureConfig.IsGroundTruthDepthEnabled(),
		CaptureConfig.IsGroundTruthSegmentationEnabled(),
		CaptureConfig.IsGroundTruthBoundingBoxesEnabled());
}


void ARobotCamRig::ResolveRoverGroundTruthComponents()
{
	RoverGroundTruthPublisher = nullptr;
	RoverPoseSource = nullptr;

	AActor* ResolvedRoverActor = GetRoverActor();
	if (ResolvedRoverActor && RoverActor != ResolvedRoverActor) {
		RoverActor = ResolvedRoverActor;
	}

	if (ResolvedRoverActor) {
		RoverGroundTruthPublisher = ResolvedRoverActor->FindComponentByClass<URoverGroundTruthPublisherComponent>();
		RoverPoseSource = ResolvedRoverActor->FindComponentByClass<UCapturePoseSourceComponent>();
	}

	// If no usable rover actor was resolved, keep the old fallback: auto-find the first reusable GT publisher.
	// Once a rover actor has been resolved explicitly/through parent ownership, do not silently
	// switch to a different actor just because the expected component is missing.
	if (!ResolvedRoverActor && !RoverGroundTruthPublisher) {
		UWorld* World = GetWorld();
		if (World) {
			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt) {
				AActor* Actor = *ActorIt;
				if (!Actor) continue;

				URoverGroundTruthPublisherComponent* Candidate = Actor->FindComponentByClass<URoverGroundTruthPublisherComponent>();
				if (Candidate) {
					RoverActor = Actor;
					ResolvedRoverActor = Actor;
					RoverGroundTruthPublisher = Candidate;
					RoverPoseSource = Candidate;
					break;
				}
			}
		}
	}

	// Fallback: if the actor has only the old CapturePoseSourceComponent, CaptureManager can still
	// write synchronized CSV trajectory rows, even though ROS /gt topics need the new publisher.
	if (!RoverPoseSource && ResolvedRoverActor) {
		RoverPoseSource = ResolvedRoverActor->FindComponentByClass<UCapturePoseSourceComponent>();
	}

	if (ResolvedRoverActor && !RoverGroundTruthPublisher) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: RoverActor '%s' has no RoverGroundTruthPublisherComponent. CSV pose may work if it has CapturePoseSourceComponent, but /gt/rover/pose, /gt/rover/odom, /tf and /gt/rover/path will not publish."), *ResolvedRoverActor->GetName());
	}
}

void ARobotCamRig::ResolveCaptureSettings()
{
	ResolvedWidth = CaptureConfig.GetResolvedWidth();
	ResolvedHeight = CaptureConfig.GetResolvedHeight();
	ResolvedStereoBaselineMeters = CaptureConfig.GetStereoBaselineMeters();
}

void ARobotCamRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	if (CameraTfROSNode) {
		CameraTfROSNode->Tick(DeltaSeconds);
	}
	if (RosPublisherComponent) {
		RosPublisherComponent->TickRos(DeltaSeconds);
	}
	if (RightRosPublisherComponent) {
		RightRosPublisherComponent->TickRos(DeltaSeconds);
	}
	
	PollRgbCaptureAndPublish();
	if (!CaptureManager || !CaptureManager->IsCaptureEnabled()) return;
	UpdatePublishTimer(DeltaSeconds);
}

void ARobotCamRig::UpdatePublishTimer(float DeltaSeconds)
{
	if (!CaptureManager) return;

	const int32 ResolvedCaptureHz = CaptureManager->GetConfig().GetResolvedCaptureHz();
	if (ResolvedCaptureHz <= 0) {
		return;
	}

	PublishAccumulator += DeltaSeconds;
	const float Hz = FMath::Clamp(static_cast<float>(ResolvedCaptureHz), 1.0f, 24.0f);
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

	if (Config.IsGroundTruthEnabled()) {
		if (GroundTruthCamera) {
			if (Camera) GroundTruthCamera->SetActorTransform(Camera->GetComponentTransform());
			ApplyGroundTruthConfig();
			GroundTruthCamera->CaptureGroundTruthNow(FrameInfo.FrameIndex, FrameInfo.StampSeconds, FrameInfo.SessionId, CaptureManager);
		} else if (!bWarnedMissingGroundTruthCamera) {
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
	PollOneRgbCaptureAndPublish(RgbCaptureComponent, RosPublisherComponent, CaptureManager && Config.IsLeftRosCameraEnabled() );
	PollOneRgbCaptureAndPublish(RightRgbCaptureComponent, RightRosPublisherComponent, CaptureManager && Config.IsRightRosCameraEnabled());
}

void ARobotCamRig::PollOneRgbCaptureAndPublish(URgbCameraCaptureComponent* CaptureComponent, UCameraRosPublisherComponent* PublisherComponent, bool bShouldPublish) {
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
		if (CaptureManager) {
			CaptureManager->StartCapture();
		}
		PublishAccumulator = 0.0f;
		bWarnedMissingGroundTruthCamera = false;
		ShowCaptureScreenMessage(TEXT("Capture started"), FColor::Green);
	} else if (ControlValue == 0) {
		if (CaptureManager) {
			CaptureManager->StopCapture();
		}
		ShowCaptureScreenMessage(TEXT("Capture stopped"), FColor::Yellow);
	}
}

void ARobotCamRig::ToggleCaptureFromKeyboard()
{
	if (!CaptureManager) {
		ShowCaptureScreenMessage(TEXT("CaptureManager not ready"), FColor::Red);
		UE_LOG(LogTemp, Warning, TEXT("C pressed but CaptureManager is not ready."));
		return;
	}
	if (CaptureManager->IsCaptureEnabled()) {
		OnCaptureControl(0);
	} else {
		OnCaptureControl(1);
	}
}

void ARobotCamRig::ShowCaptureScreenMessage(const FString& Message, const FColor& Color)
{
	if (GEngine) {
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, Color, Message);
	}
}

void ARobotCamRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CaptureManager) CaptureManager->StopCapture();
	CameraTfROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ARobotCamRig::SetCaptureConfig(const FCaptureConfig& NewConfig)
{
	CaptureConfig = NewConfig;
	ResolveCaptureSettings();
	ApplyStereoBaseline();
	ApplyGroundTruthConfig();
	if (RightRosPublisherComponent) {
		RightRosPublisherComponent->SetStereoCalibration(true, ResolvedStereoBaselineMeters);
	}
	if (CameraTfROSNode) {
		PublishStaticCameraTransforms();
	}
}

FCaptureConfig ARobotCamRig::GetCaptureConfig() const
{
	return CaptureConfig;
}

AActor* ARobotCamRig::GetRoverActor() const
{
	return ResolveRoverActor();
}

// TF2
void ARobotCamRig::EnforceCameraFrameIds()
{
	BaseFrameId = TEXT("base_link");
	LeftCameraLinkFrameId = TEXT("left_camera_link");
	RightCameraLinkFrameId = TEXT("right_camera_link");
	LeftCameraOpticalFrameId = TEXT("left_camera_optical_frame");
	RightCameraOpticalFrameId = TEXT("right_camera_optical_frame");
}

void ARobotCamRig::SetupCameraTfNode()
{
	if (CameraTfROSNode) return;
	CameraTfROSNode = UTempoROSNode::Create(TEXT("robot_cam_rig_tf_node"), this, false );
	if (!CameraTfROSNode) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: failed to create camera TF ROS node."));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("RobotCamRig: camera TF ROS node created."));
}

void ARobotCamRig::PublishStaticCameraTransforms()
{
	EnforceCameraFrameIds();
	if (!CameraTfROSNode) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: cannot publish camera TFs because CameraTfROSNode is null."));
		return;
	}
	AActor* ResolvedRoverActor = GetRoverActor();
	if (!ResolvedRoverActor) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: cannot publish camera TFs because RoverActor is null."));
		return;
	}
	if (!Camera || !RightCamera) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: cannot publish camera TFs because one or both camera components are null."));
		return;
	}

	const FTransform BaseWorldTransform = ResolvedRoverActor->GetActorTransform();

	const FTransform LeftCameraRelativeTransform =Camera->GetComponentTransform().GetRelativeTransform(BaseWorldTransform);
	const FTransform RightCameraRelativeTransform = RightCamera->GetComponentTransform().GetRelativeTransform(BaseWorldTransform);
	const bool bLeftLinkOk = CameraTfROSNode->PublishStaticTransform(LeftCameraRelativeTransform, LeftCameraLinkFrameId, BaseFrameId );
	const bool bRightLinkOk = CameraTfROSNode->PublishStaticTransform(RightCameraRelativeTransform, RightCameraLinkFrameId, BaseFrameId );

	const FTransform CameraLinkToOpticalTransform(FQuat(0.5, 0.5, 0.5, 0.5), FVector::ZeroVector, FVector::OneVector);
	const bool bLeftOpticalOk = CameraTfROSNode->PublishStaticTransform(CameraLinkToOpticalTransform, LeftCameraOpticalFrameId, LeftCameraLinkFrameId );
	const bool bRightOpticalOk = CameraTfROSNode->PublishStaticTransform(CameraLinkToOpticalTransform, RightCameraOpticalFrameId, RightCameraLinkFrameId );

	if (bLeftLinkOk && bRightLinkOk && bLeftOpticalOk && bRightOpticalOk) {
		UE_LOG(LogTemp, Log,
			TEXT("RobotCamRig: published static camera TFs: %s -> %s -> %s and %s -> %s -> %s"),
			*BaseFrameId, *LeftCameraLinkFrameId, *LeftCameraOpticalFrameId, *BaseFrameId, *RightCameraLinkFrameId, *RightCameraOpticalFrameId);
	} else {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: failed to publish one or more static camera TFs."));
	}
}
