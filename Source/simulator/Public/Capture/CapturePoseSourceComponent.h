#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Capture/CapturePoseTypes.h"
#include "CapturePoseSourceComponent.generated.h"

UCLASS(ClassGroup=(Capture), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API UCapturePoseSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCapturePoseSourceComponent();

	UFUNCTION(BlueprintCallable, Category = "Capture|Pose")
	FCapturePose GetWorldCapturePose() const;

	UFUNCTION(BlueprintPure, Category = "Capture|Pose")
	FName GetPoseSourceName() const;

private:
	UPROPERTY(EditAnywhere, Category = "Capture|Pose")
	FName PoseSourceName = TEXT("rover_base");
};