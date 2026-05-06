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
	if (bCaptureEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("Capture is already running. StartCapture ignored."));
		return;
	}

	CurrentSessionId++;
	FrameIndex = 0;

	const FString DateString = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	CurrentSessionName = FString::Printf(TEXT("%s_session_%d"), *DateString, CurrentSessionId.load());
	CurrentSessionDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Datasets"),
		CurrentSessionName));
	CurrentImagesDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Images"));
	ManifestFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("manifest.csv"));
	RoverGtTrajectoryFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("Navigation"), TEXT("rover_gt_trajectory_ros.csv"));

	// The manifest is always created for every capture session.
	// It is the main synchronization file between ROS, UnrealGT, and offline tools.
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

FCaptureFrameInfo UCaptureManager::NextFrame(double StampSeconds)
{
	FCaptureFramePoseData PoseData;
	if (LeftCameraPoseSource) {
		PoseData.LeftCameraPose.bValid = true;
		PoseData.LeftCameraPose.Position = LeftCameraPoseSource->GetComponentLocation();
		PoseData.LeftCameraPose.Rotation = LeftCameraPoseSource->GetComponentRotation();
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

	// Always write one manifest row for every frame, regardless of capture mode.
	// Mono/GT modes currently store rover + left/reference camera pose.
	// Stereo modes can extend this later with right camera pose columns.
	AppendManifestRow(FrameInfo, CompletePoseData);

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

FString UCaptureManager::GetManifestFilePath() const
{
	return ManifestFilePath;
}

FString UCaptureManager::GetRoverGtTrajectoryFilePath() const
{
	return RoverGtTrajectoryFilePath;
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
	if (!PlatformFile.DirectoryExists(*NavigationDirectory))
	{
		PlatformFile.CreateDirectoryTree(*NavigationDirectory);
	}
	
	// Write the csv header
	FString Header = TEXT(
		"session_id,frame_index,stamp_seconds,"

		"rover_valid,"
		"rover_x_ue_cm,rover_y_ue_cm,rover_z_ue_cm,"
		"rover_roll_ue_deg,rover_pitch_ue_deg,rover_yaw_ue_deg,"

		"left_camera_valid,"
		"left_camera_x_ue_cm,left_camera_y_ue_cm,left_camera_z_ue_cm,"
		"left_camera_roll_ue_deg,left_camera_pitch_ue_deg,left_camera_yaw_ue_deg\n"
	);

	FFileHelper::SaveStringToFile(Header, *ManifestFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);

	const FString RoverTrajectoryHeader = TEXT(
		"timestamp_sec,frame_index,frame_id,child_frame_id,"
		"x_m,y_m,z_m,qx,qy,qz,qw\n"
	);

	FFileHelper::SaveStringToFile(
		RoverTrajectoryHeader,
		*RoverGtTrajectoryFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_None
	);
}

void UCaptureManager::AppendManifestRow(const FCaptureFrameInfo& FrameInfo, const FCaptureFramePoseData& PoseData)
{
	const FCapturePose& RoverPose = PoseData.RoverBasePose;
	const FCapturePose& LeftCameraPose = PoseData.LeftCameraPose;

	const FVector& RoverLocation = RoverPose.Position;
	const FRotator& RoverRotation = RoverPose.Rotation;

	const FVector& LeftCameraLocation = LeftCameraPose.Position;
	const FRotator& LeftCameraRotation = LeftCameraPose.Rotation;

	FString Row = FString::Printf(
		TEXT("%d,%d,%.9f,"
			"%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
			"%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),

		FrameInfo.SessionId,
		FrameInfo.FrameIndex,
		FrameInfo.StampSeconds,

		RoverPose.bValid ? 1 : 0,
		RoverLocation.X,
		RoverLocation.Y,
		RoverLocation.Z,
		RoverRotation.Roll,
		RoverRotation.Pitch,
		RoverRotation.Yaw,

		LeftCameraPose.bValid ? 1 : 0,
		LeftCameraLocation.X,
		LeftCameraLocation.Y,
		LeftCameraLocation.Z,
		LeftCameraRotation.Roll,
		LeftCameraRotation.Pitch,
		LeftCameraRotation.Yaw
	);

	FFileHelper::SaveStringToFile(
		Row,
		*ManifestFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_Append
	);

	AppendRoverGtTrajectoryRow(FrameInfo, RoverPose);
}

FVector UCaptureManager::UnrealLocationToRosMeters(const FVector& UnrealLocation)
{
	return FVector(
		UnrealLocation.X / 100.0,
		-UnrealLocation.Y / 100.0,
		UnrealLocation.Z / 100.0
	);
}

FQuat UCaptureManager::UnrealYawToRosQuat(const FRotator& UnrealRotation)
{
	const double RosYawRad = FMath::DegreesToRadians(-UnrealRotation.Yaw);
	const double HalfYaw = RosYawRad * 0.5;

	return FQuat(
		0.0,
		0.0,
		FMath::Sin(HalfYaw),
		FMath::Cos(HalfYaw)
	);
}

void UCaptureManager::AppendRoverGtTrajectoryRow(const FCaptureFrameInfo& FrameInfo, const FCapturePose& RoverPose)
{
	if (!RoverPose.bValid || RoverGtTrajectoryFilePath.IsEmpty())
	{
		return;
	}

	const FVector RosLocation = UnrealLocationToRosMeters(RoverPose.Position);
	const FQuat RosQuat = UnrealYawToRosQuat(RoverPose.Rotation);

	const FString Row = FString::Printf(
		TEXT("%.9f,%d,map,rover_base,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"),
		FrameInfo.StampSeconds,
		FrameInfo.FrameIndex,
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
		*RoverGtTrajectoryFilePath,
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
