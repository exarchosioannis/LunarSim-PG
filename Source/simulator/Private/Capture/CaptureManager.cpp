#include "Capture/CaptureManager.h"
#include "Capture/CapturePoseSourceComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Utils/UnrealToRosConversion.h"

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

	bool TryReadCurrentDatasetRunDirectory(FString& OutRunDirectory)
	{
		FString MarkerContent;
		if (!FFileHelper::LoadFileToString(MarkerContent, *GetCurrentDatasetRunMarkerPath()))
		{
			return false;
		}

		MarkerContent.TrimStartAndEndInline();
		if (MarkerContent.IsEmpty())
		{
			return false;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*MarkerContent))
		{
			return false;
		}

		OutRunDirectory = MarkerContent;
		return true;
	}

	FString CreateNewDatasetRunDirectory()
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

	FString CaptureModeToString(ECaptureMode CaptureMode)
	{
		switch (CaptureMode)
		{
		case ECaptureMode::MonoRos:
			return TEXT("MonoRos");
		case ECaptureMode::GroundTruth:
			return TEXT("GroundTruth");
		case ECaptureMode::StereoRos:
			return TEXT("StereoRos");
		case ECaptureMode::MonoRosGroundTruth:
			return TEXT("MonoRosGroundTruth");
		case ECaptureMode::StereoRosGroundTruth:
			return TEXT("StereoRosGroundTruth");
		default:
			return TEXT("Unknown");
		}
	}
}


void UCaptureManager::Initialize(const FCaptureConfig& InConfig)
{
	Config = InConfig;
}

UWorld* UCaptureManager::GetWorld() const
{
	if (const UObject* OuterObject = GetOuter())
	{
		return OuterObject->GetWorld();
	}

	return nullptr;
}

void UCaptureManager::StartCapture()
{
	if (bCaptureEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("Capture is already running. StartCapture ignored."));
		return;
	}

	EnsureDatasetRunDirectory();

	CurrentSessionId++;
	FrameIndex = 0;

	CurrentSessionName = FString::Printf(TEXT("Session_%03d"), CurrentSessionId.load());
	CurrentSessionDirectory = FPaths::Combine(CurrentDatasetRunDirectory, CurrentSessionName);
	CurrentImagesDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Images"));
	CurrentMapsDirectory = FPaths::Combine(CurrentDatasetRunDirectory, TEXT("Maps"));

	ManifestFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("manifest.csv"));
	const FString NavigationDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Navigation"));
	RoverGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("rover_gt_trajectory_ros.csv"));
	LeftCameraGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("left_camera_gt_trajectory_ros.csv"));
	RightCameraGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("right_camera_gt_trajectory_ros.csv"));

	// The manifest is always created for every capture session.
	// It is the main synchronization file between ROS, UnrealGT, and offline tools.
	if (!RoverPoseSource) {
		RoverPoseSource = FindPoseSourceByName(TEXT("base_link"));
		if (!RoverPoseSource) {
			// Backward-compatible fallback for older Blueprint instances.
			RoverPoseSource = FindPoseSourceByName(TEXT("rover_base"));
		}
	}

	bCaptureEnabled = true;
	StartManifest();
	WriteSessionMetadata();

	UE_LOG(LogTemp, Log, TEXT("CaptureManager: started %s inside dataset run %s"),
		*CurrentSessionName,
		*CurrentDatasetRunDirectory);
}

void UCaptureManager::StopCapture()
{
	bCaptureEnabled = false;
}

bool UCaptureManager::IsCaptureEnabled() const
{
	return bCaptureEnabled;
}

FCaptureFrameInfo UCaptureManager::NextFrame(double StampSeconds)
{
	FCaptureFramePoseData PoseData;
	if (LeftCameraPoseSource) {
		PoseData.LeftCameraPose.bValid = true;
		PoseData.LeftCameraPose.Position = LeftCameraPoseSource->GetComponentLocation();
		PoseData.LeftCameraPose.Rotation = LeftCameraPoseSource->GetComponentRotation();
	}

	if (RightCameraPoseSource) {
		PoseData.RightCameraPose.bValid = true;
		PoseData.RightCameraPose.Position = RightCameraPoseSource->GetComponentLocation();
		PoseData.RightCameraPose.Rotation = RightCameraPoseSource->GetComponentRotation();
	}

	return NextFrameWithPose(StampSeconds, PoseData);
}

FCaptureFrameInfo UCaptureManager::NextFrameWithPose(double StampSeconds, const FCaptureFramePoseData& PoseData)
{
	FCaptureFramePoseData CompletePoseData = PoseData;
	if (RoverPoseSource) {
		CompletePoseData.RoverBasePose = RoverPoseSource->GetWorldCapturePose();
	}

	FrameIndex++;
	FCaptureFrameInfo FrameInfo;
	FrameInfo.FrameIndex = FrameIndex;
	FrameInfo.StampSeconds = StampSeconds;
	FrameInfo.SessionId = CurrentSessionId;

	// Always write one canonical synchronization row for every capture frame.
	AppendManifestRow(FrameInfo);

	// Pose data lives in trajectory files, in ROS coordinates, with matching column names.
	// Rover trajectory is always written when rover pose is available.
	// Left camera trajectory is written when left/reference imagery exists: left ROS or UnrealGT.
	// Right camera trajectory is written only for stereo ROS modes.
	AppendTrajectoryRow(RoverGtTrajectoryFilePath, FrameInfo, CompletePoseData.RoverBasePose, TEXT("map"), TEXT("base_link"));
	if (Config.IsLeftRosCameraEnabled() || Config.IsGroundTruthEnabled()) {
		AppendTrajectoryRow(LeftCameraGtTrajectoryFilePath, FrameInfo, CompletePoseData.LeftCameraPose, TEXT("map"), TEXT("left_camera_link"));
	}
	if (Config.IsRightRosCameraEnabled()) {
		AppendTrajectoryRow(RightCameraGtTrajectoryFilePath, FrameInfo, CompletePoseData.RightCameraPose, TEXT("map"), TEXT("right_camera_link"));
	}

	return FrameInfo;
}

const FCaptureConfig& UCaptureManager::GetConfig() const
{
	return Config;
}

bool UCaptureManager::IsSessionValid(int32 SessionId) const
{
	return SessionId == CurrentSessionId;
}

FString UCaptureManager::GetCurrentSessionName() const
{
	return CurrentSessionName;
}

FString UCaptureManager::GetCurrentSessionDirectory() const
{
	return CurrentSessionDirectory;
}

FString UCaptureManager::GetCurrentImagesDirectory() const
{
	return CurrentImagesDirectory;
}

FString UCaptureManager::GetCurrentMapsDirectory() const
{
	return CurrentMapsDirectory;
}

FString UCaptureManager::GetCurrentDatasetRunDirectory() const
{
	return CurrentDatasetRunDirectory;
}

FString UCaptureManager::GetManifestFilePath() const
{
	return ManifestFilePath;
}

FString UCaptureManager::GetRoverGtTrajectoryFilePath() const
{
	return RoverGtTrajectoryFilePath;
}

FString UCaptureManager::GetLeftCameraGtTrajectoryFilePath() const
{
	return LeftCameraGtTrajectoryFilePath;
}

FString UCaptureManager::GetRightCameraGtTrajectoryFilePath() const
{
	return RightCameraGtTrajectoryFilePath;
}

void UCaptureManager::StartManifest()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*CurrentSessionDirectory))
	{
		PlatformFile.CreateDirectoryTree(*CurrentSessionDirectory);
	}
	if (!PlatformFile.DirectoryExists(*CurrentImagesDirectory))
	{
		PlatformFile.CreateDirectoryTree(*CurrentImagesDirectory);
	}

	if (!PlatformFile.DirectoryExists(*CurrentMapsDirectory))
	{
		PlatformFile.CreateDirectoryTree(*CurrentMapsDirectory);
	}

	const FString NavigationDirectory = FPaths::GetPath(RoverGtTrajectoryFilePath);
	if (!PlatformFile.DirectoryExists(*NavigationDirectory)) {
		PlatformFile.CreateDirectoryTree(*NavigationDirectory);
	}
	
	// Manifest = synchronization index only. Pose values are stored in the ROS-style trajectory files.
	const FString Header = TEXT("session_id,frame_index,timestamp_sec\n");

	if (!FFileHelper::SaveStringToFile(Header, *ManifestFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_None))
	{
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: failed to write manifest header to %s"), *ManifestFilePath);
	}

	const FString TrajectoryHeader = TEXT(
		"timestamp_sec,frame_index,frame_id,child_frame_id,"
		"x_m,y_m,z_m,qx,qy,qz,qw\n"
	);

	FFileHelper::SaveStringToFile(TrajectoryHeader, *RoverGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
	FFileHelper::SaveStringToFile(TrajectoryHeader, *LeftCameraGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
	FFileHelper::SaveStringToFile(TrajectoryHeader, *RightCameraGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
}

void UCaptureManager::WriteSessionMetadata()
{
	const FString MetadataFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("session_metadata.json"));
	const FString CreatedAt = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));

	const FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"session_id\": %d,\n")
		TEXT("  \"session_name\": \"%s\",\n")
		TEXT("  \"created_at\": \"%s\",\n")
		TEXT("  \"capture_mode\": \"%s\",\n")
		TEXT("  \"publish_hz\": %d,\n")
		TEXT("  \"image_width\": %d,\n")
		TEXT("  \"image_height\": %d,\n")
		TEXT("  \"stereo_baseline_m\": %s\n")
		TEXT("}\n"),
		CurrentSessionId.load(),
		*CurrentSessionName,
		*CreatedAt,
		*CaptureModeToString(Config.CaptureMode),
		Config.PublishHz,
		Config.ImageWidth,
		Config.ImageHeight,
		*FString::SanitizeFloat(Config.StereoBaselineMeters)
	);

	if (!FFileHelper::SaveStringToFile(Json, *MetadataFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: failed to write session metadata to %s"), *MetadataFilePath);
	}
}



void UCaptureManager::EnsureDatasetRunDirectory()
{
	FString MarkerRunDirectory;
	if (TryReadCurrentDatasetRunDirectory(MarkerRunDirectory))
	{
		CurrentDatasetRunDirectory = MarkerRunDirectory;
	}
	else if (CurrentDatasetRunDirectory.IsEmpty())
	{
		CurrentDatasetRunDirectory = CreateNewDatasetRunDirectory();
	}

	CurrentMapsDirectory = FPaths::Combine(CurrentDatasetRunDirectory, TEXT("Maps"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*CurrentDatasetRunDirectory))
	{
		PlatformFile.CreateDirectoryTree(*CurrentDatasetRunDirectory);
	}
	if (!PlatformFile.DirectoryExists(*CurrentMapsDirectory))
	{
		PlatformFile.CreateDirectoryTree(*CurrentMapsDirectory);
	}
}

void UCaptureManager::AppendManifestRow(const FCaptureFrameInfo& FrameInfo)
{
	const FString Row = FString::Printf(
		TEXT("%d,%d,%.9f\n"),
		FrameInfo.SessionId,
		FrameInfo.FrameIndex,
		FrameInfo.StampSeconds
	);

	if (!FFileHelper::SaveStringToFile(
		Row,
		*ManifestFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append))
	{
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: failed to append manifest row to %s"), *ManifestFilePath);
	}
}

void UCaptureManager::AppendTrajectoryRow(
	const FString& FilePath,
	const FCaptureFrameInfo& FrameInfo,
	const FCapturePose& Pose,
	const FString& FrameId,
	const FString& ChildFrameId)
{
	if (!Pose.bValid || FilePath.IsEmpty())
	{
		return;
	}

	const FVector RosLocation = UnrealToRosConversion::PositionCmToRosMeters(Pose.Position);
	const FQuat RosQuat = UnrealToRosConversion::RotationToRosQuat(Pose.Rotation);

	const FString Row = FString::Printf(
		TEXT("%.9f,%d,%s,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"),
		FrameInfo.StampSeconds,
		FrameInfo.FrameIndex,
		*FrameId,
		*ChildFrameId,
		RosLocation.X,
		RosLocation.Y,
		RosLocation.Z,
		RosQuat.X,
		RosQuat.Y,
		RosQuat.Z,
		RosQuat.W
	);

	FFileHelper::SaveStringToFile(
		Row,
		*FilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_Append
	);
}

void UCaptureManager::SetRoverPoseSource(UCapturePoseSourceComponent* InRoverPoseSource)
{
	RoverPoseSource = InRoverPoseSource;
}

void UCaptureManager::SetLeftCameraPoseSource(USceneComponent* InLeftCameraPoseSource)
{
	LeftCameraPoseSource = InLeftCameraPoseSource;
}

void UCaptureManager::SetRightCameraPoseSource(USceneComponent* InRightCameraPoseSource)
{
	RightCameraPoseSource = InRightCameraPoseSource;
}

UCapturePoseSourceComponent* UCaptureManager::FindPoseSourceByName(FName SourceName) const
{
	if (SourceName.IsNone()) return nullptr;
	UWorld* World = nullptr;

	if (const UObject* OuterObject = GetOuter()) {
		World = OuterObject->GetWorld();
	}

	if (!World) {
		return nullptr;
	}

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt) {
		AActor* Actor = *ActorIt;
		if (!Actor) {
			continue;
		}
		TArray<UCapturePoseSourceComponent*> PoseComponents;
		Actor->GetComponents<UCapturePoseSourceComponent>(PoseComponents);
		for (UCapturePoseSourceComponent* PoseComponent : PoseComponents) {
			if (!PoseComponent) {
				continue;
			}
			if (PoseComponent->GetPoseSourceName() == SourceName) {
				return PoseComponent;
			}
		}
	}
	return nullptr;
}
