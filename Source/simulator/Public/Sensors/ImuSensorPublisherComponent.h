#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"

#include "sensor_msgs/msg/imu.hpp"
#include "builtin_interfaces/msg/time.hpp"

#include "ImuSensorPublisherComponent.generated.h"

class USceneComponent;

/*
 * Simulated rover IMU sensor.
 *
 * Attach this component to BP_VehicleRover and add a SceneComponent named IMU_Mount.
 * The component publishes sensor_msgs/msg/Imu on /rover/imu at its own configurable rate.
 * It also publishes the static transform base_link -> imu_link using the IMU_Mount transform.
 * Linear acceleration is sensor-frame specific force using active world gravity: a supported
 * stationary sensor reports gravity opposition, while an ideal freely falling sensor reports zero.
 *
 * This component is independent from the camera/capture FrameInfo pipeline.
 * It is synchronized with the rest of the simulator by using the same UE world time base.
 */
UCLASS(ClassGroup=(Sensors), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API UImuSensorPublisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UImuSensorPublisherComponent();

	void SetImuPublishHz(float NewPublishHz);
	float GetImuPublishHz() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	UPROPERTY()
	USceneComponent* ImuMountComponent = nullptr;

	UPROPERTY(EditAnywhere, Category = "ROS")
	FString NodeName = TEXT("imu_sensor_publisher");

	UPROPERTY(EditAnywhere, Category = "ROS")
	FString ImuTopic = TEXT("/rover/imu");

	UPROPERTY(EditAnywhere, Category = "TF")
	FString BaseFrameId = TEXT("base_link");

	UPROPERTY(EditAnywhere, Category = "TF")
	FString ImuFrameId = TEXT("imu_link");

	UPROPERTY(EditAnywhere, Category = "IMU")
	FName ImuMountComponentName = TEXT("IMU_Mount");

	UPROPERTY(EditAnywhere, Category = "IMU", meta = (ClampMin = "1.0", ClampMax = "400.0", UIMin = "1.0", UIMax = "200.0"))
	float ImuPublishHz = 100.0f;

	//Legacy serialized value retained for Blueprint/CDO compatibility. Active world gravity is authoritative.
	UPROPERTY()
	float GravityMagnitudeMps2 = 9.80665f;

	UPROPERTY(EditAnywhere, Category = "IMU|Noise")
	bool bEnableNoise = false;

	UPROPERTY(EditAnywhere, Category = "IMU|Noise", meta = (ClampMin = "0.0"))
	float GyroNoiseStdDevRadPerSec = 0.0f;

	UPROPERTY(EditAnywhere, Category = "IMU|Noise", meta = (ClampMin = "0.0"))
	float AccelNoiseStdDevMps2 = 0.0f;

	// Constant sensor bias in the IMU/body frame. Keep zero for a clean deterministic sensor.
	UPROPERTY(EditAnywhere, Category = "IMU|Bias")
	FVector GyroBiasRadPerSec = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "IMU|Bias")
	FVector AccelBiasMps2 = FVector::ZeroVector;

	// Reusable ROS message.
	sensor_msgs::msg::Imu ReusableImuMsg;

	bool bHasPreviousSample = false;
	double PreviousTimeSeconds = 0.0;
	FVector PreviousRosPositionMeters = FVector::ZeroVector;
	FQuat PreviousRosRotation = FQuat::Identity;
	FVector PreviousRosVelocityMps = FVector::ZeroVector;

	float PublishAccumulator = 0.0f;
	FRandomStream RandomStream;

private:
	void SetupRos();
	void SetupReusableMessage();
	void ResolveImuMountComponent();
	void PublishStaticImuTransform();
	void PublishImuSample(double CurrentTimeSeconds);
	void ResetPreviousState(double CurrentTimeSeconds);

	builtin_interfaces::msg::Time ToRosTime(double Seconds) const;
	float RandomGaussian(float Mean, float StdDev);
	FVector ApplyNoiseAndBias(const FVector& Value, const FVector& Bias, float NoiseStdDev);
};
