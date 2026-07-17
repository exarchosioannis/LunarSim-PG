#pragma once

#include "CoreMinimal.h"

// Canonical project-owned ROS 2 topic names. External compatibility should be
// handled with ROS remapping rather than duplicate simulator endpoints.
namespace LunarSimRosTopics
{
	inline constexpr TCHAR StereoLeftImage[] = TEXT("/stereo/left/image_raw");
	inline constexpr TCHAR StereoLeftCameraInfo[] = TEXT("/stereo/left/camera_info");
	inline constexpr TCHAR StereoRightImage[] = TEXT("/stereo/right/image_raw");
	inline constexpr TCHAR StereoRightCameraInfo[] = TEXT("/stereo/right/camera_info");

	inline constexpr TCHAR ImuData[] = TEXT("/imu/data");

	inline constexpr TCHAR GroundTruthPose[] = TEXT("/ground_truth/pose");
	inline constexpr TCHAR GroundTruthOdom[] = TEXT("/ground_truth/odom");
	inline constexpr TCHAR GroundTruthPath[] = TEXT("/ground_truth/path");
	inline constexpr TCHAR GroundTruthOccupancyMap[] = TEXT("/ground_truth/map/occupancy");
	inline constexpr TCHAR GroundTruthElevationPoints[] = TEXT("/ground_truth/map/elevation_points");

	inline constexpr TCHAR CaptureControl[] = TEXT("/capture/control");
	inline constexpr TCHAR CmdVel[] = TEXT("/cmd_vel");

	//TempoROS owns these standard endpoints through its tf2 and clock APIs.
	inline constexpr TCHAR Tf[] = TEXT("/tf");
	inline constexpr TCHAR TfStatic[] = TEXT("/tf_static");
	inline constexpr TCHAR Clock[] = TEXT("/clock");
}
