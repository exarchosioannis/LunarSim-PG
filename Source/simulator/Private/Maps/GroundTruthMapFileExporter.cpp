#include "Maps/GroundTruthMapFileExporter.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

bool FGroundTruthMapFileExporter::ExportToDirectory(
	const FString& MapsDirectory,
	const FGroundTruthMapFileExportInfo& Info)
{
	if (MapsDirectory.IsEmpty() || !Info.OccupancyMapMsg || !Info.ElevationDataMeters || !Info.SlopeDataDegrees || !Info.TraversabilityData)
	{
		UE_LOG(LogTemp, Warning, TEXT("GroundTruthMapFileExporter: missing export input."));
		return false;
	}

	const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg = *Info.OccupancyMapMsg;
	const TArray<float>& ElevationDataMeters = *Info.ElevationDataMeters;
	const TArray<float>& SlopeDataDegrees = *Info.SlopeDataDegrees;
	const TArray<int8>& TraversabilityData = *Info.TraversabilityData;

	if (OccupancyMapMsg.info.width == 0 || OccupancyMapMsg.info.height == 0 || OccupancyMapMsg.data.empty())
	{
		UE_LOG(LogTemp, Warning, TEXT("GroundTruthMapFileExporter: occupancy map is empty."));
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*MapsDirectory))
	{
		PlatformFile.CreateDirectoryTree(*MapsDirectory);
	}

	const FString SafeOccupancyBaseFileName = Info.BaseFileName.IsEmpty() ? FString(TEXT("occupancy_map")) : Info.BaseFileName;
	const FString SafeElevationBaseFileName = MakeElevationBaseFileName(SafeOccupancyBaseFileName);
	const FString SafeSlopeBaseFileName = MakeSlopeBaseFileName(SafeOccupancyBaseFileName);
	const FString SafeTraversabilityBaseFileName = MakeTraversabilityBaseFileName(SafeOccupancyBaseFileName);

	const FString OccupancyPgmFileName = SafeOccupancyBaseFileName + TEXT(".pgm");
	const FString OccupancyYamlFileName = SafeOccupancyBaseFileName + TEXT(".yaml");

	const FString ElevationCsvFileName = SafeElevationBaseFileName + TEXT(".csv");
	const FString ElevationYamlFileName = SafeElevationBaseFileName + TEXT(".yaml");
	const FString ElevationPreviewFileName = SafeElevationBaseFileName + TEXT("_preview.pgm");

	const FString SlopeCsvFileName = SafeSlopeBaseFileName + TEXT(".csv");
	const FString SlopeYamlFileName = SafeSlopeBaseFileName + TEXT(".yaml");
	const FString SlopePreviewFileName = SafeSlopeBaseFileName + TEXT("_preview.pgm");

	const FString TraversabilityCsvFileName = SafeTraversabilityBaseFileName + TEXT(".csv");
	const FString TraversabilityPgmFileName = SafeTraversabilityBaseFileName + TEXT(".pgm");
	const FString TraversabilityYamlFileName = SafeTraversabilityBaseFileName + TEXT(".yaml");

	const FString OccupancyPgmPath = FPaths::Combine(MapsDirectory, OccupancyPgmFileName);
	const FString OccupancyYamlPath = FPaths::Combine(MapsDirectory, OccupancyYamlFileName);

	const FString ElevationCsvPath = FPaths::Combine(MapsDirectory, ElevationCsvFileName);
	const FString ElevationYamlPath = FPaths::Combine(MapsDirectory, ElevationYamlFileName);
	const FString ElevationPreviewPath = FPaths::Combine(MapsDirectory, ElevationPreviewFileName);

	const FString SlopeCsvPath = FPaths::Combine(MapsDirectory, SlopeCsvFileName);
	const FString SlopeYamlPath = FPaths::Combine(MapsDirectory, SlopeYamlFileName);
	const FString SlopePreviewPath = FPaths::Combine(MapsDirectory, SlopePreviewFileName);

	const FString TraversabilityCsvPath = FPaths::Combine(MapsDirectory, TraversabilityCsvFileName);
	const FString TraversabilityPgmPath = FPaths::Combine(MapsDirectory, TraversabilityPgmFileName);
	const FString TraversabilityYamlPath = FPaths::Combine(MapsDirectory, TraversabilityYamlFileName);

	const bool bOccupancyPgmOk = SaveMapPgm(OccupancyPgmPath, OccupancyMapMsg);
	const bool bOccupancyYamlOk = SaveMapYaml(OccupancyYamlPath, OccupancyPgmFileName, OccupancyMapMsg);

	const bool bElevationCsvOk = SaveElevationCsv(ElevationCsvPath, OccupancyMapMsg, ElevationDataMeters);
	const bool bElevationPreviewOk = SaveElevationPreviewPgm(ElevationPreviewPath, OccupancyMapMsg, ElevationDataMeters);
	const bool bElevationYamlOk = SaveElevationYaml(ElevationYamlPath, ElevationCsvFileName, ElevationPreviewFileName, Info.MapFrameId, OccupancyMapMsg);

	const bool bSlopeCsvOk = SaveSlopeCsv(SlopeCsvPath, OccupancyMapMsg, SlopeDataDegrees);
	const bool bSlopePreviewOk = SaveSlopePreviewPgm(SlopePreviewPath, OccupancyMapMsg, SlopeDataDegrees);
	const bool bSlopeYamlOk = SaveSlopeYaml(SlopeYamlPath, SlopeCsvFileName, SlopePreviewFileName, Info.MapFrameId, OccupancyMapMsg);

	const bool bTraversabilityCsvOk = SaveTraversabilityCsv(TraversabilityCsvPath, OccupancyMapMsg, TraversabilityData);
	const bool bTraversabilityPgmOk = SaveTraversabilityPgm(TraversabilityPgmPath, OccupancyMapMsg, TraversabilityData, Info.SafeSlopeDegrees, Info.MaxTraversableSlopeDegrees);
	const bool bTraversabilityYamlOk = SaveTraversabilityYaml(TraversabilityYamlPath, TraversabilityCsvFileName, TraversabilityPgmFileName, Info.MapFrameId, OccupancyMapMsg, Info.SafeSlopeDegrees, Info.MaxTraversableSlopeDegrees);


	if (bOccupancyPgmOk && bOccupancyYamlOk && bElevationCsvOk && bElevationPreviewOk && bElevationYamlOk && bSlopeCsvOk && bSlopePreviewOk && bSlopeYamlOk && bTraversabilityCsvOk && bTraversabilityPgmOk && bTraversabilityYamlOk)
	{
		UE_LOG(LogTemp, Log, TEXT("GroundTruthMapFileExporter: exported required map files to %s"), *MapsDirectory);
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("GroundTruthMapFileExporter: failed to export one or more map files to %s"), *MapsDirectory);
	return false;
}

FString FGroundTruthMapFileExporter::MakeElevationBaseFileName(const FString& OccupancyBaseFileName)
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))
	{
		return TEXT("elevation_map");
	}

	return OccupancyBaseFileName + TEXT("_elevation");
}

FString FGroundTruthMapFileExporter::MakeSlopeBaseFileName(const FString& OccupancyBaseFileName)
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))
	{
		return TEXT("slope_map");
	}

	return OccupancyBaseFileName + TEXT("_slope");
}

FString FGroundTruthMapFileExporter::MakeTraversabilityBaseFileName(const FString& OccupancyBaseFileName)
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))
	{
		return TEXT("traversability_map");
	}

	return OccupancyBaseFileName + TEXT("_traversability");
}

uint8 FGroundTruthMapFileExporter::OccupancyValueToPgmPixel(int8 CellValue)
{
	if (CellValue < 0)
	{
		return 205; // unknown = gray
	}

	if (CellValue >= 65)
	{
		return 0; // occupied = black
	}

	return 254; // free = white
}

uint8 FGroundTruthMapFileExporter::ElevationValueToPreviewPixel(float ElevationMeters, float MinElevationMeters, float MaxElevationMeters)
{
	if (FMath::IsNaN(ElevationMeters))
	{
		return 205; // unknown = gray
	}

	const float Range = MaxElevationMeters - MinElevationMeters;
	if (Range <= KINDA_SMALL_NUMBER)
	{
		return 127; // almost flat map
	}

	const float Normalized = FMath::Clamp((ElevationMeters - MinElevationMeters) / Range, 0.0f, 1.0f);
	return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
}

uint8 FGroundTruthMapFileExporter::SlopeValueToPreviewPixel(float SlopeDegrees, float MaxPreviewSlopeDegrees)
{
	if (FMath::IsNaN(SlopeDegrees))
	{
		return 205; // unknown = gray
	}

	const float SafeMaxSlope = FMath::Max(1.0f, MaxPreviewSlopeDegrees);
	const float Normalized = FMath::Clamp(SlopeDegrees / SafeMaxSlope, 0.0f, 1.0f);
	return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
}

uint8 FGroundTruthMapFileExporter::TraversabilityValueToPgmPixel(int8 TraversabilityValue)
{
	if (TraversabilityValue < 0)
	{
		return 205; // unknown = gray
	}

	if (TraversabilityValue >= 100)
	{
		return 0; // blocked / lethal = black
	}

	if (TraversabilityValue > 0)
	{
		return 127; // risky / medium cost = medium gray
	}

	return 254; // safe / low cost = white
}

bool FGroundTruthMapFileExporter::SaveMapPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || OccupancyMapMsg.data.size() != static_cast<size_t>(Width * Height))
	{
		return false;
	}

	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(TEXT("P5\n# Generated by GroundTruthMapFileExporter\n%d %d\n255\n"), Width, Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			FileBytes.Add(OccupancyValueToPgmPixel(OccupancyMapMsg.data[Index]));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool FGroundTruthMapFileExporter::SaveMapYaml(const FString& FilePath, const FString& ImageFileName, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg)
{
	const FString Yaml = FString::Printf(
		TEXT("image: %s\n")
		TEXT("mode: trinary\n")
		TEXT("resolution: %.9f\n")
		TEXT("origin: [%.9f, %.9f, 0.0]\n")
		TEXT("negate: 0\n")
		TEXT("occupied_thresh: 0.65\n")
		TEXT("free_thresh: 0.196\n"),
		*ImageFileName,
		OccupancyMapMsg.info.resolution,
		OccupancyMapMsg.info.origin.position.x,
		OccupancyMapMsg.info.origin.position.y);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FGroundTruthMapFileExporter::SaveElevationCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || ElevationDataMeters.Num() != Width * Height)
	{
		return false;
	}

	FString Csv;
	Csv.Reserve(Width * Height * 8);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;

		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			const float ElevationMeters = ElevationDataMeters[Index];

			Csv += FMath::IsNaN(ElevationMeters) ? TEXT("nan") : FString::Printf(TEXT("%.6f"), ElevationMeters);

			if (X + 1 < Width)
			{
				Csv += TEXT(",");
			}
		}

		Csv += TEXT("\n");
	}

	return FFileHelper::SaveStringToFile(Csv, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FGroundTruthMapFileExporter::SaveElevationYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg)
{
	const FString Yaml = FString::Printf(
		TEXT("type: elevation_map\n")
		TEXT("data: %s\n")
		TEXT("preview: %s\n")
		TEXT("frame_id: %s\n")
		TEXT("resolution: %.9f\n")
		TEXT("width: %u\n")
		TEXT("height: %u\n")
		TEXT("origin: [%.9f, %.9f, 0.0]\n")
		TEXT("unit: meters\n")
		TEXT("unknown_value: nan\n")
		TEXT("row_order: top_to_bottom\n"),
		*CsvFileName,
		*PreviewFileName,
		*MapFrameId,
		OccupancyMapMsg.info.resolution,
		OccupancyMapMsg.info.width,
		OccupancyMapMsg.info.height,
		OccupancyMapMsg.info.origin.position.x,
		OccupancyMapMsg.info.origin.position.y);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FGroundTruthMapFileExporter::SaveElevationPreviewPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || ElevationDataMeters.Num() != Width * Height)
	{
		return false;
	}

	bool bFoundValidElevation = false;
	float MinElevationMeters = TNumericLimits<float>::Max();
	float MaxElevationMeters = TNumericLimits<float>::Lowest();

	for (const float Value : ElevationDataMeters)
	{
		if (FMath::IsNaN(Value))
		{
			continue;
		}

		MinElevationMeters = FMath::Min(MinElevationMeters, Value);
		MaxElevationMeters = FMath::Max(MaxElevationMeters, Value);
		bFoundValidElevation = true;
	}

	if (!bFoundValidElevation)
	{
		return false;
	}

	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(
		TEXT("P5\n# Elevation preview generated by GroundTruthMapFileExporter\n# min_m %.6f max_m %.6f\n%d %d\n255\n"),
		MinElevationMeters,
		MaxElevationMeters,
		Width,
		Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			FileBytes.Add(ElevationValueToPreviewPixel(ElevationDataMeters[Index], MinElevationMeters, MaxElevationMeters));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool FGroundTruthMapFileExporter::SaveSlopeCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || SlopeDataDegrees.Num() != Width * Height)
	{
		return false;
	}

	FString Csv;
	Csv.Reserve(Width * Height * 8);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			const float SlopeDegrees = SlopeDataDegrees[Index];
			Csv += FMath::IsNaN(SlopeDegrees) ? TEXT("nan") : FString::Printf(TEXT("%.6f"), SlopeDegrees);

			if (X + 1 < Width)
			{
				Csv += TEXT(",");
			}
		}

		Csv += TEXT("\n");
	}

	return FFileHelper::SaveStringToFile(Csv, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FGroundTruthMapFileExporter::SaveSlopeYaml(const FString& FilePath, const FString& CsvFileName, const FString& PreviewFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg)
{
	const FString Yaml = FString::Printf(
		TEXT("type: slope_map\n")
		TEXT("data: %s\n")
		TEXT("preview: %s\n")
		TEXT("frame_id: %s\n")
		TEXT("resolution: %.9f\n")
		TEXT("width: %u\n")
		TEXT("height: %u\n")
		TEXT("origin: [%.9f, %.9f, 0.0]\n")
		TEXT("unit: degrees\n")
		TEXT("unknown_value: nan\n")
		TEXT("row_order: top_to_bottom\n"),
		*CsvFileName,
		*PreviewFileName,
		*MapFrameId,
		OccupancyMapMsg.info.resolution,
		OccupancyMapMsg.info.width,
		OccupancyMapMsg.info.height,
		OccupancyMapMsg.info.origin.position.x,
		OccupancyMapMsg.info.origin.position.y);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FGroundTruthMapFileExporter::SaveSlopePreviewPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || SlopeDataDegrees.Num() != Width * Height)
	{
		return false;
	}

	bool bFoundValidSlope = false;
	float MaxSlopeDegrees = 0.0f;

	for (const float Value : SlopeDataDegrees)
	{
		if (FMath::IsNaN(Value))
		{
			continue;
		}

		MaxSlopeDegrees = FMath::Max(MaxSlopeDegrees, Value);
		bFoundValidSlope = true;
	}

	if (!bFoundValidSlope)
	{
		return false;
	}

	const float PreviewMaxSlopeDegrees = 45.0f;
	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(
		TEXT("P5\n# Slope preview generated by GroundTruthMapFileExporter\n# unit degrees preview_max %.6f measured_max %.6f\n%d %d\n255\n"),
		PreviewMaxSlopeDegrees,
		MaxSlopeDegrees,
		Width,
		Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			FileBytes.Add(SlopeValueToPreviewPixel(SlopeDataDegrees[Index], PreviewMaxSlopeDegrees));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool FGroundTruthMapFileExporter::SaveTraversabilityCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<int8>& TraversabilityData)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || TraversabilityData.Num() != Width * Height)
	{
		return false;
	}

	FString Csv;
	Csv.Reserve(Width * Height * 4);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			Csv += FString::Printf(TEXT("%d"), TraversabilityData[Index]);

			if (X + 1 < Width)
			{
				Csv += TEXT(",");
			}
		}

		Csv += TEXT("\n");
	}

	return FFileHelper::SaveStringToFile(Csv, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FGroundTruthMapFileExporter::SaveTraversabilityPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<int8>& TraversabilityData, float SafeSlopeDegrees, float MaxTraversableSlopeDegrees)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || TraversabilityData.Num() != Width * Height)
	{
		return false;
	}

	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(
		TEXT("P5\n# Traversability map generated by GroundTruthMapFileExporter\n# values safe=0 risky=50 blocked=100 unknown=-1\n# safe_slope_deg %.6f max_traversable_slope_deg %.6f\n%d %d\n255\n"),
		SafeSlopeDegrees,
		MaxTraversableSlopeDegrees,
		Width,
		Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			FileBytes.Add(TraversabilityValueToPgmPixel(TraversabilityData[Index]));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool FGroundTruthMapFileExporter::SaveTraversabilityYaml(const FString& FilePath, const FString& CsvFileName, const FString& PgmFileName, const FString& MapFrameId, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, float SafeSlopeDegrees, float MaxTraversableSlopeDegrees)
{
	const FString Yaml = FString::Printf(
		TEXT("type: traversability_map\n")
		TEXT("data: %s\n")
		TEXT("image: %s\n")
		TEXT("frame_id: %s\n")
		TEXT("resolution: %.9f\n")
		TEXT("width: %u\n")
		TEXT("height: %u\n")
		TEXT("origin: [%.9f, %.9f, 0.0]\n")
		TEXT("unit: score\n")
		TEXT("row_order: top_to_bottom\n")
		TEXT("values:\n")
		TEXT("  safe: 0\n")
		TEXT("  risky: 50\n")
		TEXT("  blocked: 100\n")
		TEXT("  unknown: -1\n")
		TEXT("rules:\n")
		TEXT("  occupied_threshold: 65\n")
		TEXT("  safe_slope_degrees: %.6f\n")
		TEXT("  max_traversable_slope_degrees: %.6f\n"),
		*CsvFileName,
		*PgmFileName,
		*MapFrameId,
		OccupancyMapMsg.info.resolution,
		OccupancyMapMsg.info.width,
		OccupancyMapMsg.info.height,
		OccupancyMapMsg.info.origin.position.x,
		OccupancyMapMsg.info.origin.position.y,
		SafeSlopeDegrees,
		MaxTraversableSlopeDegrees);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
