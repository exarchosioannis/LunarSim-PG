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
	UFUNCTION(BlueprintCallable, Category = "Capture")
	void Initialize(const FCaptureConfig& InConfig);

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

	UFUNCTION(BlueprintCallable, Category = "Capture|Pose")
	void SetRoverPoseSource(UCapturePoseSourceComponent* InRoverPoseSource);

	UFUNCTION(BlueprintCallable, Category = "Capture|Pose")
	void SetLeftCameraPoseSource(USceneComponent* InLeftCameraPoseSource);

private:
	void StartManifest();
	void AppendManifestRow(const FCaptureFrameInfo& FrameInfo, const FCaptureFramePoseData& PoseData);
	
	UCapturePoseSourceComponent* FindPoseSourceByName(FName SourceName) const;

	bool bCaptureEnabled = false;

	int32 FrameIndex = 0;

	std::atomic<int32> CurrentSessionId{0};

	FCaptureConfig Config;

	FString ManifestFilePath;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Pose", meta = (AllowPrivateAccess = "true"))
	UCapturePoseSourceComponent* RoverPoseSource = nullptr;

	UPROPERTY()
	USceneComponent* LeftCameraPoseSource = nullptr;
};