#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoROSNode.h"

#include "nav_msgs/msg/occupancy_grid.hpp"
#include "builtin_interfaces/msg/time.hpp"

#include "OccupancyMapPublisherComponent.generated.h"

/*
 * Publishes a static ground-truth 2D occupancy map from the Unreal level.
 *
 * This component is intentionally independent from RobotCamRig and the synchronized
 * capture pipeline. It does not create capture frames and it does not affect RGB,
 * pose, TF, path, trajectory CSVs, or UnrealGT outputs.
 *
 * Suggested usage:
 *   - Create an empty Blueprint actor, for example BP_GroundTruthMapPublisher.
 *   - Add this component to it.
 *   - Place the actor at the center of the area you want to map.
 *   - Tag obstacle actors/components with OccupiedTag, default: MapObstacle.
 *
 * ROS output:
 *   /gt/map/occupancy    nav_msgs/msg/OccupancyGrid
 *   frame_id             map
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
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	// ROS settings
	UPROPERTY(EditAnywhere, Category = "ROS")
	FString NodeName = TEXT("occupancy_map_publisher");

	UPROPERTY(EditAnywhere, Category = "ROS")
	FString MapTopic = TEXT("/gt/map/occupancy");

	UPROPERTY(VisibleAnywhere, Category = "ROS")
	FString MapFrameId = TEXT("map");

	// Map geometry
	UPROPERTY(EditAnywhere, Category = "Map", meta = (ClampMin = "0.01", UIMin = "0.05"))
	float ResolutionMeters = 0.10f;

	UPROPERTY(EditAnywhere, Category = "Map", meta = (ClampMin = "1.0", UIMin = "5.0"))
	float MapWidthMeters = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Map", meta = (ClampMin = "1.0", UIMin = "5.0"))
	float MapHeightMeters = 50.0f;

	// If true, the owner actor location is treated as the center of the map.
	// This is the easiest workflow: place BP_GroundTruthMapPublisher at the center of the level area.
	UPROPERTY(EditAnywhere, Category = "Map")
	bool bUseOwnerLocationAsMapCenter = true;

	// Used only when bUseOwnerLocationAsMapCenter is false.
	UPROPERTY(EditAnywhere, Category = "Map", meta = (EditCondition = "!bUseOwnerLocationAsMapCenter"))
	float ManualOriginXMapMeters = -25.0f;

	UPROPERTY(EditAnywhere, Category = "Map", meta = (EditCondition = "!bUseOwnerLocationAsMapCenter"))
	float ManualOriginYMapMeters = -25.0f;

	// Vertical raycast settings in Unreal centimeters.
	UPROPERTY(EditAnywhere, Category = "Trace")
	float TraceStartHeightCm = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Trace")
	float TraceEndDepthCm = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_WorldStatic;

	// Occupancy classification.
	// Recommended first version: tag obstacle actors/components with MapObstacle.
	UPROPERTY(EditAnywhere, Category = "Classification")
	FName OccupiedTag = TEXT("MapObstacle");

	// If true, only tagged hits become occupied. Other blocking hits, such as terrain/ground, become free.
	// This avoids marking the whole terrain as occupied.
	UPROPERTY(EditAnywhere, Category = "Classification")
	bool bOnlyTaggedHitsAreOccupied = true;

	// If true, cells with no blocking hit are unknown (-1). If false, no-hit cells are free (0).
	UPROPERTY(EditAnywhere, Category = "Classification")
	bool bNoHitIsUnknown = true;

	// Publishing behavior.
	UPROPERTY(EditAnywhere, Category = "Publishing")
	bool bPublishOnBeginPlay = true;

	// Useful because map publishers are normally latched in ROS. If the QoS is not latched,
	// periodic publishing lets RViz/tools receive the map even if they start later.
	UPROPERTY(EditAnywhere, Category = "Publishing")
	bool bRepublishPeriodically = true;

	UPROPERTY(EditAnywhere, Category = "Publishing", meta = (ClampMin = "0.1", UIMin = "1.0"))
	float RepublishPeriodSeconds = 2.0f;

	// Reusable map message.
	nav_msgs::msg::OccupancyGrid ReusableMapMsg;

	bool bMapGenerated = false;
	float PublishAccumulator = 0.0f;
	float ComputedOriginXMapMeters = 0.0f;
	float ComputedOriginYMapMeters = 0.0f;
	float TraceBaseZCm = 0.0f;
};
