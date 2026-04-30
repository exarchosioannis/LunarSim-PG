#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GTCamera.generated.h"

UCLASS()
class SIMULATOR_API AGTCamera : public AActor
{
    GENERATED_BODY()

public:
    AGTCamera();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;

    // C++ will call this, Blueprint will implement what happens
    UFUNCTION(BlueprintImplementableEvent, Category = "GroundTruth")
    void CaptureGroundTruthNow(int32 FrameIndex, double StampSeconds, int32 SessionId, UObject* CaptureManager);

    
    // Called to trigger GT generators with frame synchronization
    UFUNCTION(BlueprintCallable, Category = "GroundTruth")
    void TriggerGTGeneratorsWithFrame(int32 FrameIndex, double StampSeconds, int32 SessionId, UObject* CaptureManager);
};