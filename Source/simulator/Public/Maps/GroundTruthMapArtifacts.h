#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

/*
 * Canonical filenames for the run-level ground-truth map artifact set.
 *
 * Session metadata and GroundTruthMapFileExporter both use this contract so
 * the recorded paths cannot drift away from the files the exporter writes.
 */
struct SIMULATOR_API FGroundTruthMapArtifactNames
{
	FString OccupancyImage;
	FString OccupancyYaml;
	FString ElevationCsv;
	FString ElevationYaml;
	FString ElevationPreview;
	FString SlopeCsv;
	FString SlopeYaml;
	FString SlopePreview;
};

class SIMULATOR_API FGroundTruthMapArtifacts
{
public:
	static FString GetMapsDirectoryName()
	{
		return TEXT("Maps");
	}

	static FString GetDefaultOccupancyBaseFileName()
	{
		return TEXT("occupancy_map");
	}

	static FGroundTruthMapArtifactNames MakeNames(const FString& OccupancyBaseFileName)
	{
		const FString SafeOccupancyBaseFileName = OccupancyBaseFileName.IsEmpty()
			? GetDefaultOccupancyBaseFileName()
			: OccupancyBaseFileName;
		const FString ElevationBaseFileName = SafeOccupancyBaseFileName.Equals(
			GetDefaultOccupancyBaseFileName(), ESearchCase::IgnoreCase)
			? FString(TEXT("elevation_map"))
			: SafeOccupancyBaseFileName + TEXT("_elevation");
		const FString SlopeBaseFileName = SafeOccupancyBaseFileName.Equals(
			GetDefaultOccupancyBaseFileName(), ESearchCase::IgnoreCase)
			? FString(TEXT("slope_map"))
			: SafeOccupancyBaseFileName + TEXT("_slope");

		FGroundTruthMapArtifactNames Names;
		Names.OccupancyImage = SafeOccupancyBaseFileName + TEXT(".pgm");
		Names.OccupancyYaml = SafeOccupancyBaseFileName + TEXT(".yaml");
		Names.ElevationCsv = ElevationBaseFileName + TEXT(".csv");
		Names.ElevationYaml = ElevationBaseFileName + TEXT(".yaml");
		Names.ElevationPreview = ElevationBaseFileName + TEXT("_preview.pgm");
		Names.SlopeCsv = SlopeBaseFileName + TEXT(".csv");
		Names.SlopeYaml = SlopeBaseFileName + TEXT(".yaml");
		Names.SlopePreview = SlopeBaseFileName + TEXT("_preview.pgm");
		return Names;
	}

	// Metadata paths are relative to the dataset-run directory, not Session_NNN.
	static FString MakeDatasetRunRelativePath(const FString& FileName)
	{
		FString Path = FPaths::Combine(GetMapsDirectoryName(), FileName);
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Path;
	}
};
