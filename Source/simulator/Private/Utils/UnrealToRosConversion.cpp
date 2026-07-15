#include "Utils/UnrealToRosConversion.h"

namespace UnrealToRosConversion
{
	FVector PositionCmToRosMeters(const FVector& UnrealPositionCm)
	{
		return FVector(
			UnrealPositionCm.X / 100.0,
			-UnrealPositionCm.Y / 100.0,
			UnrealPositionCm.Z / 100.0
		);
	}

	FQuat RotationToRosQuat(const FQuat& UnrealRotation)
	{
		const FQuat UnrealQuat = UnrealRotation.GetNormalized();

		FQuat RosQuat(
			-UnrealQuat.X,
			 UnrealQuat.Y,
			-UnrealQuat.Z,
			 UnrealQuat.W
		);

		RosQuat.Normalize();
		return RosQuat;
	}

	FQuat RotationToRosQuat(const FRotator& UnrealRotation)
	{
		return RotationToRosQuat(UnrealRotation.Quaternion());
	}
}
