#include "Sensors/ImuSensorPublisherComponent.h"
#include "Utils/UnrealToRosConversion.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TempoROSTypes.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::Imu);

UImuSensorPublisherComponent::UImuSensorPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UImuSensorPublisherComponent::SetImuPublishHz(float NewPublishHz)
{
	ImuPublishHz = FMath::Clamp(NewPublishHz, 1.0f, 400.0f);
	PublishAccumulator = 0.0f;
}

float UImuSensorPublisherComponent::GetImuPublishHz() const
{
	return FMath::Clamp(ImuPublishHz, 1.0f, 400.0f);
}

void UImuSensorPublisherComponent::BeginPlay()
{
	Super::BeginPlay();

	RandomStream.Initialize(FMath::Rand());

	ResolveImuMountComponent();
	SetupReusableMessage();
	SetupRos();
	PublishStaticImuTransform();

	if (GetWorld()) {
		ResetPreviousState(GetWorld()->GetTimeSeconds());
	}
}

void UImuSensorPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UImuSensorPublisherComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ROSNode) {
		ROSNode->Tick(DeltaTime);
	}

	if (!GetWorld() || !ImuMountComponent || !ROSNode) {
		return;
	}

	const float SafeHz = FMath::Max(1.0f, ImuPublishHz);
	const float Period = 1.0f / SafeHz;

	PublishAccumulator += DeltaTime;
	if (PublishAccumulator < Period) {
		return;
	}

	// The IMU is intentionally game-thread driven for now.
	// If the game FPS is lower than ImuPublishHz, the effective rate is limited by TickComponent.
	PublishAccumulator = FMath::Fmod(PublishAccumulator, Period);
	PublishImuSample(GetWorld()->GetTimeSeconds());
}

void UImuSensorPublisherComponent::SetupRos()
{
	if (ROSNode) return;

	ROSNode = UTempoROSNode::Create(*NodeName, this, false);
	if (!ROSNode) {
		UE_LOG(LogTemp, Warning, TEXT("ImuSensorPublisherComponent: failed to create ROS node."));
		return;
	}

	FROSQOSProfile ImuQOS;
	ImuQOS.CustomQueueSize(50).Reliable().Volatile();

	ROSNode->AddPublisher<sensor_msgs::msg::Imu>(*ImuTopic, ImuQOS, false);

	UE_LOG(LogTemp, Log, TEXT("ImuSensorPublisherComponent: publishing %s with frame_id=%s at %.2f Hz."),
		*ImuTopic,
		*ImuFrameId,
		ImuPublishHz);
}

void UImuSensorPublisherComponent::SetupReusableMessage()
{
	ReusableImuMsg.header.frame_id = TCHAR_TO_UTF8(*ImuFrameId);

	// Simple default covariances. These are intentionally conservative and can be tuned later.
	ReusableImuMsg.orientation_covariance = {
		0.0001, 0.0,    0.0,
		0.0,    0.0001, 0.0,
		0.0,    0.0,    0.0001
	};

	ReusableImuMsg.angular_velocity_covariance = {
		0.0001, 0.0,    0.0,
		0.0,    0.0001, 0.0,
		0.0,    0.0,    0.0001
	};

	ReusableImuMsg.linear_acceleration_covariance = {
		0.001, 0.0,   0.0,
		0.0,   0.001, 0.0,
		0.0,   0.0,   0.001
	};
}

void UImuSensorPublisherComponent::ResolveImuMountComponent()
{
	ImuMountComponent = nullptr;

	AActor* Owner = GetOwner();
	if (!Owner) {
		return;
	}

	TArray<USceneComponent*> SceneComponents;
	Owner->GetComponents<USceneComponent>(SceneComponents);

	for (USceneComponent* SceneComponent : SceneComponents) {
		if (SceneComponent && SceneComponent->GetFName() == ImuMountComponentName) {
			ImuMountComponent = SceneComponent;
			break;
		}
	}

	if (!ImuMountComponent) {
		UE_LOG(LogTemp, Warning,
			TEXT("ImuSensorPublisherComponent: could not find IMU mount component '%s' on %s. Falling back to actor root transform."),
			*ImuMountComponentName.ToString(),
			*Owner->GetName());

		ImuMountComponent = Owner->GetRootComponent();
	}
}

void UImuSensorPublisherComponent::PublishStaticImuTransform()
{
	if (!ROSNode || !ImuMountComponent || !GetOwner()) {
		return;
	}

	const FTransform BaseWorldTransform = GetOwner()->GetActorTransform();
	const FTransform ImuRelativeTransform = ImuMountComponent->GetComponentTransform().GetRelativeTransform(BaseWorldTransform);

	const bool bOk = ROSNode->PublishStaticTransform(ImuRelativeTransform, ImuFrameId, BaseFrameId);
	if (bOk) {
		UE_LOG(LogTemp, Log, TEXT("ImuSensorPublisherComponent: published static TF %s -> %s."),
			*BaseFrameId,
			*ImuFrameId);
	} else {
		UE_LOG(LogTemp, Warning, TEXT("ImuSensorPublisherComponent: failed to publish static TF %s -> %s."),
			*BaseFrameId,
			*ImuFrameId);
	}
}

void UImuSensorPublisherComponent::ResetPreviousState(double CurrentTimeSeconds)
{
	if (!ImuMountComponent) {
		bHasPreviousSample = false;
		return;
	}

	const FTransform ImuWorldTransform = ImuMountComponent->GetComponentTransform();

	PreviousTimeSeconds = CurrentTimeSeconds;
	PreviousRosPositionMeters = UnrealToRosConversion::PositionCmToRosMeters(ImuWorldTransform.GetLocation());
	PreviousRosRotation = UnrealToRosConversion::RotationToRosQuat(ImuWorldTransform.GetRotation());
	PreviousRosVelocityMps = FVector::ZeroVector;
	bHasPreviousSample = true;
}

void UImuSensorPublisherComponent::PublishImuSample(double CurrentTimeSeconds)
{
	if (!ImuMountComponent) {
		return;
	}

	const FTransform ImuWorldTransform = ImuMountComponent->GetComponentTransform();
	const FVector CurrentRosPositionMeters = UnrealToRosConversion::PositionCmToRosMeters(ImuWorldTransform.GetLocation());
	const FQuat CurrentRosRotation = UnrealToRosConversion::RotationToRosQuat(ImuWorldTransform.GetRotation());

	if (!bHasPreviousSample) {
		ResetPreviousState(CurrentTimeSeconds);
		return;
	}

	const double Dt = CurrentTimeSeconds - PreviousTimeSeconds;
	if (Dt <= KINDA_SMALL_NUMBER) {
		return;
	}

	const FVector CurrentRosVelocityMps = (CurrentRosPositionMeters - PreviousRosPositionMeters) / Dt;
	const FVector WorldLinearAccelerationMps2 = (CurrentRosVelocityMps - PreviousRosVelocityMps) / Dt;

	//Convert active Unreal world gravity to the ROS world basis and m/s^2.
	const FVector UnrealGravityWorldCmPerSec2(0.0, 0.0, GetWorld()->GetGravityZ());
	const FVector GravityWorldMps2 = UnrealToRosConversion::PositionCmToRosMeters(UnrealGravityWorldCmPerSec2);
	const FVector SpecificForceWorldMps2 = WorldLinearAccelerationMps2 - GravityWorldMps2;
	FVector LinearAccelerationBodyMps2 = CurrentRosRotation.Inverse().RotateVector(SpecificForceWorldMps2);

	FQuat DeltaRotation = CurrentRosRotation * PreviousRosRotation.Inverse();
	DeltaRotation.Normalize();

	FVector DeltaAxis = FVector::ForwardVector;
	float DeltaAngle = 0.0f;
	DeltaRotation.ToAxisAndAngle(DeltaAxis, DeltaAngle);

	if (DeltaAngle > PI) {
		DeltaAngle -= 2.0f * PI;
	}

	const FVector AngularVelocityWorldRadPerSec = DeltaAxis * (DeltaAngle / static_cast<float>(Dt));
	FVector AngularVelocityBodyRadPerSec = CurrentRosRotation.Inverse().RotateVector(AngularVelocityWorldRadPerSec);

	AngularVelocityBodyRadPerSec = ApplyNoiseAndBias(AngularVelocityBodyRadPerSec, GyroBiasRadPerSec, GyroNoiseStdDevRadPerSec);
	LinearAccelerationBodyMps2 = ApplyNoiseAndBias(LinearAccelerationBodyMps2, AccelBiasMps2, AccelNoiseStdDevMps2);

	const builtin_interfaces::msg::Time Stamp = ToRosTime(CurrentTimeSeconds);

	ReusableImuMsg.header.stamp = Stamp;
	ReusableImuMsg.header.frame_id = TCHAR_TO_UTF8(*ImuFrameId);

	ReusableImuMsg.orientation.x = CurrentRosRotation.X;
	ReusableImuMsg.orientation.y = CurrentRosRotation.Y;
	ReusableImuMsg.orientation.z = CurrentRosRotation.Z;
	ReusableImuMsg.orientation.w = CurrentRosRotation.W;

	ReusableImuMsg.angular_velocity.x = AngularVelocityBodyRadPerSec.X;
	ReusableImuMsg.angular_velocity.y = AngularVelocityBodyRadPerSec.Y;
	ReusableImuMsg.angular_velocity.z = AngularVelocityBodyRadPerSec.Z;

	ReusableImuMsg.linear_acceleration.x = LinearAccelerationBodyMps2.X;
	ReusableImuMsg.linear_acceleration.y = LinearAccelerationBodyMps2.Y;
	ReusableImuMsg.linear_acceleration.z = LinearAccelerationBodyMps2.Z;

	ROSNode->Publish<sensor_msgs::msg::Imu>(*ImuTopic, ReusableImuMsg);

	PreviousTimeSeconds = CurrentTimeSeconds;
	PreviousRosPositionMeters = CurrentRosPositionMeters;
	PreviousRosRotation = CurrentRosRotation;
	PreviousRosVelocityMps = CurrentRosVelocityMps;
}

builtin_interfaces::msg::Time UImuSensorPublisherComponent::ToRosTime(double Seconds) const
{
	builtin_interfaces::msg::Time T;
	if (Seconds < 0.0) {
		Seconds = 0.0;
	}

	const int64 Sec = static_cast<int64>(Seconds);
	const double Frac = Seconds - static_cast<double>(Sec);

	T.sec = static_cast<int32>(Sec);
	T.nanosec = static_cast<uint32>(FMath::Clamp<int64>(
		static_cast<int64>(Frac * 1000000000.0),
		0,
		999999999));

	return T;
}

float UImuSensorPublisherComponent::RandomGaussian(float Mean, float StdDev)
{
	if (StdDev <= 0.0f) {
		return Mean;
	}

	const float U1 = FMath::Max(RandomStream.FRand(), KINDA_SMALL_NUMBER);
	const float U2 = RandomStream.FRand();
	const float Radius = FMath::Sqrt(-2.0f * FMath::Loge(U1));
	const float Theta = 2.0f * PI * U2;

	return Mean + StdDev * Radius * FMath::Cos(Theta);
}

FVector UImuSensorPublisherComponent::ApplyNoiseAndBias(const FVector& Value, const FVector& Bias, float NoiseStdDev)
{
	FVector Result = Value + Bias;

	if (bEnableNoise && NoiseStdDev > 0.0f) {
		Result.X += RandomGaussian(0.0f, NoiseStdDev);
		Result.Y += RandomGaussian(0.0f, NoiseStdDev);
		Result.Z += RandomGaussian(0.0f, NoiseStdDev);
	}

	return Result;
}
