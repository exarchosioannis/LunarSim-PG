#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"
#include "Maps/GroundTruthMapFileExporter.h"

#include "nav_msgs/msg/occupancy_grid.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "builtin_interfaces/msg/time.hpp"

#include "OccupancyMapPublisherComponent.generated.h"

/*
	Usage:
	- Create an empty Blueprint actor, for example BP_GroundTruthMapPublisher.
	- Add this component to it.
	- Place the actor at the center of the area you want to map. 0 0 0
	- Tag obstacle actors/components with "MapObstacle"
	- Tag the lunar landscape/terrain actor or component with "MapTerrain"
	- Tag dynamic objects to ignore, such as the rover "MapIgnore"

	ROS output:
		/gt/map/occupancy          nav_msgs/msg/OccupancyGrid
		/gt/map/traversability     nav_msgs/msg/OccupancyGrid
		/gt/map/elevation_points   sensor_msgs/msg/PointCloud2
		frame_id                   map

	Traversability ROS values follow OccupancyGrid/cost-style semantics:
		0   = safe / low cost
		50  = risky / medium cost
		100 = blocked / lethal
   		-1  = unknown
*/
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API UOccupancyMapPublisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOccupancyMapPublisherComponent();

	UFUNCTION(BlueprintCallable, Category = "Ground Truth Map")
	void RegenerateAndPublishMap();

	UFUNCTION(BlueprintCallable, Category = "Ground Truth Map")
	bool ExportMapToDirectory(const FString& MapsDirectory, const FString& BaseFileName = TEXT("occupancy_map"));

	UFUNCTION(BlueprintCallable, Category = "Ground Truth Map")
	bool ExportMapToDefaultDatasetDirectory();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetupRos();
	void GenerateOccupancyMap();
	void ComputeSlopeMap();
	void ComputeTraversabilityMap();
	void BuildElevationPointCloud();
	void PublishMap();
	builtin_interfaces::msg::Time ToRosTime(double Seconds) const;

	FVector RosMapToUnrealWorldCm(double RosX_m, double RosY_m) const;
	int8 ClassifyCell(double CellCenterRosX_m, double CellCenterRosY_m) const;
	bool SampleElevationCell(double CellCenterRosX_m, double CellCenterRosY_m, float& OutElevationMeters) const;
	bool HitHasOccupiedTag(const FHitResult& Hit) const;
	bool HitHasTerrainTag(const FHitResult& Hit) const;
	bool HitHasIgnoreTag(const FHitResult& Hit) const;

	void BuildIgnoredMapActors();
	TArray<AActor*> CachedIgnoredMapActors;

private:
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	const FString NodeName = TEXT("map_publisher");
	const FString OccupancyMapTopic = TEXT("/gt/map/occupancy");
	const FString TraversabilityMapTopic = TEXT("/gt/map/traversability");
	const FString ElevationPointCloudTopic = TEXT("/gt/map/elevation_points");
	const FString MapFrameId = TEXT("map");

	// Covers the current landscape better than the old 100m x 100m setup.
	// 220m x 220m at 0.40m/cell gives a 550 x 550 grid.
	const float ResolutionMeters = 0.40f;
	const float MapWidthMeters = 220.0f;
	const float MapHeightMeters = 220.0f;

	// Unreal uses centimeters. 2500 cm = 25m above/below the map center height.
	const float TraceStartHeightCm = 2500.0f;
	const float TraceEndDepthCm = 2500.0f;

	const ECollisionChannel TraceChannel = ECC_WorldStatic;
	const FName OccupiedTag = TEXT("MapObstacle");
	const FName TerrainTag = TEXT("MapTerrain");
	const FName IgnoreTag = TEXT("MapIgnore");

	// Republish the already-generated maps for RViz/late-subscriber stability.
	// The maps are NOT regenerated and files are NOT rewritten every 5 seconds.
	const float RepublishPeriodSeconds = 5.0f;

	// maybe needs tune later
	// The published traversability grid uses OccupancyGrid/cost-style values
	//   0   = safe / low cost
	//   50  = risky / medium cost
	//   100 = blocked / lethal
	//   -1  = unknown
	const float SafeSlopeDegrees = 15.0f;
	const float MaxTraversableSlopeDegrees = 25.0f;

	nav_msgs::msg::OccupancyGrid ReusableMapMsg;
	nav_msgs::msg::OccupancyGrid ReusableTraversabilityMapMsg;
	sensor_msgs::msg::PointCloud2 ReusableElevationPointCloudMsg;

	TArray<float> ElevationDataMeters;
	TArray<float> SlopeDataDegrees;
	TArray<int8> TraversabilityData;

	bool bMapGenerated = false;
	float PublishAccumulator = 0.0f;
	float ComputedOriginXMapMeters = 0.0f;
	float ComputedOriginYMapMeters = 0.0f;
	float TraceBaseZCm = 0.0f;
};

