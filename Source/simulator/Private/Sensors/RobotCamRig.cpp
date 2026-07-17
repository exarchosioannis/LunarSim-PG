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
#include "Utils/LunarSimRosInterface.h"
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

	ApplyCameraCalibration();
	ApplyStereoBaseline();
}

void ARobotCamRig::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ResolveCaptureSettings();
	ApplyCameraCalibration();
	ApplyStereoBaseline();
}

AActor* ARobotCamRig::ResolveRoverActor() const
{
	if (IsValid(RoverActor) && RoverActor != this) {
		return RoverActor;
	}

	// A child rig inherits its rover through the attachment hierarchy.
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

	// Mount before camera calibration and TF publication so all extrinsics use the final pose.
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

	ApplyCameraCalibration();
	ApplyStereoBaseline();

	ResolveGroundTruthCameraChild();
	ApplyGroundTruthConfig();
	ResolveRoverGroundTruthComponents();

	SetupCameraTfNode();
	PublishStaticCameraTransforms();

	if (RgbCaptureComponent && RGBCapture) {
		RgbCaptureComponent->Initialize(RGBCapture, ResolvedCameraCalibration, bUseGammaCorrection, OutputGamma, bUseFixedExposure, ExposureCompensation);
	}

	if (RightRgbCaptureComponent && RightRGBCapture) {
		RightRgbCaptureComponent->Initialize(RightRGBCapture, ResolvedCameraCalibration, bUseGammaCorrection, OutputGamma, bUseFixedExposure, ExposureCompensation);
	}

	// Image and CameraInfo messages use optical frames, not camera-link frames.
	if (RosPublisherComponent && Camera) {
		RosPublisherComponent->Initialize(
			ResolvedCameraCalibration,
			LeftCameraOpticalFrameId,
			true, false, 0.0
		);
		RosPublisherComponent->OnCaptureControlReceived.AddUObject(this, &ARobotCamRig::OnCaptureControl );
	}

	if (RightRosPublisherComponent && RightCamera) {
		RightRosPublisherComponent->Initialize(
			ResolvedCameraCalibration,
			RightCameraOpticalFrameId, false, true,
			ResolvedStereoBaselineMeters
		);
	}
	// CaptureManager receives the finalized, immutable calibration.
	CaptureManager = NewObject<UCaptureManager>(this);

	if (CaptureManager) {
		CaptureManager->Initialize(CaptureConfig, ResolvedCameraCalibration);
		CaptureManager->SetLeftCameraPoseSource(Camera);
		CaptureManager->SetRightCameraPoseSource(RightCamera);
		if (RoverPoseSource) {
			CaptureManager->SetRoverPoseSource(RoverPoseSource);
		}
	}

	// Warm the configured UnrealGT render paths before capture controls can create a session.
	InitializeGroundTruthWarmUp();

	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController()) {
		EnableInput(PlayerController);
		if (InputComponent) {
			InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ARobotCamRig::ToggleCaptureFromKeyboard);
			UE_LOG(LogTemp, Log, TEXT("RobotCamRig: keyboard capture toggle bound to C."));
		}
	}
}

void ARobotCamRig::ApplyCameraCalibration()
{
	if (!ResolvedCameraCalibration.IsValid()) {
		UE_LOG(LogTemp, Error, TEXT("RobotCamRig: resolved camera calibration is invalid; camera projection was not applied."));
		return;
	}

	const float ResolvedAspectRatio = static_cast<float>(ResolvedCameraCalibration.ImageWidth)
		/ static_cast<float>(ResolvedCameraCalibration.ImageHeight);
	const auto ConfigureCamera = [this, ResolvedAspectRatio](UCameraComponent* CameraComponent)
	{
		if (!CameraComponent) return;
		CameraComponent->ProjectionMode = ECameraProjectionMode::Perspective;
		CameraComponent->SetFieldOfView(ResolvedCameraCalibration.HorizontalFovDeg);
		CameraComponent->SetAspectRatio(ResolvedAspectRatio);
	};
	const auto ConfigureSceneCapture = [this](USceneCaptureComponent2D* SceneCapture)
	{
		if (!SceneCapture) return;
		SceneCapture->ProjectionType = ECameraProjectionMode::Perspective;
		SceneCapture->FOVAngle = ResolvedCameraCalibration.HorizontalFovDeg;
		SceneCapture->Overscan = 0.0f;
		SceneCapture->bUseCustomProjectionMatrix = false;
		SceneCapture->CustomProjectionMatrix.SetIdentity();

		if (SceneCapture->ProjectionType != ECameraProjectionMode::Perspective
			|| !FMath::IsNearlyEqual(SceneCapture->FOVAngle, ResolvedCameraCalibration.HorizontalFovDeg)
			|| !FMath::IsNearlyZero(SceneCapture->Overscan)
			|| SceneCapture->bUseCustomProjectionMatrix) {
			UE_LOG(LogTemp, Error,
				TEXT("RobotCamRig: SceneCapture %s does not match the resolved ideal pinhole calibration."),
				*SceneCapture->GetName());
		}
	};

	ConfigureCamera(Camera);
	ConfigureCamera(RightCamera);
	ConfigureSceneCapture(RGBCapture);
	ConfigureSceneCapture(RightRGBCapture);
}

void ARobotCamRig::ApplyStereoBaseline()
{
	const double SafeBaselineCm = FCaptureConfig::SanitizeStereoBaselineCm(static_cast<float>(ResolvedStereoBaselineMeters * 100.0));
	ResolvedStereoBaselineMeters = SafeBaselineCm / 100.0;
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
	}

	if (RightRGBCapture) {
		RightRGBCapture->SetRelativeLocation(FVector::ZeroVector);
		RightRGBCapture->SetRelativeRotation(FRotator::ZeroRotator);
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

	GroundTruthCamera->SetGroundTruthCalibration(ResolvedCameraCalibration);
	GroundTruthCamera->SetGroundTruthOutputs(
		CaptureConfig.IsGroundTruthRgbEnabled(),
		CaptureConfig.IsGroundTruthDepthEnabled(),
		CaptureConfig.IsGroundTruthSegmentationEnabled(),
		CaptureConfig.IsGroundTruthBoundingBoxesEnabled());
}

void ARobotCamRig::InitializeGroundTruthWarmUp()
{
	bGroundTruthWarmUpReady = false;
	bGroundTruthWarmUpFailed = false;
	GroundTruthWarmUpFailure.Reset();

	UWorld* World = GetWorld();
	UE_LOG(LogTemp, Log,
		TEXT("RobotCamRig: GT warm-up config world=%s world_id=%u resolution=%dx%d hfov=%.3f rgb=%s depth=%s segmentation=%s bounding_boxes=%s."),
		World ? *World->GetName() : TEXT("None"),
		World ? World->GetUniqueID() : 0,
		ResolvedCameraCalibration.ImageWidth,
		ResolvedCameraCalibration.ImageHeight,
		ResolvedCameraCalibration.HorizontalFovDeg,
		CaptureConfig.IsGroundTruthRgbEnabled() ? TEXT("true") : TEXT("false"),
		CaptureConfig.IsGroundTruthDepthEnabled() ? TEXT("true") : TEXT("false"),
		CaptureConfig.IsGroundTruthSegmentationEnabled() ? TEXT("true") : TEXT("false"),
		CaptureConfig.IsGroundTruthBoundingBoxesEnabled() ? TEXT("true") : TEXT("false"));

	if (!CaptureConfig.IsGroundTruthEnabled()) {
		bGroundTruthWarmUpReady = true;
		return;
	}

	if (!IsValid(GroundTruthCamera)) {
		bGroundTruthWarmUpFailed = true;
		GroundTruthWarmUpFailure =
			TEXT("Ground truth is enabled but GroundTruthCamera is unavailable.");
		UE_LOG(LogTemp, Error, TEXT("RobotCamRig: GT warm-up FAILED: %s"), *GroundTruthWarmUpFailure);
		return;
	}

	// Reapply the immutable PIE configuration immediately before the one-time warm-up.
	ApplyGroundTruthConfig();
	bGroundTruthWarmUpReady = GroundTruthCamera->RunInternalWarmUp();
	bGroundTruthWarmUpFailed = !bGroundTruthWarmUpReady;
	if (bGroundTruthWarmUpFailed)
	{
		GroundTruthWarmUpFailure = GroundTruthCamera->GetInternalWarmUpFailure();
		return;
	}
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
	// write synchronized CSV trajectory rows, even though live ROS ground-truth topics need the publisher.
	if (!RoverPoseSource && ResolvedRoverActor) {
		RoverPoseSource = ResolvedRoverActor->FindComponentByClass<UCapturePoseSourceComponent>();
	}

	if (ResolvedRoverActor && !RoverGroundTruthPublisher) {
		UE_LOG(LogTemp, Warning,
			TEXT("RobotCamRig: RoverActor '%s' has no RoverGroundTruthPublisherComponent. CSV pose may work if it has CapturePoseSourceComponent, but %s, %s, %s and %s will not publish."),
			*ResolvedRoverActor->GetName(),
			LunarSimRosTopics::GroundTruthPose,
			LunarSimRosTopics::GroundTruthOdom,
			LunarSimRosTopics::Tf,
			LunarSimRosTopics::GroundTruthPath);
	}
}

void ARobotCamRig::ResolveCaptureSettings()
{
	if (CaptureConfig.Sanitize()) {
		UE_LOG(LogTemp, Warning,
			TEXT("RobotCamRig: invalid capture config values were normalized to CaptureHz=%.3f, HorizontalFovDeg=%.2f, StereoBaselineCm=%.2f."),
			CaptureConfig.GetResolvedCaptureHz(),
			CaptureConfig.GetResolvedHorizontalFovDeg(),
			CaptureConfig.StereoBaselineCm);
	}
	ResolvedCameraCalibration = CaptureConfig.GetResolvedCameraCalibration();
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

	const float ResolvedCaptureHz = CaptureManager->GetConfig().GetResolvedCaptureHz();
	if (!FCaptureConfig::IsValidCameraCaptureHz(ResolvedCaptureHz)) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: invalid capture Hz %.3f; skipping capture tick."), ResolvedCaptureHz);
		return;
	}

	PublishAccumulator += DeltaSeconds;
	const float Period = 1.0f / ResolvedCaptureHz;
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
	if (FrameInfo.FrameIndex == 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("RobotCamRig: first real synchronized frame session_id=%d frame_index=%d stamp=%.9f gt_ready=%s."),
			FrameInfo.SessionId,
			FrameInfo.FrameIndex,
			FrameInfo.StampSeconds,
			bGroundTruthWarmUpReady ? TEXT("true") : TEXT("false"));
	}

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
		if (CaptureManager && CaptureManager->IsCaptureEnabled()) {
			UE_LOG(LogTemp, Log, TEXT("RobotCamRig: capture is already active; repeated start command ignored."));
			return;
		}
		if (!bGroundTruthWarmUpReady) {
			const FString Reason = bGroundTruthWarmUpFailed
				? GroundTruthWarmUpFailure
				: TEXT("GT pipeline warm-up is not complete.");
			UE_LOG(LogTemp, Error,
				TEXT("RobotCamRig: capture start rejected before GT Ready; no session was created. Reason: %s"),
				*Reason);
			ShowCaptureScreenMessage(
				FString::Printf(TEXT("Capture not started: %s"), *Reason), FColor::Red);
			return;
		}

		if (RoverGroundTruthPublisher) {
			RoverGroundTruthPublisher->ResetPath();
		}
		if (CaptureManager) {
			UE_LOG(LogTemp, Log,
				TEXT("RobotCamRig: real capture start accepted after GT Ready; creating the real session now."));
			CaptureManager->StartCapture();
		}
		PublishAccumulator = 0.0f;
		bWarnedMissingGroundTruthCamera = false;
		ShowCaptureScreenMessage(TEXT("Capture started"), FColor::Green);
	} else if (ControlValue == 0) {
		if (!CaptureManager || !CaptureManager->IsCaptureEnabled()) {
			UE_LOG(LogTemp, Log, TEXT("RobotCamRig: capture is already stopped; repeated stop command ignored."));
			return;
		}
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
	bGroundTruthWarmUpReady = false;
	bGroundTruthWarmUpFailed = true;
	GroundTruthWarmUpFailure = TEXT("PIE world is ending.");
	CameraTfROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ARobotCamRig::SetCaptureConfig(const FCaptureConfig& NewConfig)
{
	const UWorld* World = GetWorld();
	if ((World && World->IsGameWorld()) || HasActorBegunPlay() || (CaptureManager && CaptureManager->IsCaptureEnabled())) {
		UE_LOG(LogTemp, Warning, TEXT("RobotCamRig: SetCaptureConfig ignored. Capture configuration is editor/design-time only and is frozen once PIE/gameplay begins."));
		return;
	}

	CaptureConfig = NewConfig;
	ResolveCaptureSettings();
	ApplyCameraCalibration();
	ApplyStereoBaseline();
	ApplyGroundTruthConfig();
	if (RosPublisherComponent) {
		RosPublisherComponent->SetStereoCalibration(false, 0.0);
	}
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
