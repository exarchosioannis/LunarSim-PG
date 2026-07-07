#pragma once

#include "CoreMinimal.h"
#include "CaptureTypes.generated.h"

UENUM(BlueprintType)
enum class ELunarSimRunMode : uint8
{
	Dataset  UMETA(DisplayName = "Dataset"),
	Ros2Live UMETA(DisplayName = "ROS2 Live")
};

UENUM(BlueprintType)
enum class ELunarSimResolutionPreset : uint8
{
	R640x480   UMETA(DisplayName = "640x480"),
	R1280x720  UMETA(DisplayName = "1280x720"),
	R1024x1024 UMETA(DisplayName = "1024x1024"),
	Custom     UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class ELunarSimCaptureRatePreset : uint8
{
	Hz6    UMETA(DisplayName = "6 Hz"),
	Hz10   UMETA(DisplayName = "10 Hz"),
	Custom UMETA(DisplayName = "Custom")
};

USTRUCT(BlueprintType)
struct SIMULATOR_API FCaptureConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LunarSim-PG")
	ELunarSimRunMode RunMode = ELunarSimRunMode::Dataset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bStereoRosImages = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bGroundTruthImages = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bTrajectoryCsv = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	ELunarSimResolutionPreset ResolutionPreset = ELunarSimResolutionPreset::R1024x1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1", UIMin = "1"))
	int32 CustomWidth = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1", UIMin = "1"))
	int32 CustomHeight = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	ELunarSimCaptureRatePreset CaptureRatePreset = ELunarSimCaptureRatePreset::Hz6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1", ClampMax = "60", UIMin = "1", UIMax = "60"))
	int32 CustomCaptureHz = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo", meta = (ClampMin = "1.0", ClampMax = "200.0", UIMin = "1.0", UIMax = "200.0", Units = "cm"))
	float StereoBaselineCm = 20.0f;

	int32 GetResolvedWidth() const
	{
		switch (ResolutionPreset)
		{
			case ELunarSimResolutionPreset::R640x480:
				return 640;
			case ELunarSimResolutionPreset::R1280x720:
				return 1280;
			case ELunarSimResolutionPreset::R1024x1024:
				return 1024;
			case ELunarSimResolutionPreset::Custom:
			default:
				return CustomWidth;
		}
	}

	int32 GetResolvedHeight() const
	{
		switch (ResolutionPreset) 
		{
			case ELunarSimResolutionPreset::R640x480:
				return 480;
			case ELunarSimResolutionPreset::R1280x720:
				return 720;
			case ELunarSimResolutionPreset::R1024x1024:
				return 1024;
			case ELunarSimResolutionPreset::Custom:
			default:
				return CustomHeight;
		}
	}

	int32 GetResolvedCaptureHz() const
	{
		switch (CaptureRatePreset)
		{
			case ELunarSimCaptureRatePreset::Hz6:
				return 6;
			case ELunarSimCaptureRatePreset::Hz10:
				return 10;
			case ELunarSimCaptureRatePreset::Custom:
			default:
				return CustomCaptureHz;
		}
	}

	float GetStereoBaselineMeters() const
	{
		return StereoBaselineCm / 100.0f;
	}

	bool IsDatasetMode() const
	{
		return RunMode == ELunarSimRunMode::Dataset;
	}

	bool IsRos2LiveMode() const
	{
		return RunMode == ELunarSimRunMode::Ros2Live;
	}

	bool IsLeftRosCameraEnabled() const
	{
		return bStereoRosImages;
	}

	bool IsRightRosCameraEnabled() const
	{
		return bStereoRosImages;
	}

	bool IsStereoRosEnabled() const
	{
		return bStereoRosImages;
	}

	bool IsGroundTruthEnabled() const
	{
		return bGroundTruthImages;
	}

	bool IsTrajectoryCsvEnabled() const
	{
		return bTrajectoryCsv;
	}

	bool HasAnyCaptureOutput() const
	{
		return bStereoRosImages || bGroundTruthImages || bTrajectoryCsv;
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
