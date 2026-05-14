#include "Maps/OccupancyMapPublisherComponent.h"

#include "TempoROSTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::OccupancyGrid);

UOccupancyMapPublisherComponent::UOccupancyMapPublisherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOccupancyMapPublisherComponent::BeginPlay()
{
	Super::BeginPlay();

	SetupRos();

	if (bPublishOnBeginPlay)
	{
		RegenerateAndPublishMap();
	}
}

void UOccupancyMapPublisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ROSNode = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UOccupancyMapPublisherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ROSNode)
	{
		ROSNode->Tick(DeltaTime);
	}

	if (!bRepublishPeriodically || !bMapGenerated)
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
	MapQOS.CustomQueueSize(1).Reliable();

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

	if (bUseOwnerLocationAsMapCenter)
	{
		// Convert the owner actor location from Unreal coordinates to ROS map coordinates.
		const double CenterXMapMeters = static_cast<double>(OwnerLocation.X) / 100.0;
		const double CenterYMapMeters = -static_cast<double>(OwnerLocation.Y) / 100.0;

		ComputedOriginXMapMeters = CenterXMapMeters - static_cast<double>(WidthCells) * SafeResolution * 0.5;
		ComputedOriginYMapMeters = CenterYMapMeters - static_cast<double>(HeightCells) * SafeResolution * 0.5;
	}
	else
	{
		ComputedOriginXMapMeters = ManualOriginXMapMeters;
		ComputedOriginYMapMeters = ManualOriginYMapMeters;
	}

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

	ReusableMapMsg.data.clear();
	ReusableMapMsg.data.resize(static_cast<size_t>(WidthCells) * static_cast<size_t>(HeightCells), -1);

	for (int32 Y = 0; Y < HeightCells; ++Y)
	{
		for (int32 X = 0; X < WidthCells; ++X)
		{
			const double CellCenterRosX = ComputedOriginXMapMeters + (static_cast<double>(X) + 0.5) * SafeResolution;
			const double CellCenterRosY = ComputedOriginYMapMeters + (static_cast<double>(Y) + 0.5) * SafeResolution;

			const int8 CellValue = ClassifyCell(CellCenterRosX, CellCenterRosY);
			const size_t Index = static_cast<size_t>(Y) * static_cast<size_t>(WidthCells) + static_cast<size_t>(X);
			ReusableMapMsg.data[Index] = CellValue;
		}
	}

	bMapGenerated = true;

	UE_LOG(LogTemp, Log, TEXT("OccupancyMapPublisherComponent: generated occupancy map %dx%d at %.3f m/cell, origin=(%.3f, %.3f), frame=%s"),
		WidthCells,
		HeightCells,
		SafeResolution,
		ComputedOriginXMapMeters,
		ComputedOriginYMapMeters,
		*MapFrameId);
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
		return bNoHitIsUnknown ? static_cast<int8>(-1) : static_cast<int8>(0);
	}

	if (bOnlyTaggedHitsAreOccupied)
	{
		for (const FHitResult& Hit : Hits)
		{
			if (HitHasOccupiedTag(Hit))
			{
				return 100;
			}
		}

		// We hit the world, but not a tagged obstacle. Treat it as free ground/terrain.
		return 0;
	}

	// Simple conservative mode: any blocking hit marks the cell occupied.
	return 100;
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
