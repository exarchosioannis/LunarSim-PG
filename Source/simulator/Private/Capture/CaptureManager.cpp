#include "Capture/CaptureManager.h"
#include "Capture/CapturePoseSourceComponent.h"
#include "Utils/DatasetRunSubsystem.h"
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
	FString CaptureOutputsToString(const FCaptureConfig& Config)
	{
		TArray<FString> Outputs;
		if (Config.IsStereoRosEnabled()) {
			Outputs.Add(TEXT("StereoRosImages"));
		}
		if (Config.IsGroundTruthEnabled()) {
			TArray<FString> GroundTruthOutputs;
			if (Config.IsGroundTruthRgbEnabled()) {
				GroundTruthOutputs.Add(TEXT("RGB"));
			}
			if (Config.IsGroundTruthDepthEnabled()) {
				GroundTruthOutputs.Add(TEXT("Depth"));
			}
			if (Config.IsGroundTruthSegmentationEnabled()) {
				GroundTruthOutputs.Add(TEXT("Segmentation"));
			}
			if (Config.IsGroundTruthBoundingBoxesEnabled()) {
				GroundTruthOutputs.Add(TEXT("BoundingBoxes"));
			}
			Outputs.Add(FString::Printf(TEXT("GroundTruthImages(%s)"), *FString::Join(GroundTruthOutputs, TEXT("|"))));
		}
		if (Config.IsTrajectoryCsvEnabled()) {
			Outputs.Add(TEXT("TrajectoryCsv"));
		}

		return Outputs.Num() > 0 ? FString::Join(Outputs, TEXT("+")) : TEXT("None");
	}

	FString NormalizeManifestPath(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Path;
	}
}


void UCaptureManager::Initialize(const FCaptureConfig& InConfig, const FResolvedCameraCalibration& InCameraCalibration)
{
	if (bConfigInitialized) {
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: Initialize ignored because capture config is already initialized for this run."));
		return;
	}

	Config = InConfig;
	if (Config.Sanitize()) {
		UE_LOG(LogTemp, Warning,
			TEXT("CaptureManager: invalid capture config values were normalized to CaptureHz=%.3f, HorizontalFovDeg=%.2f, StereoBaselineCm=%.2f."),
			Config.GetResolvedCaptureHz(),
			Config.GetResolvedHorizontalFovDeg(),
			Config.StereoBaselineCm);
	}
	CameraCalibration = InCameraCalibration;
	const FResolvedCameraCalibration ConfigCalibration = Config.GetResolvedCameraCalibration();
	if (!CameraCalibration.IsValid()
		|| CameraCalibration.ImageWidth != ConfigCalibration.ImageWidth
		|| CameraCalibration.ImageHeight != ConfigCalibration.ImageHeight
		|| !FMath::IsNearlyEqual(CameraCalibration.HorizontalFovDeg, ConfigCalibration.HorizontalFovDeg)) {
		UE_LOG(LogTemp, Warning,
			TEXT("CaptureManager: supplied camera calibration did not match sanitized config; using the config-resolved calibration."));
		CameraCalibration = ConfigCalibration;
	}
	bConfigInitialized = true;
}

UWorld* UCaptureManager::GetWorld() const
{
	if (const UObject* OuterObject = GetOuter()) {
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
	if (CurrentDatasetRunDirectory.IsEmpty()) {
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: cannot start capture because dataset run directory is unavailable."));
		return;
	}

	CurrentSessionId++;
	FrameIndex = 0;

	const FString DefaultSessionName = FString::Printf(TEXT("Session_%03d"), CurrentSessionId.load());
	CurrentSessionName = DefaultSessionName;
	CurrentSessionDirectory.Empty();

	if (UWorld* World = GetWorld()) {
		if (UDatasetRunSubsystem* DatasetRunSubsystem = World->GetSubsystem<UDatasetRunSubsystem>()) {
			CurrentSessionDirectory = DatasetRunSubsystem->CreateNextSessionDirectory(CurrentSessionName);
			CurrentDatasetRunDirectory = DatasetRunSubsystem->GetOrCreateCurrentRunDirectory();
			CurrentMapsDirectory = DatasetRunSubsystem->GetMapsDirectory();
		}
	}

	if (CurrentSessionName.IsEmpty()) {
		CurrentSessionName = DefaultSessionName;
	}
	if (CurrentSessionDirectory.IsEmpty()) {
		CurrentSessionDirectory = FPaths::Combine(CurrentDatasetRunDirectory, CurrentSessionName);
	}

	CurrentImagesDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Images"));
	if (CurrentMapsDirectory.IsEmpty()) {
		CurrentMapsDirectory = FPaths::Combine(CurrentDatasetRunDirectory, TEXT("Maps"));
	}

	ManifestFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("manifest.csv"));
	const FString NavigationDirectory = FPaths::Combine(CurrentSessionDirectory, TEXT("Navigation"));
	RoverGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("rover_gt_trajectory_ros.csv"));
	LeftCameraGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("left_camera_gt_trajectory_ros.csv"));
	RightCameraGtTrajectoryFilePath = FPaths::Combine(NavigationDirectory, TEXT("right_camera_gt_trajectory_ros.csv"));

	// The manifest is always created for every capture session.
	// It is the main synchronization file between ROS, UnrealGT, and offline tools.
	if (!RoverPoseSource) {
		RoverPoseSource = FindPoseSourceByName(TEXT("base_link"));
	}

	bCaptureEnabled = true;
	StartManifest();
	WriteSessionMetadata();

	UE_LOG(LogTemp, Log, TEXT("CaptureManager: started %s inside dataset run %s"), *CurrentSessionName, *CurrentDatasetRunDirectory);
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

	FCaptureFrameInfo FrameInfo;
	FrameInfo.FrameIndex = FrameIndex;
	FrameInfo.StampSeconds = StampSeconds;
	FrameInfo.SessionId = CurrentSessionId;
	FrameIndex++;

	// Always write one canonical synchronization row for every capture frame.
	AppendManifestRow(FrameInfo);

	// Pose data lives in trajectory files, in ROS coordinates, with matching column names.
	// Rover trajectory is always written when rover pose is available.
	// Left camera trajectory is written when left/reference imagery exists: left ROS or UnrealGT.
	// Right camera trajectory is written only for stereo ROS modes.
	if (Config.IsTrajectoryCsvEnabled()) {
		AppendTrajectoryRow(RoverGtTrajectoryFilePath, FrameInfo, CompletePoseData.RoverBasePose, TEXT("map"), TEXT("base_link"));
		if (Config.IsLeftRosCameraEnabled() || Config.IsGroundTruthEnabled()) {
			AppendTrajectoryRow(LeftCameraGtTrajectoryFilePath, FrameInfo, CompletePoseData.LeftCameraPose, TEXT("map"), TEXT("left_camera_link"));
		}
		if (Config.IsRightRosCameraEnabled()) {
			AppendTrajectoryRow(RightCameraGtTrajectoryFilePath, FrameInfo, CompletePoseData.RightCameraPose, TEXT("map"), TEXT("right_camera_link"));
		}
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
	
	// Manifest = per-frame dataset index. Pose values remain stored in the ROS-style trajectory files.
	const FString Header = TEXT(
		"session_id,frame_index,timestamp_sec,"
		"rgb_path,depth_path,segmentation_path,bounding_boxes_path,"
		"rover_trajectory_file,left_camera_trajectory_file,right_camera_trajectory_file\n"
	);

	if (!FFileHelper::SaveStringToFile(Header, *ManifestFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_None))
	{
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: failed to write manifest header to %s"), *ManifestFilePath);
	}

	const FString TrajectoryHeader = TEXT(
		"timestamp_sec,frame_index,frame_id,child_frame_id,"
		"x_m,y_m,z_m,qx,qy,qz,qw\n"
	);

	if (Config.IsTrajectoryCsvEnabled()) {
		FFileHelper::SaveStringToFile(TrajectoryHeader, *RoverGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
		if (Config.IsStereoRosEnabled() || Config.IsGroundTruthEnabled()) {
			FFileHelper::SaveStringToFile(TrajectoryHeader, *LeftCameraGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
		}
		if (Config.IsStereoRosEnabled()) {
			FFileHelper::SaveStringToFile(TrajectoryHeader, *RightCameraGtTrajectoryFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
		}
	}
}

void UCaptureManager::WriteSessionMetadata()
{
	const FString MetadataFilePath = FPaths::Combine(CurrentSessionDirectory, TEXT("session_metadata.json"));
	const FString CreatedAt = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	const FString LevelName = GetWorld() ? GetWorld()->GetMapName() : TEXT("Unknown");
	const auto EscapeJsonString = [](const FString& Value) -> FString
	{
		FString Escaped;
		Escaped.Reserve(Value.Len());
		for (int32 Index = 0; Index < Value.Len(); ++Index) {
			const TCHAR Character = Value[Index];
			switch (Character) {
			case TEXT('\\'):
				Escaped += TEXT("\\\\");
				break;
			case TEXT('"'):
				Escaped += TEXT("\\\"");
				break;
			case TEXT('\b'):
				Escaped += TEXT("\\b");
				break;
			case TEXT('\f'):
				Escaped += TEXT("\\f");
				break;
			case TEXT('\n'):
				Escaped += TEXT("\\n");
				break;
			case TEXT('\r'):
				Escaped += TEXT("\\r");
				break;
			case TEXT('\t'):
				Escaped += TEXT("\\t");
				break;
			default:
				if (Character < 0x20) {
					Escaped += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
				} else {
					Escaped.AppendChar(Character);
				}
				break;
			}
		}
		return Escaped;
	};
	const auto JsonNumber = [](double Value) -> FString
	{
		return FString::SanitizeFloat(Value);
	};
	const double StereoBaselineMeters = Config.GetStereoBaselineMeters();
	const double RightTx = -CameraCalibration.Fx * StereoBaselineMeters;
	const FString LeftProjectionMatrix = FString::Printf(
		TEXT("[%s, 0.0, %s, 0.0, 0.0, %s, %s, 0.0, 0.0, 0.0, 1.0, 0.0]"),
		*JsonNumber(CameraCalibration.Fx),
		*JsonNumber(CameraCalibration.Cx),
		*JsonNumber(CameraCalibration.Fy),
		*JsonNumber(CameraCalibration.Cy));
	const FString RightProjectionMatrix = FString::Printf(
		TEXT("[%s, 0.0, %s, %s, 0.0, %s, %s, 0.0, 0.0, 0.0, 1.0, 0.0]"),
		*JsonNumber(CameraCalibration.Fx),
		*JsonNumber(CameraCalibration.Cx),
		*JsonNumber(RightTx),
		*JsonNumber(CameraCalibration.Fy),
		*JsonNumber(CameraCalibration.Cy));

	const FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"session_id\": %d,\n")
		TEXT("  \"session_name\": \"%s\",\n")
		TEXT("  \"created_at\": \"%s\",\n")
		TEXT("  \"capture_mode\": \"%s\",\n")
		TEXT("  \"publish_hz\": %s,\n")
		TEXT("  \"camera\": {\n")
		TEXT("    \"image_width\": %d,\n")
		TEXT("    \"image_height\": %d,\n")
		TEXT("    \"fov_deg\": %s,\n")
		TEXT("    \"fx\": %s,\n")
		TEXT("    \"fy\": %s,\n")
		TEXT("    \"cx\": %s,\n")
		TEXT("    \"cy\": %s,\n")
		TEXT("    \"distortion_model\": \"plumb_bob\",\n")
		TEXT("    \"distortion_coefficients\": [0.0, 0.0, 0.0, 0.0, 0.0],\n")
		TEXT("    \"stereo_baseline_m\": %s,\n")
		TEXT("    \"left_projection_matrix\": %s,\n")
		TEXT("    \"right_projection_matrix\": %s\n")
		TEXT("  },\n")
		TEXT("  \"frames\": {\n")
		TEXT("    \"map\": \"map\",\n")
		TEXT("    \"base_link\": \"base_link\",\n")
		TEXT("    \"imu\": \"imu_link\",\n")
		TEXT("    \"left_camera_link\": \"left_camera_link\",\n")
		TEXT("    \"right_camera_link\": \"right_camera_link\",\n")
		TEXT("    \"left_camera_optical\": \"left_camera_optical_frame\",\n")
		TEXT("    \"right_camera_optical\": \"right_camera_optical_frame\"\n")
		TEXT("  },\n")
		TEXT("  \"maps\": {\n")
		TEXT("    \"enabled\": true,\n")
		TEXT("    \"occupancy_map\": \"Maps/occupancy_map.yaml\",\n")
		TEXT("    \"elevation_map\": \"Maps/elevation_map.yaml\",\n")
		TEXT("    \"elevation_csv\": \"Maps/elevation_map.csv\",\n")
		TEXT("    \"elevation_preview\": \"Maps/elevation_map_preview.pgm\"\n")
		TEXT("  },\n")
		TEXT("  \"ground_truth_outputs\": {\n")
		TEXT("    \"enabled\": %s,\n")
		TEXT("    \"rgb\": %s,\n")
		TEXT("    \"depth\": %s,\n")
		TEXT("    \"segmentation\": %s,\n")
		TEXT("    \"bounding_boxes\": %s\n")
		TEXT("  },\n")
		TEXT("  \"scene\": {\n")
		TEXT("    \"level_name\": \"%s\"\n")
		TEXT("  }\n")
		TEXT("}\n"),
		CurrentSessionId.load(),
		*EscapeJsonString(CurrentSessionName),
		*EscapeJsonString(CreatedAt),
		*EscapeJsonString(CaptureOutputsToString(Config)),
		*FString::SanitizeFloat(Config.GetResolvedCaptureHz()),
		CameraCalibration.ImageWidth,
		CameraCalibration.ImageHeight,
		*JsonNumber(CameraCalibration.HorizontalFovDeg),
		*JsonNumber(CameraCalibration.Fx),
		*JsonNumber(CameraCalibration.Fy),
		*JsonNumber(CameraCalibration.Cx),
		*JsonNumber(CameraCalibration.Cy),
		*JsonNumber(StereoBaselineMeters),
		*LeftProjectionMatrix,
		*RightProjectionMatrix,
		Config.IsGroundTruthEnabled() ? TEXT("true") : TEXT("false"),
		Config.IsGroundTruthRgbEnabled() ? TEXT("true") : TEXT("false"),
		Config.IsGroundTruthDepthEnabled() ? TEXT("true") : TEXT("false"),
		Config.IsGroundTruthSegmentationEnabled() ? TEXT("true") : TEXT("false"),
		Config.IsGroundTruthBoundingBoxesEnabled() ? TEXT("true") : TEXT("false"),
		*EscapeJsonString(LevelName)
	);

	if (!FFileHelper::SaveStringToFile(Json, *MetadataFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: failed to write session metadata to %s"), *MetadataFilePath);
	}
}



void UCaptureManager::EnsureDatasetRunDirectory()
{
	if (UWorld* World = GetWorld()) {
		if (UDatasetRunSubsystem* DatasetRunSubsystem = World->GetSubsystem<UDatasetRunSubsystem>()) {
			CurrentDatasetRunDirectory = DatasetRunSubsystem->GetOrCreateCurrentRunDirectory();
			CurrentMapsDirectory = DatasetRunSubsystem->GetMapsDirectory();
		}
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (CurrentDatasetRunDirectory.IsEmpty()) {
		UE_LOG(LogTemp, Warning, TEXT("CaptureManager: dataset run directory is unavailable."));
		return;
	}

	if (!PlatformFile.DirectoryExists(*CurrentDatasetRunDirectory)) {
		PlatformFile.CreateDirectoryTree(*CurrentDatasetRunDirectory);
	}

	if (!PlatformFile.DirectoryExists(*CurrentMapsDirectory)) {
		PlatformFile.CreateDirectoryTree(*CurrentMapsDirectory);
	}
}

void UCaptureManager::AppendManifestRow(const FCaptureFrameInfo& FrameInfo)
{
	const FString FrameFileStem = FString::FromInt(FrameInfo.FrameIndex);

	const FString RgbPath = Config.IsGroundTruthRgbEnabled()
		? NormalizeManifestPath(FPaths::Combine(TEXT("Images"), TEXT("RGB"), FrameFileStem + TEXT(".png")))
		: FString();
	const FString DepthPath = Config.IsGroundTruthDepthEnabled()
		? NormalizeManifestPath(FPaths::Combine(TEXT("Images"), TEXT("Depth"), FrameFileStem + TEXT(".png")))
		: FString();
	const FString SegmentationPath = Config.IsGroundTruthSegmentationEnabled()
		? NormalizeManifestPath(FPaths::Combine(TEXT("Images"), TEXT("Segmentation"), FrameFileStem + TEXT(".png")))
		: FString();
	const FString BoundingBoxesPath = Config.IsGroundTruthBoundingBoxesEnabled()
		? NormalizeManifestPath(FPaths::Combine(TEXT("Images"), TEXT("BoundingBoxes"), FrameFileStem + TEXT(".csv")))
		: FString();

	const bool bTrajectoryCsvEnabled = Config.IsTrajectoryCsvEnabled();
	const FString RoverTrajectoryPath = !bTrajectoryCsvEnabled || RoverGtTrajectoryFilePath.IsEmpty()
		? FString()
		: NormalizeManifestPath(FPaths::Combine(TEXT("Navigation"), TEXT("rover_gt_trajectory_ros.csv")));
	const bool bLeftCameraTrajectoryEnabled = Config.IsStereoRosEnabled() || Config.IsGroundTruthEnabled();
	const bool bRightCameraTrajectoryEnabled = Config.IsStereoRosEnabled();
	const FString LeftCameraTrajectoryPath = !bTrajectoryCsvEnabled || !bLeftCameraTrajectoryEnabled || LeftCameraGtTrajectoryFilePath.IsEmpty()
		? FString()
		: NormalizeManifestPath(FPaths::Combine(TEXT("Navigation"), TEXT("left_camera_gt_trajectory_ros.csv")));
	const FString RightCameraTrajectoryPath = !bTrajectoryCsvEnabled || !bRightCameraTrajectoryEnabled || RightCameraGtTrajectoryFilePath.IsEmpty()
		? FString()
		: NormalizeManifestPath(FPaths::Combine(TEXT("Navigation"), TEXT("right_camera_gt_trajectory_ros.csv")));

	const FString Row = FString::Printf(
		TEXT("%d,%d,%.9f,%s,%s,%s,%s,%s,%s,%s\n"),
		FrameInfo.SessionId,
		FrameInfo.FrameIndex,
		FrameInfo.StampSeconds,
		*RgbPath,
		*DepthPath,
		*SegmentationPath,
		*BoundingBoxesPath,
		*RoverTrajectoryPath,
		*LeftCameraTrajectoryPath,
		*RightCameraTrajectoryPath
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
