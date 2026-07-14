#pragma once

#include "CoreMinimal.h"
#include "Maps/GroundTruthMapArtifacts.h"
#include "nav_msgs/msg/occupancy_grid.hpp"

/*
 * Writes the map dataset files for the ground-truth map stack.
 *
 * This exporter writes the layers used by the simplified map pipeline:
 * occupancy, elevation, and slope. Traversability is intentionally removed.
 */
struct FGroundTruthMapFileExportInfo
{
	FString MapFrameId;
	FString BaseFileName = TEXT("occupancy_map");

	const nav_msgs::msg::OccupancyGrid* OccupancyMapMsg = nullptr;
	const TArray<float>* ElevationDataMeters = nullptr;
};

class FGroundTruthMapFileExporter
{
public:
	static bool ExportToDirectory(
		const FString& MapsDirectory,
		const FGroundTruthMapFileExportInfo& Info);

private:
	static uint8 OccupancyValueToPgmPixel(int8 CellValue);
	static uint8 ElevationValueToPreviewPixel(float ElevationMeters, float MinElevationMeters, float MaxElevationMeters);
	static uint8 SlopeValueToPreviewPixel(float SlopeDegrees);

	static bool SaveMapPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);
	static bool SaveMapYaml(const FString& FilePath, const FString& ImageFileName, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);

	static bool SaveElevationCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters);
	static bool SaveElevationYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);
	static bool SaveElevationPreviewPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters);

	static bool BuildSlopeDataDegrees(const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters, TArray<float>& OutSlopeDataDegrees);
	static bool SaveSlopeCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees);
	static bool SaveSlopeYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg);
	static bool SaveSlopePreviewPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees);
};
