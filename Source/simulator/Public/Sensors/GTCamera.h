#pragma once

#include "CoreMinimal.h"
#include "Capture/CaptureTypes.h"
#include "GameFramework/Actor.h"
#include "GTCamera.generated.h"

class USceneCaptureComponent2D;

enum class EGTInternalWarmUpState : uint8
{
    NotRequested,
    InProgress,
    Ready,
    Failed,
    Cancelled
};

UCLASS()
class SIMULATOR_API AGTCamera : public AActor
{
    GENERATED_BODY()

public:
    AGTCamera();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void Tick(float DeltaTime) override;

    // Blueprint owns the generator graph; C++ supplies canonical frame metadata.
    UFUNCTION(BlueprintImplementableEvent, Category = "GroundTruth")
    void CaptureGroundTruthNow(int32 FrameIndex, double StampSeconds, int32 SessionId, UObject* CaptureManager);

    UFUNCTION(BlueprintCallable, Category = "GroundTruth")
    void SetGroundTruthResolution(int32 Width, int32 Height);

    UFUNCTION(BlueprintCallable, Category = "GroundTruth")
    void SetGroundTruthCalibration(const FResolvedCameraCalibration& CameraCalibration);

    UFUNCTION(BlueprintCallable, Category = "GroundTruth")
    void SetGroundTruthOutputs(bool bRGB, bool bDepth, bool bSegmentation, bool bBoundingBoxes);

    bool RunInternalWarmUp();
    bool IsInternalWarmUpReady() const;
    bool HasInternalWarmUpFailed() const;
    const FString& GetInternalWarmUpFailure() const;
    const USceneCaptureComponent2D* GetGroundTruthRgbSceneCapture() const;

    
    // Called to trigger GT generators with frame synchronization
    UFUNCTION(BlueprintCallable, Category = "GroundTruth")
    void TriggerGTGeneratorsWithFrame(int32 FrameIndex, double StampSeconds, int32 SessionId, UObject* CaptureManager);

private:
    void SetInternalWarmUpFailure(const FString& Failure);

    EGTInternalWarmUpState InternalWarmUpState = EGTInternalWarmUpState::NotRequested;
    FString InternalWarmUpFailure;
    bool bGroundTruthRGBEnabled = false;
    bool bGroundTruthDepthEnabled = false;
    bool bGroundTruthSegmentationEnabled = false;
    bool bGroundTruthBoundingBoxesEnabled = false;
};
