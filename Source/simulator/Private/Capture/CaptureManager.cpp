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

void UCaptureManager::Initialize(const FCaptureConfig& InConfig)
{
	Config = InConfig;
}

void UCaptureManager::StartCapture()
{
	if (bCaptureEnabled) {
		UE_LOG(LogTemp, Warning, TEXT("Capture is already running. StartCapture ignored."));
		return;
	}

	CurrentSessionId++;
	FrameIndex = 0;

	const FString DateString = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	//session name format 2026-05-11_15-45-12_session_1.
	CurrentSessionName = FString::Printf(TEXT("%s_session_%d"), *DateString, CurrentSessionId.load());
	CurrentSessionDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Datasets"), CurrentSessionName));
	CurrentImagesDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Images"));
	ManifestFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("manifest.csv"));
	//navigation folder and trajectory files for ROS gt pose.
	const FString NavigationDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Navigation"));
	RoverGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("rover_gt_trajectory_ros.csv"));
	LeftCameraGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("left_camera_gt_trajectory_ros.csv"));
	RightCameraGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("right_camera_gt_trajectory_ros.csv"));

	if (!RoverPoseSource) {
		RoverPoseSource = FindPoseSourceByName(TEXT("rover_base"));
	}
	bCaptureEnabled = true;
	StartManifest();
}

void UCaptureManager::StopCapture()
{
	bCaptureEnabled = false;
}

bool UCaptureManager::IsCaptureEnabled() const
{
	return bCaptureEnabled;
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

	const FString NavigationDirectory = FPaths::GetPath(RoverGtTrajectoryFilePath);
	if (!PlatformFile.DirectoryExists(*NavigationDirectory)) {
		PlatformFile.CreateDirectoryTree(*NavigationDirectory);
	}
	
	//Manifest = synchronization index.
	const FString Header = TEXT(
		"session_id,frame_index,timestamp_sec,"
		"has_rover_gt,has_left_camera,has_right_camera,has_gt_camera\n"
	);

	FFileHelper::SaveStringToFile(Header, *ManifestFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);

	const FString TrajectoryHeader = TEXT(
		"timestamp_sec,frame_index,frame_id,child_frame_id,"
		"x_m,y_m,z_m,qx,qy,qz,qw\n"
	);

	FFileHelper::SaveStringToFile(TrajectoryHeader, *RoverGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
	FFileHelper::SaveStringToFile(TrajectoryHeader, *LeftCameraGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
	FFileHelper::SaveStringToFile(TrajectoryHeader, *RightCameraGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
}

void UCaptureManager::AppendManifestRow(const FCaptureFrameInfo& FrameInfo, const FCaptureFramePoseData& PoseData)
{
	const int32 HasRoverGt = PoseData.RoverBasePose.bValid ? 1 : 0;
	const int32 HasLeftCamera = ((Config.IsLeftRosCameraEnabled() || Config.IsGroundTruthEnabled()) && PoseData.LeftCameraPose.bValid) ? 1 : 0;
	const int32 HasRightCamera = (Config.IsRightRosCameraEnabled() && PoseData.RightCameraPose.bValid) ? 1 : 0;
	const int32 HasGtCamera = Config.IsGroundTruthEnabled() ? 1 : 0;

	const FString Row = FString::Printf(
		TEXT("%d,%d,%.9f,%d,%d,%d,%d\n"),
		FrameInfo.SessionId,
		FrameInfo.FrameIndex,
		FrameInfo.StampSeconds,
		HasRoverGt,
		HasLeftCamera,
		HasRightCamera,
		HasGtCamera
	);

	FFileHelper::SaveStringToFile(Row, *ManifestFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

FCaptureFrameInfo UCaptureManager::NextFrame(double StampSeconds)
{
	//pose data collection.
	FCaptureFramePoseData PoseData;
	if (RoverPoseSource) {
		PoseData.RoverBasePose = RoverPoseSource->GetWorldCapturePose();
	}

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

	//frame info and manifest + trajectory logging.
	FrameIndex++;
	FCaptureFrameInfo FrameInfo;
	FrameInfo.FrameIndex = FrameIndex;
	FrameInfo.StampSeconds = StampSeconds;
	FrameInfo.SessionId = CurrentSessionId;

	//Manifest row.
	AppendManifestRow(FrameInfo, PoseData);

	//Navigation trajectory files.
	AppendTrajectoryRow(RoverGtTrajectoryFilePath, FrameInfo, PoseData.RoverBasePose, TEXT("map"), TEXT("rover_base"));
	if (Config.IsLeftRosCameraEnabled() || Config.IsGroundTruthEnabled()) {
		AppendTrajectoryRow(LeftCameraGtTrajectoryFilePath, FrameInfo, PoseData.LeftCameraPose, TEXT("map"), TEXT("left_camera"));
	}
	if (Config.IsRightRosCameraEnabled()) {
		AppendTrajectoryRow(RightCameraGtTrajectoryFilePath, FrameInfo, PoseData.RightCameraPose, TEXT("map"), TEXT("right_camera"));
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

FString UCaptureManager::GetCurrentImagesDirectory() const
{
	return CurrentImagesDirectory;
}

//unit conversion, from cm to meters
FVector UCaptureManager::UnrealLocationToRosMeters(const FVector& UnrealLocation)
{
	return FVector(UnrealLocation.X / 100.0, UnrealLocation.Y / 100.0, UnrealLocation.Z / 100.0);
}

//THE WHOLE LOCATION THING AND POSING THING NEEDS REVISIT.
FQuat UCaptureManager::UnrealRotationToRosQuat(const FRotator& UnrealRotation)
{
	const FQuat Q = UnrealRotation.Quaternion();
	return FQuat(Q.X, Q.Y, Q.Z, Q.W);
}

void UCaptureManager::AppendTrajectoryRow(const FString& FilePath, const FCaptureFrameInfo& FrameInfo, const FCapturePose& Pose, const FString& FrameId, const FString& ChildFrameId) {
	if (!Pose.bValid || FilePath.IsEmpty()) return;

	const FVector RosLocation = UnrealLocationToRosMeters(Pose.Position);
	const FQuat RosQuat = UnrealRotationToRosQuat(Pose.Rotation);
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

	FFileHelper::SaveStringToFile(Row, *FilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
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
