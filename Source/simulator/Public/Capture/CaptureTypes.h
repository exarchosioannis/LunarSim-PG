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
	R640x360   = 10 UMETA(DisplayName = "640x360"),
	R1024x576  = 11 UMETA(DisplayName = "1024x576"),
	R1280x720  = 12 UMETA(DisplayName = "1280x720"),
	R1920x1080 = 13 UMETA(DisplayName = "1920x1080"),
	R640x640   = 14 UMETA(DisplayName = "640x640"),
	R1024x1024 = 0 UMETA(DisplayName = "1024x1024")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs|Ground Truth")
	bool bGroundTruthRgb = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs|Ground Truth")
	bool bGroundTruthDepth = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs|Ground Truth")
	bool bGroundTruthSegmentation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs|Ground Truth")
	bool bGroundTruthBoundingBoxes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bTrajectoryCsv = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	ELunarSimResolutionPreset ResolutionPreset = ELunarSimResolutionPreset::R1024x1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	ELunarSimCaptureRatePreset CaptureRatePreset = ELunarSimCaptureRatePreset::Hz6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.001", ClampMax = "60", UIMin = "1", UIMax = "60", Units = "Hz"))
	float CustomCaptureHz = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo", meta = (ClampMin = "1.0", ClampMax = "200.0", UIMin = "1.0", UIMax = "200.0", Units = "cm"))
	float StereoBaselineCm = 20.0f;

	int32 GetResolvedWidth() const
	{
		switch (ResolutionPreset)
		{
			case ELunarSimResolutionPreset::R640x360:
				return 640;
			case ELunarSimResolutionPreset::R1024x576:
				return 1024;
			case ELunarSimResolutionPreset::R1280x720:
				return 1280;
			case ELunarSimResolutionPreset::R1920x1080:
				return 1920;
			case ELunarSimResolutionPreset::R640x640:
				return 640;
			case ELunarSimResolutionPreset::R1024x1024:
				return 1024;
			default:
				return 1024;
		}
	}

	int32 GetResolvedHeight() const
	{
		switch (ResolutionPreset) 
		{
			case ELunarSimResolutionPreset::R640x360:
				return 360;
			case ELunarSimResolutionPreset::R1024x576:
				return 576;
			case ELunarSimResolutionPreset::R1280x720:
				return 720;
			case ELunarSimResolutionPreset::R1920x1080:
				return 1080;
			case ELunarSimResolutionPreset::R640x640:
				return 640;
			case ELunarSimResolutionPreset::R1024x1024:
				return 1024;
			default:
				return 1024;
		}
	}

	static float GetDefaultCameraCaptureHz()
	{
		return 6.0f;
	}

	static float GetMinCameraCaptureHz()
	{
		return 0.001f;
	}

	static float GetMaxCameraCaptureHz()
	{
		return 60.0f;
	}

	static float GetDefaultStereoBaselineCm()
	{
		return 20.0f;
	}

	static float GetMinStereoBaselineCm()
	{
		return 1.0f;
	}

	static float GetMaxStereoBaselineCm()
	{
		return 200.0f;
	}

	static bool IsValidCameraCaptureHz(float InHz)
	{
		return FMath::IsFinite(InHz) && InHz > 0.0f;
	}

	static float SanitizeCameraCaptureHz(float InHz)
	{
		return IsValidCameraCaptureHz(InHz) ? InHz : GetDefaultCameraCaptureHz();
	}

	static float SanitizeStereoBaselineCm(float InBaselineCm)
	{
		if (!FMath::IsFinite(InBaselineCm)) {
			return GetDefaultStereoBaselineCm();
		}

		return FMath::Clamp(InBaselineCm, GetMinStereoBaselineCm(), GetMaxStereoBaselineCm());
	}

	bool Sanitize()
	{
		const float SafeCustomCaptureHz = SanitizeCameraCaptureHz(CustomCaptureHz);
		const float SafeStereoBaselineCm = SanitizeStereoBaselineCm(StereoBaselineCm);
		const bool bChanged = CustomCaptureHz != SafeCustomCaptureHz || StereoBaselineCm != SafeStereoBaselineCm;
		CustomCaptureHz = SafeCustomCaptureHz;
		StereoBaselineCm = SafeStereoBaselineCm;
		return bChanged;
	}

	float GetResolvedCaptureHz() const
	{
		switch (CaptureRatePreset)
		{
			case ELunarSimCaptureRatePreset::Hz6:
				return 6.0f;
			case ELunarSimCaptureRatePreset::Hz10:
				return 10.0f;
			case ELunarSimCaptureRatePreset::Custom:
			default:
				return SanitizeCameraCaptureHz(CustomCaptureHz);
		}
	}

	float GetStereoBaselineMeters() const
	{
		return SanitizeStereoBaselineCm(StereoBaselineCm) / 100.0f;
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
		return bGroundTruthImages && HasAnyGroundTruthOutputType();
	}

	bool IsGroundTruthMasterEnabled() const
	{
		return bGroundTruthImages;
	}

	bool HasAnyGroundTruthOutputType() const
	{
		return bGroundTruthRgb || bGroundTruthDepth || bGroundTruthSegmentation || bGroundTruthBoundingBoxes;
	}

	bool IsGroundTruthRgbEnabled() const
	{
		return bGroundTruthImages && bGroundTruthRgb;
	}

	bool IsGroundTruthDepthEnabled() const
	{
		return bGroundTruthImages && bGroundTruthDepth;
	}

	bool IsGroundTruthSegmentationEnabled() const
	{
		return bGroundTruthImages && bGroundTruthSegmentation;
	}

	bool IsGroundTruthBoundingBoxesEnabled() const
	{
		return bGroundTruthImages && bGroundTruthBoundingBoxes;
	}

	bool IsTrajectoryCsvEnabled() const
	{
		return bTrajectoryCsv;
	}

	bool HasAnyCaptureOutput() const
	{
		return bStereoRosImages || IsGroundTruthEnabled() || bTrajectoryCsv;
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
