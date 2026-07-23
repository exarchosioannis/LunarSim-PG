#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Capture/CaptureManager.h"
#include "Sensors/RgbCameraCaptureComponent.h"
#include "Sensors/CameraRosPublisherComponent.h"
#include "TempoROSNode.h"
#include "TimerManager.h"
#include "RobotCamRig.generated.h"

class USceneComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UChildActorComponent;
class AGTCamera;
class AActor;
class UCapturePoseSourceComponent;
class URoverGroundTruthPublisherComponent;
class UCaptureStatusOverlayComponent;

UCLASS()
class SIMULATOR_API ARobotCamRig : public AActor
{
	GENERATED_BODY()

public:
	ARobotCamRig();

	void SetCaptureConfig(const FCaptureConfig& NewConfig);
	FCaptureConfig GetCaptureConfig() const;

	AActor* GetRoverActor() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	enum class ECaptureRuntimeState : uint8
	{
		Idle,
		Capturing,
		Finalizing
	};

	static constexpr float CaptureFinalizationCooldownSeconds = 3.0f;

	// Components
	UPROPERTY()
	USceneComponent* Root = nullptr;

	// LeftCameraRoot is kept only as a stable parent for the old/existing left camera.
	// It must stay at (0,0,0), so the left camera remains exactly at the RobotCamRig origin.
	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneComponent* LeftCameraRoot = nullptr;

	// RightCameraRoot is placed at local +Y using the baseline resolved from CaptureConfig.
	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneComponent* RightCameraRoot = nullptr;

	// Existing camera is the left/reference camera.
	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraComponent* Camera = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneCaptureComponent2D* RGBCapture = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraComponent* RightCamera = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneCaptureComponent2D* RightRGBCapture = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	URgbCameraCaptureComponent* RgbCaptureComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	URgbCameraCaptureComponent* RightRgbCaptureComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraRosPublisherComponent* RosPublisherComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraRosPublisherComponent* RightRosPublisherComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCaptureStatusOverlayComponent* CaptureStatusOverlay = nullptr;

	// Ground Truth Camera
	// Assign BP_UnrealGT_Camera here in the RobotCamRig Details panel.
	// The BP will be spawned as a child actor inside RobotCamRig, so it does not need to be placed in the level.
	UPROPERTY(EditAnywhere, Category = "GroundTruth")
	TSubclassOf<AGTCamera> GroundTruthCameraClass;

	UPROPERTY(VisibleAnywhere, Category = "GroundTruth")
	UChildActorComponent* GroundTruthCameraChild = nullptr;

	UPROPERTY()
	AGTCamera* GroundTruthCamera = nullptr;

	bool bGroundTruthWarmUpReady = false;
	bool bGroundTruthWarmUpFailed = false;
	FString GroundTruthWarmUpFailure;

	// Rover actor used for rover ground truth and optional camera-rig mounting.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Robot", meta=(AllowPrivateAccess="true"))
	AActor* RoverActor = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover Mount", meta=(AllowPrivateAccess="true"))
	FName RoverSensorMountComponentName = TEXT("RoverSensorMount");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover Mount", meta=(AllowPrivateAccess="true"))
	bool bAttachToRoverSensorMountOnBeginPlay = true;

	UPROPERTY()
	URoverGroundTruthPublisherComponent* RoverGroundTruthPublisher = nullptr;

	UPROPERTY()
	UCapturePoseSourceComponent* RoverPoseSource = nullptr;

	// Capture Management
	UPROPERTY()
	UCaptureManager* CaptureManager = nullptr;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FCaptureConfig CaptureConfig;

	ECaptureRuntimeState CaptureRuntimeState = ECaptureRuntimeState::Idle;
	FTimerHandle FinalizationCooldownTimerHandle;

	// Camera Settings
	float PublishAccumulator = 0.0f;
	FResolvedCameraCalibration ResolvedCameraCalibration;
	double ResolvedStereoBaselineMeters = 0.2;

	// TF2
	UPROPERTY()
	UTempoROSNode* CameraTfROSNode = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "Camera|Frames")
	FString BaseFrameId = TEXT("base_link");

	UPROPERTY(VisibleAnywhere, Category = "Camera|Frames")
	FString LeftCameraLinkFrameId = TEXT("left_camera_link");
	UPROPERTY(VisibleAnywhere, Category = "Camera|Frames")
	FString RightCameraLinkFrameId = TEXT("right_camera_link");

	UPROPERTY(VisibleAnywhere, Category = "Camera|Frames")
	FString LeftCameraOpticalFrameId = TEXT("left_camera_optical_frame");
	UPROPERTY(VisibleAnywhere, Category = "Camera|Frames")
	FString RightCameraOpticalFrameId = TEXT("right_camera_optical_frame");

	void SetupCameraTfNode();
	void PublishStaticCameraTransforms();
	void EnforceCameraFrameIds();
	void ResolveCaptureSettings();
	void ApplyCameraCalibration();

	// Camera Realism
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseGammaCorrection = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseGammaCorrection", ClampMin="0.1", ClampMax="4.0"))
	float OutputGamma = 3.0f;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseFixedExposure = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseFixedExposure", ClampMin="-10.0", ClampMax="10.0"))
	float ExposureCompensation = 1.5f;

	// Publish helpers
	void PublishRgb();

	// RGB readback
	void StartRgbCaptureAndPublish();
	void PollRgbCaptureAndPublish();
	void PollOneRgbCaptureAndPublish(URgbCameraCaptureComponent* CaptureComponent, UCameraRosPublisherComponent* PublisherComponent, bool bShouldPublish);

	// Update helpers
	void UpdatePublishTimer(float DeltaSeconds);
	void ApplyStereoBaseline();

	// Command helper
	void OnCaptureControl(int32 ControlValue);
	void RequestCaptureStart();
	void RequestCaptureStop();
	void CompleteCaptureFinalization();

	// Keyboard helper
	void ToggleCaptureFromKeyboard();
	void ShowCaptureScreenMessage(const FString& Message, const FColor& Color);

	// Ground truth helpers
	void ResolveGroundTruthCameraChild();
	void ApplyGroundTruthConfig();
	void InitializeGroundTruthWarmUp();
	void ResolveRoverGroundTruthComponents();

	// Runtime rover resolution helper. Keeps the old manually-assigned RoverActor workflow,
	// but also supports RobotCamRig being spawned as a child actor inside ESA_Rover.
	AActor* ResolveRoverActor() const;
};
