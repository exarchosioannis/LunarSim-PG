// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Image/GTSegmentationGeneratorComponent.h"

#include "Algo/Accumulate.h"
#include "CanvasItem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Serialization/JsonSerializerMacros.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Canvas.h"

#include "GTFileUtilities.h"
#include "Generators/Image/GTSceneCaptureComponent2D.h"

UGTSegmentationGeneratorComponent::UGTSegmentationGeneratorComponent()
    : Super()
{
    bAntiAliasing = false;
}

void UGTSegmentationGeneratorComponent::RegisterForSegmentation(
    UPrimitiveComponent* PrimitiveComponent)
{
    WaitingToBeRegistered.Add(PrimitiveComponent);
}

void UGTSegmentationGeneratorComponent::GenerateData(const FDateTime& TimeStamp, int32 SessionId, FSessionValidationFunc SessionValidator)
{
    if (!WaitingToBeRegistered.IsEmpty())
    {
        for (auto p : WaitingToBeRegistered)
        {
            SceneCaptureComponent->RegisterForSegmentation(
                p,
                ComponentToColor,
                bColorEachComponentDifferent,
                bUseFilterForColorEachComponentDifferent,
                ColorEachComponentDifferentFilter);
        }
        SceneCaptureComponent->SetupSegmentationBlendable(bShouldApplyCloseMorph);
        FGTFileUtilities::WriteFileToSessionDirectory(
            FPaths::Combine(TEXT("SegmentationInfo"), TEXT("segmentation_info.json")),
            FGTFileUtilities::StringToCharArray(GenerateSegmentationInfoJSON()),
            GetWorld());
        WaitingToBeRegistered.Empty(WaitingToBeRegistered.Num());
    }
    Super::GenerateData(TimeStamp, SessionId, SessionValidator);
}

void UGTSegmentationGeneratorComponent::GenerateData(
    const FDateTime& TimeStamp,
    int32 SessionId,
    FSessionValidationFunc SessionValidator,
    int32 FrameIndex,
    double StampSeconds)
{
    const bool bHadPendingRegistrations = !WaitingToBeRegistered.IsEmpty();

    if (bHadPendingRegistrations)
    {
        for (auto p : WaitingToBeRegistered)
        {
            SceneCaptureComponent->RegisterForSegmentation(
                p,
                ComponentToColor,
                bColorEachComponentDifferent,
                bUseFilterForColorEachComponentDifferent,
                ColorEachComponentDifferentFilter);
        }
        SceneCaptureComponent->SetupSegmentationBlendable(bShouldApplyCloseMorph);
        WaitingToBeRegistered.Empty(WaitingToBeRegistered.Num());
    }

    // BeginPlay can run before the simulator CaptureManager creates its session folder.
    // Write the metadata again on the first synchronized frame so every dataset session
    // contains its own SegmentationInfo/segmentation_info.json file.
    if (FrameIndex == 1 || bHadPendingRegistrations)
    {
        FGTFileUtilities::WriteFileToSessionDirectory(
            FPaths::Combine(TEXT("SegmentationInfo"), TEXT("segmentation_info.json")),
            FGTFileUtilities::StringToCharArray(GenerateSegmentationInfoJSON()),
            GetWorld());
    }

    Super::GenerateData(TimeStamp, SessionId, MoveTemp(SessionValidator), FrameIndex, StampSeconds);
}

FString UGTSegmentationGeneratorComponent::GenerateSegmentationInfoJSON() const
{
    FString SegmentationJSONInformation;

    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(
            &SegmentationJSONInformation);
    FJsonSerializerWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> Serializer(JsonWriter);
    Serializer.StartArray();
    for (auto ComponentColorPair : GetSceneCaptureComponent()->ComponentToColor)
    {
        const auto Component = ComponentColorPair.Key;
        Serializer.StartObject();

        auto Name = Component->GetName();
        Serializer.Serialize(TEXT("Name"), Name);

        auto Path = Component->GetPathName();
        Serializer.Serialize(TEXT("Path"), Path);

        const auto StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
        if (StaticMeshComponent)
        {
            auto MeshName = StaticMeshComponent->GetStaticMesh()->GetName();
            Serializer.Serialize(TEXT("Mesh"), MeshName);
        }

        const auto SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component);
        if (SkeletalMeshComponent)
        {
            auto MeshName = SkeletalMeshComponent->SkeletalMesh->GetName();
            Serializer.Serialize(TEXT("SkeletalMesh"), MeshName);
        }

        TArray<FString> Tags;
        Tags.Reserve(Component->ComponentTags.Num());
        for (const auto Tag : Component->ComponentTags)
        {
            Tags.Push(Tag.ToString());
        }
        Serializer.SerializeArray(TEXT("Tags"), Tags);

        auto Color = ComponentColorPair.Value.ToString();
        Serializer.Serialize(TEXT("Color"), Color);
        Serializer.EndObject();
    }
    Serializer.EndArray();
    JsonWriter->Close();

    return SegmentationJSONInformation;
}

void UGTSegmentationGeneratorComponent::BeginPlay()
{
    Super::BeginPlay();

    SceneCaptureComponent->SetupSegmentationPostProccess(
        ComponentToColor,
        bShouldApplyCloseMorph,
        bColorEachComponentDifferent,
        bUseFilterForColorEachComponentDifferent,
        ColorEachComponentDifferentFilter);

    DebugComponentToColorString = Algo::TransformAccumulate(
        GetSceneCaptureComponent()->ComponentToColor,
        [](const auto ComponentColorPair) {
            return ComponentColorPair.Key->GetPathName() + TEXT(": ") +
                   ComponentColorPair.Value.ToString() + TEXT("\n");
        },
        FString(TEXT("")));

}

void UGTSegmentationGeneratorComponent::DrawDebug(FViewport* Viewport, FCanvas* Canvas)
{
    Super::DrawDebug(Viewport, Canvas);

    if (SceneCaptureComponent && SceneCaptureComponent->TextureTarget &&
        SceneCaptureComponent->TextureTarget->IsValidLowLevel())
    {
        UTextureRenderTarget2D* DebugTextureTarget = SceneCaptureComponent->TextureTarget;

        FCanvasTextItem TextItem(
            FVector2D(DebugTextureTarget->GetResource()->GetSizeX(), 0.f),
            FText::FromString(DebugComponentToColorString.Mid(0, 5000)),
            GEngine->GetMediumFont(),
            FLinearColor::Red);
        Canvas->DrawItem(TextItem);
    }
}
