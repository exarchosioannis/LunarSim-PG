#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimulatorRockSettings.generated.h"

UCLASS(Transient)
class USimulatorRockSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Rock Baking", meta = (FilePathFilter = "json"))
	FFilePath RockFieldJsonFile;

	UPROPERTY(EditAnywhere, Category = "Rock Baking", meta = (ContentDir))
	FDirectoryPath MeshFolderPath;
};
