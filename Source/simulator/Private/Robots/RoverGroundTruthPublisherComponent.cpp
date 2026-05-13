#include "Robots/RoverGroundTruthPublisherComponent.h"
#include "Utils/UnrealToRosConversion.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TempoROSTypes.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(geometry_msgs::msg::PoseStamped);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::Odometry);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::Path);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(tf2_msgs::msg::TFMessage);

URoverGroundTruthPublisherComponent::URoverGroundTruthPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URoverGroundTruthPublisherComponent::BeginPlay()
{
	Super::BeginPlay();
	SetupReusableMessages();
	SetupRos();
}

void URoverGroundTruthPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void URoverGroundTruthPublisherComponent::SetupReusableMessages()
{
	ReusableTfMsg.transforms.resize(1);

	ReusablePathMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusablePathMsg.poses.clear();
	ReusablePathMsg.poses.reserve((size_t)FMath::Max(1, MaxPathLength));
}

void URoverGroundTruthPublisherComponent::SetupRos()
{
	ROSNode = UTempoROSNode::Create(*NodeName, this);
	if (!ROSNode) {
		UE_LOG(LogTemp, Warning, TEXT("RoverGroundTruthPublisherComponent: failed to create ROS node."));
		return;
	}

	FROSQOSProfile DefaultQOS;
	DefaultQOS.CustomQueueSize(10).Reliable();

	if (bPublishPoseStamped) {
		ROSNode->AddPublisher<geometry_msgs::msg::PoseStamped>(*PoseTopic, DefaultQOS, false);
	}

	if (bPublishOdometry) {
		ROSNode->AddPublisher<nav_msgs::msg::Odometry>(*OdomTopic, DefaultQOS, false);
	}

	if (bPublishTf) {
		ROSNode->AddPublisher<tf2_msgs::msg::TFMessage>(*TfTopic, DefaultQOS, false);
	}

	if (bPublishPath) {
		ROSNode->AddPublisher<nav_msgs::msg::Path>(*PathTopic, DefaultQOS, false);
	}
}

void URoverGroundTruthPublisherComponent::TickRos(float DeltaTime)
{
	if (ROSNode) {
		ROSNode->Tick(DeltaTime);
	}
}

void URoverGroundTruthPublisherComponent::ResetPath()
{
	ReusablePathMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusablePathMsg.poses.clear();
}

builtin_interfaces::msg::Time URoverGroundTruthPublisherComponent::ToRosTime(double Seconds) const
{
	builtin_interfaces::msg::Time T;
	if (Seconds < 0.0) {
		Seconds = 0.0;
	}

	const int64 Sec = (int64)Seconds;
	const double Frac = Seconds - (double)Sec;

	T.sec = (int32)Sec;
	T.nanosec = (uint32)FMath::Clamp<int64>(
		(int64)(Frac * 1000000000.0),
		0,
		999999999
	);

	return T;
}

FVector URoverGroundTruthPublisherComponent::UnrealLocationToRosMeters(const FVector& UnrealLocation) const
{
	return UnrealToRosConversion::PositionCmToRosMeters(UnrealLocation);
}

FQuat URoverGroundTruthPublisherComponent::UnrealRotationToRosQuat(const FRotator& UnrealRotation) const
{
	return UnrealToRosConversion::RotationToRosQuat(UnrealRotation);
}

void URoverGroundTruthPublisherComponent::PublishGroundTruth(const FCaptureFrameInfo& FrameInfo)
{
	if (!ROSNode || FrameInfo.FrameIndex <= 0) {
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner) {
		return;
	}

	const builtin_interfaces::msg::Time Stamp = ToRosTime(FrameInfo.StampSeconds);
	const FVector RosLocation = UnrealLocationToRosMeters(Owner->GetActorLocation());
	const FQuat RosQuat = UnrealRotationToRosQuat(Owner->GetActorRotation());

	ReusablePoseMsg.header.stamp = Stamp;
	ReusablePoseMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusablePoseMsg.pose.position.x = RosLocation.X;
	ReusablePoseMsg.pose.position.y = RosLocation.Y;
	ReusablePoseMsg.pose.position.z = RosLocation.Z;
	ReusablePoseMsg.pose.orientation.x = RosQuat.X;
	ReusablePoseMsg.pose.orientation.y = RosQuat.Y;
	ReusablePoseMsg.pose.orientation.z = RosQuat.Z;
	ReusablePoseMsg.pose.orientation.w = RosQuat.W;

	if (bPublishPoseStamped) {
		ROSNode->Publish<geometry_msgs::msg::PoseStamped>(*PoseTopic, ReusablePoseMsg);
	}

	if (bPublishOdometry) {
		ReusableOdomMsg.header.stamp = Stamp;
		ReusableOdomMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
		ReusableOdomMsg.child_frame_id = TCHAR_TO_UTF8(*ChildFrameId);
		ReusableOdomMsg.pose.pose = ReusablePoseMsg.pose;

		// We intentionally leave twist at zero for now.
		// This message is synchronized pose ground truth, not estimated wheel odometry.
		ReusableOdomMsg.twist.twist.linear.x = 0.0;
		ReusableOdomMsg.twist.twist.linear.y = 0.0;
		ReusableOdomMsg.twist.twist.linear.z = 0.0;
		ReusableOdomMsg.twist.twist.angular.x = 0.0;
		ReusableOdomMsg.twist.twist.angular.y = 0.0;
		ReusableOdomMsg.twist.twist.angular.z = 0.0;

		ROSNode->Publish<nav_msgs::msg::Odometry>(*OdomTopic, ReusableOdomMsg);
	}

	if (bPublishTf) {
		auto& Transform = ReusableTfMsg.transforms[0];
		Transform.header.stamp = Stamp;
		Transform.header.frame_id = TCHAR_TO_UTF8(*FrameId);
		Transform.child_frame_id = TCHAR_TO_UTF8(*ChildFrameId);
		Transform.transform.translation.x = RosLocation.X;
		Transform.transform.translation.y = RosLocation.Y;
		Transform.transform.translation.z = RosLocation.Z;
		Transform.transform.rotation.x = RosQuat.X;
		Transform.transform.rotation.y = RosQuat.Y;
		Transform.transform.rotation.z = RosQuat.Z;
		Transform.transform.rotation.w = RosQuat.W;

		ROSNode->Publish<tf2_msgs::msg::TFMessage>(*TfTopic, ReusableTfMsg);
	}

	if (bPublishPath) {
		ReusablePathMsg.header.stamp = Stamp;
		ReusablePathMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
		ReusablePathMsg.poses.push_back(ReusablePoseMsg);

		if (MaxPathLength > 0 && ReusablePathMsg.poses.size() > (size_t)MaxPathLength) {
			ReusablePathMsg.poses.erase(ReusablePathMsg.poses.begin());
		}

		ROSNode->Publish<nav_msgs::msg::Path>(*PathTopic, ReusablePathMsg);
	}
}