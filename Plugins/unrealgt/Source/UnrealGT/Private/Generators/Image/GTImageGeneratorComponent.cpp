// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Image/GTImageGeneratorComponent.h"

#include "Generators/Image/GTSceneCaptureComponent2D.h"

UGTImageGeneratorComponent::UGTImageGeneratorComponent()
    : Super()
    , bUseDisplayGamma(false)
    , TargetGamma(2.2)
    , bUseRandomGamma(false)
    , TargetGammaMax(2.4)
{
    OrthoWidth = 512;
    ClipPlaneNormal = FVector(0, 0, 1);
}

void UGTImageGeneratorComponent::GenerateData(const FDateTime& TimeStamp, int32 SessionId, FSessionValidationFunc SessionValidator)
{
    if (bUseRandomGamma)
    {
        SceneCaptureComponent->TargetGamma = FMath::FRandRange(TargetGamma, TargetGammaMax);
    }

    Super::GenerateData(TimeStamp, SessionId, SessionValidator);
}

void UGTImageGeneratorComponent::GenerateData(
    const FDateTime& TimeStamp,
    int32 SessionId,
    FSessionValidationFunc SessionValidator,
    int32 FrameIndex,
    double StampSeconds)
{
    if (bUseRandomGamma)
    {
        SceneCaptureComponent->TargetGamma = FMath::FRandRange(TargetGamma, TargetGammaMax);
    }

    Super::GenerateData(TimeStamp, SessionId, MoveTemp(SessionValidator), FrameIndex, StampSeconds);
}

void UGTImageGeneratorComponent::BeginPlay()
{
    bAntiAliasing = false;
    bMotionBlur = false;

    Super::BeginPlay();

    bUseDisplayGamma = false;
    TargetGamma = 2.2f;
    bUseRandomGamma = false;

    PostProcessSettings = FPostProcessSettings();
    PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    PostProcessSettings.bOverride_AutoExposureBias = true;
    PostProcessSettings.AutoExposureMinBrightness = 1.0f;
    PostProcessSettings.AutoExposureMaxBrightness = 1.0f;
    PostProcessSettings.AutoExposureBias = 1.5f;

    SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    SceneCaptureComponent->bCaptureEveryFrame = false;
    SceneCaptureComponent->bCaptureOnMovement = false;
    SceneCaptureComponent->bAlwaysPersistRenderingState = false;
    SceneCaptureComponent->ShowFlags.SetTemporalAA(false);
    SceneCaptureComponent->ShowFlags.SetMotionBlur(false);
    SceneCaptureComponent->ShowFlags.SetAntiAliasing(false);
    SceneCaptureComponent->PostProcessBlendWeight = 1.0f;
    SceneCaptureComponent->SRGB = true;

    SceneCaptureComponent->bUseDisplayGamma = bUseDisplayGamma;
    SceneCaptureComponent->TargetGamma = TargetGamma;
    SceneCaptureComponent->ProjectionType = ProjectionType;
    SceneCaptureComponent->OrthoWidth = OrthoWidth;
    SceneCaptureComponent->PostProcessSettings = PostProcessSettings;
    SceneCaptureComponent->bEnableClipPlane = bEnableClipPlane;
    SceneCaptureComponent->ClipPlaneBase = ClipPlaneBase;
    SceneCaptureComponent->ClipPlaneNormal = ClipPlaneNormal;
    SceneCaptureComponent->UpdateTextureTarget();
}
