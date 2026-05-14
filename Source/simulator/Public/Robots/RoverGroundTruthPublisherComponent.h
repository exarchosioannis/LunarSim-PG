#pragma once

#include "CoreMinimal.h"
#include "Capture/CapturePoseSourceComponent.h"
#include "Capture/CaptureTypes.h"
#include "TempoROSNode.h"

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "builtin_interfaces/msg/time.hpp"

#include "RoverGroundTruthPublisherComponent.generated.h"

/**
 * Reusable synchronized rover ground-truth publisher.
 *
 * Attach this component to the current rover actor. RobotCamRig calls
 * PublishGroundTruth(FrameInfo) once per capture frame, using the same
 * frame_index and timestamp as RGB, UnrealGT, manifest, and trajectory files.
 *
 * Because this component derives from UCapturePoseSourceComponent, CaptureManager
 * can also use it as the rover pose source for CSV trajectory generation.
 */
UCLASS(ClassGroup=(Capture), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API URoverGroundTruthPublisherComponent : public UCapturePoseSourceComponent
{
	GENERATED_BODY()

public:
	URoverGroundTruthPublisherComponent();

	// Called by RobotCamRig when one synchronized capture frame is created.
	void PublishGroundTruth(const FCaptureFrameInfo& FrameInfo);

	// Called when /control = 1 starts a new capture session.
	void ResetPath();

	void TickRos(float DeltaTime);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	FString NodeName = TEXT("rover_ground_truth_publisher");

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	FString PoseTopic = TEXT("/rover/gt/pose");

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	FString OdomTopic = TEXT("/gt/odom");

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	FString TfTopic = TEXT("/tf");

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	FString PathTopic = TEXT("/gt/path");

	UPROPERTY(VisibleAnywhere, Category = "ROS|Frames")
	FString FrameId = TEXT("map");
	UPROPERTY(VisibleAnywhere, Category = "ROS|Frames")
	FString ChildFrameId = TEXT("base_link");

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	bool bPublishPoseStamped = true;

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	bool bPublishOdometry = true;

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	bool bPublishTf = true;

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth")
	bool bPublishPath = true;

	UPROPERTY(EditAnywhere, Category = "ROS Ground Truth", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxPathLength = 5000;

	geometry_msgs::msg::PoseStamped ReusablePoseMsg;
	nav_msgs::msg::Odometry ReusableOdomMsg;
	tf2_msgs::msg::TFMessage ReusableTfMsg;
	nav_msgs::msg::Path ReusablePathMsg;

	void SetupRos();
	void SetupReusableMessages();

	builtin_interfaces::msg::Time ToRosTime(double StampSeconds) const;
	FVector UnrealLocationToRosMeters(const FVector& UnrealLocation) const;
	FQuat UnrealRotationToRosQuat(const FRotator& UnrealRotation) const;
};