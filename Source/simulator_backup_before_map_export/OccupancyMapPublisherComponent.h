#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"

#include "nav_msgs/msg/occupancy_grid.hpp"
#include "builtin_interfaces/msg/time.hpp"

#include "OccupancyMapPublisherComponent.generated.h"

/*
 * Publishes a static global ground-truth 2D occupancy map from the Unreal level.
 *
 * Suggested usage:
 *   - Create an empty Blueprint actor, for example BP_GroundTruthMapPublisher.
 *   - Add this component to it.
 *   - Place the actor at the center of the 100m x 100m area you want to map.
 *   - Tag obstacle actors/components with MapObstacle.
 *
 * ROS output:
 *   /gt/map/occupancy nav_msgs/msg/OccupancyGrid
 *   frame_id map
 *
 * Notes:
 *   - The map is generated once from Unreal raycasts.
 *   - This is not part of the synchronized per-frame capture pipeline.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API UOccupancyMapPublisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOccupancyMapPublisherComponent();

	UFUNCTION(BlueprintCallable, Category = "Ground Truth Map")
	void RegenerateAndPublishMap();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetupRos();
	void GenerateOccupancyMap();
	void PublishMap();
	builtin_interfaces::msg::Time ToRosTime(double Seconds) const;

	FVector RosMapToUnrealWorldCm(double RosX_m, double RosY_m) const;
	int8 ClassifyCell(double CellCenterRosX_m, double CellCenterRosY_m) const;
	bool HitHasOccupiedTag(const FHitResult& Hit) const;

private:
	// UTempoROSNode is a UObject, so keep it as UPROPERTY for Unreal GC safety.
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	// Fixed ROS settings.
	const FString NodeName = TEXT("occupancy_map_publisher");
	const FString MapTopic = TEXT("/gt/map/occupancy");
	const FString MapFrameId = TEXT("map");

	// Fixed global map settings.
	// 100m x 100m with 0.20m cells gives a 500 x 500 occupancy grid.
	const float ResolutionMeters = 0.20f;
	const float MapWidthMeters = 100.0f;
	const float MapHeightMeters = 100.0f;

	// Vertical raycast settings in Unreal centimeters.
	// 2500 cm = 25m above/below the map actor Z location.
	const float TraceStartHeightCm = 2500.0f;
	const float TraceEndDepthCm = 2500.0f;

	// Static world geometry channel. Tagged hits become occupied cells.
	const ECollisionChannel TraceChannel = ECC_WorldStatic;
	const FName OccupiedTag = TEXT("MapObstacle");

	// The map is static, but we republish slowly so RViz/late subscribers always receive it,
	// even if their subscription QoS is volatile.
	const float RepublishPeriodSeconds = 5.0f;

	nav_msgs::msg::OccupancyGrid ReusableMapMsg;

	bool bMapGenerated = false;
	float PublishAccumulator = 0.0f;
	float ComputedOriginXMapMeters = 0.0f;
	float ComputedOriginYMapMeters = 0.0f;
	float TraceBaseZCm = 0.0f;
};