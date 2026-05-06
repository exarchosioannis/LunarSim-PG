// Fill out your copyright notice in the Description page of Project Settings.

#include "GTFileUtilities.h"

#include "Engine/World.h"
#include "Misc/Paths.h"

const FDateTime FGTFileUtilities::SessionStartTime = FDateTime::Now();
const TCHAR* FGTFileUtilities::TimeFormat = TEXT("%Y-%m-%d_%H-%M-%S");
FString FGTFileUtilities::SessionOutputDirectoryOverride;

TArray<uint8> FGTFileUtilities::StringToCharArray(const FString& InString)
{
    TArray<uint8> Data;
    Data.Reserve(InString.Len());

    FTCHARToUTF8 UTF8Str(*InString);

    for (int I = 0; I < UTF8Str.Length(); I++)
    {
        Data.Add(UTF8Str.Get()[I]);
    }

    return Data;
}

void FGTFileUtilities::SetSessionOutputDirectoryOverride(const FString& InDirectory)
{
    SessionOutputDirectoryOverride = FPaths::ConvertRelativePathToFull(InDirectory);
}

void FGTFileUtilities::ClearSessionOutputDirectoryOverride()
{
    SessionOutputDirectoryOverride.Empty();
}

FString FGTFileUtilities::GetSessionOutputDirectory(UWorld* CurrentWorld)
{
    if (!SessionOutputDirectoryOverride.IsEmpty())
    {
        return SessionOutputDirectoryOverride;
    }

    const FString MapName = CurrentWorld ? CurrentWorld->GetMapName() : TEXT("UnknownWorld");

    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Datasets"),
        MapName,
        SessionStartTime.ToString(TimeFormat)));
}

void FGTFileUtilities::WriteFileToSessionDirectory(
    FString FileName,
    const TArray<uint8>& Data,
    UWorld* CurrentWorld)
{
    FString SessionDir = GetSessionOutputDirectory(CurrentWorld);
    FString TotalFileName = FPaths::Combine(SessionDir, FileName);

    (new FAutoDeleteAsyncTask<FGTSaveFileTask>(TotalFileName, Data))->StartBackgroundTask();
}
