#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"

#include "nav_msgs/msg/occupancy_grid.hpp"
#include "builtin_interfaces/msg/time.hpp"

#include "OccupancyMapPublisherComponent.generated.h"

/*
 * Publishes a static global ground-truth 2D occupancy map from the Unreal level
 * and exports occupancy, elevation, slope, and traversability map files when the level starts.
 *
 * Suggested usage:
 *   - Create an empty Blueprint actor, for example BP_GroundTruthMapPublisher.
 *   - Add this component to it.
 *   - Place the actor at the center of the area you want to map.
 *   - Tag obstacle actors/components with MapObstacle.
 *
 * ROS output:
 *   /gt/map/occupancy    nav_msgs/msg/OccupancyGrid
 *   frame_id             map
 *
 * Disk export:
 *   Saved/Datasets/<run_timestamp>/Maps/occupancy_map.pgm
 *   Saved/Datasets/<run_timestamp>/Maps/occupancy_map.yaml
 *   Saved/Datasets/<run_timestamp>/Maps/elevation_map.csv
 *   Saved/Datasets/<run_timestamp>/Maps/elevation_map.yaml
 *   Saved/Datasets/<run_timestamp>/Maps/elevation_map_preview.pgm
 *   Saved/Datasets/<run_timestamp>/Maps/slope_map.csv
 *   Saved/Datasets/<run_timestamp>/Maps/slope_map.yaml
 *   Saved/Datasets/<run_timestamp>/Maps/slope_map_preview.pgm
 *   Saved/Datasets/<run_timestamp>/Maps/traversability_map.csv
 *   Saved/Datasets/<run_timestamp>/Maps/traversability_map.pgm
 *   Saved/Datasets/<run_timestamp>/Maps/traversability_map.yaml
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
	void PublishMap();
	builtin_interfaces::msg::Time ToRosTime(double Seconds) const;

	FVector RosMapToUnrealWorldCm(double RosX_m, double RosY_m) const;
	int8 ClassifyCell(double CellCenterRosX_m, double CellCenterRosY_m) const;
	bool SampleElevationCell(double CellCenterRosX_m, double CellCenterRosY_m, float& OutElevationMeters) const;
	bool HitHasOccupiedTag(const FHitResult& Hit) const;

	bool SaveMapPgm(const FString& FilePath) const;
	bool SaveMapYaml(const FString& FilePath, const FString& ImageFileName) const;

	bool SaveElevationCsv(const FString& FilePath) const;
	bool SaveElevationYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName) const;
	bool SaveElevationPreviewPgm(const FString& FilePath) const;

	bool SaveSlopeCsv(const FString& FilePath) const;
	bool SaveSlopeYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName) const;
	bool SaveSlopePreviewPgm(const FString& FilePath) const;

	bool SaveTraversabilityCsv(const FString& FilePath) const;
	bool SaveTraversabilityPgm(const FString& FilePath) const;
	bool SaveTraversabilityYaml(const FString& FilePath, const FString& CsvFileName, const FString& PgmFileName) const;

	uint8 OccupancyValueToPgmPixel(int8 CellValue) const;
	uint8 ElevationValueToPreviewPixel(float ElevationMeters, float MinElevationMeters, float MaxElevationMeters) const;
	uint8 SlopeValueToPreviewPixel(float SlopeDegrees, float MaxPreviewSlopeDegrees) const;
	uint8 TraversabilityValueToPgmPixel(int8 TraversabilityValue) const;
	FString MakeElevationBaseFileName(const FString& OccupancyBaseFileName) const;
	FString MakeSlopeBaseFileName(const FString& OccupancyBaseFileName) const;
	FString MakeTraversabilityBaseFileName(const FString& OccupancyBaseFileName) const;

private:
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	const FString NodeName = TEXT("occupancy_map_publisher");
	const FString MapTopic = TEXT("/gt/map/occupancy");
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

	// Republish the already-generated occupancy map for RViz/late-subscriber stability.
	// The map is NOT regenerated and files are NOT rewritten every 5 seconds.
	const float RepublishPeriodSeconds = 5.0f;

	// Simple first-version traversability thresholds for rover terrain safety.
	// These are intentionally conservative and can be tuned later for the real rover limits.
	const float SafeSlopeDegrees = 15.0f;
	const float MaxTraversableSlopeDegrees = 25.0f;

	nav_msgs::msg::OccupancyGrid ReusableMapMsg;
	TArray<float> ElevationDataMeters;
	TArray<float> SlopeDataDegrees;
	TArray<int8> TraversabilityData;

	bool bMapGenerated = false;
	float PublishAccumulator = 0.0f;
	float ComputedOriginXMapMeters = 0.0f;
	float ComputedOriginYMapMeters = 0.0f;
	float TraceBaseZCm = 0.0f;
};

