#pragma once

#include "CoreMinimal.h"

#include "sensor_msgs/msg/point_cloud2.hpp"

/*
 * Builds a ROS PointCloud2 from the already-sampled elevation grid.
 *
 * This keeps the 3D terrain visualization logic separate from
 * UOccupancyMapPublisherComponent. The component owns the map generation,
 * while this helper only converts the elevation layer into a ROS-native
 * point cloud for RViz and robotics tools.
 */
struct FGroundTruthElevationPointCloudBuildInfo
{
	FString FrameId;

	float ResolutionMeters = 0.0f;
	uint32 Width = 0;
	uint32 Height = 0;

	double OriginX = 0.0;
	double OriginY = 0.0;
};

class FGroundTruthElevationPointCloudBuilder
{
public:
	static sensor_msgs::msg::PointCloud2 BuildElevationPointCloud(
		const FGroundTruthElevationPointCloudBuildInfo& Info,
		const TArray<float>& ElevationDataMeters);
};
