#pragma once

#include "CoreMinimal.h"
#include "Capture/CapturePoseSourceComponent.h"
#include "Capture/CaptureTypes.h"
#include "TempoROSNode.h"

#include "geometry_msgs/msg/pose_stamped.hpp"
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
	// This is a UObject pointer, so keep it as UPROPERTY for Unreal GC safety.
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	const FString NodeName = TEXT("rover_ground_truth_publisher");

	const FString PoseTopic = TEXT("/gt/rover/pose");
	const FString TfTopic = TEXT("/tf");
	const FString PathTopic = TEXT("/gt/rover/path");

	const FString FrameId = TEXT("map");
	const FString ChildFrameId = TEXT("base_link");

	const bool bPublishPoseStamped = true;
	const bool bPublishTf = true;
	const bool bPublishPath = true;

	const int32 MaxPathLength = 5000; // number of poses to keep in the pat

	geometry_msgs::msg::PoseStamped ReusablePoseMsg;
	tf2_msgs::msg::TFMessage ReusableTfMsg;
	nav_msgs::msg::Path ReusablePathMsg;

	void SetupRos();
	void SetupReusableMessages();

	builtin_interfaces::msg::Time ToRosTime(double StampSeconds) const;
	FVector UnrealLocationToRosMeters(const FVector& UnrealLocation) const;
	FQuat UnrealRotationToRosQuat(const FRotator& UnrealRotation) const;
};