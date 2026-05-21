#pragma once

#include "CoreMinimal.h"
#include "nav_msgs/msg/occupancy_grid.hpp"

/*
 * Writes the map dataset files for the ground-truth map stack.
 *
 * This keeps all CSV / PGM / YAML export code away from
 * UOccupancyMapPublisherComponent, while preserving the same map generation
 * path and startup behavior.
 */
struct FGroundTruthMapFileExportInfo
{
	FString MapFrameId;
	FString BaseFileName = TEXT("occupancy_map");

	const nav_msgs::msg::OccupancyGrid* OccupancyMapMsg = nullptr;
	const TArray<float>* ElevationDataMeters = nullptr;
	const TArray<float>* SlopeDataDegrees = nullptr;
	const TArray<int8>* TraversabilityData = nullptr;

	float SafeSlopeDegrees = 15.0f;
	float MaxTraversableSlopeDegrees = 25.0f;
};

class FGroundTruthMapFileExporter
{
public:
	static bool ExportToDirectory(
		const FString& MapsDirectory,
		const FGroundTruthMapFileExportInfo& Info);

private:
	static FString MakeElevationBaseFileName(const FString& OccupancyBaseFileName);
	static FString MakeSlopeBaseFileName(const FString& OccupancyBaseFileName);
	static FString MakeTraversabilityBaseFileName(const FString& OccupancyBaseFileName);

	static uint8 OccupancyValueToPgmPixel(int8 CellValue);
	static uint8 ElevationValueToPreviewPixel(float ElevationMeters, float MinElevationMeters, float MaxElevationMeters);
	static uint8 SlopeValueToPreviewPixel(float SlopeDegrees, float MaxPreviewSlopeDegrees);
	static uint8 TraversabilityValueToPgmPixel(int8 TraversabilityValue);

	static bool SaveMapPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);
	static bool SaveMapYaml(const FString& FilePath, const FString& ImageFileName, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);

	static bool SaveElevationCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters);
	static bool SaveElevationYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);
	static bool SaveElevationPreviewPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters);

	static bool SaveSlopeCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees);
	static bool SaveSlopeYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);
	static bool SaveSlopePreviewPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees);

	static bool SaveTraversabilityCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<int8>& TraversabilityData);
	static bool SaveTraversabilityPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<int8>& TraversabilityData, float SafeSlopeDegrees, float MaxTraversableSlopeDegrees);
	static bool SaveTraversabilityYaml(const FString& FilePath, const FString& CsvFileName, const FString& PgmFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, float SafeSlopeDegrees, float MaxTraversableSlopeDegrees);
};
