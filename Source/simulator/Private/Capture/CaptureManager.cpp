#include "Capture/CaptureManager.h"
#include "Capture/CapturePoseSourceComponent.h"
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
	CurrentSessionId++;
	FrameIndex = 0;
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

void UCaptureManager::StartManifest()
{
	//Create the file if it doesn't exist.
	FString ManifestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CaptureManifests"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ManifestDir))
	{
		PlatformFile.CreateDirectoryTree(*ManifestDir);
	}
	FString DateString = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));

	ManifestFilePath = FPaths::Combine(ManifestDir, FString::Printf(TEXT("%s_session_%d.csv"), *DateString, CurrentSessionId.load()));
	
	//Write the csv header
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
}

void UCaptureManager::SetRoverPoseSource(UCapturePoseSourceComponent* InRoverPoseSource)
{
	RoverPoseSource = InRoverPoseSource;
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