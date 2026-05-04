#include "Sensors/CameraRosPublisherComponent.h"
#include "TempoROSTypes.h"
#include "Camera/CameraComponent.h"

//TempoROS message traits
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::Image);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::CameraInfo);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(std_msgs::msg::Int32);

UCameraRosPublisherComponent::UCameraRosPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCameraRosPublisherComponent::Initialize(
	int32 InWidth,
	int32 InHeight,
	const FString& InFrameId,
	const FString& InTopicBase,
	UCameraComponent* InCamera)
{
	Width = InWidth;
	Height = InHeight;
	FrameId = InFrameId;
	TopicBase = InTopicBase;
	CameraName = InFrameId;
	Camera = InCamera;

	SetupReusableMessages();
	SetupRos();

	bInitialized = true;
}

void UCameraRosPublisherComponent::SetupReusableMessages()
{
	ReusableImgMsg.height = Height;
	ReusableImgMsg.width = Width;
	ReusableImgMsg.encoding = "bgra8";
	ReusableImgMsg.is_bigendian = false;
	ReusableImgMsg.step = Width * 4;
	ReusableImgMsg.data.resize((size_t)Width * (size_t)Height * 4);

	ReusableCamInfoMsg.height = Height;
	ReusableCamInfoMsg.width = Width;
	ReusableCamInfoMsg.distortion_model = "plumb_bob";
	ReusableCamInfoMsg.d = {0.0, 0.0, 0.0, 0.0, 0.0};
}

void UCameraRosPublisherComponent::SetupRos()
{
       ROSNode = UTempoROSNode::Create(*CameraName, this);

       FROSQOSProfile ImageQOS;
       ImageQOS.CustomQueueSize(20).Reliable();

       FROSQOSProfile DefaultQOS;
       DefaultQOS.CustomQueueSize(10).Reliable();

       ROSNode->AddPublisher<sensor_msgs::msg::Image>(
	       *(TopicBase + TEXT("/rgb/image_raw/compressed")),
	       ImageQOS,
	       false
       );

       ROSNode->AddPublisher<sensor_msgs::msg::CameraInfo>(
	       *(TopicBase + TEXT("/camera_info")),
	       DefaultQOS,
	       false
       );

       ROSNode->AddPublisher<std_msgs::msg::Int32>(
	       *(TopicBase + TEXT("/frame_index")),
	       DefaultQOS,
	       false
       );

       ROSNode->AddSubscription<std_msgs::msg::Int32>(
	       *(TopicBase + TEXT("/control")),
	       TROSSubscriptionDelegate<std_msgs::msg::Int32>::CreateUObject(this, &UCameraRosPublisherComponent::OnCaptureControl)
       );
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

	// Convert pixel data to ROS message format
	const int32 W = Width;
	const int32 H = Height; 
	const int32 BytesPerPixel = 4;
	
	if (PixelData.Num() != W * H * BytesPerPixel) return;
	
	// Copy pixel data to reusable message
	FMemory::Memcpy(ReusableImgMsg.data.data(), PixelData.GetData(), PixelData.Num());
	
	// Set message headers using FrameInfo
	const builtin_interfaces::msg::Time Stamp = ToRosTime(FrameInfo.StampSeconds);
	ReusableImgMsg.header.frame_id = TCHAR_TO_UTF8(*FrameId);
	ReusableImgMsg.header.stamp = Stamp;
	ReusableFrameIndexMsg.data = FrameInfo.FrameIndex;

	// Publish messages
	ROSNode->Publish<sensor_msgs::msg::Image>(*(TopicBase + TEXT("/rgb/image_raw/compressed")), ReusableImgMsg);
	ROSNode->Publish<std_msgs::msg::Int32>(*(TopicBase + TEXT("/frame_index")), ReusableFrameIndexMsg);
	PublishCameraInfo(Stamp);
}

void UCameraRosPublisherComponent::PublishCameraInfo(const builtin_interfaces::msg::Time& Stamp)
{
	if (!ROSNode || !Camera) return;

	const double Fx = (double)Width / (2.0 * FMath::Tan(FMath::DegreesToRadians((double)Camera->FieldOfView) * 0.5));
	const double Fy = Fx;
	const double Cx = (double)Width * 0.5;
	const double Cy = (double)Height * 0.5;

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
	ReusableCamInfoMsg.p = {
		Fx, 0.0, Cx, 0.0,
		0.0, Fy, Cy, 0.0,
		0.0, 0.0, 1.0, 0.0
	};
	ROSNode->Publish<sensor_msgs::msg::CameraInfo>(TEXT("sim_camera/camera_info"), ReusableCamInfoMsg);
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