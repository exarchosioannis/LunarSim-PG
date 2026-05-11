#pragma once

#include <atomic>
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Capture/CaptureTypes.h"
#include "Capture/CapturePoseTypes.h"
#include "CaptureManager.generated.h"

class UCapturePoseSourceComponent;
class USceneComponent;

UCLASS()
class SIMULATOR_API UCaptureManager : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(const FCaptureConfig& InConfig);

	void StartCapture();
	void StopCapture();
	bool IsCaptureEnabled() const;

	FCaptureFrameInfo NextFrame(double StampSeconds);
	const FCaptureConfig& GetConfig() const;
	bool IsSessionValid(int32 SessionId) const;

	//unrealgt uses this.
	FString GetCurrentImagesDirectory() const;

	void SetRoverPoseSource(UCapturePoseSourceComponent* InRoverPoseSource); //Could be usefull in the future ?? 
	void SetLeftCameraPoseSource(USceneComponent* InLeftCameraPoseSource);
	void SetRightCameraPoseSource(USceneComponent* InRightCameraPoseSource);

private:
	// Manifest is a log of all frames with timestamps
	void StartManifest();
	void AppendManifestRow(const FCaptureFrameInfo& FrameInfo, const FCaptureFramePoseData& PoseData);
	void AppendTrajectoryRow(const FString& FilePath, const FCaptureFrameInfo& FrameInfo, const FCapturePose& Pose, const FString& FrameId, const FString& ChildFrameId);

	static FVector UnrealLocationToRosMeters(const FVector& UnrealLocation);
	static FQuat UnrealRotationToRosQuat(const FRotator& UnrealRotation);
	
	UCapturePoseSourceComponent* FindPoseSourceByName(FName SourceName) const;

	bool bCaptureEnabled = false;

	int32 FrameIndex = 0;

	std::atomic<int32> CurrentSessionId{0};

	FCaptureConfig Config;

	FString ManifestFilePath;
	FString RoverGtTrajectoryFilePath;
	FString LeftCameraGtTrajectoryFilePath;
	FString RightCameraGtTrajectoryFilePath;
	FString CurrentSessionName;
	FString CurrentSessionDirectory;
	FString CurrentImagesDirectory;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Pose", meta = (AllowPrivateAccess = "true"))
	UCapturePoseSourceComponent* RoverPoseSource = nullptr;

	UPROPERTY()
	USceneComponent* LeftCameraPoseSource = nullptr;

	UPROPERTY()
	USceneComponent* RightCameraPoseSource = nullptr;
};
