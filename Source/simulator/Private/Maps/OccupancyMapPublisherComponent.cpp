#include "Maps/OccupancyMapPublisherComponent.h"

#include "TempoROSTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	FString GetDatasetRootDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Datasets")
		));
	}

	FString GetCurrentDatasetRunMarkerPath()
	{
		return FPaths::Combine(GetDatasetRootDirectory(), TEXT("current_dataset_run.txt"));
	}

	FString CreateNewBeginPlayDatasetRunDirectory()
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		const FString DatasetRootDirectory = GetDatasetRootDirectory();
		if (!PlatformFile.DirectoryExists(*DatasetRootDirectory))
		{
			PlatformFile.CreateDirectoryTree(*DatasetRootDirectory);
		}

		const FString DateString = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
		FString RunDirectory = FPaths::Combine(DatasetRootDirectory, DateString);

		int32 Suffix = 2;
		while (PlatformFile.DirectoryExists(*RunDirectory))
		{
			RunDirectory = FPaths::Combine(
				DatasetRootDirectory,
				FString::Printf(TEXT("%s_%02d"), *DateString, Suffix)
			);
			++Suffix;
		}

		PlatformFile.CreateDirectoryTree(*RunDirectory);
		FFileHelper::SaveStringToFile(
			RunDirectory,
			*GetCurrentDatasetRunMarkerPath(),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
		);

		return RunDirectory;
	}
}

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::OccupancyGrid);

UOccupancyMapPublisherComponent::UOccupancyMapPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOccupancyMapPublisherComponent::BeginPlay()
{
	Super::BeginPlay();

	SetupRos();

	// Maps are environment-level ground truth, so they are generated/exported when the level starts,
	// not when /control starts an image/trajectory capture session.
	RegenerateAndPublishMap();
	ExportMapToDefaultDatasetDirectory();
}

void UOccupancyMapPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UOccupancyMapPublisherComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ROSNode)
	{
		ROSNode->Tick(DeltaTime);
	}

	if (!bMapGenerated)
	{
		return;
	}

	PublishAccumulator += DeltaTime;

	if (PublishAccumulator >= RepublishPeriodSeconds)
	{
		PublishAccumulator = 0.0f;
		PublishMap();
	}
}

void UOccupancyMapPublisherComponent::SetupRos()
{
	if (ROSNode)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create(*NodeName, this, false);
	if (!ROSNode)
	{
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: failed to create ROS node."));
		return;
	}

	FROSQOSProfile MapQOS;
	MapQOS.CustomQueueSize(1).Reliable().TransientLocal();

	ROSNode->AddPublisher<nav_msgs::msg::OccupancyGrid>(*MapTopic, MapQOS, false);
}

builtin_interfaces::msg::Time UOccupancyMapPublisherComponent::ToRosTime(double Seconds) const
{
	builtin_interfaces::msg::Time T;

	if (Seconds < 0.0)
	{
		Seconds = 0.0;
	}

	const int64 Sec = static_cast<int64>(Seconds);
	const double Frac = Seconds - static_cast<double>(Sec);

	T.sec = static_cast<int32>(Sec);
	T.nanosec = static_cast<uint32>(
		FMath::Clamp<int64>(
			static_cast<int64>(Frac * 1000000000.0),
			0,
			999999999
		)
	);

	return T;
}

void UOccupancyMapPublisherComponent::RegenerateAndPublishMap()
{
	GenerateOccupancyMap();
	PublishMap();
}

void UOccupancyMapPublisherComponent::GenerateOccupancyMap()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World || !Owner)
	{
		return;
	}

	const float SafeResolution = FMath::Max(0.01f, ResolutionMeters);
	const int32 WidthCells = FMath::Max(1, FMath::CeilToInt(MapWidthMeters / SafeResolution));
	const int32 HeightCells = FMath::Max(1, FMath::CeilToInt(MapHeightMeters / SafeResolution));

	const FVector OwnerLocation = Owner->GetActorLocation();
	TraceBaseZCm = OwnerLocation.Z;

	// The owner actor is the center of the map area.
	// Place BP_GroundTruthMapPublisher at the center of the landscape/area you want to map.
	const double CenterXMapMeters = static_cast<double>(OwnerLocation.X) / 100.0;
	const double CenterYMapMeters = -static_cast<double>(OwnerLocation.Y) / 100.0;

	ComputedOriginXMapMeters = CenterXMapMeters - static_cast<double>(WidthCells) * SafeResolution * 0.5;
	ComputedOriginYMapMeters = CenterYMapMeters - static_cast<double>(HeightCells) * SafeResolution * 0.5;

	ReusableMapMsg.header.frame_id = TCHAR_TO_UTF8(*MapFrameId);
	ReusableMapMsg.info.resolution = SafeResolution;
	ReusableMapMsg.info.width = static_cast<uint32>(WidthCells);
	ReusableMapMsg.info.height = static_cast<uint32>(HeightCells);

	ReusableMapMsg.info.origin.position.x = ComputedOriginXMapMeters;
	ReusableMapMsg.info.origin.position.y = ComputedOriginYMapMeters;
	ReusableMapMsg.info.origin.position.z = 0.0;
	ReusableMapMsg.info.origin.orientation.x = 0.0;
	ReusableMapMsg.info.origin.orientation.y = 0.0;
	ReusableMapMsg.info.origin.orientation.z = 0.0;
	ReusableMapMsg.info.origin.orientation.w = 1.0;

	const int32 TotalCells = WidthCells * HeightCells;

	ReusableMapMsg.data.clear();
	ReusableMapMsg.data.resize(static_cast<size_t>(TotalCells), -1);

	ElevationDataMeters.Empty(TotalCells);
	ElevationDataMeters.SetNum(TotalCells);

	SlopeDataDegrees.Empty(TotalCells);
	SlopeDataDegrees.SetNum(TotalCells);

	TraversabilityData.Empty(TotalCells);
	TraversabilityData.SetNum(TotalCells);

	for (int32 Y = 0; Y < HeightCells; ++Y)
	{
		for (int32 X = 0; X < WidthCells; ++X)
		{
			const double CellCenterRosX = ComputedOriginXMapMeters + (static_cast<double>(X) + 0.5) * SafeResolution;
			const double CellCenterRosY = ComputedOriginYMapMeters + (static_cast<double>(Y) + 0.5) * SafeResolution;

			const int32 Index = Y * WidthCells + X;

			ReusableMapMsg.data[Index] = ClassifyCell(CellCenterRosX, CellCenterRosY);

			float ElevationMeters = NAN;
			if (SampleElevationCell(CellCenterRosX, CellCenterRosY, ElevationMeters))
			{
				ElevationDataMeters[Index] = ElevationMeters;
			}
			else
			{
				ElevationDataMeters[Index] = NAN;
			}
		}
	}

	ComputeSlopeMap();
	ComputeTraversabilityMap();

	bMapGenerated = true;

	UE_LOG(LogTemp, Log,
		TEXT("OccupancyMapPublisherComponent: generated occupancy/elevation/slope/traversability maps %dx%d at %.3f m/cell, origin=(%.3f, %.3f), frame=%s"),
		WidthCells,
		HeightCells,
		SafeResolution,
		ComputedOriginXMapMeters,
		ComputedOriginYMapMeters,
		*MapFrameId);
}

void UOccupancyMapPublisherComponent::ComputeSlopeMap()
{
	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);
	const float Resolution = ReusableMapMsg.info.resolution;

	if (Width <= 0 || Height <= 0 || Resolution <= KINDA_SMALL_NUMBER || ElevationDataMeters.Num() != Width * Height)
	{
		SlopeDataDegrees.Empty();
		return;
	}

	SlopeDataDegrees.Empty(Width * Height);
	SlopeDataDegrees.SetNum(Width * Height);

	auto IsValidElevation = [](float Value) -> bool
	{
		return !FMath::IsNaN(Value);
	};

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = Y * Width + X;
			const float Center = ElevationDataMeters[Index];

			if (!IsValidElevation(Center))
			{
				SlopeDataDegrees[Index] = NAN;
				continue;
			}

			bool bHasDx = false;
			bool bHasDy = false;
			float DzDx = 0.0f;
			float DzDy = 0.0f;

			// Prefer central difference. If one side is unknown/outside the map, fall back to one-sided difference.
			if (X > 0 && X < Width - 1)
			{
				const float Left = ElevationDataMeters[Y * Width + (X - 1)];
				const float Right = ElevationDataMeters[Y * Width + (X + 1)];
				if (IsValidElevation(Left) && IsValidElevation(Right))
				{
					DzDx = (Right - Left) / (2.0f * Resolution);
					bHasDx = true;
				}
			}

			if (!bHasDx && X > 0)
			{
				const float Left = ElevationDataMeters[Y * Width + (X - 1)];
				if (IsValidElevation(Left))
				{
					DzDx = (Center - Left) / Resolution;
					bHasDx = true;
				}
			}

			if (!bHasDx && X < Width - 1)
			{
				const float Right = ElevationDataMeters[Y * Width + (X + 1)];
				if (IsValidElevation(Right))
				{
					DzDx = (Right - Center) / Resolution;
					bHasDx = true;
				}
			}

			if (Y > 0 && Y < Height - 1)
			{
				const float Down = ElevationDataMeters[(Y - 1) * Width + X];
				const float Up = ElevationDataMeters[(Y + 1) * Width + X];
				if (IsValidElevation(Down) && IsValidElevation(Up))
				{
					DzDy = (Up - Down) / (2.0f * Resolution);
					bHasDy = true;
				}
			}

			if (!bHasDy && Y > 0)
			{
				const float Down = ElevationDataMeters[(Y - 1) * Width + X];
				if (IsValidElevation(Down))
				{
					DzDy = (Center - Down) / Resolution;
					bHasDy = true;
				}
			}

			if (!bHasDy && Y < Height - 1)
			{
				const float Up = ElevationDataMeters[(Y + 1) * Width + X];
				if (IsValidElevation(Up))
				{
					DzDy = (Up - Center) / Resolution;
					bHasDy = true;
				}
			}

			if (!bHasDx && !bHasDy)
			{
				SlopeDataDegrees[Index] = 0.0f;
				continue;
			}

			const float SlopeRadians = FMath::Atan(FMath::Sqrt((DzDx * DzDx) + (DzDy * DzDy)));
			SlopeDataDegrees[Index] = FMath::RadiansToDegrees(SlopeRadians);
		}
	}
}


void UOccupancyMapPublisherComponent::ComputeTraversabilityMap()
{
	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 ||
		ReusableMapMsg.data.size() != static_cast<size_t>(Width * Height) ||
		SlopeDataDegrees.Num() != Width * Height)
	{
		TraversabilityData.Empty();
		return;
	}

	TraversabilityData.Empty(Width * Height);
	TraversabilityData.SetNum(Width * Height);

	for (int32 Index = 0; Index < Width * Height; ++Index)
	{
		const int8 OccupancyValue = ReusableMapMsg.data[Index];
		const float SlopeDegrees = SlopeDataDegrees[Index];

		if (OccupancyValue < 0)
		{
			TraversabilityData[Index] = -1; // unknown
			continue;
		}

		if (OccupancyValue >= 65)
		{
			TraversabilityData[Index] = 0; // blocked by obstacle
			continue;
		}

		if (FMath::IsNaN(SlopeDegrees))
		{
			TraversabilityData[Index] = -1; // unknown slope
			continue;
		}

		if (SlopeDegrees > MaxTraversableSlopeDegrees)
		{
			TraversabilityData[Index] = 0; // too steep
			continue;
		}

		if (SlopeDegrees > SafeSlopeDegrees)
		{
			TraversabilityData[Index] = 50; // risky / caution
			continue;
		}

		TraversabilityData[Index] = 100; // safe
	}
}

FVector UOccupancyMapPublisherComponent::RosMapToUnrealWorldCm(double RosX_m, double RosY_m) const
{
	return FVector(
		RosX_m * 100.0,
		-RosY_m * 100.0,
		TraceBaseZCm
	);
}

int8 UOccupancyMapPublisherComponent::ClassifyCell(double CellCenterRosX_m, double CellCenterRosY_m) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World)
	{
		return -1;
	}

	const FVector CenterUnreal = RosMapToUnrealWorldCm(CellCenterRosX_m, CellCenterRosY_m);
	const FVector TraceStart(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm + TraceStartHeightCm);
	const FVector TraceEnd(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm - TraceEndDepthCm);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(OccupancyMapTrace), true);
	if (Owner)
	{
		QueryParams.AddIgnoredActor(Owner);
	}

	TArray<FHitResult> Hits;
	const bool bHitAnything = World->LineTraceMultiByChannel(
		Hits,
		TraceStart,
		TraceEnd,
		TraceChannel,
		QueryParams
	);

	if (!bHitAnything || Hits.Num() == 0)
	{
		return -1;
	}

	for (const FHitResult& Hit : Hits)
	{
		if (HitHasOccupiedTag(Hit))
		{
			return 100;
		}
	}

	// We hit terrain/ground or another untagged object. Treat it as free.
	return 0;
}

bool UOccupancyMapPublisherComponent::SampleElevationCell(
	double CellCenterRosX_m,
	double CellCenterRosY_m,
	float& OutElevationMeters) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World)
	{
		return false;
	}

	const FVector CenterUnreal = RosMapToUnrealWorldCm(CellCenterRosX_m, CellCenterRosY_m);
	const FVector TraceStart(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm + TraceStartHeightCm);
	const FVector TraceEnd(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm - TraceEndDepthCm);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ElevationMapTrace), true);
	if (Owner)
	{
		QueryParams.AddIgnoredActor(Owner);
	}

	TArray<FHitResult> Hits;
	const bool bHitAnything = World->LineTraceMultiByChannel(
		Hits,
		TraceStart,
		TraceEnd,
		TraceChannel,
		QueryParams
	);

	if (!bHitAnything || Hits.Num() == 0)
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		// Obstacles belong in occupancy. Elevation should describe the terrain surface.
		if (HitHasOccupiedTag(Hit))
		{
			continue;
		}

		OutElevationMeters = static_cast<float>(Hit.ImpactPoint.Z / 100.0);
		return true;
	}

	return false;
}

bool UOccupancyMapPublisherComponent::HitHasOccupiedTag(const FHitResult& Hit) const
{
	const AActor* HitActor = Hit.GetActor();
	if (HitActor && HitActor->ActorHasTag(OccupiedTag))
	{
		return true;
	}

	const UActorComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->ComponentHasTag(OccupiedTag))
	{
		return true;
	}

	return false;
}

bool UOccupancyMapPublisherComponent::ExportMapToDefaultDatasetDirectory()
{
	// One dataset run folder is created when the level starts.
	// Capture sessions created through /control live inside this folder as Session_001, Session_002, etc.
	// Maps are environment-level artifacts, so they live once per run in <run>/Maps.
	static UWorld* CachedWorld = nullptr;
	static FString CachedRunDirectory;

	UWorld* CurrentWorld = GetWorld();
	if (CachedWorld != CurrentWorld || CachedRunDirectory.IsEmpty())
	{
		CachedWorld = CurrentWorld;
		CachedRunDirectory = CreateNewBeginPlayDatasetRunDirectory();
	}

	const FString MapsDirectory = FPaths::Combine(CachedRunDirectory, TEXT("Maps"));
	return ExportMapToDirectory(MapsDirectory, TEXT("occupancy_map"));
}

bool UOccupancyMapPublisherComponent::ExportMapToDirectory(const FString& MapsDirectory, const FString& BaseFileName)
{
	if (MapsDirectory.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot export map because MapsDirectory is empty."));
		return false;
	}

	if (!bMapGenerated)
	{
		GenerateOccupancyMap();
	}

	if (!bMapGenerated)
	{
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot export map because map generation failed."));
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*MapsDirectory))
	{
		PlatformFile.CreateDirectoryTree(*MapsDirectory);
	}

	const FString SafeOccupancyBaseFileName = BaseFileName.IsEmpty() ? FString(TEXT("occupancy_map")) : BaseFileName;
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

	const bool bOccupancyPgmOk = SaveMapPgm(OccupancyPgmPath);
	const bool bOccupancyYamlOk = SaveMapYaml(OccupancyYamlPath, OccupancyPgmFileName);

	const bool bElevationCsvOk = SaveElevationCsv(ElevationCsvPath);
	const bool bElevationPreviewOk = SaveElevationPreviewPgm(ElevationPreviewPath);
	const bool bElevationYamlOk = SaveElevationYaml(ElevationYamlPath, ElevationCsvFileName, ElevationPreviewFileName);

	const bool bSlopeCsvOk = SaveSlopeCsv(SlopeCsvPath);
	const bool bSlopePreviewOk = SaveSlopePreviewPgm(SlopePreviewPath);
	const bool bSlopeYamlOk = SaveSlopeYaml(SlopeYamlPath, SlopeCsvFileName, SlopePreviewFileName);

	const bool bTraversabilityCsvOk = SaveTraversabilityCsv(TraversabilityCsvPath);
	const bool bTraversabilityPgmOk = SaveTraversabilityPgm(TraversabilityPgmPath);
	const bool bTraversabilityYamlOk = SaveTraversabilityYaml(TraversabilityYamlPath, TraversabilityCsvFileName, TraversabilityPgmFileName);

	if (bOccupancyPgmOk && bOccupancyYamlOk && bElevationCsvOk && bElevationPreviewOk && bElevationYamlOk && bSlopeCsvOk && bSlopePreviewOk && bSlopeYamlOk && bTraversabilityCsvOk && bTraversabilityPgmOk && bTraversabilityYamlOk)
	{
		UE_LOG(LogTemp, Log, TEXT("OccupancyMapPublisherComponent: exported occupancy, elevation, slope and traversability map files to %s"), *MapsDirectory);
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: failed to export one or more map files to %s"), *MapsDirectory);
	return false;
}

FString UOccupancyMapPublisherComponent::MakeElevationBaseFileName(const FString& OccupancyBaseFileName) const
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))
	{
		return TEXT("elevation_map");
	}

	return OccupancyBaseFileName + TEXT("_elevation");
}

FString UOccupancyMapPublisherComponent::MakeSlopeBaseFileName(const FString& OccupancyBaseFileName) const
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))
	{
		return TEXT("slope_map");
	}

	return OccupancyBaseFileName + TEXT("_slope");
}


FString UOccupancyMapPublisherComponent::MakeTraversabilityBaseFileName(const FString& OccupancyBaseFileName) const
{
	if (OccupancyBaseFileName.Equals(TEXT("occupancy_map"), ESearchCase::IgnoreCase))
	{
		return TEXT("traversability_map");
	}

	return OccupancyBaseFileName + TEXT("_traversability");
}

uint8 UOccupancyMapPublisherComponent::OccupancyValueToPgmPixel(int8 CellValue) const
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

uint8 UOccupancyMapPublisherComponent::ElevationValueToPreviewPixel(
	float ElevationMeters,
	float MinElevationMeters,
	float MaxElevationMeters) const
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

	const float Normalized = FMath::Clamp(
		(ElevationMeters - MinElevationMeters) / Range,
		0.0f,
		1.0f
	);

	return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
}

uint8 UOccupancyMapPublisherComponent::SlopeValueToPreviewPixel(float SlopeDegrees, float MaxPreviewSlopeDegrees) const
{
	if (FMath::IsNaN(SlopeDegrees))
	{
		return 205; // unknown = gray
	}

	const float SafeMaxSlope = FMath::Max(1.0f, MaxPreviewSlopeDegrees);
	const float Normalized = FMath::Clamp(SlopeDegrees / SafeMaxSlope, 0.0f, 1.0f);
	return static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
}


uint8 UOccupancyMapPublisherComponent::TraversabilityValueToPgmPixel(int8 TraversabilityValue) const
{
	if (TraversabilityValue < 0)
	{
		return 205; // unknown = gray
	}

	if (TraversabilityValue <= 0)
	{
		return 0; // not traversable = black
	}

	if (TraversabilityValue < 100)
	{
		return 127; // risky = medium gray
	}

	return 254; // safe = white
}

bool UOccupancyMapPublisherComponent::SaveMapPgm(const FString& FilePath) const
{
	if (!bMapGenerated)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || ReusableMapMsg.data.size() != static_cast<size_t>(Width * Height)) {
		return false;
	}

	TArray<uint8> FileBytes;

	const FString Header = FString::Printf(
		TEXT("P5\n# Generated by OccupancyMapPublisherComponent\n%d %d\n255\n"),
		Width,
		Height
	);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());

	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	// PGM first row is the top row, so flip map Y during export.
	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;

		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			FileBytes.Add(OccupancyValueToPgmPixel(ReusableMapMsg.data[Index]));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool UOccupancyMapPublisherComponent::SaveMapYaml(const FString& FilePath, const FString& ImageFileName) const
{
	if (!bMapGenerated)
	{
		return false;
	}

	const FString Yaml = FString::Printf(
		TEXT("image: %s\n")
		TEXT("mode: trinary\n")
		TEXT("resolution: %.9f\n")
		TEXT("origin: [%.9f, %.9f, 0.0]\n")
		TEXT("negate: 0\n")
		TEXT("occupied_thresh: 0.65\n")
		TEXT("free_thresh: 0.196\n"),
		*ImageFileName,
		ReusableMapMsg.info.resolution,
		ReusableMapMsg.info.origin.position.x,
		ReusableMapMsg.info.origin.position.y
	);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UOccupancyMapPublisherComponent::SaveElevationCsv(const FString& FilePath) const
{
	if (!bMapGenerated || ElevationDataMeters.Num() == 0)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || ElevationDataMeters.Num() != Width * Height)
	{
		return false;
	}

	FString Csv;
	Csv.Reserve(Width * Height * 8);

	// Same orientation as the exported PGM: first CSV row is the top row.
	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;

		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			const float ElevationMeters = ElevationDataMeters[Index];

			if (FMath::IsNaN(ElevationMeters))
			{
				Csv += TEXT("nan");
			}
			else
			{
				Csv += FString::Printf(TEXT("%.6f"), ElevationMeters);
			}

			if (X + 1 < Width)
			{
				Csv += TEXT(",");
			}
		}

		Csv += TEXT("\n");
	}

	return FFileHelper::SaveStringToFile(Csv, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UOccupancyMapPublisherComponent::SaveElevationYaml(
	const FString& FilePath,
	const FString& CsvFileName,
	const FString& PreviewFileName) const
{
	if (!bMapGenerated)
	{
		return false;
	}

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
		ReusableMapMsg.info.resolution,
		ReusableMapMsg.info.width,
		ReusableMapMsg.info.height,
		ReusableMapMsg.info.origin.position.x,
		ReusableMapMsg.info.origin.position.y
	);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UOccupancyMapPublisherComponent::SaveElevationPreviewPgm(const FString& FilePath) const
{
	if (!bMapGenerated || ElevationDataMeters.Num() == 0)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

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
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot save elevation preview because all elevation values are NaN."));
		return false;
	}

	TArray<uint8> FileBytes;

	const FString Header = FString::Printf(
		TEXT("P5\n# Elevation preview generated by OccupancyMapPublisherComponent\n# min_m %.6f max_m %.6f\n%d %d\n255\n"),
		MinElevationMeters,
		MaxElevationMeters,
		Width,
		Height
	);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());

	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	// Same orientation as elevation_map.csv and occupancy_map.pgm.
	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;

		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			const float ElevationMeters = ElevationDataMeters[Index];

			FileBytes.Add(ElevationValueToPreviewPixel(
				ElevationMeters,
				MinElevationMeters,
				MaxElevationMeters
			));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}

bool UOccupancyMapPublisherComponent::SaveSlopeCsv(const FString& FilePath) const
{
	if (!bMapGenerated || SlopeDataDegrees.Num() == 0)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || SlopeDataDegrees.Num() != Width * Height)
	{
		return false;
	}

	FString Csv;
	Csv.Reserve(Width * Height * 8);

	// Same orientation as the exported PGM previews: first CSV row is the top row.
	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;

		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			const float SlopeDegrees = SlopeDataDegrees[Index];

			if (FMath::IsNaN(SlopeDegrees))
			{
				Csv += TEXT("nan");
			}
			else
			{
				Csv += FString::Printf(TEXT("%.6f"), SlopeDegrees);
			}

			if (X + 1 < Width)
			{
				Csv += TEXT(",");
			}
		}

		Csv += TEXT("\n");
	}

	return FFileHelper::SaveStringToFile(Csv, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UOccupancyMapPublisherComponent::SaveSlopeYaml(
	const FString& FilePath,
	const FString& CsvFileName,
	const FString& PreviewFileName) const
{
	if (!bMapGenerated)
	{
		return false;
	}

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
		ReusableMapMsg.info.resolution,
		ReusableMapMsg.info.width,
		ReusableMapMsg.info.height,
		ReusableMapMsg.info.origin.position.x,
		ReusableMapMsg.info.origin.position.y
	);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UOccupancyMapPublisherComponent::SaveSlopePreviewPgm(const FString& FilePath) const
{
	if (!bMapGenerated || SlopeDataDegrees.Num() == 0)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

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
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot save slope preview because all slope values are NaN."));
		return false;
	}

	// For visualization, slopes >= 45 degrees become white.
	// The CSV still contains the real slope values.
	const float PreviewMaxSlopeDegrees = 45.0f;

	TArray<uint8> FileBytes;

	const FString Header = FString::Printf(
		TEXT("P5\n# Slope preview generated by OccupancyMapPublisherComponent\n# unit degrees preview_max %.6f measured_max %.6f\n%d %d\n255\n"),
		PreviewMaxSlopeDegrees,
		MaxSlopeDegrees,
		Width,
		Height
	);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());

	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	// Same orientation as slope_map.csv and the other PGM previews.
	for (int32 ImageY = 0; ImageY < Height; ++ImageY)
	{
		const int32 MapY = Height - 1 - ImageY;

		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = MapY * Width + X;
			const float SlopeDegrees = SlopeDataDegrees[Index];

			FileBytes.Add(SlopeValueToPreviewPixel(SlopeDegrees, PreviewMaxSlopeDegrees));
		}
	}

	return FFileHelper::SaveArrayToFile(FileBytes, *FilePath);
}


bool UOccupancyMapPublisherComponent::SaveTraversabilityCsv(const FString& FilePath) const
{
	if (!bMapGenerated || TraversabilityData.Num() == 0)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || TraversabilityData.Num() != Width * Height)
	{
		return false;
	}

	FString Csv;
	Csv.Reserve(Width * Height * 4);

	// Same orientation as the exported PGM previews: first CSV row is the top row.
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

bool UOccupancyMapPublisherComponent::SaveTraversabilityPgm(const FString& FilePath) const
{
	if (!bMapGenerated || TraversabilityData.Num() == 0)
	{
		return false;
	}

	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || TraversabilityData.Num() != Width * Height)
	{
		return false;
	}

	TArray<uint8> FileBytes;

	const FString Header = FString::Printf(
		TEXT("P5\n# Traversability map generated by OccupancyMapPublisherComponent\n# values safe=100 risky=50 blocked=0 unknown=-1\n# safe_slope_deg %.6f max_traversable_slope_deg %.6f\n%d %d\n255\n"),
		SafeSlopeDegrees,
		MaxTraversableSlopeDegrees,
		Width,
		Height
	);

	FTCHARToUTF8 HeaderUtf8(*Header);
	FileBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());

	FileBytes.Reserve(FileBytes.Num() + Width * Height);

	// Same orientation as traversability_map.csv and the other PGM previews.
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

bool UOccupancyMapPublisherComponent::SaveTraversabilityYaml(
	const FString& FilePath,
	const FString& CsvFileName,
	const FString& PgmFileName) const
{
	if (!bMapGenerated)
	{
		return false;
	}

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
		TEXT("  safe: 100\n")
		TEXT("  risky: 50\n")
		TEXT("  blocked: 0\n")
		TEXT("  unknown: -1\n")
		TEXT("rules:\n")
		TEXT("  occupied_threshold: 65\n")
		TEXT("  safe_slope_degrees: %.6f\n")
		TEXT("  max_traversable_slope_degrees: %.6f\n"),
		*CsvFileName,
		*PgmFileName,
		*MapFrameId,
		ReusableMapMsg.info.resolution,
		ReusableMapMsg.info.width,
		ReusableMapMsg.info.height,
		ReusableMapMsg.info.origin.position.x,
		ReusableMapMsg.info.origin.position.y,
		SafeSlopeDegrees,
		MaxTraversableSlopeDegrees
	);

	return FFileHelper::SaveStringToFile(Yaml, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UOccupancyMapPublisherComponent::PublishMap()
{
	if (!ROSNode || !bMapGenerated)
	{
		return;
	}

	const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const builtin_interfaces::msg::Time Stamp = ToRosTime(NowSeconds);

	ReusableMapMsg.header.stamp = Stamp;
	ReusableMapMsg.info.map_load_time = Stamp;

	ROSNode->Publish<nav_msgs::msg::OccupancyGrid>(*MapTopic, ReusableMapMsg);
}
