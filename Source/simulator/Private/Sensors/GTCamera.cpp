#include "Sensors/GTCamera.h"
#include "Capture/CaptureManager.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"

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