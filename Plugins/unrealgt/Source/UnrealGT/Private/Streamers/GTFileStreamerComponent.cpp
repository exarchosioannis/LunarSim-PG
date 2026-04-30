// Fill out your copyright notice in the Description page of Project Settings.

#include "Streamers/GTFileStreamerComponent.h"

#include "Engine/World.h"
#include "Misc/Paths.h"

#include "GTFileUtilities.h"

UGTFileStreamerComponent::UGTFileStreamerComponent()
    : FileNameFormat(TEXT("{ID}_{Time}.txt"))
    , IDCounter(0)
{
}

void UGTFileStreamerComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UGTFileStreamerComponent::OnDataReady(const TArray<uint8>& Data, const FDateTime& TimeStamp, int32 FrameIndex)
{
    FString TimeString = TimeStamp.ToString(FGTFileUtilities::TimeFormat);

    // Use FrameIndex if valid, otherwise use IDCounter
    const int32 FileId = FrameIndex >= 0 ? FrameIndex : IDCounter;

    TMap<FString, FStringFormatArg> GlobalProperties{{TEXT("ID"), FileId},
                                                     {TEXT("Time"), TimeString}};

    // Only increment IDCounter for legacy frames
    if (FrameIndex < 0)
    {
        IDCounter++;
    }

    FString FileName = FString::Format(*FileNameFormat, GlobalProperties);

    FString TotalFileName = FPaths::Combine(GetName(), FileName);

    FGTFileUtilities::WriteFileToSessionDirectory(TotalFileName, Data, GetWorld());
}
