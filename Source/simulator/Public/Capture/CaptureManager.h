#pragma once

#include <atomic>
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Capture/CaptureTypes.h"
#include "Capture/CapturePoseTypes.h"
#include "CaptureManager.generated.h"

class UCapturePoseSourceComponent;
class USceneComponent;
class UWorld;

UCLASS()
class SIMULATOR_API UCaptureManager : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Capture")
	void Initialize(const FCaptureConfig& InConfig);

	virtual UWorld* GetWorld() const override;

	UFUNCTION(BlueprintCallable, Category = "Capture")
	void StartCapture();

	UFUNCTION(BlueprintCallable, Category = "Capture")
	void StopCapture();

	UFUNCTION(BlueprintPure, Category = "Capture")
	bool IsCaptureEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Capture")
	FCaptureFrameInfo NextFrame(double StampSeconds);

	UFUNCTION(BlueprintCallable, Category = "Capture")
	FCaptureFrameInfo NextFrameWithPose(double StampSeconds, const FCaptureFramePoseData& PoseData);

	UFUNCTION(BlueprintPure, Category = "Capture")
	const FCaptureConfig& GetConfig() const;

	UFUNCTION(BlueprintPure, Category = "Capture")
	bool IsSessionValid(int32 SessionId) const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetCurrentSessionName() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetCurrentSessionDirectory() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetCurrentImagesDirectory() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetCurrentMapsDirectory() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetCurrentDatasetRunDirectory() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetManifestFilePath() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetRoverGtTrajectoryFilePath() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetLeftCameraGtTrajectoryFilePath() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Session")
	FString GetRightCameraGtTrajectoryFilePath() const;

	UFUNCTION(BlueprintCallable, Category = "Capture|Pose")
	void SetRoverPoseSource(UCapturePoseSourceComponent* InRoverPoseSource);

	UFUNCTION(BlueprintCallable, Category = "Capture|Pose")
	void SetLeftCameraPoseSource(USceneComponent* InLeftCameraPoseSource);

	UFUNCTION(BlueprintCallable, Category = "Capture|Pose")
	void SetRightCameraPoseSource(USceneComponent* InRightCameraPoseSource);

private:
	void StartManifest();
	void WriteSessionMetadata();
	void EnsureDatasetRunDirectory();
	void AppendManifestRow(const FCaptureFrameInfo& FrameInfo);
	void AppendTrajectoryRow(const FString& FilePath, const FCaptureFrameInfo& FrameInfo, const FCapturePose& Pose, const FString& FrameId, const FString& ChildFrameId);
	
	UCapturePoseSourceComponent* FindPoseSourceByName(FName SourceName) const;

	bool bCaptureEnabled = false;

	int32 FrameIndex = 0;

	std::atomic<int32> CurrentSessionId{0};

	FCaptureConfig Config;

	FString ManifestFilePath;
	FString RoverGtTrajectoryFilePath;
	FString LeftCameraGtTrajectoryFilePath;
	FString RightCameraGtTrajectoryFilePath;
	FString CurrentDatasetRunDirectory;
	FString CurrentSessionName;
	FString CurrentSessionDirectory;
	FString CurrentImagesDirectory;
	FString CurrentMapsDirectory;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Pose", meta = (AllowPrivateAccess = "true"))
	UCapturePoseSourceComponent* RoverPoseSource = nullptr;

	UPROPERTY()
	USceneComponent* LeftCameraPoseSource = nullptr;

	UPROPERTY()
	USceneComponent* RightCameraPoseSource = nullptr;
};
