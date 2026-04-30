#pragma once

#include "CoreMinimal.h"
#include "CaptureTypes.generated.h"

USTRUCT(BlueprintType)
struct SIMULATOR_API FCaptureConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bEnableGt = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bEnableRosRgb = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	int32 PublishHz = 6;
};

USTRUCT(BlueprintType)
struct SIMULATOR_API FCaptureFrameInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 FrameIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	double StampSeconds = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 SessionId = 0;
};