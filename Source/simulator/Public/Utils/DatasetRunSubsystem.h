#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DatasetRunSubsystem.generated.h"

UCLASS()
class SIMULATOR_API UDatasetRunSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	FString GetDatasetRootDirectory() const;
	FString GetCurrentDatasetRunMarkerPath() const;
	FString GetCurrentRunDirectory() const;
	FString GetOrCreateCurrentRunDirectory();
	FString CreateNewRunDirectory();
	FString GetMapsDirectory();
	bool TryReadCurrentDatasetRunDirectory(FString& OutRunDirectory) const;
	FString CreateNextSessionDirectory(FString& OutSessionName);

private:
	void CreateRunForPlaySessionIfNeeded();
	void EnsureDirectoryExists(const FString& Directory) const;

	FString CurrentRunDirectory;
	int32 NextSessionIndex = 0;
	bool bCreatedRunForPlaySession = false;
};
