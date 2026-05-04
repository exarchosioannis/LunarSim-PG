#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"
#include "Capture/CaptureTypes.h"

#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "std_msgs/msg/int32.hpp"

#include "CameraRosPublisherComponent.generated.h"

class UCameraComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API UCameraRosPublisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
       UCameraRosPublisherComponent();

       void Initialize(
	       int32 InWidth,
	       int32 InHeight,
	       const FString& InFrameId,
	       const FString& InTopicBase,
	       UCameraComponent* InCamera
       );

	void TickRos(float DeltaTime);

	void PublishFrame(
		const FCaptureFrameInfo& FrameInfo,
		const TArray<uint8>& PixelData
	);

	bool IsReady() const;

	DECLARE_MULTICAST_DELEGATE_OneParam(FCaptureControlDelegate, int32);
	FCaptureControlDelegate OnCaptureControlReceived;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetupRos();
	void SetupReusableMessages();
	builtin_interfaces::msg::Time ToRosTime(double StampSeconds) const;
	void PublishCameraInfo(const builtin_interfaces::msg::Time& Stamp);
	void OnCaptureControl(const std_msgs::msg::Int32& Msg);


private:
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	UPROPERTY()
	UCameraComponent* Camera = nullptr;

	int32 Width = 1280;
	int32 Height = 720;
	FString CameraName = TEXT("left_camera");
	FString TopicBase = TEXT("/left_camera");
	FString FrameId = TEXT("left_camera");

	// Reusable ROS Messages
	sensor_msgs::msg::Image ReusableImgMsg;
	sensor_msgs::msg::CameraInfo ReusableCamInfoMsg;
	std_msgs::msg::Int32 ReusableFrameIndexMsg;

	bool bInitialized = false;
};