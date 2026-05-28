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
 * Live rover ground-truth publisher.
 *
 * Attach this component to the current rover actor. It publishes live ROS
 * rover pose, ground-truth odometry, TF, and path from Play using world time.
 * Dataset CSV trajectory generation remains owned by CaptureManager.
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

	// Kept for compatibility. Live ROS publishing owns the rover ROS topics.
	void PublishGroundTruth(const FCaptureFrameInfo& FrameInfo);

	// Called when /control = 1 starts a new capture session.
	void ResetPath();

	void TickRos(float DeltaTime);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// This is a UObject pointer, so keep it as UPROPERTY for Unreal GC safety.
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	const FString NodeName = TEXT("rover_ground_truth_publisher");

	const FString PoseTopic = TEXT("/gt/rover/pose");
	const FString OdomTopic = TEXT("/gt/rover/odom");
	const FString TfTopic = TEXT("/tf");
	const FString PathTopic = TEXT("/gt/rover/path");

	const FString FrameId = TEXT("map");
	const FString ChildFrameId = TEXT("base_link");

	UPROPERTY(EditAnywhere, Category = "ROS|Live")
	bool bEnableLiveGroundTruthPublishing = true;

	UPROPERTY(EditAnywhere, Category = "ROS|Live")
	bool bPublishLivePoseStamped = true;

	UPROPERTY(EditAnywhere, Category = "ROS|Live")
	bool bPublishLiveOdometry = true;

	UPROPERTY(EditAnywhere, Category = "ROS|Live")
	bool bPublishLiveTf = true;

	UPROPERTY(EditAnywhere, Category = "ROS|Live")
	bool bPublishLivePath = true;

	UPROPERTY(EditAnywhere, Category = "ROS|Live", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float LivePoseTfPublishHz = 20.0f;

	UPROPERTY(EditAnywhere, Category = "ROS|Live", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float LivePathPublishHz = 10.0f;

	const int32 MaxPathLength = 5000; // number of poses to keep in the path

	geometry_msgs::msg::PoseStamped ReusablePoseMsg;
	nav_msgs::msg::Odometry ReusableOdomMsg;
	tf2_msgs::msg::TFMessage ReusableTfMsg;
	nav_msgs::msg::Path ReusablePathMsg;

	float LivePoseTfPublishAccumulator = 0.0f;
	float LivePathPublishAccumulator = 0.0f;

	bool bHasPreviousOdomSample = false;
	double PreviousOdomTimeSeconds = 0.0;
	FVector PreviousOdomRosPositionMeters = FVector::ZeroVector;
	FQuat PreviousOdomRosRotation = FQuat::Identity;

	void SetupRos();
	void SetupReusableMessages();
	void PublishLiveGroundTruth(float DeltaTime);
	void PublishLivePoseAndTf(double StampSeconds);
	void PublishLiveOdometry(double StampSeconds);
	void PublishLivePath(double StampSeconds);
	bool PopulateReusablePoseMessage(double StampSeconds);
	void ResetOdometryState();

	builtin_interfaces::msg::Time ToRosTime(double StampSeconds) const;
	FVector UnrealLocationToRosMeters(const FVector& UnrealLocation) const;
	FQuat UnrealRotationToRosQuat(const FRotator& UnrealRotation) const;
};