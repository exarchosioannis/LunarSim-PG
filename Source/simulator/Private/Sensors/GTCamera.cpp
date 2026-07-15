#include "Sensors/GTCamera.h"
#include "Components/ActorComponent.h"
#include "Generators/GTDataGeneratorComponent.h"
#include "Generators/Image/GTImageGeneratorBase.h"
#include "Generators/Image/GTSceneCaptureComponent2D.h"
#include "Generators/Text/GTActorInfoGeneratorComponent.h"
#include "Triggers/GTGeneratorTrigger.h"

namespace
{
	void AddGeneratorPropertyName(TArray<FName>& OutNames, const TCHAR* BaseName)
	{
		OutNames.Add(FName(BaseName));
		OutNames.Add(FName(*(FString(BaseName) + TEXT("_GEN_VARIABLE"))));
	}

	bool ContainsGeneratorProperty(const TSet<FName>& Properties, const TCHAR* BaseName)
	{
		return Properties.Contains(FName(BaseName)) ||
			Properties.Contains(FName(*(FString(BaseName) + TEXT("_GEN_VARIABLE"))));
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

void AGTCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	InternalWarmUpState = EGTInternalWarmUpState::Cancelled;
	InternalWarmUpFailure = TEXT("PIE world is ending.");
	Super::EndPlay(EndPlayReason);
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

void AGTCamera::SetGroundTruthCalibration(const FResolvedCameraCalibration& CameraCalibration)
{
	if (!CameraCalibration.IsValid()) {
		UE_LOG(LogTemp, Error, TEXT("GTCamera: invalid resolved camera calibration; UnrealGT projection was not applied."));
		return;
	}

	const FIntPoint Resolution(CameraCalibration.ImageWidth, CameraCalibration.ImageHeight);
	TArray<UGTImageGeneratorBase*> ImageGenerators;
	GetComponents<UGTImageGeneratorBase>(ImageGenerators);
	for (UGTImageGeneratorBase* ImageGenerator : ImageGenerators) {
		if (!ImageGenerator) continue;
		ImageGenerator->SetResolution(Resolution);
		ImageGenerator->SetHorizontalFOV(CameraCalibration.HorizontalFovDeg);
	}
	TArray<UGTSceneCaptureComponent2D*> SceneCaptures;
	GetComponents<UGTSceneCaptureComponent2D>(SceneCaptures);
	for (UGTSceneCaptureComponent2D* SceneCapture : SceneCaptures) {
		if (!SceneCapture) continue;
		SceneCapture->SetResolution(Resolution);
		SceneCapture->ProjectionType = ECameraProjectionMode::Perspective;
		SceneCapture->FOVAngle = CameraCalibration.HorizontalFovDeg;
		SceneCapture->Overscan = 0.0f;
		SceneCapture->bUseCustomProjectionMatrix = false;
		SceneCapture->CustomProjectionMatrix.SetIdentity();

		if (!FMath::IsNearlyEqual(SceneCapture->FOVAngle, CameraCalibration.HorizontalFovDeg)
			|| !FMath::IsNearlyZero(SceneCapture->Overscan)
			|| SceneCapture->bUseCustomProjectionMatrix) {
			UE_LOG(LogTemp, Error,
				TEXT("GTCamera: UnrealGT SceneCapture %s does not match the resolved calibration."),
				*SceneCapture->GetName());
		}
	}
}

void AGTCamera::SetGroundTruthOutputs(
	bool bRGB,
	bool bDepth,
	bool bSegmentation,
	bool bBoundingBoxes)
{
	bGroundTruthRGBEnabled = bRGB;
	bGroundTruthDepthEnabled = bDepth;
	bGroundTruthSegmentationEnabled = bSegmentation;
	bGroundTruthBoundingBoxesEnabled = bBoundingBoxes;

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

bool AGTCamera::RunInternalWarmUp()
{
	if (InternalWarmUpState == EGTInternalWarmUpState::Ready)
	{
		return true;
	}
	if (InternalWarmUpState == EGTInternalWarmUpState::InProgress)
	{
		SetInternalWarmUpFailure(TEXT("Re-entrant UnrealGT warm-up request."));
		return false;
	}
	if (InternalWarmUpState == EGTInternalWarmUpState::Cancelled ||
		InternalWarmUpState == EGTInternalWarmUpState::Failed)
	{
		return false;
	}

	InternalWarmUpState = EGTInternalWarmUpState::InProgress;
	InternalWarmUpFailure.Reset();

	const bool bHasEnabledOutput =
		bGroundTruthRGBEnabled ||
		bGroundTruthDepthEnabled ||
		bGroundTruthSegmentationEnabled ||
		bGroundTruthBoundingBoxesEnabled;
	if (!bHasEnabledOutput)
	{
		InternalWarmUpState = EGTInternalWarmUpState::Ready;
		UE_LOG(LogTemp, Log,
			TEXT("UnrealGT warm-up READY world=%s world_id=%u participants=none (GT outputs disabled)."),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
			GetWorld() ? GetWorld()->GetUniqueID() : 0);
		return true;
	}

	TArray<UGTGeneratorTrigger*> GeneratorTriggers;
	GetComponents<UGTGeneratorTrigger>(GeneratorTriggers);
	if (GeneratorTriggers.IsEmpty())
	{
		SetInternalWarmUpFailure(TEXT("No GTGeneratorTrigger component was found."));
		return false;
	}

	TSet<UGTDataGeneratorComponent*> EnabledGenerators;
	TSet<FName> ResolvedGeneratorProperties;
	for (UGTGeneratorTrigger* GeneratorTrigger : GeneratorTriggers)
	{
		if (!IsValid(GeneratorTrigger))
		{
			continue;
		}

		TArray<UGTDataGeneratorComponent*> TriggerGenerators;
		TArray<FName> TriggerProperties;
		FString ResolveError;
		if (!GeneratorTrigger->ResolveEnabledGeneratorComponents(
				TriggerGenerators, TriggerProperties, ResolveError))
		{
			SetInternalWarmUpFailure(ResolveError);
			return false;
		}

		for (UGTDataGeneratorComponent* Generator : TriggerGenerators)
		{
			EnabledGenerators.Add(Generator);
		}
		for (const FName Property : TriggerProperties)
		{
			ResolvedGeneratorProperties.Add(Property);
		}
	}

	const auto RequireGeneratorProperty = [this, &ResolvedGeneratorProperties](
		bool bRequired, const TCHAR* BaseName) -> bool
	{
		if (!bRequired || ContainsGeneratorProperty(ResolvedGeneratorProperties, BaseName))
		{
			return true;
		}

		SetInternalWarmUpFailure(FString::Printf(
			TEXT("Enabled UnrealGT output '%s' has no resolved generator reference."),
			BaseName));
		return false;
	};

	if (!RequireGeneratorProperty(bGroundTruthRGBEnabled, TEXT("RGBGenerator")) ||
		!RequireGeneratorProperty(bGroundTruthDepthEnabled, TEXT("GTDepthImageGenerator")) ||
		!RequireGeneratorProperty(bGroundTruthSegmentationEnabled, TEXT("GTSegmentationGenerator")) ||
		!RequireGeneratorProperty(bGroundTruthBoundingBoxesEnabled, TEXT("GTActorInfoGenerator")))
	{
		return false;
	}

	TArray<UGTImageGeneratorBase*> ImageParticipants;
	TArray<UGTActorInfoGeneratorComponent*> ActorInfoParticipants;
	TArray<FString> ParticipantNames;
	for (UGTDataGeneratorComponent* Generator : EnabledGenerators)
	{
		if (UGTImageGeneratorBase* ImageGenerator = Cast<UGTImageGeneratorBase>(Generator))
		{
			ImageParticipants.Add(ImageGenerator);
			ParticipantNames.Add(ImageGenerator->GetName());
			continue;
		}

		if (UGTActorInfoGeneratorComponent* ActorInfo =
				Cast<UGTActorInfoGeneratorComponent>(Generator))
		{
			if (ActorInfo->RequiresInternalRenderWarmUp())
			{
				ActorInfoParticipants.Add(ActorInfo);
				ParticipantNames.Add(ActorInfo->GetName() + TEXT("/InternalSegmentationSceneCapture"));
			}
			else
			{
				UE_LOG(LogTemp, Log,
					TEXT("UnrealGT warm-up participant=%s complete (no render required; accurate bounding boxes disabled)."),
					*ActorInfo->GetName());
			}
			continue;
		}

		SetInternalWarmUpFailure(FString::Printf(
			TEXT("Enabled generator '%s' has no supported internal warm-up path."),
			Generator ? *Generator->GetName() : TEXT("None")));
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("UnrealGT warm-up START world=%s world_id=%u participants=[%s]."),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
		GetWorld() ? GetWorld()->GetUniqueID() : 0,
		*FString::Join(ParticipantNames, TEXT(", ")));

	for (UGTImageGeneratorBase* ImageParticipant : ImageParticipants)
	{
		FString Error;
		if (!ImageParticipant->WarmUpCaptureNoOutput(Error))
		{
			SetInternalWarmUpFailure(FString::Printf(
				TEXT("%s failed: %s"), *ImageParticipant->GetName(), *Error));
			return false;
		}
		UE_LOG(LogTemp, Log,
			TEXT("UnrealGT warm-up participant=%s COMPLETE (pixels discarded; no DataReady)."),
			*ImageParticipant->GetName());
	}

	for (UGTActorInfoGeneratorComponent* ActorInfoParticipant : ActorInfoParticipants)
	{
		FString Error;
		if (!ActorInfoParticipant->WarmUpCaptureNoOutput(Error))
		{
			SetInternalWarmUpFailure(FString::Printf(
				TEXT("%s hidden segmentation failed: %s"),
				*ActorInfoParticipant->GetName(), *Error));
			return false;
		}
		UE_LOG(LogTemp, Log,
			TEXT("UnrealGT warm-up participant=%s/InternalSegmentationSceneCapture COMPLETE (pixels discarded; no DataReady)."),
			*ActorInfoParticipant->GetName());
	}

	InternalWarmUpState = EGTInternalWarmUpState::Ready;
	UE_LOG(LogTemp, Log,
		TEXT("UnrealGT warm-up READY world=%s world_id=%u participant_count=%d."),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
		GetWorld() ? GetWorld()->GetUniqueID() : 0,
		ImageParticipants.Num() + ActorInfoParticipants.Num());
	return true;
}

bool AGTCamera::IsInternalWarmUpReady() const
{
	return InternalWarmUpState == EGTInternalWarmUpState::Ready;
}

bool AGTCamera::HasInternalWarmUpFailed() const
{
	return InternalWarmUpState == EGTInternalWarmUpState::Failed;
}

const FString& AGTCamera::GetInternalWarmUpFailure() const
{
	return InternalWarmUpFailure;
}

void AGTCamera::SetInternalWarmUpFailure(const FString& Failure)
{
	InternalWarmUpState = EGTInternalWarmUpState::Failed;
	InternalWarmUpFailure = Failure;
	UE_LOG(LogTemp, Error,
		TEXT("UnrealGT warm-up FAILED world=%s world_id=%u reason=%s"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
		GetWorld() ? GetWorld()->GetUniqueID() : 0,
		*InternalWarmUpFailure);
}


void AGTCamera::TriggerGTGeneratorsWithFrame(int32 FrameIndex, double StampSeconds, int32 SessionId, UObject* CaptureManager) {
	// Blueprint-generated trigger components require this string/reflection bridge.
	for (UActorComponent* Component : GetComponents().Array())
	{
		if (Component && Component->GetClass()->GetName().Contains(TEXT("GTGeneratorTrigger")))
		{
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
