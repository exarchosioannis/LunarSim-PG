// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Text/GTActorInfoGeneratorComponent.h"

#include <CanvasItem.h>
#include <CanvasTypes.h>
#include <Components/InstancedStaticMeshComponent.h>
#include <Components/StaticMeshComponent.h>
#include <Engine/Engine.h>
#include <Engine/StaticMesh.h>
#include <Engine/TextureRenderTarget2D.h>
#include <Engine/World.h>
#include <EngineUtils.h>
#include "StaticMeshResources.h"

#include "GTFileUtilities.h"
#include "Generators/Image/GTImageGeneratorBase.h"
#include "Generators/Image/GTSceneCaptureComponent2D.h"

namespace
{
void ApplyLinkedCameraCalibration(
    UGTSceneCaptureComponent2D* TargetCapture,
    const UGTSceneCaptureComponent2D* SourceCapture)
{
    if (!TargetCapture || !SourceCapture)
    {
        return;
    }

    TargetCapture->SetResolution(SourceCapture->Resolution);
    TargetCapture->ProjectionType = ECameraProjectionMode::Perspective;
    TargetCapture->FOVAngle = SourceCapture->FOVAngle;
    TargetCapture->Overscan = 0.0f;
    TargetCapture->bUseCustomProjectionMatrix = false;
    TargetCapture->CustomProjectionMatrix.SetIdentity();
}
}

UGTActorInfoGeneratorComponent::UGTActorInfoGeneratorComponent()
    : Super()
    , MinimalRequiredBoundingBoxSize(0, 0)
    , MaxDistanceToCamera(20000.f)
    , Header(TEXT(""))
    , FormatActorString(TEXT("{ActorName} {WorldLocation}"))
    , Separator(TEXT("\n"))
    , Footer(TEXT(""))
    , FormatVector2DString(TEXT("{X} {Y}"))
    , FormatVector3DString(TEXT("{X} {Y} {Z}"))
    , FormatRotatorString(TEXT("{Yaw} {Pitch} {Roll}"))
    , Format2DBoxString(TEXT("{Min} {Max} {Center} {Extent} {Width} {Height}"))
    , Format3DBoxString(TEXT("{Min} {Max} {Center} {Extent}"))
{
    SegmentationSceneCapture = CreateDefaultSubobject<UGTSceneCaptureComponent2D>(
        TEXT("InternalSegmentationSceneCapture"));
    SegmentationSceneCapture->SetupAttachment(this);
}

void UGTActorInfoGeneratorComponent::GenerateData(const FDateTime& TimeStamp, int32 SessionId, FSessionValidationFunc SessionValidator)
{
    if (SessionValidator && !SessionValidator(SessionId))
    {
        return;
    }

    GenerateDataInternal(TimeStamp, -1);
}

void UGTActorInfoGeneratorComponent::GenerateData(
    const FDateTime& TimeStamp,
    int32 SessionId,
    FSessionValidationFunc SessionValidator,
    int32 FrameIndex,
    double StampSeconds)
{
    if (SessionValidator && !SessionValidator(SessionId))
    {
        return;
    }

    GenerateDataInternal(TimeStamp, FrameIndex);
}

void UGTActorInfoGeneratorComponent::GenerateDataInternal(
    const FDateTime& TimeStamp,
    int32 FrameIndex)
{
    CachedBoundingBoxes.Empty();
    CachedInstancedBoundingBoxes.Empty();

    UGTImageGeneratorBase* LinkedImageGeneratorComponent =
        Cast<UGTImageGeneratorBase>(LinkedImageGenerator.GetComponent(GetOwner()));

    if (bAccurateBoundingBoxes && LinkedImageGeneratorComponent)
    {
        ApplyLinkedCameraCalibration(
            SegmentationSceneCapture,
            LinkedImageGeneratorComponent->GetSceneCaptureComponent());
        SegmentationSceneCapture->CaptureImage(CachedSegmentation);
    }

    TArray<AActor*> TrackedActors;
    FString Result = Header;


    for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
    {
        AActor* Actor = *ActorItr;

        if (bTrackInstancedStaticMeshInstances)
        {
            TInlineComponentArray<UInstancedStaticMeshComponent*> InstancedComponents;
            Actor->GetComponents(InstancedComponents);

            if (!InstancedComponents.IsEmpty())
            {
                for (UInstancedStaticMeshComponent* InstancedComponent : InstancedComponents)
                {
                    if (!IsValid(InstancedComponent) || !InstancedComponent->GetStaticMesh())
                    {
                        continue;
                    }

                    bool bMatchesAnyFilter = false;
                    for (const FGTObjectFilter& ObjectFilter : TrackActorsThatMatchFilter)
                    {
                        if (DoesFilterMatchInstancedComponent(
                                ObjectFilter, InstancedComponent))
                        {
                            bMatchesAnyFilter = true;
                            break;
                        }
                    }

                    if (!bMatchesAnyFilter)
                    {
                        continue;
                    }

                    const int32 InstanceCount = InstancedComponent->GetInstanceCount();
                    const FBox LocalBounds =
                        InstancedComponent->GetStaticMesh()->GetBoundingBox();
                    if (!LocalBounds.IsValid)
                    {
                        continue;
                    }

                    const FVector LocalCenter = LocalBounds.GetCenter();
                    for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
                    {
                        FTransform InstanceTransform;
                        if (!InstancedComponent->GetInstanceTransform(
                                InstanceIndex, InstanceTransform, true))
                        {
                            continue;
                        }

                        const FVector InstanceCenter =
                            InstanceTransform.TransformPosition(LocalCenter);

                        if (FVector::DistSquared2D(InstanceCenter, GetComponentLocation()) >
                            MaxDistanceToCamera * MaxDistanceToCamera)
                        {
                            continue;
                        }

                        if (!LinkedImageGeneratorComponent)
                        {
                            continue;
                        }

                        FBox2D ScreenBoundingBox;
                        if (!GetInstancedStaticMeshScreenBoundingBox(
                                LocalBounds,
                                InstanceTransform,
                                LinkedImageGeneratorComponent,
                                ScreenBoundingBox))
                        {
                            continue;
                        }

                        if (bOnlyTrackOnScreenActors &&
                            !IsScreenBoundingBoxVisible(ScreenBoundingBox))
                        {
                            continue;
                        }

                        if (bClipInstancedBoundingBoxesAgainstLandscape)
                        {
                            FBox2D LandscapeClippedBox;
                            const bool bGotLandscapeClippedBox =
                                GetLandscapeClippedInstancedStaticMeshScreenBoundingBox(
                                    LocalBounds,
                                    InstanceTransform,
                                    InstancedComponent,
                                    LinkedImageGeneratorComponent,
                                    LandscapeClippedBox);

                            if (bGotLandscapeClippedBox)
                            {
                                ScreenBoundingBox = LandscapeClippedBox;
                                // Check again after clipping. The visible/above-landscape part may be too small.
                                if (bOnlyTrackOnScreenActors &&
                                    !IsScreenBoundingBoxVisible(ScreenBoundingBox))
                                {
                                    continue;
                                }
                            }
                            else if (bFallbackToProjectedBoxWhenLandscapeClipFails)
                            {
                            }
                            else
                            {
                                continue;
                            }
                        }

                        FVector2D ScreenLocation(-1.f, -1.f);
                        LinkedImageGeneratorComponent->GetSceneCaptureComponent()
                            ->ProjectToPixelLocation(InstanceCenter, ScreenLocation);

                        const FVector2D ScreenLocationNormalized =
                            LinkedImageGeneratorComponent->GetSceneCaptureComponent()
                                ->NormalizePixelLocation(ScreenLocation);

                        FBox2D ScreenBoundingBoxNormalized;
                        ScreenBoundingBoxNormalized.Min =
                            LinkedImageGeneratorComponent->GetSceneCaptureComponent()
                                ->NormalizePixelLocation(ScreenBoundingBox.Min);
                        ScreenBoundingBoxNormalized.Max =
                            LinkedImageGeneratorComponent->GetSceneCaptureComponent()
                                ->NormalizePixelLocation(ScreenBoundingBox.Max);

                        const FString ObjectName = FString::Printf(
                            TEXT("%s/%s/Instance_%d"),
                            *Actor->GetActorNameOrLabel(),
                            *InstancedComponent->GetName(),
                            InstanceIndex);
                        const FString MeshName =
                            InstancedComponent->GetStaticMesh()->GetName();

                        AppendFormattedRow(
                            Result,
                            ObjectName,
                            MeshName,
                            InstanceCenter,
                            InstanceTransform.Rotator(),
                            ScreenLocation,
                            ScreenLocationNormalized,
                            ScreenBoundingBox,
                            ScreenBoundingBoxNormalized);

                        CachedInstancedBoundingBoxes.Add(ScreenBoundingBox);
                    }
                }

                // Per-instance mode intentionally suppresses the combined parent actor row.
                continue;
            }
        }

        for (const FGTObjectFilter& ObjectFilter : TrackActorsThatMatchFilter)
        {
            if (ObjectFilter.MatchesActor(Actor))
            {
                if (FVector::DistSquared2D(Actor->GetActorLocation(), GetComponentLocation()) <=
                    MaxDistanceToCamera * MaxDistanceToCamera)
                {
                    if (!bOnlyTrackOnScreenActors ||
                        (bOnlyTrackOnScreenActors &&
                         IsActorRenderedOnScreen(Actor)))
                    {
                        TrackedActors.Push(Actor);
                    }
                }
            }
        }
    }

    for (AActor* TrackedActor : TrackedActors)
    {
        UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(
            TrackedActor->GetComponentByClass(UStaticMeshComponent::StaticClass()));

        FString MeshName(TEXT("NOMESHONTHISACTOR"));
        if (MeshComp)
        {
            MeshComp->GetStaticMesh()->GetName(MeshName);
        }

        FVector2D ScreenLocation(-1.f, -1.f);
        FVector2D ScreenLocationNormalized;
        FBox2D ScreenBoundingBox;
        FBox2D ScreenBoundingBoxNormalized;
        if (LinkedImageGeneratorComponent)
        {
            LinkedImageGeneratorComponent->GetSceneCaptureComponent()->ProjectToPixelLocation(
                TrackedActor->GetActorLocation(), ScreenLocation);

            ScreenLocationNormalized =
                LinkedImageGeneratorComponent->GetSceneCaptureComponent()->NormalizePixelLocation(
                    ScreenLocation);

            GetActorScreenBoundingBox(
                TrackedActor, LinkedImageGeneratorComponent, ScreenBoundingBox);

            ScreenBoundingBoxNormalized.Min =
                LinkedImageGeneratorComponent->GetSceneCaptureComponent()->NormalizePixelLocation(
                    ScreenBoundingBox.Min);
            ScreenBoundingBoxNormalized.Max =
                LinkedImageGeneratorComponent->GetSceneCaptureComponent()->NormalizePixelLocation(
                    ScreenBoundingBox.Max);
        }

        AppendFormattedRow(
            Result,
            TrackedActor->GetName(),
            MeshName,
            TrackedActor->GetActorLocation(),
            TrackedActor->GetActorRotation(),
            ScreenLocation,
            ScreenLocationNormalized,
            ScreenBoundingBox,
            ScreenBoundingBoxNormalized);
    }

    Result.RemoveFromEnd(Separator);
    Result.Append(Footer);

    CurrentResult = Result.ReplaceEscapedCharWithChar();

    const auto Data = FGTFileUtilities::StringToCharArray(CurrentResult);

    DataReadyDelegate.Broadcast(Data, TimeStamp, FrameIndex);
}

void UGTActorInfoGeneratorComponent::AppendFormattedRow(
    FString& Result,
    const FString& ObjectName,
    const FString& MeshName,
    const FVector& WorldLocation,
    const FRotator& WorldRotation,
    const FVector2D& ScreenLocation,
    const FVector2D& ScreenLocationNormalized,
    const FBox2D& ScreenBoundingBox,
    const FBox2D& ScreenBoundingBoxNormalized)
{
    TMap<FString, FStringFormatArg> GlobalProperties{
        {TEXT("WorldLocation"), Vector3DToFormattedString(WorldLocation)},
        {TEXT("WorldRotation"), RotatorToFormattedString(WorldRotation)},
        {TEXT("ScreenLocation"), Vector2DToFormattedString(ScreenLocation)},
        {TEXT("ScreenLocationNormalized"), Vector2DToFormattedString(ScreenLocationNormalized)},
        {TEXT("ScreenBoundingBox"), Box2DToFormattedString(ScreenBoundingBox)},
        {TEXT("ScreenBoundingBoxNormalized"),
         Box2DToFormattedString(ScreenBoundingBoxNormalized)},
        {TEXT("ActorName"), ObjectName},
        {TEXT("MeshName"), MeshName}};

    FString Row = FString::Format(*FormatActorString, GlobalProperties);
    for (const TPair<FString, FString>& ReplacePair : ReplaceStrings)
    {
        Row.ReplaceInline(*ReplacePair.Key, *ReplacePair.Value, ESearchCase::CaseSensitive);
    }

    Result.Append(Row);
    Result.Append(Separator);
}

bool UGTActorInfoGeneratorComponent::DoesFilterMatchInstancedComponent(
    const FGTObjectFilter& ObjectFilter,
    UInstancedStaticMeshComponent* InstancedComponent) const
{
    if (!IsValid(InstancedComponent))
    {
        return false;
    }

    if (ObjectFilter.ActorInstance)
    {
        return ObjectFilter.ActorInstance == InstancedComponent->GetOwner();
    }

    return ObjectFilter.MatchesComponent(InstancedComponent);
}

bool UGTActorInfoGeneratorComponent::GetInstancedStaticMeshScreenBoundingBox(
    const FBox& LocalBounds,
    const FTransform& InstanceTransform,
    UGTImageGeneratorBase* ImageGeneratorComponent,
    FBox2D& OutBox) const
{
    if (!LocalBounds.IsValid || !ImageGeneratorComponent)
    {
        return false;
    }

    const FVector LocalCenter = LocalBounds.GetCenter();
    const FVector LocalExtent = LocalBounds.GetExtent();
    OutBox = FBox2D(EForceInit::ForceInit);

    const FVector BoundsPointMapping[8] = {
        FVector(1, 1, 1),
        FVector(1, 1, -1),
        FVector(1, -1, 1),
        FVector(1, -1, -1),
        FVector(-1, 1, 1),
        FVector(-1, 1, -1),
        FVector(-1, -1, 1),
        FVector(-1, -1, -1)};

    int32 ProjectedPointCount = 0;
    for (const FVector& BoundsPoint : BoundsPointMapping)
    {
        const FVector WorldBoundsPoint =
            InstanceTransform.TransformPosition(LocalCenter + BoundsPoint * LocalExtent);
        FVector2D ProjectedWorldLocation;
        if (ImageGeneratorComponent->GetSceneCaptureComponent()->ProjectToPixelLocation(
                WorldBoundsPoint, ProjectedWorldLocation))
        {
            OutBox += ProjectedWorldLocation;
            ProjectedPointCount++;
        }
    }

    return ProjectedPointCount > 0 && OutBox.bIsValid &&
           !OutBox.GetSize().IsNearlyZero();
}

bool UGTActorInfoGeneratorComponent::GetLandscapeHeightAtXY(
    const FVector& WorldPoint,
    const UInstancedStaticMeshComponent* SourceComponent,
    float& OutLandscapeZ) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const FVector TraceStart(
        WorldPoint.X,
        WorldPoint.Y,
        WorldPoint.Z + LandscapeClipTraceHalfHeightCm);

    const FVector TraceEnd(
        WorldPoint.X,
        WorldPoint.Y,
        WorldPoint.Z - LandscapeClipTraceHalfHeightCm);

    FCollisionQueryParams QueryParams(FName(TEXT("ActorInfoLandscapeClipTrace")), false);
    QueryParams.bTraceComplex = false;

    // Ignore the owner of the ISM/HISM rocks, otherwise the trace can hit the rock instances
    // instead of the landscape/world surface below them.
    if (SourceComponent && SourceComponent->GetOwner())
    {
        QueryParams.AddIgnoredActor(SourceComponent->GetOwner());
    }

    FHitResult Hit;
    const bool bHit = World->LineTraceSingleByChannel(
        Hit,
        TraceStart,
        TraceEnd,
        ECC_WorldStatic,
        QueryParams);

    if (!bHit || !Hit.bBlockingHit)
    {
        return false;
    }

    OutLandscapeZ = Hit.ImpactPoint.Z;
    return true;
}

bool UGTActorInfoGeneratorComponent::GetLandscapeClippedInstancedStaticMeshScreenBoundingBox(
    const FBox& LocalBounds,
    const FTransform& InstanceTransform,
    const UInstancedStaticMeshComponent* InstancedComponent,
    UGTImageGeneratorBase* ImageGeneratorComponent,
    FBox2D& OutBox) const
{
    if (!LocalBounds.IsValid || !ImageGeneratorComponent || !InstancedComponent)
    {
        return false;
    }

    OutBox = FBox2D(EForceInit::ForceInit);

    int32 KeptPointCount = 0;
    int32 ProjectedPointCount = 0;
    int32 LandscapeHitCount = 0;

    auto TryAddLocalSamplePoint = [this,
                                   &InstanceTransform,
                                   InstancedComponent,
                                   ImageGeneratorComponent,
                                   &OutBox,
                                   &KeptPointCount,
                                   &ProjectedPointCount,
                                   &LandscapeHitCount](const FVector& LocalSamplePoint)
    {
        const FVector WorldSamplePoint =
            InstanceTransform.TransformPosition(LocalSamplePoint);

        float LandscapeZ = 0.0f;
        if (!GetLandscapeHeightAtXY(WorldSamplePoint, InstancedComponent, LandscapeZ))
        {
            return;
        }

        LandscapeHitCount++;

        // Keep points that are above the landscape/world static surface. The tolerance lets
        // a rock remain slightly buried visually without making the annotation too tiny.
        if (WorldSamplePoint.Z < LandscapeZ - LandscapeClipToleranceCm)
        {
            return;
        }

        KeptPointCount++;

        FVector2D ProjectedPoint;
        if (ImageGeneratorComponent->GetSceneCaptureComponent()->ProjectToPixelLocation(
                WorldSamplePoint,
                ProjectedPoint))
        {
            OutBox += ProjectedPoint;
            ProjectedPointCount++;
        }
    };

    bool bUsedMeshVertices = false;

    if (bUseMeshVerticesForLandscapeClip)
    {
        const UStaticMesh* StaticMesh = InstancedComponent->GetStaticMesh();
        const FStaticMeshRenderData* RenderData = StaticMesh ? StaticMesh->GetRenderData() : nullptr;

        if (RenderData && RenderData->LODResources.Num() > 0)
        {
            const FStaticMeshLODResources& LODResources = RenderData->LODResources[0];
            const FPositionVertexBuffer& PositionVertexBuffer =
                LODResources.VertexBuffers.PositionVertexBuffer;

            const int32 VertexCount = static_cast<int32>(PositionVertexBuffer.GetNumVertices());
            if (VertexCount > 0)
            {
                bUsedMeshVertices = true;

                const int32 MaxVertexSamples =
                    FMath::Clamp(MaxLandscapeClipVertexSamples, 8, 4096);
                const int32 SamplesToUse = FMath::Min(VertexCount, MaxVertexSamples);

                for (int32 SampleIndex = 0; SampleIndex < SamplesToUse; ++SampleIndex)
                {
                    int32 VertexIndex = 0;
                    if (SamplesToUse > 1)
                    {
                        VertexIndex = FMath::Clamp(
                            FMath::RoundToInt(
                                static_cast<float>(SampleIndex) *
                                static_cast<float>(VertexCount - 1) /
                                static_cast<float>(SamplesToUse - 1)),
                            0,
                            VertexCount - 1);
                    }

                    const FVector3f LocalVertexPosition =
                        PositionVertexBuffer.VertexPosition(VertexIndex);
                    TryAddLocalSamplePoint(FVector(LocalVertexPosition));
                }
            }
        }
    }

    // If mesh vertices are requested and available, their result is the result.
    // If they are unavailable, fall back to the old grid sampling path so the option
    // remains safe for meshes without CPU-visible render vertices.
    if (!bUsedMeshVertices)
    {
        const int32 SamplesPerAxis = FMath::Clamp(LandscapeClipSamplesPerAxis, 2, 8);

        for (int32 XIndex = 0; XIndex < SamplesPerAxis; ++XIndex)
        {
            const float XAlpha = static_cast<float>(XIndex) /
                static_cast<float>(SamplesPerAxis - 1);

            for (int32 YIndex = 0; YIndex < SamplesPerAxis; ++YIndex)
            {
                const float YAlpha = static_cast<float>(YIndex) /
                    static_cast<float>(SamplesPerAxis - 1);

                for (int32 ZIndex = 0; ZIndex < SamplesPerAxis; ++ZIndex)
                {
                    const float ZAlpha = static_cast<float>(ZIndex) /
                        static_cast<float>(SamplesPerAxis - 1);

                    const FVector LocalSamplePoint(
                        FMath::Lerp(LocalBounds.Min.X, LocalBounds.Max.X, XAlpha),
                        FMath::Lerp(LocalBounds.Min.Y, LocalBounds.Max.Y, YAlpha),
                        FMath::Lerp(LocalBounds.Min.Z, LocalBounds.Max.Z, ZAlpha));

                    TryAddLocalSamplePoint(LocalSamplePoint);
                }
            }
        }
    }

    return LandscapeHitCount > 0 &&
           KeptPointCount > 0 &&
           ProjectedPointCount > 0 &&
           OutBox.bIsValid &&
           !OutBox.GetSize().IsNearlyZero();
}

bool UGTActorInfoGeneratorComponent::IsScreenBoundingBoxVisible(
    const FBox2D& ScreenBoundingBox) const
{
    const FVector2D ScreenBoundingBoxSize = ScreenBoundingBox.GetSize();
    if (!bRequireMinimumVisibleBoundingBox && !ScreenBoundingBoxSize.IsNearlyZero())
    {
        return true;
    }

    return ScreenBoundingBoxSize.X >= MinimalRequiredBoundingBoxSize.X &&
           ScreenBoundingBoxSize.Y >= MinimalRequiredBoundingBoxSize.Y;
}

void UGTActorInfoGeneratorComponent::DrawDebug(FViewport* Viewport, FCanvas* Canvas)
{
    float TextOffset = 0.f;
    Canvas->Clear(FLinearColor::White);

    if (LinkedImageGenerator.GetComponent(GetOwner()))
    {
        // Draw boundingboxes
        UGTImageGeneratorBase* LinkedImageGeneratorComponent =
            Cast<UGTImageGeneratorBase>(LinkedImageGenerator.GetComponent(GetOwner()));

        UTextureRenderTarget2D* LinkedImageTextureTarget =
            LinkedImageGeneratorComponent->GetSceneCaptureComponent()->TextureTarget;
        FTexture* LinkedImageTextureResource = LinkedImageTextureTarget->GetResource();
        FCanvasTileItem LinkedImageItem(
            FVector2D(0.f, 0.f),
            LinkedImageTextureResource,
            FVector2D(LinkedImageTextureTarget->SizeX, LinkedImageTextureTarget->SizeY),
            FVector2D::ZeroVector,
            FVector2D::ZeroVector + FVector2D(1.f, 1.f),
            FLinearColor::White);
        LinkedImageItem.Rotation = FRotator(0.f, 0.f, 0.f);
        LinkedImageItem.PivotPoint = FVector2D(0.5f, 0.5f);
        LinkedImageItem.BlendMode = FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Opaque);
        Canvas->DrawItem(LinkedImageItem);
        for (const auto& ActorBoundingBoxPair : CachedBoundingBoxes)
        {
            // we need to add 1 because the canvas box item draws around the specified coords?????
            FVector2D VectorOffset(1.f, 1.f);
            FCanvasBoxItem BoxItem(
                ActorBoundingBoxPair.Value.Min + VectorOffset,
                ActorBoundingBoxPair.Value.GetSize());
            BoxItem.SetColor(FColor::Red);
            BoxItem.LineThickness = 1.f;
            Canvas->DrawItem(BoxItem);
        }
        for (const FBox2D& InstanceBoundingBox : CachedInstancedBoundingBoxes)
        {
            FVector2D VectorOffset(1.f, 1.f);
            FCanvasBoxItem BoxItem(
                InstanceBoundingBox.Min + VectorOffset,
                InstanceBoundingBox.GetSize());
            BoxItem.SetColor(FColor::Green);
            BoxItem.LineThickness = 1.f;
            Canvas->DrawItem(BoxItem);
        }
        TextOffset += LinkedImageItem.Size.Y;

        if (bAccurateBoundingBoxes && SegmentationSceneCapture)
        {
            if (SegmentationSceneCapture && SegmentationSceneCapture->TextureTarget &&
                SegmentationSceneCapture->TextureTarget->IsValidLowLevel())
            {
                // Draw segmetnation
                UTextureRenderTarget2D* SegmentationTextureTarget =
                    SegmentationSceneCapture->TextureTarget;
                FTexture* SegmentationResource = SegmentationTextureTarget->GetResource();
                FCanvasTileItem SegmentationItem(
                    FVector2D(LinkedImageItem.Size.X, 0.f),
                    SegmentationResource,
                    FVector2D(SegmentationTextureTarget->SizeX, SegmentationTextureTarget->SizeY),
                    FVector2D::ZeroVector,
                    FVector2D::ZeroVector + FVector2D(1.f, 1.f),
                    FLinearColor::White);
                SegmentationItem.Rotation = FRotator(0.f, 0.f, 0.f);
                SegmentationItem.PivotPoint = FVector2D(0.5f, 0.5f);
                SegmentationItem.BlendMode =
                    FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Opaque);
                Canvas->DrawItem(SegmentationItem);

                // Draw colormap
                UTexture2D* ColorMap = SegmentationSceneCapture->ColorMap;
                FTexture* ColorMapResource = ColorMap->GetResource();
                FCanvasTileItem ColorMapItem(
                    FVector2D(0.f, SegmentationItem.Size.Y),
                    ColorMapResource,
                    FVector2D(ColorMapResource->GetSizeX(), ColorMapResource->GetSizeY() * 8.f),
                    FVector2D::ZeroVector,
                    FVector2D::ZeroVector + FVector2D(1.f, 1.f),
                    FLinearColor::White);
                ColorMapItem.Rotation = FRotator(0.f, 0.f, 0.f);
                ColorMapItem.PivotPoint = FVector2D(0.5f, 0.5f);
                ColorMapItem.BlendMode =
                    FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Opaque);
                Canvas->DrawItem(ColorMapItem);
                TextOffset += ColorMapItem.Size.Y;
            }
        }
    }

    FCanvasTextItem TextItem(
        FVector2D(0.f, TextOffset),
        FText::FromString(CurrentResult.Mid(0, 5000)),
        GEngine->GetMediumFont(),
        FLinearColor::Red);
    Canvas->DrawItem(TextItem);
}

#if WITH_EDITOR
bool UGTActorInfoGeneratorComponent::CanEditChange(const FProperty* InProperty) const
{
    if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UGTActorInfoGeneratorComponent, bOnlyTrackOnScreenActors) ||
        InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UGTActorInfoGeneratorComponent, bAccurateBoundingBoxes))
    {
        if (!LinkedImageGenerator.IsSet())
        {
            return false;
        }
    }

    return Super::CanEditChange(InProperty);
}
#endif

void UGTActorInfoGeneratorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (bTrackInstancedStaticMeshInstances &&
        !LinkedImageGenerator.GetComponent(GetOwner()))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "UnrealGT ActorInfo per-instance tracking requires a linked image "
                "generator; no ISM/HISM rows will be written."));
    }

    if (bTrackInstancedStaticMeshInstances && bAccurateBoundingBoxes)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "UnrealGT ActorInfo: Accurate Bounding Boxes is not recommended "
                "for ISM/HISM per-instance boxes. Use landscape clipping instead, "
                "or turn Accurate Bounding Boxes off."));
    }

    if (bAccurateBoundingBoxes)
    {
        UGTImageGeneratorBase* LinkedImageGeneratorComponent =
            Cast<UGTImageGeneratorBase>(LinkedImageGenerator.GetComponent(GetOwner()));

        if (LinkedImageGeneratorComponent)
        {
            ApplyLinkedCameraCalibration(
                SegmentationSceneCapture,
                LinkedImageGeneratorComponent->GetSceneCaptureComponent());
            SegmentationSceneCapture->SetupSegmentationPostProccess(
                TrackActorsThatMatchFilter, bShouldApplyCloseMorph);
        }
    }
}

bool UGTActorInfoGeneratorComponent::IsActorRenderedOnScreen(AActor* Actor)
{
    UGTImageGeneratorBase* LinkedImageGeneratorComponent =
        Cast<UGTImageGeneratorBase>(LinkedImageGenerator.GetComponent(GetOwner()));
    if (LinkedImageGeneratorComponent)
    {
        FBox2D ScreenBoundingBox;
        GetActorScreenBoundingBox(Actor, LinkedImageGeneratorComponent, ScreenBoundingBox);
        return IsScreenBoundingBoxVisible(ScreenBoundingBox);
    }

    return true;
}

bool UGTActorInfoGeneratorComponent::GetActorScreenBoundingBox(
    AActor* InActor,
    UGTImageGeneratorBase* ImageGeneratorComponent,
    FBox2D& OutBox)
{
    if (CachedBoundingBoxes.Contains(InActor))
    {
        OutBox = CachedBoundingBoxes[InActor];
        return true;
    }

    FVector ActorCenter;
    FVector ActorExtent;
    InActor->GetActorBounds(false, ActorCenter, ActorExtent);

    const FVector BoundsPointMapping[8] = {FVector(1, 1, 1),
                                           FVector(1, 1, -1),
                                           FVector(1, -1, 1),
                                           FVector(1, -1, -1),
                                           FVector(-1, 1, 1),
                                           FVector(-1, 1, -1),
                                           FVector(-1, -1, 1),
                                           FVector(-1, -1, -1)};
    FBox2D ScreenBoundingBox(EForceInit::ForceInitToZero);

    for (uint8 BoundsPointItr = 0; BoundsPointItr < 8; BoundsPointItr++)
    {
        FVector2D ProjectedWorldLocation;

        bool bValidPixelLocation =
            ImageGeneratorComponent->GetSceneCaptureComponent()->ProjectToPixelLocation(
                ActorCenter + (BoundsPointMapping[BoundsPointItr] * ActorExtent),
                ProjectedWorldLocation);

        if (bValidPixelLocation)
        {
            ScreenBoundingBox += FVector2D(ProjectedWorldLocation.X, ProjectedWorldLocation.Y);
        }
    }

    if (bAccurateBoundingBoxes && CachedSegmentation.IsValid())
    {
        FBox2D AccurateScreenBoundingBox(EForceInit::ForceInitToZero);

        TArray<FColor> SegmentColors =
            SegmentationSceneCapture->GetSegmentColorsUsedForActor(InActor);
        int TotalPixelOverlap = 0;
        for (const FColor& SegmentColor : SegmentColors)
        {
            for (int X = ScreenBoundingBox.Min.X; X <= ScreenBoundingBox.Max.X; X++)
            {
                for (int Y = ScreenBoundingBox.Min.Y; Y <= ScreenBoundingBox.Max.Y; Y++)
                {
                    FColor ColorInSegmentationMap = CachedSegmentation.GetPixel(X, Y);
                    if (SegmentColor == ColorInSegmentationMap)
                    {
                        AccurateScreenBoundingBox += FVector2D(X, Y);
                        TotalPixelOverlap++;
                    }
                }
            }
        }
        // Discard bbox if not enough pixels are detetected inside the original box
        // This will not work great if the detect object is a ring shaped or similiar
        // TODO make configurable
        if (TotalPixelOverlap <= ScreenBoundingBox.GetArea() / 8)
        {
            AccurateScreenBoundingBox = FBox2D(EForceInit::ForceInitToZero);
        }

        ScreenBoundingBox = AccurateScreenBoundingBox;
    }

    CachedBoundingBoxes.Add(InActor, ScreenBoundingBox);
    OutBox = ScreenBoundingBox;

    return true;
}

FString UGTActorInfoGeneratorComponent::Vector2DToFormattedString(const FVector2D& InVector)
{
    return FString::Format(
        *FormatVector2DString, {{TEXT("X"), InVector.X}, {TEXT("Y"), InVector.Y}});
}

FString UGTActorInfoGeneratorComponent::Vector3DToFormattedString(const FVector& InVector)
{
    return FString::Format(
        *FormatVector3DString,
        {{TEXT("X"), InVector.X}, {TEXT("Y"), InVector.Y}, {TEXT("Z"), InVector.Z}});
}

FString UGTActorInfoGeneratorComponent::RotatorToFormattedString(const FRotator& InRotator)
{
    return FString::Format(
        *FormatRotatorString,
        {{TEXT("Yaw"), InRotator.Yaw},
         {TEXT("Pitch"), InRotator.Pitch},
         {TEXT("Roll"), InRotator.Roll}});
}

FString UGTActorInfoGeneratorComponent::Box2DToFormattedString(const FBox2D& InBox)
{
    return FString::Format(
        *Format2DBoxString,
        {{TEXT("Min"), Vector2DToFormattedString(InBox.Min)},
         {TEXT("Max"), Vector2DToFormattedString(InBox.Max)},
         {TEXT("Width"), InBox.GetSize().X},
         {TEXT("Height"), InBox.GetSize().Y},
         {TEXT("Center"), Vector2DToFormattedString(InBox.GetCenter())},
         {TEXT("Extent"), Vector2DToFormattedString(InBox.GetExtent())}});
}

FString UGTActorInfoGeneratorComponent::Box3DToFormattedString(const FBox& InBox)
{
    return FString::Format(
        *Format3DBoxString,
        {{TEXT("Min"), Vector3DToFormattedString(InBox.Min)},
         {TEXT("Max"), Vector3DToFormattedString(InBox.Max)},
         {TEXT("Center"), Vector3DToFormattedString(InBox.GetCenter())},
         {TEXT("Extent"), Vector3DToFormattedString(InBox.GetExtent())}});
}
