#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Capture/CaptureTypes.h"
#include "CaptureManager.generated.h"

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

	UFUNCTION(BlueprintPure, Category = "Capture")
	const FCaptureConfig& GetConfig() const;

	UFUNCTION(BlueprintPure, Category = "Capture")
	bool IsSessionValid(int32 SessionId) const;

private:
	void StartManifest();
	void AppendManifestRow(const FCaptureFrameInfo& FrameInfo);

	bool bCaptureEnabled = false;

	int32 FrameIndex = 0;

	std::atomic<int32> CurrentSessionId{0};

	FCaptureConfig Config;

	FString ManifestFilePath;
};