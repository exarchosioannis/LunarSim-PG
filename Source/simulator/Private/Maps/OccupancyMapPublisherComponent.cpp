#include "Maps/OccupancyMapPublisherComponent.h"
#include "Maps/GroundTruthElevationPointCloudBuilder.h"
#include "Maps/GroundTruthMapFileExporter.h"
#include "Utils/DatasetRunSubsystem.h"

#include "TempoROSTypes.h"
#include "EngineUtils.h"
#include "Engine/World.h"	
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::OccupancyGrid);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::PointCloud2);

UOccupancyMapPublisherComponent::UOccupancyMapPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOccupancyMapPublisherComponent::BeginPlay()
{
	Super::BeginPlay();
	TryInitializeFromRunCaptureConfig();
}

void UOccupancyMapPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UOccupancyMapPublisherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Actor BeginPlay order is not guaranteed. Wait until CaptureManager has
	// frozen the sanitized dataset-run config, then initialize exactly once.
	if (!bRunCaptureConfigResolved && !TryInitializeFromRunCaptureConfig()) return;
	if (!IsGroundTruthMapsEnabledForRun()) return;

	if (ROSNode) {
		ROSNode->Tick(DeltaTime);
	}

	if (!bMapGenerated) return;
	
	PublishAccumulator += DeltaTime;
	if (PublishAccumulator >= RepublishPeriodSeconds) {
		PublishAccumulator = 0.0f;
		PublishMap();
	}
}

bool UOccupancyMapPublisherComponent::TryInitializeFromRunCaptureConfig()
{
	if (bRunCaptureConfigResolved) return true;

	UWorld* World = GetWorld();
	UDatasetRunSubsystem* DatasetRunSubsystem = World ? World->GetSubsystem<UDatasetRunSubsystem>() : nullptr;
	if (!DatasetRunSubsystem || !DatasetRunSubsystem->TryGetCaptureConfigForRun(RunCaptureConfig)) {
		return false;
	}

	bRunCaptureConfigResolved = true;
	if (!IsGroundTruthMapsEnabledForRun()) {
		UE_LOG(LogTemp, Log,
			TEXT("OccupancyMapPublisherComponent: ground truth maps are disabled in the frozen run capture config."));
		return true;
	}

	SetupRos();
	// Maps are environment-level ground truth, generated/exported once per run.
	GenerateOccupancyMap();
	PublishMap();
	ExportMapToDefaultDatasetDirectory();
	return true;
}

bool UOccupancyMapPublisherComponent::IsGroundTruthMapsEnabledForRun() const
{
	return bRunCaptureConfigResolved && RunCaptureConfig.IsGroundTruthMapsEnabled();
}

void UOccupancyMapPublisherComponent::SetupRos()
{
	if (ROSNode) return;
	ROSNode = UTempoROSNode::Create(*NodeName, this, false);
	if (!ROSNode) {
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: failed to create ROS node."));
		return;
	}

	FROSQOSProfile MapQOS;
	MapQOS.CustomQueueSize(1).Reliable().TransientLocal();

	ROSNode->AddPublisher<nav_msgs::msg::OccupancyGrid>(*OccupancyMapTopic, MapQOS, false);
	ROSNode->AddPublisher<sensor_msgs::msg::PointCloud2>(*ElevationPointCloudTopic, MapQOS, false);
}

builtin_interfaces::msg::Time UOccupancyMapPublisherComponent::ToRosTime(double Seconds) const
{
	builtin_interfaces::msg::Time T;
	if (Seconds < 0.0) Seconds = 0.0;

	const int64 Sec = static_cast<int64>(Seconds);
	const double Frac = Seconds - static_cast<double>(Sec);

	T.sec = static_cast<int32>(Sec);
	T.nanosec = static_cast<uint32>(FMath::Clamp<int64>(static_cast<int64>(Frac * 1000000000.0), 0, 999999999));
	return T;
}

void UOccupancyMapPublisherComponent::RegenerateAndPublishMap()
{
	if (!IsGroundTruthMapsEnabledForRun()) {
		UE_LOG(LogTemp, Log, TEXT("OccupancyMapPublisherComponent: map generation skipped because ground truth maps are disabled."));
		return;
	}

	GenerateOccupancyMap();
	PublishMap();
}

void UOccupancyMapPublisherComponent::GenerateOccupancyMap()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World || !Owner) return;
	
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


	BuildIgnoredMapActors();
	for (int32 Y = 0; Y < HeightCells; ++Y) {
		for (int32 X = 0; X < WidthCells; ++X) {
			const double CellCenterRosX = ComputedOriginXMapMeters + (static_cast<double>(X) + 0.5) * SafeResolution;
			const double CellCenterRosY = ComputedOriginYMapMeters + (static_cast<double>(Y) + 0.5) * SafeResolution;

			const int32 Index = Y * WidthCells + X;

			ReusableMapMsg.data[Index] = ClassifyCell(CellCenterRosX, CellCenterRosY);
			float ElevationMeters = NAN;
			if (SampleElevationCell(CellCenterRosX, CellCenterRosY, ElevationMeters)) {
				ElevationDataMeters[Index] = ElevationMeters;
			} else {
				ElevationDataMeters[Index] = NAN;
			}
		}
	}


	BuildElevationPointCloud();

	bMapGenerated = true;
	UE_LOG(LogTemp, Log, TEXT("OccupancyMapPublisherComponent: generated occupancy/elevation/elevation point cloud maps %dx%d at %.3f m/cell, origin=(%.3f, %.3f), frame=%s"),
		WidthCells, HeightCells, SafeResolution,
		ComputedOriginXMapMeters, ComputedOriginYMapMeters, *MapFrameId);
}

FVector UOccupancyMapPublisherComponent::RosMapToUnrealWorldCm(double RosX_m, double RosY_m) const
{
	return FVector(RosX_m * 100.0, -RosY_m * 100.0, TraceBaseZCm);
}

int8 UOccupancyMapPublisherComponent::ClassifyCell(double CellCenterRosX_m, double CellCenterRosY_m) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World) return -1;
	if (!Owner) return -1;
	
	const FVector CenterUnreal = RosMapToUnrealWorldCm(CellCenterRosX_m, CellCenterRosY_m);
	const FVector TraceStart(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm + TraceStartHeightCm);
	const FVector TraceEnd(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm - TraceEndDepthCm);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(OccupancyMapTrace), true);
	QueryParams.AddIgnoredActors(CachedIgnoredMapActors);

	TArray<FHitResult> Hits;
	const bool bHitAnything = World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, TraceChannel, QueryParams);

	if (!bHitAnything || Hits.Num() == 0) {
		return -1;
	}

	bool bHitTerrain = false;
	for (const FHitResult& Hit : Hits) {
		if (HitHasIgnoreTag(Hit)) {
			continue;
		}
		if (HitHasOccupiedTag(Hit)) {
			return 100;
		}
		if (HitHasTerrainTag(Hit)){
			bHitTerrain = true;
		}
	}

	if (bHitTerrain) return 0;
	// Untagged actors/components, such as the rover or camera rig, are ignored for static map generation.
	return -1;
}

void UOccupancyMapPublisherComponent::BuildIgnoredMapActors()
{
	CachedIgnoredMapActors.Empty();

	UWorld* World = GetWorld();
	if (!World) {
		return;
	}

	AActor* Owner = GetOwner();
	if (Owner) {
		CachedIgnoredMapActors.Add(Owner);
	}

	for (TActorIterator<AActor> It(World); It; ++It) {
		AActor* Actor = *It;
		if (!Actor || Actor == Owner) {
			continue;
		}

		if (Actor->ActorHasTag(IgnoreTag)) {
			CachedIgnoredMapActors.Add(Actor);
			continue;
		}

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components) {
			if (Component && Component->ComponentHasTag(IgnoreTag)) {
				CachedIgnoredMapActors.Add(Actor);
				break;
			}
		}
	}
}

bool UOccupancyMapPublisherComponent::SampleElevationCell(double CellCenterRosX_m, double CellCenterRosY_m, float& OutElevationMeters) const
{
	UWorld* World = GetWorld();

	if (!World) return false;
	const FVector CenterUnreal = RosMapToUnrealWorldCm(CellCenterRosX_m, CellCenterRosY_m);
	const FVector TraceStart(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm + TraceStartHeightCm);
	const FVector TraceEnd(CenterUnreal.X, CenterUnreal.Y, TraceBaseZCm - TraceEndDepthCm);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ElevationMapTrace), true);
	QueryParams.AddIgnoredActors(CachedIgnoredMapActors);

	TArray<FHitResult> Hits;
	const bool bHitAnything = World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, TraceChannel, QueryParams);

	if (!bHitAnything || Hits.Num() == 0) return false;
	
	for (const FHitResult& Hit : Hits) {
		if (HitHasIgnoreTag(Hit)) continue;
		if (HitHasTerrainTag(Hit) || HitHasOccupiedTag(Hit)) {
			OutElevationMeters = static_cast<float>(Hit.ImpactPoint.Z / 100.0);
			return true;
		}
	}

	return false;
}

bool UOccupancyMapPublisherComponent::HitHasOccupiedTag(const FHitResult& Hit) const
{
	const AActor* HitActor = Hit.GetActor();
	if (HitActor && HitActor->ActorHasTag(OccupiedTag)) return true;

	const UActorComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->ComponentHasTag(OccupiedTag)) return true;
	return false;
}

bool UOccupancyMapPublisherComponent::HitHasTerrainTag(const FHitResult& Hit) const
{
	const AActor* HitActor = Hit.GetActor();
	if (HitActor && HitActor->ActorHasTag(TerrainTag)) return true;
	
	const UActorComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->ComponentHasTag(TerrainTag)) return true;
	return false;
}

bool UOccupancyMapPublisherComponent::HitHasIgnoreTag(const FHitResult& Hit) const
{
	const AActor* HitActor = Hit.GetActor();
	if (HitActor && HitActor->ActorHasTag(IgnoreTag)) return true;

	const UActorComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->ComponentHasTag(IgnoreTag)) return true;
	return false;
}

bool UOccupancyMapPublisherComponent::ExportMapToDefaultDatasetDirectory()
{
	if (!IsGroundTruthMapsEnabledForRun()) {
		UE_LOG(LogTemp, Log, TEXT("OccupancyMapPublisherComponent: map export skipped because ground truth maps are disabled."));
		return false;
	}

	UWorld* World = GetWorld();
	UDatasetRunSubsystem* DatasetRunSubsystem = World ? World->GetSubsystem<UDatasetRunSubsystem>() : nullptr;
	if (!DatasetRunSubsystem) {
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot export map because DatasetRunSubsystem is unavailable."));
		return false;
	}

	const FString MapsDirectory = DatasetRunSubsystem->GetMapsDirectory();
	return ExportMapToDirectory(MapsDirectory, FGroundTruthMapArtifacts::GetDefaultOccupancyBaseFileName());
}

bool UOccupancyMapPublisherComponent::ExportMapToDirectory(const FString& MapsDirectory, const FString& BaseFileName)
{
	if (!IsGroundTruthMapsEnabledForRun()) {
		UE_LOG(LogTemp, Log, TEXT("OccupancyMapPublisherComponent: map export skipped because ground truth maps are disabled."));
		return false;
	}

	if (MapsDirectory.IsEmpty()) {
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot export map because MapsDirectory is empty."));
		return false;
	}

	if (!bMapGenerated) GenerateOccupancyMap();
	if (!bMapGenerated) {
		UE_LOG(LogTemp, Warning, TEXT("OccupancyMapPublisherComponent: cannot export map because map generation failed."));
		return false;
	}

	FGroundTruthMapFileExportInfo ExportInfo;
	ExportInfo.MapFrameId = MapFrameId;
	ExportInfo.BaseFileName = BaseFileName;
	ExportInfo.OccupancyMapMsg = &ReusableMapMsg;
	ExportInfo.ElevationDataMeters = &ElevationDataMeters;
	return FGroundTruthMapFileExporter::ExportToDirectory(MapsDirectory, ExportInfo);
}

void UOccupancyMapPublisherComponent::BuildElevationPointCloud()
{
	const int32 Width = static_cast<int32>(ReusableMapMsg.info.width);
	const int32 Height = static_cast<int32>(ReusableMapMsg.info.height);

	if (Width <= 0 || Height <= 0 || ElevationDataMeters.Num() != Width * Height) {
		ReusableElevationPointCloudMsg = sensor_msgs::msg::PointCloud2();
		return;
	}

	FGroundTruthElevationPointCloudBuildInfo CloudInfo;
	CloudInfo.FrameId = MapFrameId;
	CloudInfo.ResolutionMeters = ReusableMapMsg.info.resolution;
	CloudInfo.Width = ReusableMapMsg.info.width;
	CloudInfo.Height = ReusableMapMsg.info.height;
	CloudInfo.OriginX = ReusableMapMsg.info.origin.position.x;
	CloudInfo.OriginY = ReusableMapMsg.info.origin.position.y;

	ReusableElevationPointCloudMsg = FGroundTruthElevationPointCloudBuilder::BuildElevationPointCloud(CloudInfo, ElevationDataMeters);
}

void UOccupancyMapPublisherComponent::PublishMap()
{
	if (!IsGroundTruthMapsEnabledForRun()) return;
	if (!ROSNode || !bMapGenerated) return;
	
	const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const builtin_interfaces::msg::Time Stamp = ToRosTime(NowSeconds);
	ReusableMapMsg.header.stamp = Stamp;
	ReusableMapMsg.info.map_load_time = Stamp;
	ReusableMapMsg.header.frame_id = TCHAR_TO_UTF8(*MapFrameId);

	ROSNode->Publish<nav_msgs::msg::OccupancyGrid>(*OccupancyMapTopic, ReusableMapMsg);

	ReusableElevationPointCloudMsg.header.stamp = Stamp;
	ReusableElevationPointCloudMsg.header.frame_id = TCHAR_TO_UTF8(*MapFrameId);

	ROSNode->Publish<sensor_msgs::msg::PointCloud2>(*ElevationPointCloudTopic, ReusableElevationPointCloudMsg);
}
