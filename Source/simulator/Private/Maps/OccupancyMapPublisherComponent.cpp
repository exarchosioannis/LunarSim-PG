#include "Maps/OccupancyMapPublisherComponent.h"
#include "simulator.h"
#include "Maps/GroundTruthElevationPointCloudBuilder.h"
#include "Maps/GroundTruthMapFileExporter.h"
#include "Utils/DatasetRunSubsystem.h"

#include "TempoROSTypes.h"
#include "EngineUtils.h"
#include "Engine/World.h"	
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(nav_msgs::msg::OccupancyGrid);
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(sensor_msgs::msg::PointCloud2);

namespace
{
	constexpr int64 MaxGroundTruthMapCellCount = 10000000;

	struct FLogicalLandscapeCandidate
	{
		ALandscapeProxy* Landscape = nullptr;
		FBox ComponentBounds = FBox(EForceInit::ForceInit);
		int32 ProxyCount = 0;
		int32 StreamingProxyCount = 0;
		int32 ComponentCount = 0;
		bool bHasTerrainTag = false;
	};

	struct FResolvedLandscapeMapBounds
	{
		ALandscapeProxy* Landscape = nullptr;
		FString SelectionReason;
		int32 ProxyCount = 0;
		int32 StreamingProxyCount = 0;
		int32 ComponentCount = 0;

		FBox RawWorldBounds = FBox(EForceInit::ForceInit);
		double OriginXMapMeters = 0.0;
		double OriginYMapMeters = 0.0;
		double AlignedMaxXMapMeters = 0.0;
		double AlignedMaxYMapMeters = 0.0;
		int32 WidthCells = 0;
		int32 HeightCells = 0;
		int64 TotalCells = 0;
		double TraceStartZCm = 0.0;
		double TraceEndZCm = 0.0;
	};

	bool IsFiniteBox(const FBox& Box)
	{
		return Box.IsValid &&
			FMath::IsFinite(Box.Min.X) && FMath::IsFinite(Box.Min.Y) && FMath::IsFinite(Box.Min.Z) &&
			FMath::IsFinite(Box.Max.X) && FMath::IsFinite(Box.Max.Y) && FMath::IsFinite(Box.Max.Z);
	}

	bool HasValidComponentBounds(const FLogicalLandscapeCandidate& Candidate)
	{
		return Candidate.ComponentCount > 0 && IsFiniteBox(Candidate.ComponentBounds) &&
			Candidate.ComponentBounds.Max.X > Candidate.ComponentBounds.Min.X &&
			Candidate.ComponentBounds.Max.Y > Candidate.ComponentBounds.Min.Y;
	}

	const FLogicalLandscapeCandidate* FindLandscapeAtPublisher(
		UWorld* World,
		AActor* Owner,
		const TMap<FGuid, FLogicalLandscapeCandidate>& Candidates,
		const FBox& AllCandidateBounds,
		ECollisionChannel TraceChannel,
		float TraceStartMarginCm,
		float TraceEndMarginCm)
	{
		const FVector OwnerLocation = Owner->GetActorLocation();
		const FVector TraceStart(
			OwnerLocation.X,
			OwnerLocation.Y,
			AllCandidateBounds.Max.Z + static_cast<double>(TraceStartMarginCm));
		const FVector TraceEnd(
			OwnerLocation.X,
			OwnerLocation.Y,
			AllCandidateBounds.Min.Z - static_cast<double>(TraceEndMarginCm));

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LandscapeMapBoundsSelectionTrace), true);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && !Actor->IsA<ALandscapeProxy>()) QueryParams.AddIgnoredActor(Actor);
		}

		TArray<FHitResult> Hits;
		if (World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, TraceChannel, QueryParams))
		{
			for (const FHitResult& Hit : Hits)
			{
				const ALandscapeProxy* HitProxy = Cast<ALandscapeProxy>(Hit.GetActor());
				const FLogicalLandscapeCandidate* Candidate = HitProxy
					? Candidates.Find(HitProxy->GetLandscapeGuid())
					: nullptr;
				if (Candidate && HasValidComponentBounds(*Candidate))
				{
					return Candidate;
				}
			}
		}

		const FLogicalLandscapeCandidate* ContainingCandidate = nullptr;
		int32 ContainingCandidateCount = 0;
		for (const TPair<FGuid, FLogicalLandscapeCandidate>& Pair : Candidates)
		{
			const FLogicalLandscapeCandidate& Candidate = Pair.Value;
			if (!HasValidComponentBounds(Candidate)) continue;

			const FBox& Bounds = Candidate.ComponentBounds;
			if (OwnerLocation.X >= Bounds.Min.X && OwnerLocation.X <= Bounds.Max.X &&
				OwnerLocation.Y >= Bounds.Min.Y && OwnerLocation.Y <= Bounds.Max.Y)
			{
				ContainingCandidate = &Candidate;
				++ContainingCandidateCount;
			}
		}

		return ContainingCandidateCount == 1 ? ContainingCandidate : nullptr;
	}

	bool TrySelectLandscape(
		UWorld* World,
		AActor* Owner,
		FName TerrainTag,
		ECollisionChannel TraceChannel,
		float TraceStartMarginCm,
		float TraceEndMarginCm,
		FResolvedLandscapeMapBounds& OutBounds)
	{
		TMap<FGuid, FLogicalLandscapeCandidate> Candidates;

		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Proxy = *It;
			if (!IsValid(Proxy) || Proxy->IsTemplate() || Proxy->IsEditorOnly()) continue;

			const FGuid LandscapeGuid = Proxy->GetLandscapeGuid();
			if (!LandscapeGuid.IsValid()) continue;

			FLogicalLandscapeCandidate& Candidate = Candidates.FindOrAdd(LandscapeGuid);
			if (!Candidate.Landscape || Cast<ALandscape>(Proxy))
			{
				Candidate.Landscape = Proxy;
			}

			++Candidate.ProxyCount;
			Candidate.StreamingProxyCount += Cast<ALandscapeStreamingProxy>(Proxy) ? 1 : 0;
			Candidate.bHasTerrainTag |= Proxy->ActorHasTag(TerrainTag);

			TInlineComponentArray<UActorComponent*> ActorComponents(Proxy);
			for (UActorComponent* ActorComponent : ActorComponents)
			{
				if (!ActorComponent) continue;
				Candidate.bHasTerrainTag |= ActorComponent->ComponentHasTag(TerrainTag);

				ULandscapeComponent* LandscapeComponent = Cast<ULandscapeComponent>(ActorComponent);
				if (!IsValid(LandscapeComponent) || LandscapeComponent->IsTemplate() ||
					LandscapeComponent->IsEditorOnly() || !LandscapeComponent->IsRegistered())
					continue;

				const FBox ComponentBounds = LandscapeComponent->Bounds.GetBox();
				if (!IsFiniteBox(ComponentBounds) || ComponentBounds.Max.X <= ComponentBounds.Min.X ||
					ComponentBounds.Max.Y <= ComponentBounds.Min.Y) continue;

				Candidate.ComponentBounds += ComponentBounds;
				++Candidate.ComponentCount;
			}
		}

		FBox AllCandidateBounds(EForceInit::ForceInit);
		int32 ValidCandidateCount = 0;
		const FLogicalLandscapeCandidate* OnlyCandidate = nullptr;
		const FLogicalLandscapeCandidate* TaggedCandidate = nullptr;
		int32 TaggedCandidateCount = 0;
		for (const TPair<FGuid, FLogicalLandscapeCandidate>& Pair : Candidates)
		{
			const FLogicalLandscapeCandidate& Candidate = Pair.Value;
			if (!HasValidComponentBounds(Candidate)) continue;

			OnlyCandidate = &Candidate;
			++ValidCandidateCount;
			AllCandidateBounds += Candidate.ComponentBounds;
			if (Candidate.bHasTerrainTag)
			{
				TaggedCandidate = &Candidate;
				++TaggedCandidateCount;
			}
		}

		if (ValidCandidateCount == 0)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed: no valid registered typed Landscape components were found."));
			return false;
		}

		const FLogicalLandscapeCandidate* SelectedCandidate = nullptr;
		FString SelectionReason;
		if (TaggedCandidateCount == 1)
		{
			SelectedCandidate = TaggedCandidate;
			SelectionReason = TEXT("unique MapTerrain-tagged logical Landscape");
		}
		else
		{
			const FLogicalLandscapeCandidate* LandscapeAtPublisher = FindLandscapeAtPublisher(
				World, Owner, Candidates, AllCandidateBounds, TraceChannel,
				TraceStartMarginCm, TraceEndMarginCm);
			if (LandscapeAtPublisher &&
				(TaggedCandidateCount == 0 || LandscapeAtPublisher->bHasTerrainTag))
			{
				SelectedCandidate = LandscapeAtPublisher;
				SelectionReason = TEXT("Landscape underneath/containing the map publisher");
			}
			else if (ValidCandidateCount == 1)
			{
				SelectedCandidate = OnlyCandidate;
				SelectionReason = TEXT("only logical Landscape in the world");
			}
		}

		if (!SelectedCandidate)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed: %d unrelated logical Landscapes have valid components (%d are MapTerrain-tagged), and no unique Landscape is associated with map publisher %s at (%.3f, %.3f, %.3f) cm. Refusing to union or choose arbitrarily."),
				ValidCandidateCount, TaggedCandidateCount,
				*Owner->GetPathName(),
				Owner->GetActorLocation().X,
				Owner->GetActorLocation().Y,
				Owner->GetActorLocation().Z);
			return false;
		}

		OutBounds.Landscape = SelectedCandidate->Landscape;
		OutBounds.SelectionReason = SelectionReason;
		OutBounds.ProxyCount = SelectedCandidate->ProxyCount;
		OutBounds.StreamingProxyCount = SelectedCandidate->StreamingProxyCount;
		OutBounds.ComponentCount = SelectedCandidate->ComponentCount;
		OutBounds.RawWorldBounds = SelectedCandidate->ComponentBounds;

		return IsValid(OutBounds.Landscape);
	}

	bool TryAlignLandscapeBounds(
		float ResolutionMeters,
		float TraceStartMarginCm,
		float TraceEndMarginCm,
		FResolvedLandscapeMapBounds& OutBounds)
	{
		const FString LandscapeName = OutBounds.Landscape->GetPathName();
		const FBox& RawBounds = OutBounds.RawWorldBounds;
		const double RawMinXMapMeters = RawBounds.Min.X / 100.0;
		const double RawMaxXMapMeters = RawBounds.Max.X / 100.0;
		const double RawMinYMapMeters = -RawBounds.Max.Y / 100.0;
		const double RawMaxYMapMeters = -RawBounds.Min.Y / 100.0;
		const double Resolution = static_cast<double>(ResolutionMeters);

		if (!FMath::IsFinite(RawMinXMapMeters) || !FMath::IsFinite(RawMaxXMapMeters) ||
			!FMath::IsFinite(RawMinYMapMeters) || !FMath::IsFinite(RawMaxYMapMeters) ||
			RawMaxXMapMeters <= RawMinXMapMeters || RawMaxYMapMeters <= RawMinYMapMeters)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed for %s: non-finite or zero-sized XY bounds."),
				*LandscapeName);
			return false;
		}

		const double ScaledMinX = RawMinXMapMeters / Resolution;
		const double ScaledMaxX = RawMaxXMapMeters / Resolution;
		const double ScaledMinY = RawMinYMapMeters / Resolution;
		const double ScaledMaxY = RawMaxYMapMeters / Resolution;
		const double MinSafeCellEdge = static_cast<double>(TNumericLimits<int64>::Lowest() / 2);
		const double MaxSafeCellEdge = static_cast<double>(TNumericLimits<int64>::Max() / 2);
		if (!FMath::IsFinite(ScaledMinX) || !FMath::IsFinite(ScaledMaxX) ||
			!FMath::IsFinite(ScaledMinY) || !FMath::IsFinite(ScaledMaxY) ||
			ScaledMinX <= MinSafeCellEdge || ScaledMaxX >= MaxSafeCellEdge ||
			ScaledMinY <= MinSafeCellEdge || ScaledMaxY >= MaxSafeCellEdge)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed for %s: bounds/resolution exceed supported integer alignment range."),
				*LandscapeName);
			return false;
		}

		// ROS origins are cell edges; sampling remains origin + (index + 0.5) * resolution.
		const int64 MinCellEdgeX = FMath::FloorToInt64(ScaledMinX);
		const int64 MaxCellEdgeX = FMath::CeilToInt64(ScaledMaxX);
		const int64 MinCellEdgeY = FMath::FloorToInt64(ScaledMinY);
		const int64 MaxCellEdgeY = FMath::CeilToInt64(ScaledMaxY);
		const int64 WidthCells64 = MaxCellEdgeX - MinCellEdgeX;
		const int64 HeightCells64 = MaxCellEdgeY - MinCellEdgeY;

		if (WidthCells64 <= 0 || HeightCells64 <= 0)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed for %s: invalid grid dimensions (%lld x %lld)."),
				*LandscapeName, WidthCells64, HeightCells64);
			return false;
		}

		if (WidthCells64 > MaxGroundTruthMapCellCount / HeightCells64)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed for %s: detected %.3f x %.3f m at %.6f m/cell requests %lld x %lld cells, exceeding the safe %lld-cell limit. Resolution was not reduced and bounds were not cropped."),
				*LandscapeName,
				RawMaxXMapMeters - RawMinXMapMeters,
				RawMaxYMapMeters - RawMinYMapMeters,
				Resolution, WidthCells64, HeightCells64,
				MaxGroundTruthMapCellCount);
			return false;
		}

		const int64 TotalCells = WidthCells64 * HeightCells64;
		OutBounds.OriginXMapMeters = static_cast<double>(MinCellEdgeX) * Resolution;
		OutBounds.OriginYMapMeters = static_cast<double>(MinCellEdgeY) * Resolution;
		OutBounds.AlignedMaxXMapMeters = static_cast<double>(MaxCellEdgeX) * Resolution;
		OutBounds.AlignedMaxYMapMeters = static_cast<double>(MaxCellEdgeY) * Resolution;
		OutBounds.WidthCells = static_cast<int32>(WidthCells64);
		OutBounds.HeightCells = static_cast<int32>(HeightCells64);
		OutBounds.TotalCells = TotalCells;
		OutBounds.TraceStartZCm = RawBounds.Max.Z + static_cast<double>(TraceStartMarginCm);
		OutBounds.TraceEndZCm = RawBounds.Min.Z - static_cast<double>(TraceEndMarginCm);

		if (!FMath::IsFinite(OutBounds.TraceStartZCm) || !FMath::IsFinite(OutBounds.TraceEndZCm) ||
			OutBounds.TraceStartZCm <= OutBounds.TraceEndZCm)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed for %s: invalid Z trace range [%.3f, %.3f] cm."),
				*LandscapeName, OutBounds.TraceEndZCm, OutBounds.TraceStartZCm);
			return false;
		}

		return true;
	}

	bool TryResolveLandscapeMapBounds(
		UWorld* World,
		AActor* Owner,
		FName TerrainTag,
		ECollisionChannel TraceChannel,
		float ResolutionMeters,
		float TraceStartMarginCm,
		float TraceEndMarginCm,
		FResolvedLandscapeMapBounds& OutBounds)
	{
		if (!World || !Owner)
		{
			return false;
		}

		if (!FMath::IsFinite(ResolutionMeters) || ResolutionMeters <= 0.0f ||
			!FMath::IsFinite(TraceStartMarginCm) || TraceStartMarginCm < 0.0f ||
			!FMath::IsFinite(TraceEndMarginCm) || TraceEndMarginCm < 0.0f)
		{
			UE_LOG(LogLunarSimMaps, Error,
				TEXT("OccupancyMapPublisherComponent: automatic Landscape bounds failed: invalid resolution or vertical trace margins."));
			return false;
		}

		return TrySelectLandscape(World, Owner, TerrainTag, TraceChannel,
			TraceStartMarginCm, TraceEndMarginCm, OutBounds) &&
			TryAlignLandscapeBounds(ResolutionMeters, TraceStartMarginCm, TraceEndMarginCm, OutBounds);
	}
}

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
		UE_LOG(LogLunarSimROS, Error,
			TEXT("Map ROS initialization failed: subsystem=maps, resource=%s, stage=node creation, cause=TempoROS node unavailable, effect=occupancy and elevation topics disabled."),
			*NodeName);
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
		return;
	}

	GenerateOccupancyMap();
	PublishMap();
}

void UOccupancyMapPublisherComponent::GenerateOccupancyMap()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	bMapGenerated = false;
	if (!World || !Owner)
	{
		UE_LOG(LogLunarSimMaps, Error,
			TEXT("OccupancyMapPublisherComponent: map generation failed: invalid world or map-publisher owner."));
		return;
	}

	FResolvedLandscapeMapBounds ResolvedBounds;
	if (!TryResolveLandscapeMapBounds(
		World,
		Owner,
		TerrainTag,
		TraceChannel,
		ResolutionMeters,
		TraceStartHeightCm,
		TraceEndDepthCm,
		ResolvedBounds))
	{
		return;
	}

	const float SafeResolution = ResolutionMeters;
	const int32 WidthCells = ResolvedBounds.WidthCells;
	const int32 HeightCells = ResolvedBounds.HeightCells;
	ComputedOriginXMapMeters = ResolvedBounds.OriginXMapMeters;
	ComputedOriginYMapMeters = ResolvedBounds.OriginYMapMeters;
	ComputedTraceStartZCm = ResolvedBounds.TraceStartZCm;
	ComputedTraceEndZCm = ResolvedBounds.TraceEndZCm;

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

	const int32 TotalCells = static_cast<int32>(ResolvedBounds.TotalCells);

	ReusableMapMsg.data.clear();
	ReusableMapMsg.data.resize(static_cast<size_t>(TotalCells), -1);

	ElevationDataMeters.Empty(TotalCells);
	ElevationDataMeters.SetNum(TotalCells);


	BuildIgnoredMapActors();
	int32 UnknownOccupancyCells = 0;
	int32 NoElevationHitCells = 0;
	for (int32 Y = 0; Y < HeightCells; ++Y) {
		for (int32 X = 0; X < WidthCells; ++X) {
			const double CellCenterRosX = ComputedOriginXMapMeters + (static_cast<double>(X) + 0.5) * SafeResolution;
			const double CellCenterRosY = ComputedOriginYMapMeters + (static_cast<double>(Y) + 0.5) * SafeResolution;

			const int32 Index = Y * WidthCells + X;

			ReusableMapMsg.data[Index] = ClassifyCell(CellCenterRosX, CellCenterRosY);
			if (ReusableMapMsg.data[Index] < 0) {
				++UnknownOccupancyCells;
			}
			float ElevationMeters = NAN;
			if (SampleElevationCell(CellCenterRosX, CellCenterRosY, ElevationMeters)) {
				ElevationDataMeters[Index] = ElevationMeters;
			} else {
				ElevationDataMeters[Index] = NAN;
				++NoElevationHitCells;
			}
		}
	}


	BuildElevationPointCloud();

	bMapGenerated = true;
	UE_LOG(LogLunarSimMaps, Verbose,
		TEXT("Map build summary: landscape=%s, selection=%s, proxies=%d (%d streaming), components=%d, raw_bounds_cm=[(%.3f,%.3f,%.3f),(%.3f,%.3f,%.3f)], aligned_ros_xy=[(%.6f,%.6f),(%.6f,%.6f)], resolution=%.6f m/cell, grid=%dx%d (%lld cells), trace_z_cm=[%.3f,%.3f], unknown=%d/%d (%.2f%%), elevation_no_hit=%d/%d (%.2f%%)."),
		*ResolvedBounds.Landscape->GetPathName(),
		*ResolvedBounds.SelectionReason,
		ResolvedBounds.ProxyCount,
		ResolvedBounds.StreamingProxyCount,
		ResolvedBounds.ComponentCount,
		ResolvedBounds.RawWorldBounds.Min.X,
		ResolvedBounds.RawWorldBounds.Min.Y,
		ResolvedBounds.RawWorldBounds.Min.Z,
		ResolvedBounds.RawWorldBounds.Max.X,
		ResolvedBounds.RawWorldBounds.Max.Y,
		ResolvedBounds.RawWorldBounds.Max.Z,
		ResolvedBounds.OriginXMapMeters,
		ResolvedBounds.OriginYMapMeters,
		ResolvedBounds.AlignedMaxXMapMeters,
		ResolvedBounds.AlignedMaxYMapMeters,
		SafeResolution,
		WidthCells,
		HeightCells,
		ResolvedBounds.TotalCells,
		ComputedTraceEndZCm,
		ComputedTraceStartZCm,
		UnknownOccupancyCells,
		TotalCells,
		100.0 * static_cast<double>(UnknownOccupancyCells) / static_cast<double>(TotalCells),
		NoElevationHitCells,
		TotalCells,
		100.0 * static_cast<double>(NoElevationHitCells) / static_cast<double>(TotalCells));
}

FVector UOccupancyMapPublisherComponent::RosMapToUnrealWorldCm(double RosX_m, double RosY_m) const
{
	return FVector(RosX_m * 100.0, -RosY_m * 100.0, 0.0);
}

int8 UOccupancyMapPublisherComponent::ClassifyCell(double CellCenterRosX_m, double CellCenterRosY_m) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World) return -1;
	if (!Owner) return -1;
	
	const FVector CenterUnreal = RosMapToUnrealWorldCm(CellCenterRosX_m, CellCenterRosY_m);
	const FVector TraceStart(CenterUnreal.X, CenterUnreal.Y, ComputedTraceStartZCm);
	const FVector TraceEnd(CenterUnreal.X, CenterUnreal.Y, ComputedTraceEndZCm);

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
	const FVector TraceStart(CenterUnreal.X, CenterUnreal.Y, ComputedTraceStartZCm);
	const FVector TraceEnd(CenterUnreal.X, CenterUnreal.Y, ComputedTraceEndZCm);

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
	if (HitActor && HitActor->IsA<ALandscapeProxy>()) return true;
	if (HitActor && HitActor->ActorHasTag(TerrainTag)) return true;
	
	const UActorComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->IsA<ULandscapeComponent>()) return true;
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
		return false;
	}

	UWorld* World = GetWorld();
	UDatasetRunSubsystem* DatasetRunSubsystem = World ? World->GetSubsystem<UDatasetRunSubsystem>() : nullptr;
	if (!DatasetRunSubsystem) {
		UE_LOG(LogLunarSimMaps, Error,
			TEXT("Map export failed: subsystem=maps, resource=dataset run, stage=directory resolution, cause=DatasetRunSubsystem unavailable, effect=map artifacts not written."));
		return false;
	}

	const FString MapsDirectory = DatasetRunSubsystem->GetMapsDirectory();
	return ExportMapToDirectory(MapsDirectory, FGroundTruthMapArtifacts::GetDefaultOccupancyBaseFileName());
}

bool UOccupancyMapPublisherComponent::ExportMapToDirectory(const FString& MapsDirectory, const FString& BaseFileName)
{
	if (!IsGroundTruthMapsEnabledForRun()) {
		return false;
	}

	if (MapsDirectory.IsEmpty()) {
		UE_LOG(LogLunarSimMaps, Error,
			TEXT("Map export failed: subsystem=maps, resource=maps directory, stage=directory resolution, cause=empty output path, effect=map artifacts not written."));
		return false;
	}

	if (!bMapGenerated) GenerateOccupancyMap();
	if (!bMapGenerated) {
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
