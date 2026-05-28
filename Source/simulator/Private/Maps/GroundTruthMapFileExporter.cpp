#include "Maps/GroundTruthMapFileExporter.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

bool FGroundTruthMapFileExporter::ExportToDirectory(const FString& MapsDirectory, const FGroundTruthMapFileExportInfo& Info)
{
	if (MapsDirectory.IsEmpty() || !Info.OccupancyMapMsg || !Info.ElevationDataMeters) {
		UE_LOG(LogTemp, Warning, TEXT("GroundTruthMapFileExporter: missing export input."));
		return false;
	}

	const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg = *Info.OccupancyMapMsg;
	const TArray<float>& ElevationDataMeters = *Info.ElevationDataMeters;

	if (OccupancyMapMsg.info.width == 0 || OccupancyMapMsg.info.height == 0 || OccupancyMapMsg.data.empty()) {
		UE_LOG(LogTemp, Warning, TEXT("GroundTruthMapFileExporter: occupancy map is empty."));
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*MapsDirectory)) {
		PlatformFile.CreateDirectoryTree(*MapsDirectory);
	}

	const FString SafeOccupancyBaseFileName = Info.BaseFileName.IsEmpty() ? FString(TEXT("occupancy_map")) : Info.BaseFileName;
	const FString SafeElevationBaseFileName = MakeElevationBaseFileName(SafeOccupancyBaseFileName);
	const FString SafeSlopeBaseFileName = MakeSlopeBaseFileName(SafeOccupancyBaseFileName);

	const FString OccupancyPgmFileName = SafeOccupancyBaseFileName + TEXT(".pgm");
	const FString OccupancyYamlFileName = SafeOccupancyBaseFileName + TEXT(".yaml");

	const FString ElevationCsvFileName = SafeElevationBaseFileName + TEXT(".csv");
	const FString ElevationYamlFileName = SafeElevationBaseFileName + TEXT(".yaml");
	const FString ElevationPreviewFileName = SafeElevationBaseFileName + TEXT("_preview.pgm");

	const FString SlopeCsvFileName = SafeSlopeBaseFileName + TEXT(".csv");
	const FString SlopeYamlFileName = SafeSlopeBaseFileName + TEXT(".yaml");
	const FString SlopePreviewFileName = SafeSlopeBaseFileName + TEXT("_preview.pgm");

	const FString OccupancyPgmPath = FPaths::Combine(MapsDirectory, OccupancyPgmFileName);
	const FString OccupancyYamlPath = FPaths::Combine(MapsDirectory, OccupancyYamlFileName);

	const FString ElevationCsvPath = FPaths::Combine(MapsDirectory, ElevationCsvFileName);
	const FString ElevationYamlPath = FPaths::Combine(MapsDirectory, ElevationYamlFileName);
	const FString ElevationPreviewPath = FPaths::Combine(MapsDirectory, ElevationPreviewFileName);

	const FString SlopeCsvPath = FPaths::Combine(MapsDirectory, SlopeCsvFileName);
	const FString SlopeYamlPath = FPaths::Combine(MapsDirectory, SlopeYamlFileName);
	const FString SlopePreviewPath = FPaths::Combine(MapsDirectory, SlopePreviewFileName);

	TArray<float> SlopeDataDegrees;
	const bool bSlopeBuildOk = BuildSlopeDataDegrees(OccupancyMapMsg, ElevationDataMeters, SlopeDataDegrees);

	const bool bOccupancyPgmOk = SaveMapPgm(OccupancyPgmPath, OccupancyMapMsg);
	const bool bOccupancyYamlOk = SaveMapYaml(OccupancyYamlPath, OccupancyPgmFileName, OccupancyMapMsg);

	const bool bElevationCsvOk = SaveElevationCsv(ElevationCsvPath, OccupancyMapMsg, ElevationDataMeters);
	const bool bElevationPreviewOk = SaveElevationPreviewPgm(ElevationPreviewPath, OccupancyMapMsg, ElevationDataMeters);
	const bool bElevationYamlOk = SaveElevationYaml(ElevationYamlPath, ElevationCsvFileName, ElevationPreviewFileName, Info.MapFrameId, OccupancyMapMsg);

	const bool bSlopeCsvOk = bSlopeBuildOk && SaveSlopeCsv(SlopeCsvPath, OccupancyMapMsg, SlopeDataDegrees);
	const bool bSlopePreviewOk = bSlopeBuildOk && SaveSlopePreviewPgm(SlopePreviewPath, OccupancyMapMsg, SlopeDataDegrees);
	const bool bSlopeYamlOk = bSlopeBuildOk && SaveSlopeYaml(SlopeYamlPath, SlopeCsvFileName, SlopePreviewFileName, Info.MapFrameId, OccupancyMapMsg);

	if (bOccupancyPgmOk && bOccupancyYamlOk && bElevationCsvOk && bElevationPreviewOk && bElevationYamlOk && bSlopeBuildOk && bSlopeCsvOk && bSlopePreviewOk && bSlopeYamlOk) {
		UE_LOG(LogTemp, Log, TEXT("GroundTruthMapFileExporter: exported occupancy, elevation, and slope map files to %s"), *MapsDirectory);
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("GroundTruthMapFileExporter: failed to export one or more map files to %s"), *MapsDirectory);
	return false;
}

FString FGroundTruthMapFileExporter::MakeElevationBaseFileName(const FString& OccupancyBaseFileName)
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))  return TEXT("elevation_map");
	return OccupancyBaseFileName + TEXT("_elevation");
}

FString FGroundTruthMapFileExporter::MakeSlopeBaseFileName(const FString& OccupancyBaseFileName)
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))  return TEXT("slope_map");
	return OccupancyBaseFileName + TEXT("_slope");
}

uint8 FGroundTruthMapFileExporter::OccupancyValueToPgmPixel(int8 CellValue)
{
	if (CellValue < 0) return 205; // unknown = gray
	if (CellValue >= 65) return 0; // occupied = black
	return 254; // free = white
}

uint8 FGroundTruthMapFileExporter::ElevationValueToPreviewPixel(float ElevationMeters, float MinElevationMeters, float MaxElevationMeters)
{
	if (FMath::IsNaN(ElevationMeters)) return 205; // unknown = gray

	const float Range = MaxElevationMeters - MinElevationMeters;
	if (Range <= KINDA_SMALL_NUMBER) return 127; // almost flat map
	
	const float Normalized = FMath::Clamp((ElevationMeters - MinElevationMeters) / Range, 0.0f, 1.0f);
	return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
}

uint8 FGroundTruthMapFileExporter::SlopeValueToPreviewPixel(float SlopeDegrees)
{
	if (FMath::IsNaN(SlopeDegrees)) return 205; // unknown = gray

	constexpr float MaxVisualizationSlopeDegrees = 45.0f;
	const float Normalized = FMath::Clamp(SlopeDegrees / MaxVisualizationSlopeDegrees, 0.0f, 1.0f);
	return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
}

bool FGroundTruthMapFileExporter::SaveMapPgm(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || OccupancyMapMsg.data.size() != static_cast<size_t>(Width * Height)) return false;
	
	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(TEXT("P5\n# Generated by GroundTruthMapFileExporter\n%d %d\n255\n"), Width, Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY) {
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X) {
			const int32 Index = MapY * Width + X;
			FileBytes.Add(OccupancyValueToPgmPixel(OccupancyMapMsg.data[Index]));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool FGroundTruthMapFileExporter::BuildSlopeDataDegrees(const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& ElevationDataMeters, TArray<float>& OutSlopeDataDegrees)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);
	const float ResolutionMeters = OccupancyMapMsg.info.resolution;

	if (Width <= 0 || Height <= 0 || ResolutionMeters <= 0.0f || ElevationDataMeters.Num() != Width * Height) return false;

	OutSlopeDataDegrees.Empty(Width * Height);
	OutSlopeDataDegrees.SetNum(Width * Height);

	for (int32 Index = 0; Index < OutSlopeDataDegrees.Num(); ++Index) {
		OutSlopeDataDegrees[Index] = NAN;
	}

	if (Width < 2 || Height < 2) return true;

	for (int32 Y = 0; Y < Height; ++Y) {
		for (int32 X = 0; X < Width; ++X) {
			const int32 Index = Y * Width + X;
			const float Center = ElevationDataMeters[Index];

			if (FMath::IsNaN(Center)) continue;

			float DzDx = NAN;
			if (X == 0) {
				const float Right = ElevationDataMeters[Index + 1];
				if (FMath::IsNaN(Right)) continue;
				DzDx = (Right - Center) / ResolutionMeters;
			} else if (X == Width - 1) {
				const float Left = ElevationDataMeters[Index - 1];
				if (FMath::IsNaN(Left)) continue;
				DzDx = (Center - Left) / ResolutionMeters;
			} else {
				const float Left = ElevationDataMeters[Index - 1];
				const float Right = ElevationDataMeters[Index + 1];
				if (FMath::IsNaN(Left) || FMath::IsNaN(Right)) continue;
				DzDx = (Right - Left) / (2.0f * ResolutionMeters);
			}

			float DzDy = NAN;
			if (Y == 0) {
				const float Up = ElevationDataMeters[Index + Width];
				if (FMath::IsNaN(Up)) continue;
				DzDy = (Up - Center) / ResolutionMeters;
			} else if (Y == Height - 1) {
				const float Down = ElevationDataMeters[Index - Width];
				if (FMath::IsNaN(Down)) continue;
				DzDy = (Center - Down) / ResolutionMeters;
			} else {
				const float Down = ElevationDataMeters[Index - Width];
				const float Up = ElevationDataMeters[Index + Width];
				if (FMath::IsNaN(Down) || FMath::IsNaN(Up)) continue;
				DzDy = (Up - Down) / (2.0f * ResolutionMeters);
			}

			const float SlopeRadians = FMath::Atan(FMath::Sqrt(DzDx * DzDx + DzDy * DzDy));
			OutSlopeDataDegrees[Index] = FMath::RadiansToDegrees(SlopeRadians);
		}
	}

	return true;
}

bool FGroundTruthMapFileExporter::SaveSlopeCsv(const FString& FilePath, const nav_msgs::msg::OccupancyGrid& OccupancyMapMsg, const TArray<float>& SlopeDataDegrees)
{
	const int32 Width = static_cast<int32>(OccupancyMapMsg.info.width);
	const int32 Height = static_cast<int32>(OccupancyMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || SlopeDataDegrees.Num() != Width * Height) return false;

	FString Csv;
	Csv.Reserve(Width * Height * 8);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY) {
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X) {
			const int32 Index = MapY * Width + X;
			const float SlopeDegrees = SlopeDataDegrees[Index];
			Csv += FMath::IsNaN(SlopeDegrees) ? TEXT("nan") : FString::Printf(TEXT("%.6f"), SlopeDegrees);
			if (X + 1 < Width) Csv += TEXT(",");
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
		TEXT("row_order: top_to_bottom\n")
		TEXT("derived_from: elevation_map\n"),
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

	if (Width <= 0 || Height <= 0 || SlopeDataDegrees.Num() != Width * Height) return false;

	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(
		TEXT("P5\n# Slope preview generated by GroundTruthMapFileExporter\n# 0 degrees black, 45 degrees max visualization slope white\n%d %d\n255\n"),
		Width, Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY) {
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X) {
			const int32 Index = MapY * Width + X;
			FileBytes.Add(SlopeValueToPreviewPixel(SlopeDataDegrees[Index]));
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

	if (Width <= 0 || Height <= 0 || ElevationDataMeters.Num() != Width * Height) return false;
	FString Csv;
	Csv.Reserve(Width * Height * 8);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY) {
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X) {
			const int32 Index = MapY * Width + X;
			const float ElevationMeters = ElevationDataMeters[Index];
			Csv += FMath::IsNaN(ElevationMeters) ? TEXT("nan") : FString::Printf(TEXT("%.6f"), ElevationMeters);
			if (X + 1 < Width) Csv += TEXT(",");
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

	if (Width <= 0 || Height <= 0 || ElevationDataMeters.Num() != Width * Height) return false;

	bool bFoundValidElevation = false;
	float MinElevationMeters = TNumericLimits<float>::Max();
	float MaxElevationMeters = TNumericLimits<float>::Lowest();

	for (const float Value : ElevationDataMeters) {
		if (FMath::IsNaN(Value)) continue;
		
		MinElevationMeters = FMath::Min(MinElevationMeters, Value);
		MaxElevationMeters = FMath::Max(MaxElevationMeters, Value);
		bFoundValidElevation = true;
	}

	if (!bFoundValidElevation) return false;
	

	TArray<uint8> FileBytes;
	const FString Header = FString::Printf(
		TEXT("P5\n# Elevation preview generated by GroundTruthMapFileExporter\n# min_m %.6f max_m %.6f\n%d %d\n255\n"),
		MinElevationMeters, MaxElevationMeters,
		Width, Height);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	for (int32 ImageY = 0; ImageY < Height; ++ImageY) {
		const int32 MapY = Height - 1 - ImageY;
		for (int32 X = 0; X < Width; ++X) {
			const int32 Index = MapY * Width + X;
			FileBytes.Add(ElevationValueToPreviewPixel(ElevationDataMeters[Index], MinElevationMeters, MaxElevationMeters));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}
