// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "Components/SceneCaptureComponent2D.h"
#include "CoreMinimal.h"
#include "RHIGPUReadback.h"
#include <Engine/Texture.h>
#include <Materials/MaterialInstance.h>

#include "GTImage.h"
#include "GTObjectFilter.h"
#include "GTRandomColorGenerator.h"
#include "Generators/GTDataGeneratorComponent.h"

#include "GTSceneCaptureComponent2D.generated.h"

class UTexture2D;
class UMaterial;
class UMaterialInstanceDynamic;
class UGTImageGeneratorBase;
class FRHIGPUTextureReadback;
/**
 *
 */
UCLASS()
class UNREALGT_API UGTSceneCaptureComponent2D : public USceneCaptureComponent2D
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = Image)
    bool bColorAsFloat;

    UPROPERTY(EditAnywhere, Category = Image)
    bool bUseDisplayGamma;

    UPROPERTY(EditAnywhere, Category = Image, Meta = (EditCondition = "!bUseDisplayGamma"))
    float TargetGamma;

    UPROPERTY(EditAnywhere, Category = Image)
    FIntPoint Resolution;

    bool SRGB;

    TextureCompressionSettings CompressionSettings;

    UGTSceneCaptureComponent2D();

    bool GetPerspectiveMatrix(FMatrix& OutMatrix) const;

    bool ProjectToPixelLocation(const FVector& Location, FVector2D& OutPixelLocation) const;

    FVector2D NormalizePixelLocation(const FVector2D& PixelLocation) const;

    void CaptureImage(FGTImage& OutImage);

    // Async GPU readback support
    void StartCaptureImageAsync(
        int32 FrameId,
        const FDateTime& TimeStamp,
        int32 SessionId,
        FSessionValidationFunc SessionValidator);
    bool IsAsyncCaptureReady() const;
    bool FinishCaptureImageAsync(
        FGTImage& OutImage,
        int32& OutFrameId,
        FDateTime& OutTimeStamp,
        int32& OutSessionId,
        FSessionValidationFunc& OutSessionValidator);

    UTexture2D* TextureFromImage(const FGTImage& Image, bool bIsLookupTable = false);

    void SetupSegmentationPostProccess(
        const TMap<FGTObjectFilter, FColor>& ComponentFilterToColor,
        bool bShouldApplyCloseMorph = false,
        bool bColorEachComponentDifferent = false,
        bool bUseFilterForColorEachComponentDifferent = false,
        const FGTObjectFilter& ColorEachComponentDifferentFilter = FGTObjectFilter());

    void SetupSegmentationPostProccess(
        const TArray<FGTObjectFilter>& ComponentFilters,
        bool bColorEachComponentDifferent = false);

    void SetupSegmentationBlendable(bool bShouldApplyCloseMorph);





     

    void RegisterForSegmentation(
        UPrimitiveComponent *PrimitiveComponent,
        const TMap<FGTObjectFilter, FColor>& ComponentFilterToColor,
        bool bColorEachComponentDifferent = false,
        bool bUseFilterForColorEachComponentDifferent = false,
        const FGTObjectFilter& ColorEachComponentDifferentFilter = FGTObjectFilter());

    TArray<FColor> GetSegmentColorsUsedForActor(AActor* Actor);

    void SetupDepthPostProccess(float MaxZ = 1000.f, bool bShouldUsePerspectiveDepth = false);

    void SetupWorldNormalPostProccess();

    void SetResolution(const FIntPoint& NewResolution);

    void UpdateTextureTarget();

protected:
    void BeginPlay() override;

private:
    TArray<FColor> ColorArray;
    int NextAssignableColorArrayIndex = 1;
    TMap<FColor, int> ColorIndexCache;
    FGTRandomColorGenerator RandomColorGenerator;

    // Async GPU readback members
    struct FPendingReadback
    {
        TUniquePtr<FRHIGPUTextureReadback> Readback;
        int32 FrameId;
        FDateTime TimeStamp;
        int32 SessionId;
        FSessionValidationFunc SessionValidator;
    };

    TArray<FPendingReadback> PendingReadbacks;
    
    // TODO remove after debuging or write getter
public:
    UPROPERTY()
    UTexture2D* ColorMap;

    TMap<UPrimitiveComponent*, FColor> ComponentToColor;

private:
    UPROPERTY()
    UMaterial* SegmentationPostProcessMaterial;

    UPROPERTY()
    UMaterialInstance* SegmentationPostProcessMaterialClose;

    UPROPERTY()
    UMaterialInstanceDynamic* SegmentationPostProcessMaterialInstance;

    UPROPERTY()
    UMaterial* DepthPlannerPostProcessMaterial;

    UPROPERTY()
    UMaterial* DepthPerspectivePostProcessMaterial;

    UPROPERTY()
    UMaterialInstanceDynamic* DepthPlannerPostProcessMaterialInstance;

    UPROPERTY()
    UMaterialInstanceDynamic* DepthPerspectivePostProcessMaterialInstance;

    UPROPERTY()
    UMaterial* WorldNormalMaterial;
};
