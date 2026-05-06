// Fill out your copyright notice in the Description page of Project Settings.

#include "Triggers/GTGeneratorTrigger.h"

#include <Engine/World.h>
#include <TimerManager.h>

#include "Generators/GTDataGeneratorComponent.h"
#include "GTFileUtilities.h"

// Sets default values for this component's properties
UGTGeneratorTrigger::UGTGeneratorTrigger()
{
}

namespace
{
    FString GetImagesDirectoryFromCaptureManager(UObject* CaptureManager)
    {
        if (!IsValid(CaptureManager))
        {
            return FString();
        }

        if (UFunction* ImagesDirectoryFunction = CaptureManager->GetClass()->FindFunctionByName(TEXT("GetCurrentImagesDirectory")))
        {
            struct FImagesDirectoryParams
            {
                FString ReturnValue;
            };

            FImagesDirectoryParams Params;
            CaptureManager->ProcessEvent(ImagesDirectoryFunction, &Params);
            return Params.ReturnValue;
        }

        return FString();
    }

    void ApplyCaptureManagerOutputDirectory(UObject* CaptureManager)
    {
        const FString ImagesDirectory = GetImagesDirectoryFromCaptureManager(CaptureManager);
        if (!ImagesDirectory.IsEmpty())
        {
            FGTFileUtilities::SetSessionOutputDirectoryOverride(ImagesDirectory);
        }
    }
}

// Legacy trigger method - calls generators without session validation
void UGTGeneratorTrigger::Trigger()
{
    FDateTime TimeStamp = FDateTime::Now();
    for (const FGTGeneratorReference& GeneratorReference : DataGenerators)
    {
        UGTDataGeneratorComponent* GeneratorComponent = GeneratorReference.GetComponent(GetOwner());
        if (GeneratorComponent)
        {
            GeneratorComponent->GenerateData(TimeStamp);
        }
    }
}

// Session-aware trigger method - validates sessions to prevent stale async tasks
void UGTGeneratorTrigger::TriggerWithSession(int32 SessionId, UObject* CaptureManager)
{
    if (!CaptureManager)
    {
        // Fallback to legacy trigger if no CaptureManager provided
        Trigger();
        return;
    }

    ApplyCaptureManagerOutputDirectory(CaptureManager);

    // Create session validator that validates against the provided CaptureManager
    auto SessionValidator = [CaptureManager](int32 TestSessionId) -> bool
    {
        if (!IsValid(CaptureManager))
        {
            return false;
        }
        
        // Use reflection to call IsSessionValid on the CaptureManager
        if (UFunction* ValidateFunction = CaptureManager->GetClass()->FindFunctionByName(TEXT("IsSessionValid")))
        {
            struct FValidateParams
            {
                int32 SessionId;
                bool ReturnValue;
            };
            
            FValidateParams Params;
            Params.SessionId = TestSessionId;
            Params.ReturnValue = false;
            
            CaptureManager->ProcessEvent(ValidateFunction, &Params);
            return Params.ReturnValue;
        }
        
        return false;
    };

    FDateTime TimeStamp = FDateTime::Now();
    for (const FGTGeneratorReference& GeneratorReference : DataGenerators)
    {
        UGTDataGeneratorComponent* GeneratorComponent = GeneratorReference.GetComponent(GetOwner());
        if (GeneratorComponent)
        {
            // Call session-aware GenerateData method
            GeneratorComponent->GenerateData(TimeStamp, SessionId, SessionValidator);
        }
    }
}

// Frame-aware trigger method - triggers generators with external frame index
void UGTGeneratorTrigger::TriggerWithFrame(
    int32 FrameIndex,
    double StampSeconds,
    int32 SessionId,
    UObject* CaptureManager)
{
    if (!CaptureManager)
    {
        // Fallback to legacy trigger if no CaptureManager provided
        Trigger();
        return;
    }

    ApplyCaptureManagerOutputDirectory(CaptureManager);

    // Create session validator that validates against the provided CaptureManager
    auto SessionValidator = [CaptureManager](int32 TestSessionId) -> bool
    {
        if (!IsValid(CaptureManager))
        {
            return false;
        }
        
        // Use reflection to call IsSessionValid on the CaptureManager
        if (UFunction* ValidateFunction = CaptureManager->GetClass()->FindFunctionByName(TEXT("IsSessionValid")))
        {
            struct FValidateParams
            {
                int32 SessionId;
                bool ReturnValue;
            };
            
            FValidateParams Params;
            Params.SessionId = TestSessionId;
            Params.ReturnValue = false;
            
            CaptureManager->ProcessEvent(ValidateFunction, &Params);
            return Params.ReturnValue;
        }
        
        return false;
    };

    FDateTime TimeStamp = FDateTime::Now();
    for (const FGTGeneratorReference& GeneratorReference : DataGenerators)
    {
        UGTDataGeneratorComponent* GeneratorComponent = GeneratorReference.GetComponent(GetOwner());
        if (GeneratorComponent)
        {
            // Call frame-aware GenerateData method
            GeneratorComponent->GenerateData(TimeStamp, SessionId, SessionValidator, FrameIndex, StampSeconds);
        }
    }
}


// Called when the game starts
void UGTGeneratorTrigger::BeginPlay()
{
    Super::BeginPlay();
}

