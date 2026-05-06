// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Async/Async.h"
#include "Async/AsyncWork.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

class FGTSaveFileTask : public FNonAbandonableTask
{
    friend class FAutoDeleteAsyncTask<FGTSaveFileTask>;

public:
    FGTSaveFileTask(const FString& TotalFileName, const TArray<uint8>& Data)
        : TotalFileName(TotalFileName)
        , Data(Data)
    {
    }

protected:
    FString TotalFileName;
    TArray<uint8> Data;

    void DoWork()
    {
        const FString Directory = FPaths::GetPath(TotalFileName);
        if (!Directory.IsEmpty())
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
        }
        FFileHelper::SaveArrayToFile(Data, *TotalFileName);
    }

    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FGTSaveFileTask, STATGROUP_ThreadPoolAsyncTasks);
    }
};

/**
 * File helpers for UnrealGT dataset output.
 *
 * By default this keeps the original UnrealGT behaviour:
 * Saved/Datasets/<MapName>/<SessionStartTime>/...
 *
 * When the simulator CaptureManager is running, GTGeneratorTrigger can set an
 * output directory override so UnrealGT writes into:
 * Saved/Datasets/<CaptureSessionName>/Images/...
 */
class UNREALGT_API FGTFileUtilities
{
public:
    static const FDateTime SessionStartTime;
    static const TCHAR* TimeFormat;

    static TArray<uint8> StringToCharArray(const FString& InString);

    static void SetSessionOutputDirectoryOverride(const FString& InDirectory);
    static void ClearSessionOutputDirectoryOverride();
    static FString GetSessionOutputDirectory(UWorld* CurrentWorld);

    static void
    WriteFileToSessionDirectory(FString FileName, const TArray<uint8>& Data, UWorld* CurrentWorld);

private:
    static FString SessionOutputDirectoryOverride;
};
