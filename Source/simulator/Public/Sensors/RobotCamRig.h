#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TempoROSNode.h"
#include "RHIGPUReadback.h"
#include "Capture/CaptureManager.h"

#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "std_msgs/msg/int32.hpp"

#include "RobotCamRig.generated.h"

class USceneComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class AGTCamera;

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

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RGBRenderTarget = nullptr;

	//Ground Truth Camera
	UPROPERTY(EditAnywhere, Category = "GroundTruth")
	AGTCamera* GroundTruthCamera = nullptr;

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
	FString FrameId = TEXT("camera_link");

	//Camera Realism
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseGammaCorrection = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseGammaCorrection", ClampMin="0.1", ClampMax="4.0"))
	float OutputGamma = 2.2f;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism")
	bool bUseFixedExposure = true;
	UPROPERTY(EditAnywhere, Category = "Camera|Realism", meta=(EditCondition="bUseFixedExposure", ClampMin="-10.0", ClampMax="10.0"))
	float ExposureCompensation = 1.5f;

	//ROS
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	//GPU Readback
	TUniquePtr<FRHIGPUTextureReadback> GPUReadback;
	bool bReadbackInFlight = false;

	//Reusable ROS Messages
	builtin_interfaces::msg::Time PendingStamp;
	sensor_msgs::msg::Image ReusableImgMsg;
	sensor_msgs::msg::CameraInfo ReusableCamInfoMsg;
	std_msgs::msg::Int32 ReusableFrameIndexMsg;

	//Publish Helpers
	void PublishRgb();
	void PublishCameraInfo(const builtin_interfaces::msg::Time& Stamp);

	FCaptureFrameInfo CreateSynchronizedFrame(double CaptureTimeSeconds);

	//RGB Readback
	void StartRgbReadback();
	void FinishRgbReadbackAndPublish();

	//Update Helpers
	void UpdatePublishTimer(float DeltaSeconds);

	//Setup
	void SetupRenderTarget();
	void SetupReusableMessages();
	void SetupRos();
	void ApplyRoverCameraLook();

	//Frame tracking for readback
	int32 CurrentFrameIndex = 0;
	//Session tracking for validation
	int32 CurrentSessionId = 0;

	//command helper
	void OnCaptureControl(const std_msgs::msg::Int32& Msg);
};