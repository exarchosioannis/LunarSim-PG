#pragma once

#include "CoreMinimal.h"
#include "CapturePoseTypes.generated.h"

USTRUCT(BlueprintType)
struct SIMULATOR_API FCapturePose
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Pose")
	bool bValid = false;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Pose")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Pose")
	FRotator Rotation = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct SIMULATOR_API FCaptureFramePoseData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Pose")
	FCapturePose RoverBasePose;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Pose")
	FCapturePose LeftCameraPose;

	UPROPERTY(BlueprintReadWrite, Category = "Capture|Pose")
	FCapturePose RightCameraPose;
};