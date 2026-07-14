#include "Utils/DatasetRunSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Maps/GroundTruthMapArtifacts.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void UDatasetRunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CreateRunForPlaySessionIfNeeded();
}

FString UDatasetRunSubsystem::GetDatasetRootDirectory() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Datasets")));
}

FString UDatasetRunSubsystem::GetCurrentDatasetRunMarkerPath() const
{
	return FPaths::Combine(GetDatasetRootDirectory(), TEXT("current_dataset_run.txt"));
}

FString UDatasetRunSubsystem::GetCurrentRunDirectory() const
{
	return CurrentRunDirectory;
}

FString UDatasetRunSubsystem::GetOrCreateCurrentRunDirectory()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!CurrentRunDirectory.IsEmpty() && PlatformFile.DirectoryExists(*CurrentRunDirectory)) {
		return CurrentRunDirectory;
	}

	FString MarkerRunDirectory;
	if (TryReadCurrentDatasetRunDirectory(MarkerRunDirectory)) {
		CurrentRunDirectory = MarkerRunDirectory;
		return CurrentRunDirectory;
	}

	return CreateNewRunDirectory();
}

FString UDatasetRunSubsystem::CreateNewRunDirectory()
{
	const FString DatasetRootDirectory = GetDatasetRootDirectory();
	EnsureDirectoryExists(DatasetRootDirectory);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString DateString = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	FString RunDirectory = FPaths::Combine(DatasetRootDirectory, DateString);

	int32 Suffix = 2;
	while (PlatformFile.DirectoryExists(*RunDirectory)) {
		RunDirectory = FPaths::Combine(DatasetRootDirectory, FString::Printf(TEXT("%s_%02d"), *DateString, Suffix));
		++Suffix;
	}

	EnsureDirectoryExists(RunDirectory);
	FFileHelper::SaveStringToFile(RunDirectory, *GetCurrentDatasetRunMarkerPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	CurrentRunDirectory = RunDirectory;
	NextSessionIndex = 0;
	bCreatedRunForPlaySession = true;
	return CurrentRunDirectory;
}

FString UDatasetRunSubsystem::GetMapsDirectory()
{
	const FString RunDirectory = GetOrCreateCurrentRunDirectory();
	if (RunDirectory.IsEmpty()) {
		return FString();
	}

	const FString MapsDirectory = FPaths::Combine(RunDirectory, FGroundTruthMapArtifacts::GetMapsDirectoryName());
	EnsureDirectoryExists(MapsDirectory);
	return MapsDirectory;
}

bool UDatasetRunSubsystem::TryReadCurrentDatasetRunDirectory(FString& OutRunDirectory) const
{
	FString MarkerContent;
	if (!FFileHelper::LoadFileToString(MarkerContent, *GetCurrentDatasetRunMarkerPath())) {
		return false;
	}

	MarkerContent.TrimStartAndEndInline();
	if (MarkerContent.IsEmpty()) {
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*MarkerContent)) {
		return false;
	}

	OutRunDirectory = MarkerContent;
	return true;
}

FString UDatasetRunSubsystem::CreateNextSessionDirectory(FString& OutSessionName)
{
	const FString RunDirectory = GetOrCreateCurrentRunDirectory();
	if (RunDirectory.IsEmpty()) {
		OutSessionName.Empty();
		return FString();
	}

	++NextSessionIndex;
	OutSessionName = FString::Printf(TEXT("Session_%03d"), NextSessionIndex);

	const FString SessionDirectory = FPaths::Combine(RunDirectory, OutSessionName);
	EnsureDirectoryExists(SessionDirectory);
	return SessionDirectory;
}

FCaptureConfig UDatasetRunSubsystem::RegisterCaptureConfigForRun(const FCaptureConfig& InConfig)
{
	if (bHasRunCaptureConfig) {
		UE_LOG(LogTemp, Warning,
			TEXT("DatasetRunSubsystem: capture config is already frozen for this play session; returning the existing run config."));
		return RunCaptureConfig;
	}

	RunCaptureConfig = InConfig;
	RunCaptureConfig.Sanitize();
	bHasRunCaptureConfig = true;
	return RunCaptureConfig;
}

bool UDatasetRunSubsystem::TryGetCaptureConfigForRun(FCaptureConfig& OutConfig) const
{
	if (!bHasRunCaptureConfig) return false;

	OutConfig = RunCaptureConfig;
	return true;
}

void UDatasetRunSubsystem::CreateRunForPlaySessionIfNeeded()
{
	if (bCreatedRunForPlaySession) return;

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;
	
	CreateNewRunDirectory();
}

void UDatasetRunSubsystem::EnsureDirectoryExists(const FString& Directory) const
{
	if (Directory.IsEmpty()) return;
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Directory)) {
		PlatformFile.CreateDirectoryTree(*Directory);
	}
}
