#include "Sensors/CameraRosPublisherComponent.h"
#include "TempoROSTypes.h"
#include "Utils/LunarSimRosInterface.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::Image);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::CameraInfo);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(std_msgs::msg::Int32);

UCameraRosPublisherComponent::UCameraRosPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCameraRosPublisherComponent::Initialize(
	const FResolvedCameraCalibration& InCalibration,
	const FString& InFrameId,
	bool bInSubscribeToControl,
	bool bInIsRightStereoCamera,
	double InStereoBaselineMeters)
{
	Calibration = InCalibration;
	if (!Calibration.IsValid()) {
		UE_LOG(LogTemp, Error, TEXT("CameraRosPublisherComponent: invalid resolved calibration; using the default 1024x1024, 90 degree calibration."));
		Calibration = FCaptureConfig().GetResolvedCameraCalibration();
	}
	FrameId = InFrameId;
	CameraName = InFrameId;
	bSubscribeToControl = bInSubscribeToControl;
	ImageTopic = bInIsRightStereoCamera
		? LunarSimRosTopics::StereoRightImage
		: LunarSimRosTopics::StereoLeftImage;
	CameraInfoTopic = bInIsRightStereoCamera
		? LunarSimRosTopics::StereoRightCameraInfo
		: LunarSimRosTopics::StereoLeftCameraInfo;
	SetStereoCalibration(bInIsRightStereoCamera, InStereoBaselineMeters);

	SetupReusableMessages();
	SetupRos();

	bInitialized = true;
}


void UCameraRosPublisherComponent::SetStereoCalibration(bool bInIsRightStereoCamera, double InStereoBaselineMeters)
{
	bIsRightStereoCamera = bInIsRightStereoCamera;
	if (!bIsRightStereoCamera) {
		StereoBaselineMeters = FMath::IsFinite(InStereoBaselineMeters)
			? FMath::Max(0.0, InStereoBaselineMeters)
			: 0.0;
		return;
	}

	if (!FMath::IsFinite(InStereoBaselineMeters) || InStereoBaselineMeters <= 0.0) {
		StereoBaselineMeters = FCaptureConfig::GetDefaultStereoBaselineCm() / 100.0;
		UE_LOG(LogTemp, Warning,
			TEXT("CameraRosPublisherComponent: invalid right stereo baseline %.6f m; using %.6f m for camera_info."),
			InStereoBaselineMeters,
			StereoBaselineMeters);
		return;
	}

	StereoBaselineMeters = InStereoBaselineMeters;
}

void UCameraRosPublisherComponent::SetupReusableMessages()
{
	ReusableImgMsg.height = Calibration.ImageHeight;
	ReusableImgMsg.width = Calibration.ImageWidth;
	ReusableImgMsg.encoding = "bgr8";
	ReusableImgMsg.is_bigendian = false;
	ReusableImgMsg.step = Calibration.ImageWidth * 3;
	ReusableImgMsg.data.resize((size_t)Calibration.ImageWidth * (size_t)Calibration.ImageHeight * 3);

	ReusableCamInfoMsg.height = Calibration.ImageHeight;
	ReusableCamInfoMsg.width = Calibration.ImageWidth;
	ReusableCamInfoMsg.distortion_model = "plumb_bob";
	ReusableCamInfoMsg.d = {0.0, 0.0, 0.0, 0.0, 0.0};
}

void UCameraRosPublisherComponent::SetupRos()
{
	ROSNode = UTempoROSNode::Create(*CameraName, this);

	FROSQOSProfile StereoQOS;
	StereoQOS.CustomQueueSize(20).Reliable().Volatile();

	ROSNode->AddPublisher<sensor_msgs::msg::Image>(
		*ImageTopic,
		StereoQOS,
		false
	);

	ROSNode->AddPublisher<sensor_msgs::msg::CameraInfo>(
		*CameraInfoTopic,
		StereoQOS,
		false
	);

	if (bSubscribeToControl)
	{
		FROSQOSProfile ControlQOS;
		ControlQOS.CustomQueueSize(1).Reliable().Volatile();
		ROSNode->AddSubscription<std_msgs::msg::Int32>(
			LunarSimRosTopics::CaptureControl,
			TROSSubscriptionDelegate<std_msgs::msg::Int32>::CreateUObject(this, &UCameraRosPublisherComponent::OnCaptureControl),
			ControlQOS
		);
	}
}

builtin_interfaces::msg::Time UCameraRosPublisherComponent::ToRosTime(double Seconds) const
{
	builtin_interfaces::msg::Time T;
	if (Seconds < 0.0) Seconds = 0.0;
	const int64 Sec = (int64)Seconds;
	const double Frac = Seconds - (double)Sec;
	T.sec = (int32)Sec;
	T.nanosec = (uint32)FMath::Clamp<int64>((int64)(Frac * 1000000000.0), 0, 999999999);
	return T;
}

void UCameraRosPublisherComponent::TickRos(float DeltaTime)
{
	if (ROSNode) {
		ROSNode->Tick(DeltaTime);
	}
}

void UCameraRosPublisherComponent::PublishFrame(
	const FCaptureFrameInfo& FrameInfo,
	const TArray<uint8>& PixelData)
{
	if (!ROSNode || !bInitialized) return;

	const int32 W = Calibration.ImageWidth;
	const int32 H = Calibration.ImageHeight;
	const int32 PixelCount = W * H;
	const int32 InputBytesPerPixel = 4;
	
	if (PixelData.Num() != PixelCount * InputBytesPerPixel) return;
	
	// Unreal readback is BGRA; ROS image_raw is published as bgr8 by dropping alpha.
	uint8* OutData = ReusableImgMsg.data.data();
	const uint8* InData = PixelData.GetData();
	for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex) {
		const int32 InOffset = PixelIndex * 4;
		const int32 OutOffset = PixelIndex * 3;
		OutData[OutOffset + 0] = InData[InOffset + 0];
		OutData[OutOffset + 1] = InData[InOffset + 1];
		OutData[OutOffset + 2] = InData[InOffset + 2];
	}
	
	const builtin_interfaces::msg::Time Stamp = ToRosTime(FrameInfo.StampSeconds);
	ReusableImgMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusableImgMsg.header.stamp = Stamp;

	// Publish image and matching camera_info. FrameIndex remains internal for
	// manifest/trajectory alignment and is not exposed as a ROS topic.
	ROSNode->Publish<sensor_msgs::msg::Image>(*ImageTopic, ReusableImgMsg);
	PublishCameraInfo(Stamp);
}

void UCameraRosPublisherComponent::PublishCameraInfo(const builtin_interfaces::msg::Time& Stamp)
{
	if (!ROSNode || !Calibration.IsValid()) return;

	const double Fx = Calibration.Fx;
	const double Fy = Calibration.Fy;
	const double Cx = Calibration.Cx;
	const double Cy = Calibration.Cy;

	ReusableCamInfoMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusableCamInfoMsg.header.stamp = Stamp;

	ReusableCamInfoMsg.k = {
		Fx, 0.0, Cx,
		0.0, Fy, Cy,
		0.0, 0.0, 1.0
	};
	ReusableCamInfoMsg.r = {
		1.0, 0.0, 0.0,
		0.0, 1.0, 0.0,
		0.0, 0.0, 1.0
	};
	// For ROS stereo tools, the right camera projection matrix must contain
	// the stereo baseline in P[3]. Left camera keeps Tx = 0.
	const double Tx = bIsRightStereoCamera ? (-Fx * StereoBaselineMeters) : 0.0;

	ReusableCamInfoMsg.p = {
		Fx, 0.0, Cx, Tx,
		0.0, Fy, Cy, 0.0,
		0.0, 0.0, 1.0, 0.0
	};
	ROSNode->Publish<sensor_msgs::msg::CameraInfo>(*CameraInfoTopic, ReusableCamInfoMsg);
}

bool UCameraRosPublisherComponent::IsReady() const
{
	return bInitialized && ROSNode != nullptr;
}

void UCameraRosPublisherComponent::OnCaptureControl(const std_msgs::msg::Int32& Msg)
{
	OnCaptureControlReceived.Broadcast(Msg.data);
}

void UCameraRosPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bInitialized = false;
	Super::EndPlay(EndPlayReason);
}
