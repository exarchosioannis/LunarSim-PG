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
	FVector PositionCmToRosMeters(const FVector& UnrealPositionCm);
	FQuat RotationToRosQuat(const FQuat& UnrealRotation);
	FQuat RotationToRosQuat(const FRotator& UnrealRotation);
}
