#pragma once

#include "CoreMinimal.h"
#include "Capture/CaptureTypes.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"

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
		/gt/map/elevation_points   sensor_msgs/msg/PointCloud2
		frame_id                   map

	The elevation layer samples the first valid vertical hit from either
	MapTerrain or MapObstacle. Actors/components tagged MapIgnore are skipped.
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
	bool TryInitializeFromRunCaptureConfig();
	bool IsGroundTruthMapsEnabledForRun() const;
	void SetupRos();
	void GenerateOccupancyMap();
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
	const FString ElevationPointCloudTopic = TEXT("/gt/map/elevation_points");
	const FString MapFrameId = TEXT("map");

	// 520m x 520m at 0.40m/cell gives a 1300 x 1300 grid.
	// With the map publisher centered at ROS (0,0), this covers [-260,+260]m.
	const float ResolutionMeters = 0.40f;
	const float MapWidthMeters = 520.0f;
	const float MapHeightMeters = 520.0f;

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


	nav_msgs::msg::OccupancyGrid ReusableMapMsg;
	sensor_msgs::msg::PointCloud2 ReusableElevationPointCloudMsg;

	TArray<float> ElevationDataMeters;

	bool bMapGenerated = false;
	bool bRunCaptureConfigResolved = false;
	FCaptureConfig RunCaptureConfig;
	float PublishAccumulator = 0.0f;
	float ComputedOriginXMapMeters = 0.0f;
	float ComputedOriginYMapMeters = 0.0f;
	float TraceBaseZCm = 0.0f;
};
