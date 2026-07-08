#include "Sensors/GTCamera.h"
#include "Capture/CaptureManager.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Generators/Image/GTImageGeneratorBase.h"
#include "Triggers/GTGeneratorTrigger.h"

namespace
{
	void AddGeneratorPropertyName(TArray<FName>& OutNames, const TCHAR* BaseName)
	{
		OutNames.Add(FName(BaseName));
		OutNames.Add(FName(*(FString(BaseName) + TEXT("_GEN_VARIABLE"))));
	}
}

AGTCamera::AGTCamera()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AGTCamera::BeginPlay()
{
	Super::BeginPlay();
}

void AGTCamera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AGTCamera::SetGroundTruthResolution(int32 Width, int32 Height)
{
	const FIntPoint Resolution(FMath::Max(1, Width), FMath::Max(1, Height));

	TArray<UGTImageGeneratorBase*> ImageGenerators;
	GetComponents<UGTImageGeneratorBase>(ImageGenerators);
	for (UGTImageGeneratorBase* ImageGenerator : ImageGenerators)
	{
		if (ImageGenerator)
		{
			ImageGenerator->SetResolution(Resolution);
		}
	}
}

void AGTCamera::SetGroundTruthOutputs(
	bool bRGB,
	bool bDepth,
	bool bSegmentation,
	bool bBoundingBoxes)
{
	TArray<FName> EnabledGeneratorComponentProperties;
	if (bRGB)
	{
		AddGeneratorPropertyName(EnabledGeneratorComponentProperties, TEXT("RGBGenerator"));
	}
	if (bDepth)
	{
		AddGeneratorPropertyName(EnabledGeneratorComponentProperties, TEXT("GTDepthImageGenerator"));
	}
	if (bSegmentation)
	{
		AddGeneratorPropertyName(EnabledGeneratorComponentProperties, TEXT("GTSegmentationGenerator"));
	}
	if (bBoundingBoxes)
	{
		AddGeneratorPropertyName(EnabledGeneratorComponentProperties, TEXT("GTActorInfoGenerator"));
	}

	TArray<UGTGeneratorTrigger*> GeneratorTriggers;
	GetComponents<UGTGeneratorTrigger>(GeneratorTriggers);
	for (UGTGeneratorTrigger* GeneratorTrigger : GeneratorTriggers)
	{
		if (GeneratorTrigger)
		{
			GeneratorTrigger->SetEnabledGeneratorComponentProperties(EnabledGeneratorComponentProperties);
		}
	}
}


void AGTCamera::TriggerGTGeneratorsWithFrame(int32 FrameIndex, double StampSeconds, int32 SessionId, UObject* CaptureManager) {
	// Find GTGeneratorTrigger component on this actor using reflection
	for (UActorComponent* Component : GetComponents().Array())
	{
		if (Component && Component->GetClass()->GetName().Contains(TEXT("GTGeneratorTrigger")))
		{
			// Call TriggerWithFrame using reflection
			if (UFunction* TriggerFunction = Component->GetClass()->FindFunctionByName(TEXT("TriggerWithFrame")))
			{
				struct FTriggerFrameParams
				{
					int32 FrameIndex;
					double StampSeconds;
					int32 SessionId;
					UObject* CaptureManager;
				};
				
				FTriggerFrameParams Params;
				Params.FrameIndex = FrameIndex;
				Params.StampSeconds = StampSeconds;
				Params.SessionId = SessionId;
				Params.CaptureManager = CaptureManager;
				
				Component->ProcessEvent(TriggerFunction, &Params);
			}
			break;
		}
	}
}
