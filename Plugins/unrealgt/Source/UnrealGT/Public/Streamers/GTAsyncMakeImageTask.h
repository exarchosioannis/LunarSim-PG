// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GTImage.h"
#include "GTImageFileFormat.h"
#include "Generators/GTDataGeneratorComponent.h"

#include "Async/AsyncWork.h"

class UGTImageGeneratorBase;

/**
 *
 */
class UNREALGT_API FGTAsyncMakeImageTask : public FNonAbandonableTask
{
    friend class FAutoDeleteAsyncTask<FGTAsyncMakeImageTask>;

public:
    // Constructor for image data processing
    FGTAsyncMakeImageTask(
        UGTImageGeneratorBase* SourceComponent,
        const FGTImage& Image,
        EGTImageFileFormat ImageFormat,
        bool bWriteAlpha,
        FDateTime TimeStamp,
        int32 SessionId,
        FSessionValidationFunc SessionValidator,
        int32 FrameIndex = -1);

    void DoWork();

    TStatId GetStatId() const;

private:
    UGTImageGeneratorBase* SourceComponent;
    FGTImage Image;
    EGTImageFileFormat ImageFormat;
    FDateTime TimeStamp;
    bool bWriteAlpha;
    int32 SessionId;
    FSessionValidationFunc SessionValidator;
    int32 FrameIndex;
};
