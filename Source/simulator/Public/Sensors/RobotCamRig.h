#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Capture/CaptureManager.h"
#include "Sensors/RgbCameraCaptureComponent.h"
#include "Sensors/CameraRosPublisherComponent.h"

#include "RobotCamRig.generated.h"

class USceneComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UChildActorComponent;
class AGTCamera;
class AActor;
class UCapturePoseSourceComponent;
class URoverGroundTruthPublisherComponent;

UCLASS()
class SIMULATOR_API ARobotCamRig : public AActor
{
	GENERATED_BODY()

public:
	ARobotCamRig();

	void SetCaptureConfig(const FCaptureConfig& NewConfig);
	FCaptureConfig GetCaptureConfig() const;

	void SetStereoBaselineCm(float NewBaselineCm);
	float GetStereoBaselineCm() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	// Components
	UPROPERTY()
	USceneComponent* Root = nullptr;

	// LeftCameraRoot is kept only as a stable parent for the old/existing left camera.
	// It must stay at (0,0,0), so the left camera remains exactly at the RobotCamRig origin.
	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneComponent* LeftCameraRoot = nullptr;

	// RightCameraRoot is placed at local +Y * StereoBaselineCm.
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

	// Ground Truth Camera
	// Assign BP_UnrealGT_Camera here in the RobotCamRig Details panel.
	// The BP will be spawned as a child actor inside RobotCamRig, so it does not need to be placed in the level.
	UPROPERTY(EditAnywhere, Category = "GroundTruth")
	TSubclassOf<AGTCamera> GroundTruthCameraClass;

	UPROPERTY(VisibleAnywhere, Category = "GroundTruth")
	UChildActorComponent* GroundTruthCameraChild = nullptr;

	UPROPERTY()
	AGTCamera* GroundTruthCamera = nullptr;

	bool bWarnedMissingGroundTruthCamera = false;

	// RoverGroundTruthPublisherComponent attached.
	UPROPERTY(EditAnywhere, Category = "Robot")
	AActor* RoverActor = nullptr;

	UPROPERTY()
	URoverGroundTruthPublisherComponent* RoverGroundTruthPublisher = nullptr;

	UPROPERTY()
	UCapturePoseSourceComponent* RoverPoseSource = nullptr;

	// Capture Management
	UPROPERTY()
	UCaptureManager* CaptureManager = nullptr;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FCaptureConfig CaptureConfig;

	// Camera Settings
	float PublishAccumulator = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	int32 Width = 1280;

	UPROPERTY(EditAnywhere, Category = "Camera")
	int32 Height = 720;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FString FrameId = TEXT("left_camera");

	UPROPERTY(EditAnywhere, Category = "Camera")
	FString RightFrameId = TEXT("right_camera");

	// Unreal uses centimeters.
	// The left/reference camera remains at RobotCamRig local (0,0,0).
	// The right camera is always placed at local (0, StereoBaselineCm, 0).
	UPROPERTY(EditAnywhere, Category = "Stereo", meta = (ClampMin = "1.0", ClampMax = "200.0", UIMin = "1.0", UIMax = "100.0"))
	float StereoBaselineCm = 20.0f;

	// Camera Realism
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseGammaCorrection = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseGammaCorrection", ClampMin="0.1", ClampMax="4.0"))
	float OutputGamma = 2.2f;
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

	// Ground truth helpers
	void ResolveGroundTruthCameraChild();
	void ResolveRoverGroundTruthComponents();
};