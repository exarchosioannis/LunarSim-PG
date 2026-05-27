// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Image/GTSegmentationGeneratorComponent.h"

#include "Algo/Accumulate.h"
#include "CanvasItem.h"
#include "Components/PrimitiveComponent.h"
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
    bool bShouldWriteSegmentationClasses = false;
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
        bShouldWriteSegmentationClasses = true;
        WaitingToBeRegistered.Empty(WaitingToBeRegistered.Num());
    }
    Super::GenerateData(TimeStamp, SessionId, SessionValidator);
    if (bShouldWriteSegmentationClasses)
    {
        FGTFileUtilities::WriteFileToSessionDirectory(
            FPaths::Combine(TEXT("SegmentationInfo"), TEXT("segmentation_classes.json")),
            FGTFileUtilities::StringToCharArray(GenerateSegmentationClassesJSON()),
            GetWorld());
    }
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
    const bool bShouldWriteSegmentationClasses = FrameIndex == 1 || bHadPendingRegistrations;
    if (bShouldWriteSegmentationClasses)
    {
        FGTFileUtilities::WriteFileToSessionDirectory(
            FPaths::Combine(TEXT("SegmentationInfo"), TEXT("segmentation_info.json")),
            FGTFileUtilities::StringToCharArray(GenerateSegmentationInfoJSON()),
            GetWorld());
    }

    Super::GenerateData(TimeStamp, SessionId, MoveTemp(SessionValidator), FrameIndex, StampSeconds);
    if (bShouldWriteSegmentationClasses)
    {
        FGTFileUtilities::WriteFileToSessionDirectory(
            FPaths::Combine(TEXT("SegmentationInfo"), TEXT("segmentation_classes.json")),
            FGTFileUtilities::StringToCharArray(GenerateSegmentationClassesJSON()),
            GetWorld());
    }
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

FString UGTSegmentationGeneratorComponent::GenerateSegmentationClassesJSON() const
{
    TArray<FSegmentationClassLegendEntry> Entries;
    Entries.Reserve(ComponentToColor.Num());

    int32 FallbackIndex = 0;
    for (const TPair<FGTObjectFilter, FColor>& FilterColorPair : ComponentToColor)
    {
        FSegmentationClassLegendEntry Entry;
        Entry.Name = DeriveClassNameFromFilter(FilterColorPair.Key, FallbackIndex);
        Entry.Color = FilterColorPair.Value;
        Entries.Add(Entry);
        FallbackIndex++;
    }

    Entries.Sort([](const FSegmentationClassLegendEntry& A, const FSegmentationClassLegendEntry& B) {
        if (A.Name != B.Name)
        {
            return A.Name < B.Name;
        }
        if (A.Color.R != B.Color.R)
        {
            return A.Color.R < B.Color.R;
        }
        if (A.Color.G != B.Color.G)
        {
            return A.Color.G < B.Color.G;
        }
        if (A.Color.B != B.Color.B)
        {
            return A.Color.B < B.Color.B;
        }
        return A.Color.A < B.Color.A;
    });

    FString SegmentationClassesJSON;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(
            &SegmentationClassesJSON);

    JsonWriter->WriteObjectStart();
    JsonWriter->WriteValue(TEXT("type"), TEXT("segmentation_classes"));
    JsonWriter->WriteValue(TEXT("color_format"), TEXT("RGBA"));
    JsonWriter->WriteArrayStart(TEXT("classes"));
    for (int32 ClassId = 0; ClassId < Entries.Num(); ++ClassId)
    {
        const FSegmentationClassLegendEntry& Entry = Entries[ClassId];
        JsonWriter->WriteObjectStart();
        JsonWriter->WriteValue(TEXT("class_id"), ClassId);
        JsonWriter->WriteValue(TEXT("name"), Entry.Name);
        JsonWriter->WriteArrayStart(TEXT("color"));
        JsonWriter->WriteValue(static_cast<int32>(Entry.Color.R));
        JsonWriter->WriteValue(static_cast<int32>(Entry.Color.G));
        JsonWriter->WriteValue(static_cast<int32>(Entry.Color.B));
        JsonWriter->WriteValue(static_cast<int32>(Entry.Color.A));
        JsonWriter->WriteArrayEnd();
        JsonWriter->WriteObjectEnd();
    }
    JsonWriter->WriteArrayEnd();
    JsonWriter->WriteObjectEnd();
    JsonWriter->Close();

    return SegmentationClassesJSON;
}

FString UGTSegmentationGeneratorComponent::DeriveClassNameFromFilter(
    const FGTObjectFilter& Filter,
    int32 FallbackIndex) const
{
    if (!Filter.ActorTag.IsNone())
    {
        return Filter.ActorTag.ToString();
    }
    if (!Filter.ComponentTag.IsNone())
    {
        return Filter.ComponentTag.ToString();
    }
    if (IsValid(Filter.StaticMesh))
    {
        return Filter.StaticMesh->GetName();
    }
    if (IsValid(Filter.SkeletalMesh))
    {
        return Filter.SkeletalMesh->GetName();
    }
    if (!Filter.WildcardMeshName.IsEmpty())
    {
        return Filter.WildcardMeshName;
    }
    if (const UClass* ActorClass = Filter.ActorClass.Get())
    {
        return ActorClass->GetName();
    }
    if (const UClass* ComponentClass = Filter.ComponentClass.Get())
    {
        return ComponentClass->GetName();
    }
    if (IsValid(Filter.ActorInstance))
    {
        return Filter.ActorInstance->GetName();
    }
    return FString::Printf(TEXT("class_%d"), FallbackIndex);
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
