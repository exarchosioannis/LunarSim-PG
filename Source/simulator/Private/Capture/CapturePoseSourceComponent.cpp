#include "Capture/CapturePoseSourceComponent.h"
#include "GameFramework/Actor.h"

UCapturePoseSourceComponent::UCapturePoseSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FCapturePose UCapturePoseSourceComponent::GetWorldCapturePose() const
{
	FCapturePose Pose;
	const AActor* Owner = GetOwner();
	if (!Owner) {
		return Pose;
	}

	Pose.bValid = true;
	Pose.Position = Owner->GetActorLocation();
	Pose.Rotation = Owner->GetActorRotation();

	return Pose;
}

FName UCapturePoseSourceComponent::GetPoseSourceName() const
{
	return PoseSourceName;
}