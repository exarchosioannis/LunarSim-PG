#pragma once

#include "CoreMinimal.h"

class USimulatorRockSettings;
class UWorld;

struct FSimulatorRockBakeResult
{
	bool bSucceeded = false;
	bool bHadWarnings = false;
	FString Message;

	int32 TotalInputRecords = 0;
	int32 ValidRecords = 0;
	int32 InvalidRecords = 0;
	int32 DuplicateIds = 0;
	int32 TraceFailures = 0;
	int32 PlacementFailures = 0;
	int32 PlacedRocks = 0;
	int32 LoadedMeshes = 0;
	int32 ClearedActors = 0;
};

class FSimulatorRockBaker
{
public:
	static FSimulatorRockBakeResult BakeRocksToLevel(UWorld* World, const USimulatorRockSettings& Settings);

	static FSimulatorRockBakeResult ClearBakedRocks(UWorld* World);
};
