// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Image/GTImageGeneratorBase.h"

#include <CanvasItem.h>
#include <CanvasTypes.h>
#include <Components/StaticMeshComponent.h>
#include <Engine/CollisionProfile.h>
#include <Engine/StaticMesh.h>
#include <Engine/TextureRenderTarget2D.h>

#include "Generators/Image/GTSceneCaptureComponent2D.h"
#include "Streamers/GTAsyncMakeImageTask.h"

UGTImageGeneratorBase::UGTImageGeneratorBase()
    : Super()
    , ImageFormat(EGTImageFileFormat::BMP)
    , Resolution(512, 512)
    , FOVAngle(90.f)
    , bAntiAliasing(true)
    , bMotionBlur(false)
    , bUseAsyncGPUReadback(true)
    , bWriteAlpha(false)
    , NextFrameId(0)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    SceneCaptureComponent =
        CreateDefaultSubobject<UGTSceneCaptureComponent2D>(TEXT("InternalSceneCapture"));
    SceneCaptureComponent->SetupAttachment(this);

    CameraStaticMeshComponent =
        CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InternalCameraStaticMesh"));
    CameraStaticMeshComponent->SetupAttachment(this);
    CameraStaticMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    CameraStaticMeshComponent->bHiddenInGame = true;
    CameraStaticMeshComponent->CastShadow = false;
    // TODO is this needed ?
    // CameraStaticMeshComponent->PostPhysicsComponentTick.bCanEverTick = false;

    if (!IsRunningCommandlet())
    {
        if (!CameraStaticMeshComponent->GetStaticMesh())
        {
            UStaticMesh* CamMesh = LoadObject<UStaticMesh>(
                nullptr,
                TEXT("/Engine/EditorMeshes/MatineeCam_SM.MatineeCam_SM"),
                nullptr,
                LOAD_None,
                nullptr);
            CameraStaticMeshComponent->SetStaticMesh(CamMesh);
        }
    }
}

void UGTImageGeneratorBase::DrawDebug(FViewport* Viewport, FCanvas* Canvas)
{
    if (SceneCaptureComponent && SceneCaptureComponent->TextureTarget &&
        SceneCaptureComponent->TextureTarget->IsValidLowLevel())
    {
        UTextureRenderTarget2D* DebugTextureTarget = SceneCaptureComponent->TextureTarget;
        FTexture* RenderTextureResource = DebugTextureTarget->GetResource();
        FCanvasTileItem TileItem(
            FVector2D::ZeroVector,
            RenderTextureResource,
            FVector2D(DebugTextureTarget->SizeX, DebugTextureTarget->SizeY),
            FVector2D::ZeroVector,
            FVector2D::ZeroVector + FVector2D(1.f, 1.f),
            FLinearColor::White);
        TileItem.Rotation = FRotator(0.f, 0.f, 0.f);
        TileItem.PivotPoint = FVector2D(0.5f, 0.5f);
        TileItem.BlendMode = FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Opaque);
        Canvas->DrawItem(TileItem);
    }
}

void UGTImageGeneratorBase::GenerateData(const FDateTime& TimeStamp, int32 SessionId, FSessionValidationFunc SessionValidator)
{
    // Set resolution for next image
    if (bRandomResolution)
    {
        SceneCaptureComponent->Resolution = GenerateRandomResolution();
    }
    
    if (bUseAsyncGPUReadback)
    {
        ProcessReadyAsyncReadbacks();

        // Start async readback with frame ID
        const int32 FrameId = NextFrameId++;
        SceneCaptureComponent->StartCaptureImageAsync(
            FrameId,
            TimeStamp,
            SessionId,
            MoveTemp(SessionValidator));
    }
    else
    {
        // Fallback to synchronous capture
        FGTImage Image;
        SceneCaptureComponent->CaptureImage(Image);
        (new FAutoDeleteAsyncTask<FGTAsyncMakeImageTask>(
             this, Image, ImageFormat, bWriteAlpha, TimeStamp, SessionId, MoveTemp(SessionValidator), -1))
            ->StartBackgroundTask();
    }
}

void UGTImageGeneratorBase::GenerateData(
    const FDateTime& TimeStamp,
    int32 SessionId,
    FSessionValidationFunc SessionValidator,
    int32 FrameIndex,
    double StampSeconds)
{
    // Set resolution for next image
    if (bRandomResolution)
    {
        SceneCaptureComponent->Resolution = GenerateRandomResolution();
    }
    
    if (bUseAsyncGPUReadback)
    {
        ProcessReadyAsyncReadbacks();

        // Use external FrameIndex when valid, otherwise use NextFrameId
        const int32 FrameId = FrameIndex >= 0 ? FrameIndex : NextFrameId++;
        SceneCaptureComponent->StartCaptureImageAsync(
            FrameId,
            TimeStamp,
            SessionId,
            MoveTemp(SessionValidator));
    }
    else
    {
        // Fallback to synchronous capture
        FGTImage Image;
        SceneCaptureComponent->CaptureImage(Image);
        (new FAutoDeleteAsyncTask<FGTAsyncMakeImageTask>(
             this, Image, ImageFormat, bWriteAlpha, TimeStamp, SessionId, MoveTemp(SessionValidator), FrameIndex))
            ->StartBackgroundTask();
    }
}

UGTSceneCaptureComponent2D* UGTImageGeneratorBase::GetSceneCaptureComponent() const
{
    return SceneCaptureComponent;
}

void UGTImageGeneratorBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    ProcessReadyAsyncReadbacks();
}

void UGTImageGeneratorBase::ProcessReadyAsyncReadbacks()
{
    if (!bUseAsyncGPUReadback || !SceneCaptureComponent)
    {
        return;
    }

    FGTImage Image;
    int32 CompletedFrameId = -1;
    FDateTime CompletedTimeStamp;
    int32 CompletedSessionId = 0;
    FSessionValidationFunc CompletedSessionValidator;

    while (SceneCaptureComponent->FinishCaptureImageAsync(Image, CompletedFrameId, CompletedTimeStamp, CompletedSessionId, CompletedSessionValidator))
    {
        (new FAutoDeleteAsyncTask<FGTAsyncMakeImageTask>(
             this,
             Image,
             ImageFormat,
             bWriteAlpha,
             CompletedTimeStamp,
             CompletedSessionId,
             MoveTemp(CompletedSessionValidator),
             CompletedFrameId))
            ->StartBackgroundTask();
    }
}

bool UGTImageGeneratorBase::GetCalibrationParameters(
    FVector2D& OutFocalLength,
    FVector2D& OutPrincipalPoint) const
{
    FIntPoint CaptureSize = Resolution;

    float FocalLength =
        (float)Resolution.X / (2.f * FMath::Tan(FMath::DegreesToRadians(FOVAngle / 2.f)));

    OutFocalLength.X = FocalLength;
    OutFocalLength.Y = FocalLength;

    OutPrincipalPoint = FVector2D(Resolution) / 2.f;

    return true;
}

bool UGTImageGeneratorBase::GetStereoCalibrationParameters(
    const UGTImageGeneratorBase* SecondCamera,
    FVector2D& OutFocalLengthOne,
    FVector2D& OutPrincipalPointOne,
    FVector2D& OutFocalLengthTwo,
    FVector2D& OutPrincipalPointTwo,
    FIntPoint& OutImageSize,
    FRotator& OutRotation,
    FVector& OutTranslation) const
{
    if (Resolution != SecondCamera->Resolution)
    {
        return false;
    }
    if (!GetCalibrationParameters(OutFocalLengthOne, OutPrincipalPointOne))
    {
        return false;
    }
    if (!SecondCamera->GetCalibrationParameters(OutFocalLengthTwo, OutPrincipalPointTwo))
    {
        return false;
    }

    OutImageSize = Resolution;

    // TODO Getattachparent is somewhat uncclean but since we scenecaptures are
    // usually only attached to iamgegenerators its okay for know i guess
    OutRotation = SecondCamera->GetRelativeRotation() - GetRelativeRotation();
    OutTranslation = SecondCamera->GetRelativeLocation() - GetRelativeLocation();

    return true;
}

void UGTImageGeneratorBase::BeginPlay()
{
    Super::BeginPlay();

    SetComponentTickEnabled(true);
    PrimaryComponentTick.SetTickFunctionEnable(true);

    // TODO i have no idea why we need to attach again maybe a bug in
    // setupattachment or the usage???
    SceneCaptureComponent->AttachToComponent(
        this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

    SceneCaptureComponent->Resolution = Resolution;
    SceneCaptureComponent->FOVAngle = FOVAngle;
    SceneCaptureComponent->ShowFlags.AntiAliasing = bAntiAliasing;
    SceneCaptureComponent->ShowFlags.TemporalAA = bAntiAliasing;
    SceneCaptureComponent->ShowFlags.MotionBlur = bMotionBlur;
}

FIntPoint UGTImageGeneratorBase::GenerateRandomResolution()
{
    return FIntPoint(
        FMath::RandRange(Resolution.X, ResolutionMax.X),
        FMath::RandRange(Resolution.Y, ResolutionMax.Y));
}
