#include "Robots/RoverGroundTruthPublisherComponent.h"
#include "Utils/UnrealToRosConversion.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TempoROSTypes.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(geometry_msgs::msg::PoseStamped);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::Path);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(tf2_msgs::msg::TFMessage);

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
	DefaultQOS.CustomQueueSize(10).Reliable().Volatile();

	if (bPublishLivePoseStamped) {
		ROSNode->AddPublisher<geometry_msgs::msg::PoseStamped>(*PoseTopic, DefaultQOS, false);
	}

	if (bPublishLiveTf) {
		// Dynamic TF must be volatile, not transient local.
		ROSNode->AddPublisher<tf2_msgs::msg::TFMessage>(*TfTopic, DefaultQOS, false);
	}

	if (bPublishLivePath) {
		ROSNode->AddPublisher<nav_msgs::msg::Path>(*PathTopic, DefaultQOS, false);
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
}

void URoverGroundTruthPublisherComponent::PublishLiveGroundTruth(float DeltaTime)
{
	if (!bEnableLiveGroundTruthPublishing || !ROSNode || !GetWorld()) {
		return;
	}

	const double NowSeconds = GetWorld()->GetTimeSeconds();

	if (bPublishLivePoseStamped || bPublishLiveTf) {
		const float SafePoseTfHz = FMath::Max(1.0f, LivePoseTfPublishHz);
		const float PoseTfPeriod = 1.0f / SafePoseTfHz;
		LivePoseTfPublishAccumulator += DeltaTime;
		if (LivePoseTfPublishAccumulator >= PoseTfPeriod) {
			LivePoseTfPublishAccumulator = FMath::Fmod(LivePoseTfPublishAccumulator, PoseTfPeriod);
			PublishLivePoseAndTf(NowSeconds);
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
		auto& Transform = ReusableTfMsg.transforms[0];
		Transform.header = ReusablePoseMsg.header;
		Transform.child_frame_id = TCHAR_TO_UTF8(*ChildFrameId);
		Transform.transform.translation.x = ReusablePoseMsg.pose.position.x;
		Transform.transform.translation.y = ReusablePoseMsg.pose.position.y;
		Transform.transform.translation.z = ReusablePoseMsg.pose.position.z;
		Transform.transform.rotation.x = ReusablePoseMsg.pose.orientation.x;
		Transform.transform.rotation.y = ReusablePoseMsg.pose.orientation.y;
		Transform.transform.rotation.z = ReusablePoseMsg.pose.orientation.z;
		Transform.transform.rotation.w = ReusablePoseMsg.pose.orientation.w;

		ROSNode->Publish<tf2_msgs::msg::TFMessage>(*TfTopic, ReusableTfMsg);
	}
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
