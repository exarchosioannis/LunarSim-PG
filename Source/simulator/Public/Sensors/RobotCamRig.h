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
class AGTCamera;
class ARoverRobot;

UCLASS()
class SIMULATOR_API ARobotCamRig : public AActor
{
	GENERATED_BODY()

public:
	ARobotCamRig();
	void SetCaptureConfig(const FCaptureConfig& NewConfig);
	FCaptureConfig GetCaptureConfig() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	//Components
	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraComponent* Camera = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneCaptureComponent2D* RGBCapture = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	URgbCameraCaptureComponent* RgbCaptureComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraRosPublisherComponent* RosPublisherComponent = nullptr;

	//Ground Truth Camera
	UPROPERTY(EditAnywhere, Category = "GroundTruth")
	AGTCamera* GroundTruthCamera = nullptr;
	
	//Rover
	UPROPERTY(EditAnywhere, Category = "Robot")
	ARoverRobot* RoverRobot = nullptr;

	//Capture Management
	UPROPERTY()
	UCaptureManager* CaptureManager = nullptr;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FCaptureConfig CaptureConfig;

	//Camera Settings
	float PublishAccumulator = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	int32 Width = 1280;
	UPROPERTY(EditAnywhere, Category = "Camera")
	int32 Height = 720;
	UPROPERTY(EditAnywhere, Category = "Camera")
	FString FrameId = TEXT("left_camera");

	//Camera Realism
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseGammaCorrection = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseGammaCorrection", ClampMin="0.1", ClampMax="4.0"))
	float OutputGamma = 2.2f;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseFixedExposure = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseFixedExposure", ClampMin="-10.0", ClampMax="10.0"))
	float ExposureCompensation = 1.5f;

	//Publish Helpers
	void PublishRgb();

	//RGB Readback
	void StartRgbCaptureAndPublish();
	void PollRgbCaptureAndPublish();

	//Update Helpers
	void UpdatePublishTimer(float DeltaSeconds);

	//command helper
	void OnCaptureControl(int32 ControlValue);
};