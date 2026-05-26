#pragma once

#include "CoreMinimal.h"
#include "CaptureTypes.generated.h"

UENUM(BlueprintType)
enum class ECaptureMode : uint8
{
	MonoRos               UMETA(DisplayName = "Mono ROS"),
	GroundTruth           UMETA(DisplayName = "Ground Truth"),
	StereoRos             UMETA(DisplayName = "Stereo ROS"),
	MonoRosGroundTruth    UMETA(DisplayName = "Mono ROS + Ground Truth"),
	StereoRosGroundTruth  UMETA(DisplayName = "Stereo ROS + Ground Truth")
};

USTRUCT(BlueprintType)
struct SIMULATOR_API FCaptureConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1", ClampMax = "24", UIMin = "1", UIMax = "24"))
	int32 PublishHz = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera System")
	ECaptureMode CaptureMode = ECaptureMode::MonoRosGroundTruth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ROS Ground Truth")
	bool bEnableRosRoverGtPose = true;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Acquisition")
	int32 ImageWidth = 1280;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Acquisition")
	int32 ImageHeight = 720;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Acquisition")
	double StereoBaselineMeters = 0.2;

	bool IsLeftRosCameraEnabled() const
	{
		return CaptureMode == ECaptureMode::MonoRos ||
			CaptureMode == ECaptureMode::StereoRos ||
			CaptureMode == ECaptureMode::MonoRosGroundTruth ||
			CaptureMode == ECaptureMode::StereoRosGroundTruth;
	}

	bool IsRightRosCameraEnabled() const
	{
		return CaptureMode == ECaptureMode::StereoRos ||
			CaptureMode == ECaptureMode::StereoRosGroundTruth;
	}

	bool IsGroundTruthEnabled() const
	{
		return CaptureMode == ECaptureMode::GroundTruth ||
			CaptureMode == ECaptureMode::MonoRosGroundTruth ||
			CaptureMode == ECaptureMode::StereoRosGroundTruth;
	}

	bool HasAnyCaptureOutput() const
	{
		return IsLeftRosCameraEnabled() || IsRightRosCameraEnabled() || IsGroundTruthEnabled() || bEnableRosRoverGtPose;
	}
};

USTRUCT(BlueprintType)
struct SIMULATOR_API FCaptureFrameInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 FrameIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	double StampSeconds = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 SessionId = 0;
};
