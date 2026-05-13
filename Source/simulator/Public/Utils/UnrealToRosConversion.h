#pragma once

#include "CoreMinimal.h"

/*
* Shared Unreal Engine -> ROS coordinate conversion helper.
*
* Unreal Engine:
*   X forward, Y right, Z up
*   distance unit: centimeters
*
* ROS:
*   X forward, Y left, Z up
*   distance unit: meters
*
* Position conversion:
*   p_ros = S * p_ue / 100
*
* Rotation conversion:
*   R_ros = S * R_ue * S
*
* where:
*   S = diag(1, -1, 1)
*/
namespace UnrealToRosConversion
{
	//Converts Unreal position in centimeters to ROS position in meters.
	FVector PositionCmToRosMeters(const FVector& UnrealPositionCm);

	//Converts Unreal rotation to ROS rotation using the same basis change as position.
	FQuat RotationToRosQuat(const FQuat& UnrealRotation);

	//Convenience overload for Unreal FRotator.
	FQuat RotationToRosQuat(const FRotator& UnrealRotation);

	//Converts a full Unreal transform into a ROS-style transform:
	//translation in meters, rotation in ROS coordinates.
	FTransform TransformToRos(const FTransform& UnrealTransform);
}