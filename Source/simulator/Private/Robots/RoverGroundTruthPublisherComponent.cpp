#include "Robots/RoverGroundTruthPublisherComponent.h"
#include "simulator.h"
#include "Utils/UnrealToRosConversion.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TempoROSTypes.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(geometry_msgs::msg::PoseStamped);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::Odometry);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::Path);

URoverGroundTruthPublisherComponent::URoverGroundTruthPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void URoverGroundTruthPublisherComponent::BeginPlay()
{
	Super::BeginPlay();
	SetupReusableMessages();
	SetupRos();

	if (bEnableLiveGroundTruthPublishing && GetWorld()) {
		const double NowSeconds = GetWorld()->GetTimeSeconds();
		PublishLivePoseAndTf(NowSeconds);
		PublishLiveOdometry(NowSeconds);
		PublishLivePath(NowSeconds);
	}
}

void URoverGroundTruthPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void URoverGroundTruthPublisherComponent::SetupReusableMessages()
{
	ReusableOdomMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusableOdomMsg.child_frame_id = TCHAR_TO_UTF8(*ChildFrameId);

	ReusablePathMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusablePathMsg.poses.clear();
	ReusablePathMsg.poses.reserve((size_t)FMath::Max(1, MaxPathLength));

	ResetOdometryState();
}

void URoverGroundTruthPublisherComponent::SetupRos()
{
	ROSNode = UTempoROSNode::Create(*NodeName, this);
	if (!ROSNode) {
		UE_LOG(LogLunarSimROS, Error,
			TEXT("Rover ground-truth initialization failed: subsystem=ROS, resource=%s, stage=node creation, cause=TempoROS node unavailable, effect=enabled pose/odometry/path/TF outputs disabled."),
			*NodeName);
		return;
	}

	FROSQOSProfile PoseOdomQOS;
	PoseOdomQOS.CustomQueueSize(10).Reliable().Volatile();

	if (bPublishLivePoseStamped) {
		ROSNode->AddPublisher<geometry_msgs::msg::PoseStamped>(*PoseTopic, PoseOdomQOS, false);
	}

	if (bPublishLiveOdometry) {
		ROSNode->AddPublisher<nav_msgs::msg::Odometry>(*OdomTopic, PoseOdomQOS, false);
	}

	if (bPublishLivePath) {
		FROSQOSProfile PathQOS;
		PathQOS.CustomQueueSize(1).Reliable().TransientLocal();
		ROSNode->AddPublisher<nav_msgs::msg::Path>(*PathTopic, PathQOS, false);
	}
}

void URoverGroundTruthPublisherComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickRos(DeltaTime);
	PublishLiveGroundTruth(DeltaTime);
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

	// Start odometry velocity estimation cleanly after a new capture session starts.
	ResetOdometryState();
}

void URoverGroundTruthPublisherComponent::ResetOdometryState()
{
	bHasPreviousOdomSample = false;
	PreviousOdomTimeSeconds = 0.0;
	PreviousOdomRosPositionMeters = FVector::ZeroVector;
	PreviousOdomRosRotation = FQuat::Identity;
}

void URoverGroundTruthPublisherComponent::PublishLiveGroundTruth(float DeltaTime)
{
	if (!bEnableLiveGroundTruthPublishing || !ROSNode || !GetWorld()) {
		return;
	}

	const double NowSeconds = GetWorld()->GetTimeSeconds();

	if (bPublishLivePoseStamped || bPublishLiveOdometry || bPublishLiveTf) {
		const float SafePoseTfHz = FMath::Max(1.0f, LivePoseTfPublishHz);
		const float PoseTfPeriod = 1.0f / SafePoseTfHz;
		LivePoseTfPublishAccumulator += DeltaTime;
		if (LivePoseTfPublishAccumulator >= PoseTfPeriod) {
			LivePoseTfPublishAccumulator = FMath::Fmod(LivePoseTfPublishAccumulator, PoseTfPeriod);
			PublishLivePoseAndTf(NowSeconds);
			PublishLiveOdometry(NowSeconds);
		}
	}

	if (bPublishLivePath) {
		const float SafePathHz = FMath::Max(1.0f, LivePathPublishHz);
		const float PathPeriod = 1.0f / SafePathHz;
		LivePathPublishAccumulator += DeltaTime;
		if (LivePathPublishAccumulator >= PathPeriod) {
			LivePathPublishAccumulator = FMath::Fmod(LivePathPublishAccumulator, PathPeriod);
			PublishLivePath(NowSeconds);
		}
	}
}

bool URoverGroundTruthPublisherComponent::PopulateReusablePoseMessage(double StampSeconds)
{
	if (!ROSNode) {
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner) {
		return false;
	}

	const builtin_interfaces::msg::Time Stamp = ToRosTime(StampSeconds);
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

	return true;
}

void URoverGroundTruthPublisherComponent::PublishLivePoseAndTf(double StampSeconds)
{
	if (!PopulateReusablePoseMessage(StampSeconds)) {
		return;
	}

	if (bPublishLivePoseStamped) {
		ROSNode->Publish<geometry_msgs::msg::PoseStamped>(*PoseTopic, ReusablePoseMsg);
	}

	if (bPublishLiveTf) {
		const AActor* Owner = GetOwner();
		if (Owner) {
			ROSNode->PublishDynamicTransform(
				Owner->GetActorTransform(),
				ChildFrameId,
				FrameId,
				StampSeconds);
		}
	}
}

void URoverGroundTruthPublisherComponent::PublishLiveOdometry(double StampSeconds)
{
	if (!bPublishLiveOdometry || !PopulateReusablePoseMessage(StampSeconds)) {
		return;
	}

	const FVector CurrentRosPositionMeters(
		ReusablePoseMsg.pose.position.x,
		ReusablePoseMsg.pose.position.y,
		ReusablePoseMsg.pose.position.z
	);

	FQuat CurrentRosRotation(
		ReusablePoseMsg.pose.orientation.x,
		ReusablePoseMsg.pose.orientation.y,
		ReusablePoseMsg.pose.orientation.z,
		ReusablePoseMsg.pose.orientation.w
	);
	CurrentRosRotation.Normalize();

	FVector LinearVelocityBodyMps = FVector::ZeroVector;
	FVector AngularVelocityBodyRadPerSec = FVector::ZeroVector;

	if (bHasPreviousOdomSample) {
		const double Dt = StampSeconds - PreviousOdomTimeSeconds;

		if (Dt > KINDA_SMALL_NUMBER) {
			const FVector LinearVelocityWorldMps = (CurrentRosPositionMeters - PreviousOdomRosPositionMeters) / Dt;
			LinearVelocityBodyMps = CurrentRosRotation.Inverse().RotateVector(LinearVelocityWorldMps);

			FQuat DeltaRotation = CurrentRosRotation * PreviousOdomRosRotation.Inverse();
			DeltaRotation.Normalize();

			FVector DeltaAxis = FVector::ForwardVector;
			float DeltaAngle = 0.0f;
			DeltaRotation.ToAxisAndAngle(DeltaAxis, DeltaAngle);

			if (DeltaAngle > PI) {
				DeltaAngle -= 2.0f * PI;
			}

			const FVector AngularVelocityWorldRadPerSec = DeltaAxis * (DeltaAngle / static_cast<float>(Dt));
			AngularVelocityBodyRadPerSec = CurrentRosRotation.Inverse().RotateVector(AngularVelocityWorldRadPerSec);
		}
	}

	ReusableOdomMsg.header = ReusablePoseMsg.header;
	ReusableOdomMsg.child_frame_id = TCHAR_TO_UTF8(*ChildFrameId);

	ReusableOdomMsg.pose.pose = ReusablePoseMsg.pose;

	ReusableOdomMsg.twist.twist.linear.x = LinearVelocityBodyMps.X;
	ReusableOdomMsg.twist.twist.linear.y = LinearVelocityBodyMps.Y;
	ReusableOdomMsg.twist.twist.linear.z = LinearVelocityBodyMps.Z;

	ReusableOdomMsg.twist.twist.angular.x = AngularVelocityBodyRadPerSec.X;
	ReusableOdomMsg.twist.twist.angular.y = AngularVelocityBodyRadPerSec.Y;
	ReusableOdomMsg.twist.twist.angular.z = AngularVelocityBodyRadPerSec.Z;

	ROSNode->Publish<nav_msgs::msg::Odometry>(*OdomTopic, ReusableOdomMsg);

	PreviousOdomTimeSeconds = StampSeconds;
	PreviousOdomRosPositionMeters = CurrentRosPositionMeters;
	PreviousOdomRosRotation = CurrentRosRotation;
	bHasPreviousOdomSample = true;
}

void URoverGroundTruthPublisherComponent::PublishLivePath(double StampSeconds)
{
	if (!bPublishLivePath || !PopulateReusablePoseMessage(StampSeconds)) {
		return;
	}

	ReusablePathMsg.header = ReusablePoseMsg.header;
	ReusablePathMsg.poses.push_back(ReusablePoseMsg);

	if (MaxPathLength > 0 && ReusablePathMsg.poses.size() > (size_t)MaxPathLength) {
		ReusablePathMsg.poses.erase(ReusablePathMsg.poses.begin());
	}

	ROSNode->Publish<nav_msgs::msg::Path>(*PathTopic, ReusablePathMsg);
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
	(void)FrameInfo;
}
