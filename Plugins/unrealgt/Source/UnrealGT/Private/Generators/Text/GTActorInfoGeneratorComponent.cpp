// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Text/GTActorInfoGeneratorComponent.h"

#include <CanvasItem.h>
#include <CanvasTypes.h>
#include <Components/InstancedStaticMeshComponent.h>
#include <Components/StaticMeshComponent.h>
#include <Engine/Engine.h>
#include <Engine/StaticMesh.h>
#include <Engine/TextureRenderTarget2D.h>
#include <EngineUtils.h>

#include "GTFileUtilities.h"
#include "Generators/Image/GTImageGeneratorBase.h"
#include "Generators/Image/GTSceneCaptureComponent2D.h"

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

    if (bAccurateBoundingBoxes && LinkedImageGenerator.GetComponent(GetOwner()))
    {
        SegmentationSceneCapture->SetResolution(
            LinkedImageGeneratorComponent->GetSceneCaptureComponent()->Resolution);
        SegmentationSceneCapture->CaptureImage(CachedSegmentation);
    }

    TArray<AActor*> TrackedActors;
    FString Result = Header;

    int32 InstancedComponentsConsidered = 0;
    int32 InstancesConsidered = 0;
    int32 InstancesWritten = 0;

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

                    InstancedComponentsConsidered++;

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
                        InstancesConsidered++;

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
                        InstancesWritten++;
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

    if (bTrackInstancedStaticMeshInstances)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT(
                "UnrealGT ActorInfo frame %d: ISM/HISM components considered=%d, "
                "instances considered=%d, rows written=%d"),
            FrameIndex,
            InstancedComponentsConsidered,
            InstancesConsidered,
            InstancesWritten);
    }
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

bool UGTActorInfoGeneratorComponent::CanEditChange(const FProperty* InProperty) const
{
    if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UGTActorInfoGeneratorComponent, bOnlyTrackOnScreenActors) || InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UGTActorInfoGeneratorComponent, bAccurateBoundingBoxes))
    {
        if (!LinkedImageGenerator.IsSet())
        {
            return false;
        }
    }
    
    return Super::CanEditChange(InProperty);
}

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
                "UnrealGT ActorInfo per-instance boxes use projected mesh bounds. "
                "Accurate segmentation refinement is actor/component based and is not "
                "applied per ISM/HISM instance."));
    }

    if (bAccurateBoundingBoxes && LinkedImageGenerator.GetComponent(GetOwner()))
    {
        UGTImageGeneratorBase* LinkedImageGeneratorComponent =
            Cast<UGTImageGeneratorBase>(LinkedImageGenerator.GetComponent(GetOwner()));

        SegmentationSceneCapture->SetResolution(
            LinkedImageGeneratorComponent->GetSceneCaptureComponent()->Resolution);
        SegmentationSceneCapture->SetupSegmentationPostProccess(
            TrackActorsThatMatchFilter, bShouldApplyCloseMorph);
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
