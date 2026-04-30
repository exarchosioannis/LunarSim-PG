#include "Capture/CaptureManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"

void UCaptureManager::Initialize(const FCaptureConfig& InConfig)
{
	Config = InConfig;
}

void UCaptureManager::StartCapture()
{
	CurrentSessionId++;
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
	FrameIndex++;
	FCaptureFrameInfo FrameInfo;
	FrameInfo.FrameIndex = FrameIndex;
	FrameInfo.StampSeconds = StampSeconds;
	FrameInfo.SessionId = CurrentSessionId;
	
	AppendManifestRow(FrameInfo);
	
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
	
	//Set the file path
	FString DateString = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));

	ManifestFilePath = FPaths::Combine(
		ManifestDir,
		FString::Printf(TEXT("%s_session_%d.csv"), *DateString, CurrentSessionId.load())
	);
	
	//Write the csv header
	FString Header = TEXT("session_id,frame_index,stamp_seconds\n");
	FFileHelper::SaveStringToFile(
		Header,
		*ManifestFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_None
	);
}

void UCaptureManager::AppendManifestRow(const FCaptureFrameInfo& FrameInfo)
{
	//Format of the csv <SessionId>,<FrameIndex>,<StampSeconds>
	FString Row = FString::Printf(TEXT("%d,%d,%.9f\n"), FrameInfo.SessionId, FrameInfo.FrameIndex, FrameInfo.StampSeconds);
	FFileHelper::SaveStringToFile(Row, *ManifestFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}