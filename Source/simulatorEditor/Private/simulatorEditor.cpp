#include "simulatorEditor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Components/ChildActorComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Maps/OccupancyMapPublisherComponent.h"
#include "Robots/RoverGroundTruthPublisherComponent.h"
#include "Sensors/ImuSensorPublisherComponent.h"
#include "Sensors/RobotCamRig.h"
#include "Capture/CaptureTypes.h"
#include "RockBaking/SRockBakingPanel.h"

#include "Framework/Docking/TabManager.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FsimulatorEditorModule"

const FName FsimulatorEditorModule::SimulatorConfigTabName(TEXT("SimulatorConfigTab"));

namespace
{
ELunarSimResolutionPreset NormalizeEditorResolutionPreset(ELunarSimResolutionPreset InPreset, int32 Width, int32 Height)
{
	switch (InPreset) {
	case ELunarSimResolutionPreset::R640x360:
	case ELunarSimResolutionPreset::R1024x576:
	case ELunarSimResolutionPreset::R1280x720:
	case ELunarSimResolutionPreset::R1920x1080:
	case ELunarSimResolutionPreset::R640x640:
	case ELunarSimResolutionPreset::R1024x1024:
		return InPreset;
	default:
		break;
	}

	if (Width == 640 && Height == 360)
		return ELunarSimResolutionPreset::R640x360;
	if (Width == 1024 && Height == 576)
		return ELunarSimResolutionPreset::R1024x576;
	if (Width == 1280 && Height == 720)
		return ELunarSimResolutionPreset::R1280x720;
	if (Width == 1920 && Height == 1080)
		return ELunarSimResolutionPreset::R1920x1080;
	if (Width == 640 && Height == 640)
		return ELunarSimResolutionPreset::R640x640;
	if (Width == 1024 && Height == 1024)
		return ELunarSimResolutionPreset::R1024x1024;

	return ELunarSimResolutionPreset::R1024x1024;
}

float NormalizeEditorCaptureHz(float InCaptureHz)
{
	return FMath::Clamp(FCaptureConfig::SanitizeCameraCaptureHz(InCaptureHz), FCaptureConfig::GetMinCameraCaptureHz(),
	                    FCaptureConfig::GetMaxCameraCaptureHz());
}

constexpr float MinRoverSpeedKmh = 3.0f;
constexpr float MaxForwardRoverSpeedKmh = 10.0f;
constexpr float MaxReverseRoverSpeedKmh = 8.0f;

float ClampForwardRoverSpeedKmh(float InSpeedKmh)
{
	return FMath::Clamp(InSpeedKmh, MinRoverSpeedKmh, MaxForwardRoverSpeedKmh);
}

float ClampReverseRoverSpeedKmh(float InSpeedKmh)
{
	return FMath::Clamp(InSpeedKmh, MinRoverSpeedKmh, MaxReverseRoverSpeedKmh);
}

bool IsEditorPlaySessionRunning()
{
	return GEditor && GEditor->IsPlaySessionInProgress();
}

bool HasInvalidEditorFlags(const UObject* Object)
{
	return !Object ||
	       Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed | RF_FinishDestroyed);
}

bool IsUsableEditorActor(AActor* Actor, const UWorld* ExpectedWorld)
{
	return IsValid(Actor) && Actor->GetWorld() == ExpectedWorld && !Actor->IsActorBeingDestroyed() &&
	       !HasInvalidEditorFlags(Actor);
}

bool IsUsableRobotCamRig(ARobotCamRig* RobotCamRig, const UWorld* ExpectedWorld)
{
	return IsUsableEditorActor(RobotCamRig, ExpectedWorld) && IsValid(RobotCamRig->GetRootComponent()) &&
	       !HasInvalidEditorFlags(RobotCamRig->GetRootComponent());
}

bool IsUsableComponent(const UActorComponent* Component)
{
	return IsValid(Component) && !HasInvalidEditorFlags(Component);
}

template <typename ComponentType> ComponentType* FindUsableComponentByClass(AActor* Actor)
{
	ComponentType* Component = Actor ? Actor->FindComponentByClass<ComponentType>() : nullptr;
	return IsUsableComponent(Component) ? Component : nullptr;
}

ARobotCamRig* FindUsableRobotCamRigChildActor(AActor* RoverActor, const UWorld* EditorWorld,
                                              UChildActorComponent*& OutChildActorComponent)
{
	OutChildActorComponent = nullptr;
	if (!IsUsableEditorActor(RoverActor, EditorWorld)) {
		return nullptr;
	}

	TArray<UChildActorComponent*> ChildActorComponents;
	RoverActor->GetComponents<UChildActorComponent>(ChildActorComponents);
	for (UChildActorComponent* ChildActorComponent : ChildActorComponents) {
		if (!IsUsableComponent(ChildActorComponent)) {
			continue;
		}

		ARobotCamRig* RobotCamRig = Cast<ARobotCamRig>(ChildActorComponent->GetChildActor());
		if (!IsUsableRobotCamRig(RobotCamRig, EditorWorld)) {
			continue;
		}

		OutChildActorComponent = ChildActorComponent;
		return RobotCamRig;
	}

	return nullptr;
}

struct FResolvedRoverPipeline
{
	ARobotCamRig* RobotCamRig = nullptr;
	UChildActorComponent* RobotCamRigChildComponent = nullptr;
	URoverGroundTruthPublisherComponent* GroundTruthPublisher = nullptr;
	UImuSensorPublisherComponent* ImuPublisher = nullptr;
	URoverVehicleControllerComponent* RoverController = nullptr;
	URoverCmdVelVehicleControllerComponent* CmdVelController = nullptr;

	bool IsComplete() const
	{
		return RobotCamRig && RobotCamRigChildComponent && GroundTruthPublisher && ImuPublisher && RoverController &&
		       CmdVelController;
	}
};

bool ResolveCompleteRoverPipeline(AActor* RoverActor, UWorld* EditorWorld, FResolvedRoverPipeline& OutPipeline)
{
	OutPipeline = FResolvedRoverPipeline();

	if (!IsUsableEditorActor(RoverActor, EditorWorld)) {
		return false;
	}

	OutPipeline.GroundTruthPublisher = FindUsableComponentByClass<URoverGroundTruthPublisherComponent>(RoverActor);
	OutPipeline.ImuPublisher = FindUsableComponentByClass<UImuSensorPublisherComponent>(RoverActor);
	OutPipeline.RoverController = FindUsableComponentByClass<URoverVehicleControllerComponent>(RoverActor);
	OutPipeline.CmdVelController = FindUsableComponentByClass<URoverCmdVelVehicleControllerComponent>(RoverActor);
	OutPipeline.RobotCamRig =
	    FindUsableRobotCamRigChildActor(RoverActor, EditorWorld, OutPipeline.RobotCamRigChildComponent);

	return OutPipeline.IsComplete();
}

const TCHAR* SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

const TCHAR* SunMaterialPath =
	TEXT(
		"/Game/Brushify/Maps/Moon/MaterialOverrides/"
		"M_SunGlow.M_SunGlow"
	);

const TCHAR* EarthMaterialPath =
	TEXT("/Game/3D_Models/Earth/Earth/Earth.Earth");

const TCHAR* SkyMaterialPath =
	TEXT(
		"/Game/Brushify/Maps/Moon/MaterialOverrides/"
		"MI_SkyHDR_Inst.MI_SkyHDR_Inst"
	);

const TArray<FString>& GetSunGlowBlueprintClassPaths()
{
	static const TArray<FString> Paths = {
		TEXT(
			"/Game/Blueprints/BP_SunGLowController."
			"BP_SunGLowController_C"
		)
	};

	return Paths;
}

const TArray<FString>& GetGroundTruthBlueprintClassPaths()
{
	static const TArray<FString> Paths = {
		TEXT(
			"/Game/UnrealGT/BP_GroundTruthMapPublisher."
			"BP_GroundTruthMapPublisher_C"
		)
	};

	return Paths;
}

const TArray<FString>& GetRoverBlueprintClassPaths()
{
	static const TArray<FString> Paths = {
		TEXT("/Game/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/ESA_Rover/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Rover/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Rovers/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Robots/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Vehicles/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Vehicles/Rover/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Blueprints/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/Blueprints/Robots/ESA_Rover.ESA_Rover_C"),
		TEXT("/Game/BP_RoverVehicle.BP_RoverVehicle_C"),
		TEXT("/Game/Robots/BP_RoverVehicle.BP_RoverVehicle_C"),
		TEXT("/Game/Blueprints/BP_RoverVehicle.BP_RoverVehicle_C")
	};

	return Paths;
}

FText WorldSetupStatus = LOCTEXT("WorldSetupReadyStatus", "World setup ready.");

template <typename ActorType> ActorType* FindActorByLabel(UWorld* World, const FString& ActorLabel)
{
	if (!World)
		return nullptr;

	for (TActorIterator<ActorType> It(World); It; ++It) {
		ActorType* Actor = *It;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
			continue;

#if WITH_EDITOR
		if (Actor->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return Actor;
#endif
	}

	return nullptr;
}

UClass* FindLoadedActorClass(const TArray<FString>& CandidateClassNames)
{
	for (TObjectIterator<UClass> It; It; ++It) {
		UClass* ActorClass = *It;
		if (!IsValid(ActorClass) || !ActorClass->IsChildOf(AActor::StaticClass()) ||
		    ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) {
			continue;
		}

		if (CandidateClassNames.Contains(ActorClass->GetName()))
			return ActorClass;
	}

	return nullptr;
}

UClass* LoadFirstActorClass(const TArray<FString>& CandidateClassPaths,
                            const TArray<FString>& CandidateClassNames, const FString& ActorDescription)
{
	if (UClass* LoadedClass = FindLoadedActorClass(CandidateClassNames))
		return LoadedClass;

	for (const FString& CandidatePath : CandidateClassPaths) {
		const FString PackageName = FPackageName::ObjectPathToPackageName(CandidatePath);
		if (!FPackageName::DoesPackageExist(PackageName))
			continue;

		UClass* ActorClass = LoadClass<AActor>(nullptr, *CandidatePath);
		if (ActorClass && ActorClass->IsChildOf(AActor::StaticClass()))
			return ActorClass;
	}

	UE_LOG(LogTemp, Error, TEXT("MoonSim: Could not load the %s Blueprint class."), *ActorDescription);
	return nullptr;
}

AActor* CreateOrUpdateBlueprintActor(UWorld* World, const TArray<FString>& CandidateClassPaths,
                                     const TArray<FString>& CandidateClassNames, const FString& ActorLabel,
                                     const FVector& Location, const FRotator& Rotation, const FName& FolderPath)
{
	if (!World)
		return nullptr;

	AActor* Actor = FindActorByLabel<AActor>(World, ActorLabel);
	if (!Actor) {
		UClass* ActorClass = LoadFirstActorClass(CandidateClassPaths, CandidateClassNames, ActorLabel);
		if (!ActorClass)
			return nullptr;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.OverrideLevel = World->PersistentLevel;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Actor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParameters);
	}

	if (!Actor)
		return nullptr;

	Actor->Modify();
	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

#if WITH_EDITOR
	Actor->SetActorLabel(ActorLabel);
#endif

	Actor->SetFolderPath(FolderPath);
	Actor->SetActorLocation(Location);
	Actor->SetActorRotation(Rotation);
	Actor->MarkPackageDirty();

	return Actor;
}

AStaticMeshActor* CreateOrUpdateEnvironmentSphere(UWorld* World, const FString& ActorLabel,
                                                  const TCHAR* MaterialPath, const FVector& Location,
                                                  const FVector& Scale, bool bMovable)
{
	if (!World)
		return nullptr;

	AStaticMeshActor* Actor = FindActorByLabel<AStaticMeshActor>(World, ActorLabel);
	if (!Actor) {
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.OverrideLevel = World->PersistentLevel;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location,
		                                              FRotator::ZeroRotator, SpawnParameters);
	}

	if (!Actor)
		return nullptr;

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (!SphereMesh || !Material) {
		UE_LOG(LogTemp, Error, TEXT("MoonSim: Could not load the mesh or material for '%s'."), *ActorLabel);
		return nullptr;
	}

	Actor->Modify();
	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

#if WITH_EDITOR
	Actor->SetActorLabel(ActorLabel);
#endif

	Actor->SetFolderPath(TEXT("Moon Environment"));
	Actor->SetActorLocation(Location);
	Actor->SetActorRotation(FRotator::ZeroRotator);
	Actor->SetActorScale3D(Scale);
	Actor->SetActorEnableCollision(false);

	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	if (!MeshComponent)
		return nullptr;

	MeshComponent->Modify();
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(SphereMesh);
	MeshComponent->SetMaterial(0, Material);
	MeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetCastShadow(false);
	MeshComponent->bCastDynamicShadow = false;
	MeshComponent->bCastStaticShadow = false;
	MeshComponent->bCastContactShadow = false;
	MeshComponent->bAffectDistanceFieldLighting = false;
	MeshComponent->bAffectDynamicIndirectLighting = false;
	MeshComponent->SetMobility(bMovable ? EComponentMobility::Movable : EComponentMobility::Static);
	MeshComponent->MarkRenderStateDirty();
	Actor->MarkPackageDirty();

	return Actor;
}

ADirectionalLight* CreateOrUpdateMoonDirectionalLight(UWorld* World)
{
	if (!World)
		return nullptr;

	ADirectionalLight* Light = FindActorByLabel<ADirectionalLight>(World, TEXT("DirectionalLight"));
	const bool bWasCreated = (Light == nullptr);

	if (!Light) {
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.OverrideLevel = World->PersistentLevel;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Light = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(),
		                                                FVector(0.0, 0.0, 1000.0),
		                                                FRotator(-35.0, -145.0, 0.0), SpawnParameters);
	}

	if (!Light)
		return nullptr;

	Light->Modify();

#if WITH_EDITOR
	Light->SetActorLabel(TEXT("DirectionalLight"));
#endif

	Light->SetFolderPath(TEXT("Moon Environment"));

	ULightComponent* LightComponent = Light->GetLightComponent();
	if (LightComponent) {
		LightComponent->Modify();
		LightComponent->SetMobility(EComponentMobility::Movable);
		if (bWasCreated) {
			LightComponent->SetIntensity(10.0f);
			LightComponent->SetLightColor(FLinearColor::White, false);
			LightComponent->SetCastShadows(true);
		}
		LightComponent->MarkRenderStateDirty();
	}

	Light->MarkPackageDirty();
	return Light;
}

FString NormalizeBlueprintPropertyName(const FString& PropertyName)
{
	FString NormalizedName = PropertyName;
	NormalizedName.ReplaceInline(TEXT(" "), TEXT(""));
	NormalizedName.ReplaceInline(TEXT("_"), TEXT(""));
	return NormalizedName.ToLower();
}

FObjectPropertyBase* FindBlueprintActorReferenceProperty(AActor* TargetActor, const FName& InternalName,
                                                         const FString& DisplayName)
{
	if (!TargetActor)
		return nullptr;

	if (FObjectPropertyBase* ExactProperty =
	        FindFProperty<FObjectPropertyBase>(TargetActor->GetClass(), InternalName)) {
		if (ExactProperty->PropertyClass && ExactProperty->PropertyClass->IsChildOf(AActor::StaticClass()))
			return ExactProperty;
	}

	const FString NormalizedInternalName = NormalizeBlueprintPropertyName(InternalName.ToString());
	const FString NormalizedDisplayName = NormalizeBlueprintPropertyName(DisplayName);

	for (TFieldIterator<FObjectPropertyBase> It(TargetActor->GetClass()); It; ++It) {
		FObjectPropertyBase* Property = *It;
		if (!Property || !Property->PropertyClass ||
		    !Property->PropertyClass->IsChildOf(AActor::StaticClass())) {
			continue;
		}

		const FString CandidateInternalName = NormalizeBlueprintPropertyName(Property->GetName());
		const FString CandidateDisplayName =
		    NormalizeBlueprintPropertyName(Property->GetDisplayNameText().ToString());

		if (CandidateInternalName == NormalizedInternalName ||
		    CandidateInternalName == NormalizedDisplayName ||
		    CandidateDisplayName == NormalizedInternalName ||
		    CandidateDisplayName == NormalizedDisplayName) {
			return Property;
		}
	}

	UE_LOG(LogTemp, Error,
	       TEXT("MoonSim: Could not find actor-reference property '%s' / '%s' on '%s'."),
	       *InternalName.ToString(), *DisplayName, *TargetActor->GetActorLabel());

	for (TFieldIterator<FObjectPropertyBase> It(TargetActor->GetClass()); It; ++It) {
		FObjectPropertyBase* Property = *It;
		if (!Property || !Property->PropertyClass ||
		    !Property->PropertyClass->IsChildOf(AActor::StaticClass())) {
			continue;
		}

		UE_LOG(LogTemp, Warning,
		       TEXT("MoonSim: Available actor property on '%s': internal='%s', display='%s', type='%s'."),
		       *TargetActor->GetActorLabel(), *Property->GetName(),
		       *Property->GetDisplayNameText().ToString(), *Property->PropertyClass->GetName());
	}

	return nullptr;
}

bool SetBlueprintActorReferenceValue(AActor* TargetActor, FObjectPropertyBase* Property,
                                     AActor* ReferencedActor)
{
	if (!TargetActor || !Property)
		return false;

	if (ReferencedActor && !ReferencedActor->IsA(Property->PropertyClass)) {
		UE_LOG(LogTemp, Error,
		       TEXT("MoonSim: Actor '%s' is not compatible with property '%s' on '%s'; expected '%s'."),
		       *ReferencedActor->GetActorLabel(), *Property->GetName(), *TargetActor->GetActorLabel(),
		       *Property->PropertyClass->GetName());
		return false;
	}

	Property->SetObjectPropertyValue_InContainer(TargetActor, ReferencedActor);
	return Property->GetObjectPropertyValue_InContainer(TargetActor) == ReferencedActor;
}

bool ConfigureSunGlowController(AActor* SunGlowController, ADirectionalLight* DirectionalLight,
                                AStaticMeshActor* Sun, AActor* Rover)
{
	if (!SunGlowController)
		return false;

	SunGlowController->SetActorTickEnabled(false);

	FObjectPropertyBase* DirectionalLightProperty = FindBlueprintActorReferenceProperty(
	    SunGlowController, TEXT("DirectionalLight"), TEXT("Directional Light"));
	FObjectPropertyBase* SunProperty = FindBlueprintActorReferenceProperty(
	    SunGlowController, TEXT("SunGlowSPhere"), TEXT("Sun Glow Sphere"));
	FObjectPropertyBase* FollowActorProperty = FindBlueprintActorReferenceProperty(
	    SunGlowController, TEXT("FollowActor"), TEXT("Follow Actor"));

	if (!DirectionalLightProperty || !SunProperty || !FollowActorProperty)
		return false;

	SunGlowController->Modify();

	const bool bDirectionalLightSet =
	    SetBlueprintActorReferenceValue(SunGlowController, DirectionalLightProperty, DirectionalLight);
	const bool bSunSet =
	    SetBlueprintActorReferenceValue(SunGlowController, SunProperty, Sun);
	const bool bFollowActorSet =
	    SetBlueprintActorReferenceValue(SunGlowController, FollowActorProperty, Rover);

#if WITH_EDITOR
	SunGlowController->PostEditChange();

	DirectionalLightProperty = FindBlueprintActorReferenceProperty(
	    SunGlowController, TEXT("DirectionalLight"), TEXT("Directional Light"));
	SunProperty = FindBlueprintActorReferenceProperty(
	    SunGlowController, TEXT("SunGlowSPhere"), TEXT("Sun Glow Sphere"));
	FollowActorProperty = FindBlueprintActorReferenceProperty(
	    SunGlowController, TEXT("FollowActor"), TEXT("Follow Actor"));

	if (DirectionalLightProperty)
		SetBlueprintActorReferenceValue(SunGlowController, DirectionalLightProperty, DirectionalLight);
	if (SunProperty)
		SetBlueprintActorReferenceValue(SunGlowController, SunProperty, Sun);
	if (FollowActorProperty)
		SetBlueprintActorReferenceValue(SunGlowController, FollowActorProperty, Rover);
#endif

	const bool bDirectionalLightPersisted =
	    DirectionalLightProperty &&
	    DirectionalLightProperty->GetObjectPropertyValue_InContainer(SunGlowController) == DirectionalLight;
	const bool bSunPersisted =
	    SunProperty &&
	    SunProperty->GetObjectPropertyValue_InContainer(SunGlowController) == Sun;
	const bool bFollowActorPersisted =
	    FollowActorProperty &&
	    FollowActorProperty->GetObjectPropertyValue_InContainer(SunGlowController) == Rover;

	const bool bComplete =
	    bDirectionalLightSet && bSunSet && bFollowActorSet &&
	    bDirectionalLightPersisted && bSunPersisted && bFollowActorPersisted &&
	    DirectionalLight && Sun && Rover;

	SunGlowController->SetActorTickEnabled(bComplete);
	SunGlowController->MarkPackageDirty();

	if (DirectionalLight && bDirectionalLightPersisted) {
		UE_LOG(LogTemp, Warning, TEXT("MoonSim: Assigned %s.DirectionalLight = %s."),
		       *SunGlowController->GetActorLabel(), *DirectionalLight->GetActorLabel());
	}

	if (Sun && bSunPersisted) {
		UE_LOG(LogTemp, Warning, TEXT("MoonSim: Assigned %s.SunGlowSPhere = %s."),
		       *SunGlowController->GetActorLabel(), *Sun->GetActorLabel());
	}

	if (Rover && bFollowActorPersisted) {
		UE_LOG(LogTemp, Warning, TEXT("MoonSim: Assigned %s.FollowActor = %s."),
		       *SunGlowController->GetActorLabel(), *Rover->GetActorLabel());
	}

	return bComplete;
}

AActor* FindExistingCompleteRover(UWorld* World)
{
	if (!World)
		return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It) {
		AActor* Candidate = *It;
		FResolvedRoverPipeline Pipeline;
		if (ResolveCompleteRoverPipeline(Candidate, World, Pipeline))
			return Candidate;
	}

	return nullptr;
}

bool CreateOrUpdateMoonEnvironment(UWorld* World, FString& OutStatus)
{
	if (!World) {
		OutStatus = TEXT("Moon environment failed: no editor world.");
		return false;
	}

	ADirectionalLight* DirectionalLight = CreateOrUpdateMoonDirectionalLight(World);
	AStaticMeshActor* Earth = CreateOrUpdateEnvironmentSphere(
	    World, TEXT("Earth"), EarthMaterialPath, FVector(2040.0, -32560.0, 200.0), FVector::OneVector, false);
	AStaticMeshActor* Sky = CreateOrUpdateEnvironmentSphere(World, TEXT("Sky"), SkyMaterialPath,
	                                                        FVector(0.0, 0.0, 0.0),
	                                                        FVector(10000.0, 10000.0, 10000.0), false);
	AStaticMeshActor* Sun = CreateOrUpdateEnvironmentSphere(World, TEXT("Sun"), SunMaterialPath,
	                                                        FVector(572.863499, 13052.0, 241.965723),
	                                                        FVector(10.0, 10.0, 10.0), true);

	AActor* SunGlowController = CreateOrUpdateBlueprintActor(
	    World, GetSunGlowBlueprintClassPaths(), {TEXT("BP_SunGLowController_C")}, TEXT("BP_SunGLowController"),
	    FVector::ZeroVector, FRotator::ZeroRotator, TEXT("Moon Environment"));

	if (DirectionalLight && Sun) {
		Sun->Modify();
		Sun->AttachToActor(DirectionalLight, FAttachmentTransformRules::KeepWorldTransform);
		Sun->MarkPackageDirty();
	}

	ConfigureSunGlowController(
	    SunGlowController, DirectionalLight, Sun, FindExistingCompleteRover(World));

	const bool bSuccess = DirectionalLight && Earth && Sky && Sun && SunGlowController;
	OutStatus = bSuccess
	                ? TEXT("Created/updated DirectionalLight, Earth, Sky, Sun, and BP_SunGLowController.")
	                : TEXT("Moon environment was only partially created. Check the Output Log and asset paths.");
	return bSuccess;
}

bool SnapRoverToGround(UWorld* World, AActor* Rover, const FVector2D& DesiredXY)
{
	if (!World || !Rover)
		return false;

	constexpr double TraceDistanceCm = 200000.0;
	constexpr double GroundClearanceCm = 5.0;

	// Set the final horizontal position and rotation before measuring bounds.
	const FVector OriginalLocation = Rover->GetActorLocation();
	Rover->SetActorRotation(FRotator::ZeroRotator);
	Rover->SetActorLocation(
	    FVector(DesiredXY.X, DesiredXY.Y, OriginalLocation.Z),
	    false,
	    nullptr,
	    ETeleportType::TeleportPhysics);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RoverGroundTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.AddIgnoredActor(Rover);

	// A child-actor camera rig can be a separate actor, so ignore attached
	// actors as well as the rover itself during the downward trace.
	TArray<AActor*> AttachedActors;
	Rover->GetAttachedActors(AttachedActors);
	QueryParams.AddIgnoredActors(AttachedActors);

	const FVector TraceStart(DesiredXY.X, DesiredXY.Y, TraceDistanceCm);
	const FVector TraceEnd(DesiredXY.X, DesiredXY.Y, -TraceDistanceCm);

	FHitResult Hit;
	bool bHitGround = World->LineTraceSingleByChannel(
	    Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);

	// Some landscape collision profiles block Visibility rather than a trace
	// performed on the WorldStatic channel, so use Visibility as a fallback.
	if (!bHitGround) {
		bHitGround = World->LineTraceSingleByChannel(
		    Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	}

	if (!bHitGround) {
		UE_LOG(
		    LogTemp,
		    Warning,
		    TEXT("MoonSim: No ground trace hit below rover at X=%.2f Y=%.2f; keeping its previous Z=%.2f."),
		    DesiredXY.X,
		    DesiredXY.Y,
		    OriginalLocation.Z);
		return false;
	}

	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;

	// Only use components with collision. Using all components can include the
	// camera rig, sensors, debug meshes, or other non-physical geometry and can
	// calculate an offset that leaves the rover visibly floating.
	Rover->GetActorBounds(true, BoundsOrigin, BoundsExtent);

	// Fallback for Blueprints that have no colliding component bounds.
	if (BoundsExtent.IsNearlyZero()) {
		Rover->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		UE_LOG(
		    LogTemp,
		    Warning,
		    TEXT("MoonSim: Rover has no colliding bounds; using all component bounds as a fallback."));
	}

	if (BoundsOrigin.ContainsNaN() || BoundsExtent.ContainsNaN() || BoundsExtent.IsNearlyZero()) {
		UE_LOG(LogTemp, Warning, TEXT("MoonSim: Rover bounds are invalid; cannot snap rover to ground."));
		return false;
	}

	const double BoundsBottomZ = BoundsOrigin.Z - BoundsExtent.Z;
	const double ActorOriginToBottom = Rover->GetActorLocation().Z - BoundsBottomZ;
	if (!FMath::IsFinite(ActorOriginToBottom) || ActorOriginToBottom < 0.0) {
		UE_LOG(
		    LogTemp,
		    Warning,
		    TEXT("MoonSim: Invalid rover origin-to-bottom offset: %.2f cm."),
		    ActorOriginToBottom);
		return false;
	}

	const FVector GroundedLocation(
	    DesiredXY.X,
	    DesiredXY.Y,
	    Hit.ImpactPoint.Z + ActorOriginToBottom + GroundClearanceCm);

	const bool bMoved = Rover->SetActorLocation(
	    GroundedLocation,
	    false,
	    nullptr,
	    ETeleportType::TeleportPhysics);

	UE_LOG(
	    LogTemp,
	    Log,
	    TEXT("MoonSim: Rover ground snap: hit actor='%s', ground Z=%.2f, bounds bottom Z=%.2f, offset=%.2f, final Z=%.2f."),
	    Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("None"),
	    Hit.ImpactPoint.Z,
	    BoundsBottomZ,
	    ActorOriginToBottom,
	    GroundedLocation.Z);

	return bMoved;
}

AActor* CreateOrUpdateRoverActor(UWorld* World)
{
	if (!World)
		return nullptr;

	AActor* Rover = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It) {
		AActor* Candidate = *It;
		FResolvedRoverPipeline Pipeline;
		if (ResolveCompleteRoverPipeline(Candidate, World, Pipeline)) {
			Rover = Candidate;
			break;
		}
	}

	if (!Rover) {
		Rover = CreateOrUpdateBlueprintActor(
		    World,
		    GetRoverBlueprintClassPaths(),
		    {TEXT("ESA_Rover_C"), TEXT("BP_RoverVehicle_C")},
		    TEXT("ESA_Rover"),
		    FVector::ZeroVector,
		    FRotator::ZeroRotator,
		    TEXT("MoonSim Rover"));
	}

	if (!Rover)
		return nullptr;

	Rover->Modify();
#if WITH_EDITOR
	Rover->SetActorLabel(TEXT("ESA_Rover"));
#endif
	Rover->SetFolderPath(TEXT("MoonSim Rover"));

	// Preserve the original requested X/Y spawn position, but derive Z from
	// the landscape or other WorldStatic surface directly below it.
	const FVector2D RoverXY(0.0, 0.0);
	if (!SnapRoverToGround(World, Rover, RoverXY)) {
		// Do not overwrite Z with zero when the trace fails. This preserves an
		// existing manually placed rover and makes the Output Log warning useful.
		const FVector CurrentLocation = Rover->GetActorLocation();
		Rover->SetActorLocation(
		    FVector(RoverXY.X, RoverXY.Y, CurrentLocation.Z),
		    false,
		    nullptr,
		    ETeleportType::TeleportPhysics);
		Rover->SetActorRotation(FRotator::ZeroRotator);
	}

	Rover->MarkPackageDirty();
	return Rover;
}

bool CreateOrUpdateRoverAndGroundTruth(UWorld* World, FString& OutStatus)
{
	if (!World) {
		OutStatus = TEXT("Rover setup failed: no editor world.");
		return false;
	}

	AActor* Rover = CreateOrUpdateRoverActor(World);
	AActor* GroundTruth = CreateOrUpdateBlueprintActor(
	    World, GetGroundTruthBlueprintClassPaths(), {TEXT("BP_GroundTruthMapPublisher_C")},
	    TEXT("BP_GroundTruthMapPublisher"), FVector::ZeroVector, FRotator::ZeroRotator,
	    TEXT("MoonSim Ground Truth"));

	ADirectionalLight* DirectionalLight =
	    FindActorByLabel<ADirectionalLight>(World, TEXT("DirectionalLight"));
	AStaticMeshActor* Sun = FindActorByLabel<AStaticMeshActor>(World, TEXT("Sun"));
	AActor* SunGlowController =
	    FindActorByLabel<AActor>(World, TEXT("BP_SunGLowController"));

	ConfigureSunGlowController(SunGlowController, DirectionalLight, Sun, Rover);

	FResolvedRoverPipeline Pipeline;
	const bool bRoverComplete = ResolveCompleteRoverPipeline(Rover, World, Pipeline);
	const bool bSuccess = bRoverComplete && GroundTruth;
	OutStatus = bSuccess
	                ? TEXT("Created/updated ESA_Rover and BP_GroundTruthMapPublisher")
	                : TEXT("Rover/Ground Truth setup was only partially created. Check the Output Log and asset paths.");
	return bSuccess;
}

TSharedRef<SWidget> MakeSimulatorConfigSection(const FText& Title, const TSharedRef<SWidget>& Body)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(10.f, 8.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Body
			]
		];
}

TSharedRef<SWidget> MakeSimulatorConfigFormRow(const FText& Label, const TSharedRef<SWidget>& Control)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0.f, 3.f, 12.f, 3.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Label)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 3.f)
		.VAlign(VAlign_Center)
		[
			Control
		];
}

TSharedRef<SWidget> MakeSimulatorConfigCheckRow(const TSharedRef<SWidget>& CheckBox, float Indent = 0.f)
{
	return SNew(SBox)
		.Padding(FMargin(Indent, 3.f, 0.f, 3.f))
		[
			CheckBox
		];
}
}

void FsimulatorEditorModule::StartupModule()
{
	InitResolutionPresetOptions();
	InitRoverControlOptions();

	FGlobalTabmanager::Get()
	    ->RegisterNomadTabSpawner(SimulatorConfigTabName,
	                              FOnSpawnTab::CreateRaw(this, &FsimulatorEditorModule::OnSpawnSimulatorConfigTab))
	    .SetDisplayName(LOCTEXT("SimulatorConfigTabTitle", "Simulator Config"))
	    .SetMenuType(ETabSpawnerMenuType::Hidden);

	if (UToolMenus::IsToolMenuUIEnabled()) {
		UToolMenus::RegisterStartupCallback(
		    FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FsimulatorEditorModule::RegisterMenus));
	}
}

void FsimulatorEditorModule::ShutdownModule()
{
	if (UToolMenus::IsToolMenuUIEnabled()) {
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SimulatorConfigTabName);
}

void FsimulatorEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	if (!Menu)
		return;

	FToolMenuSection& Section =
	    Menu->AddSection("SimulatorConfigSection", LOCTEXT("SimulatorConfigSection", "Simulator"));

	Section.AddMenuEntry("OpenSimulatorConfig", LOCTEXT("OpenSimulatorConfigLabel", "Simulator Config"),
	                     LOCTEXT("OpenSimulatorConfigTooltip", "Open the Simulator Config window"), FSlateIcon(),
	                     FUIAction(FExecuteAction::CreateRaw(this, &FsimulatorEditorModule::OpenSimulatorConfigTab)));
}

void FsimulatorEditorModule::OpenSimulatorConfigTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(SimulatorConfigTabName);
}

TSharedRef<SDockTab> FsimulatorEditorModule::OnSpawnSimulatorConfigTab(const FSpawnTabArgs& SpawnTabArgs)
{
	RefreshTargetsFromEditorWorld();
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			BuildSimulatorConfigPanel()
		];
}

TSharedRef<SWidget> FsimulatorEditorModule::BuildSimulatorConfigPanel()
{
	InitResolutionPresetOptions();
	InitRoverControlOptions();
	SelectedResolutionPresetOption = FindResolutionPresetOption(ResolutionPreset);
	SelectedRoverControlModeOption = FindRoverControlModeOption(RoverControlMode);

	return SNew(SBox)
		.Padding(12.0f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 10.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SimulatorConfigTitle", "Simulator Config"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DataGenerationTitle", "Data Generation and ROS 2"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 10.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(0.f, 0.f, 5.f, 0.f)
					[
						MakeSimulatorConfigSection(
							LOCTEXT("OutputsSectionLabel", "Outputs"),
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetStereoRosImagesCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnStereoRosImagesChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("StereoRosImagesLabel", "Stereo ROS Images + CameraInfo"))
										.AutoWrapText(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetTrajectoryCsvCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnTrajectoryCsvChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("TrajectoryCsvLabel", "Trajectory CSV"))
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthMaps)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetEnableGroundTruthMapsCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnEnableGroundTruthMapsChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GroundTruthRosMapsLabel", "Ground Truth ROS Maps"))
										.AutoWrapText(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(18.f, 0.f, 0.f, 3.f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("GroundTruthRosMapsHelpText", "May reduce runtime performance while ground-truth maps are being computed."))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.AutoWrapText(true)
							])
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(5.f, 0.f, 0.f, 0.f)
					[
						MakeSimulatorConfigSection(
							LOCTEXT("GroundTruthOutputsSectionLabel", "Ground Truth Outputs"),
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetGroundTruthImagesCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnGroundTruthImagesChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GroundTruthImagesLabel", "Ground Truth Images"))
										.AutoWrapText(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthOutput)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetGroundTruthRgbCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnGroundTruthRgbChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GroundTruthRgbLabel", "RGB"))
									],
									18.f)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthOutput)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetGroundTruthDepthCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnGroundTruthDepthChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GroundTruthDepthLabel", "Depth"))
									],
									18.f)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthOutput)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetGroundTruthSegmentationCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnGroundTruthSegmentationChanged)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GroundTruthSegmentationLabel", "Segmentation"))
									],
									18.f)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigCheckRow(
									SNew(SCheckBox)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthOutput)
									.IsChecked_Raw(this, &FsimulatorEditorModule::GetGroundTruthBoundingBoxesCheckState)
									.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnGroundTruthBoundingBoxesChanged)
									.ToolTipText(LOCTEXT("GroundTruthBoundingBoxesTooltip", "Bounding boxes can significantly reduce capture performance in dense scenes. Enable only when needed."))
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GroundTruthBoundingBoxesLabel", "Bounding Boxes"))
									],
									18.f)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(36.f, 0.f, 0.f, 3.f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("GroundTruthBoundingBoxesHelpText", "May significantly reduce capture performance in dense scenes."))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.AutoWrapText(true)
							])
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CaptureKeyHelp", "Press C to Start/Stop Capture Session"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(0.f, 0.f, 5.f, 0.f)
					[
						MakeSimulatorConfigSection(
							LOCTEXT("CaptureSectionLabel", "Capture"),
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("ResolutionLabel", "Resolution"),
									SNew(SBox)
									.WidthOverride(180.f)
									[
										SNew(SComboBox<TSharedPtr<ELunarSimResolutionPreset>>)
										.OptionsSource(&ResolutionPresetOptions)
										.InitiallySelectedItem(SelectedResolutionPresetOption)
										.OnGenerateWidget_Raw(this, &FsimulatorEditorModule::MakeResolutionPresetComboWidget)
										.OnSelectionChanged_Raw(this, &FsimulatorEditorModule::OnResolutionPresetSelectionChanged)
										[
											SNew(STextBlock)
											.Text_Raw(this, &FsimulatorEditorModule::GetResolutionPresetText)
										]
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("CameraHorizontalFovDegLabel", "Horizontal FOV deg"),
									SNew(SBox)
									.WidthOverride(120.f)
									[
										SNew(SNumericEntryBox<float>)
										.Value_Lambda([this]() -> TOptional<float> {
											return CameraHorizontalFovDeg;
										})
										.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnCameraHorizontalFovDegChanged)
										.MinValue(FCaptureConfig::GetMinHorizontalFovDeg())
										.MaxValue(FCaptureConfig::GetMaxHorizontalFovDeg())
										.MinSliderValue(FCaptureConfig::GetMinHorizontalFovDeg())
										.MaxSliderValue(FCaptureConfig::GetMaxHorizontalFovDeg())
										.AllowSpin(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("CaptureHzLabel", "Capture Hz"),
									SNew(SBox)
									.WidthOverride(120.f)
									[
										SNew(SNumericEntryBox<float>)
										.Value_Lambda([this]() -> TOptional<float> {
											return CustomCaptureHz;
										})
										.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnCustomCaptureHzChanged)
										.MinValue(FCaptureConfig::GetMinCameraCaptureHz())
										.MaxValue(FCaptureConfig::GetMaxCameraCaptureHz())
										.MinSliderValue(1.0f)
										.MaxSliderValue(FCaptureConfig::GetMaxCameraCaptureHz())
										.AllowSpin(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("StereoBaselineCmLabel", "Stereo Baseline cm"),
									SNew(SBox)
									.WidthOverride(120.f)
									[
										SNew(SNumericEntryBox<float>)
										.Value_Lambda([this]() -> TOptional<float> {
											return StereoBaselineCm;
										})
										.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnStereoBaselineCmChanged)
										.MinValue(FCaptureConfig::GetMinStereoBaselineCm())
										.MaxValue(FCaptureConfig::GetMaxStereoBaselineCm())
										.MinSliderValue(FCaptureConfig::GetMinStereoBaselineCm())
										.MaxSliderValue(100.0f)
										.AllowSpin(true)
									])
							])
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(5.f, 0.f, 0.f, 0.f)
					[
						MakeSimulatorConfigSection(
							LOCTEXT("RoverSensorsSectionLabel", "Rover / Sensors"),
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("ImuHzLabel", "IMU Hz"),
									SNew(SBox)
									.WidthOverride(120.f)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditImuHz)
									[
										SNew(SNumericEntryBox<float>)
										.Value_Lambda([this]() -> TOptional<float> {
											return ImuPublishHz;
										})
										.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnImuHzChanged)
										.MinValue(1.0f)
										.MaxValue(400.0f)
										.MinSliderValue(1.0f)
										.MaxSliderValue(200.0f)
										.AllowSpin(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(36.f, 0.f, 0.f, 3.f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ImuHzNote", "IMU publishing rate is limited by the simulation FPS."))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.AutoWrapText(true)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("RoverControlModeLabel", "Control Mode"),
									SNew(SBox)
									.WidthOverride(180.f)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditRoverControl)
									[
										SNew(SComboBox<TSharedPtr<ERoverControlMode>>)
										.OptionsSource(&RoverControlModeOptions)
										.InitiallySelectedItem(SelectedRoverControlModeOption)
										.OnGenerateWidget_Raw(this, &FsimulatorEditorModule::MakeRoverControlModeComboWidget)
										.OnSelectionChanged_Raw(this, &FsimulatorEditorModule::OnRoverControlModeSelectionChanged)
										[
											SNew(STextBlock)
											.Text_Raw(this, &FsimulatorEditorModule::GetRoverControlModeText)
										]
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("MaxForwardSpeedKmhLabel", "Max Forward Speed (km/h)"),
									SNew(SBox)
									.WidthOverride(120.f)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditRoverControl)
									[
										SNew(SNumericEntryBox<float>)
										.Value_Lambda([this]() -> TOptional<float> {
											return MaxForwardSpeedKmh;
										})
										.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnMaxForwardSpeedKmhChanged)
										.MinValue(MinRoverSpeedKmh)
										.MaxValue(MaxForwardRoverSpeedKmh)
										.MinSliderValue(MinRoverSpeedKmh)
										.MaxSliderValue(MaxForwardRoverSpeedKmh)
										.Delta(0.1f)
										.MinFractionalDigits(1)
										.MaxFractionalDigits(1)
										.AllowSpin(true)
									])
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeSimulatorConfigFormRow(
									LOCTEXT("MaxReverseSpeedKmhLabel", "Max Reverse Speed (km/h)"),
									SNew(SBox)
									.WidthOverride(120.f)
									.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditRoverControl)
									[
										SNew(SNumericEntryBox<float>)
										.Value_Lambda([this]() -> TOptional<float> {
											return MaxReverseSpeedKmh;
										})
										.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnMaxReverseSpeedKmhChanged)
										.MinValue(MinRoverSpeedKmh)
										.MaxValue(MaxReverseRoverSpeedKmh)
										.MinSliderValue(MinRoverSpeedKmh)
										.MaxSliderValue(MaxReverseRoverSpeedKmh)
										.Delta(0.1f)
										.MinFractionalDigits(1)
										.MaxFractionalDigits(1)
										.AllowSpin(true)
									])
							])
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 0.f)
				.HAlign(HAlign_Left)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyButtonLabel", "Apply Settings"))
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanApplySettings)
					.OnClicked_Lambda([this]() {
						OnApplyClicked();
						return FReply::Handled();
					})
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 10.f)
				[
					MakeSimulatorConfigSection(
						LOCTEXT("WorldSetupSectionLabel", "World Setup"),
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 0.f, 0.f, 8.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("CreateMoonEnvironmentButtonLabel", "Create / Update Sky, Earth and Sun"))
							.IsEnabled_Lambda([]() {
								return !IsEditorPlaySessionRunning();
							})
							.OnClicked_Lambda([this]() {
								UWorld* EditorWorld = GetEditorWorld();
								if (!EditorWorld) {
									WorldSetupStatus = LOCTEXT("WorldSetupNoWorldStatus", "No editor world available.");
									return FReply::Handled();
								}

								const FScopedTransaction Transaction(
								    LOCTEXT("CreateMoonEnvironmentTransaction", "Create Moon Environment"));
								EditorWorld->Modify();

								FString Status;
								CreateOrUpdateMoonEnvironment(EditorWorld, Status);
								WorldSetupStatus = FText::FromString(Status);
								EditorWorld->MarkPackageDirty();

								if (GEditor)
									GEditor->RedrawLevelEditingViewports();

								return FReply::Handled();
							})
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 0.f, 0.f, 8.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("CreateRoverGroundTruthButtonLabel", "Create / Update Rover + Ground Truth"))
							.IsEnabled_Lambda([]() {
								return !IsEditorPlaySessionRunning();
							})
							.OnClicked_Lambda([this]() {
								UWorld* EditorWorld = GetEditorWorld();
								if (!EditorWorld) {
									WorldSetupStatus = LOCTEXT("RoverSetupNoWorldStatus", "No editor world available.");
									return FReply::Handled();
								}

								const FScopedTransaction Transaction(
								    LOCTEXT("CreateRoverGroundTruthTransaction", "Create Rover and Ground Truth"));
								EditorWorld->Modify();

								FString Status;
								const bool bSuccess = CreateOrUpdateRoverAndGroundTruth(EditorWorld, Status);
								WorldSetupStatus = FText::FromString(Status);
								EditorWorld->MarkPackageDirty();

								if (bSuccess)
									RefreshTargetsFromEditorWorld();

								if (GEditor)
									GEditor->RedrawLevelEditingViewports();

								return FReply::Handled();
							})
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text_Lambda([]() {
								return WorldSetupStatus;
							})
							.AutoWrapText(true)
						]
					)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 14.f, 0.f, 0.f)
				[
					MakeSimulatorConfigSection(
						LOCTEXT("TerrainGenerationSectionLabel", "Terrain Generation"),
						SNew(SRockBakingPanel))
				]
			]
		];
}

void FsimulatorEditorModule::InitResolutionPresetOptions()
{
	if (ResolutionPresetOptions.Num() > 0)
		return;

	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R640x360));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1024x576));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1280x720));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1920x1080));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R640x640));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1024x1024));
}

TSharedPtr<ELunarSimResolutionPreset>
FsimulatorEditorModule::FindResolutionPresetOption(ELunarSimResolutionPreset InPreset) const
{
	TSharedPtr<ELunarSimResolutionPreset> DefaultOption;
	for (const TSharedPtr<ELunarSimResolutionPreset>& Option : ResolutionPresetOptions) {
		if (Option.IsValid() && *Option == ELunarSimResolutionPreset::R1024x1024) {
			DefaultOption = Option;
		}
		if (Option.IsValid() && *Option == InPreset) {
			return Option;
		}
	}
	return DefaultOption.IsValid() ? DefaultOption
	                               : (ResolutionPresetOptions.Num() > 0 ? ResolutionPresetOptions[0] : nullptr);
}

FString FsimulatorEditorModule::ResolutionPresetToString(ELunarSimResolutionPreset InPreset) const
{
	switch (InPreset) {
	case ELunarSimResolutionPreset::R640x360:
		return TEXT("640x360");
	case ELunarSimResolutionPreset::R1024x576:
		return TEXT("1024x576");
	case ELunarSimResolutionPreset::R1280x720:
		return TEXT("1280x720");
	case ELunarSimResolutionPreset::R1920x1080:
		return TEXT("1920x1080");
	case ELunarSimResolutionPreset::R640x640:
		return TEXT("640x640");
	case ELunarSimResolutionPreset::R1024x1024:
		return TEXT("1024x1024");
	default:
		return TEXT("1024x1024");
	}
}

FText FsimulatorEditorModule::GetResolutionPresetText() const
{
	return FText::FromString(ResolutionPresetToString(ResolutionPreset));
}

TSharedRef<SWidget>
FsimulatorEditorModule::MakeResolutionPresetComboWidget(TSharedPtr<ELunarSimResolutionPreset> InOption) const
{
	const ELunarSimResolutionPreset Preset = InOption.IsValid() ? *InOption : ELunarSimResolutionPreset::R1024x1024;
	return SNew(STextBlock).Text(FText::FromString(ResolutionPresetToString(Preset)));
}

void FsimulatorEditorModule::OnResolutionPresetSelectionChanged(TSharedPtr<ELunarSimResolutionPreset> NewSelection,
                                                                ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid())
		return;

	SelectedResolutionPresetOption = NewSelection;
	ResolutionPreset = *NewSelection;
}

void FsimulatorEditorModule::InitRoverControlOptions()
{
	if (RoverControlModeOptions.Num() > 0)
		return;

	RoverControlModeOptions.Add(MakeShared<ERoverControlMode>(ERoverControlMode::Manual));
	RoverControlModeOptions.Add(MakeShared<ERoverControlMode>(ERoverControlMode::RosCmdVel));
}

TSharedPtr<ERoverControlMode> FsimulatorEditorModule::FindRoverControlModeOption(ERoverControlMode InMode) const
{
	for (const TSharedPtr<ERoverControlMode>& Option : RoverControlModeOptions) {
		if (Option.IsValid() && *Option == InMode) {
			return Option;
		}
	}
	return nullptr;
}

FString FsimulatorEditorModule::RoverControlModeToString(ERoverControlMode InMode) const
{
	switch (InMode) {
	case ERoverControlMode::Manual:
		return TEXT("WASD");
	case ERoverControlMode::RosCmdVel:
		return TEXT("cmd_vel");
	default:
		return TEXT("WASD");
	}
}

FText FsimulatorEditorModule::GetRoverControlModeText() const
{
	return FText::FromString(RoverControlModeToString(RoverControlMode));
}

TSharedRef<SWidget>
FsimulatorEditorModule::MakeRoverControlModeComboWidget(TSharedPtr<ERoverControlMode> InOption) const
{
	const ERoverControlMode Mode = InOption.IsValid() ? *InOption : ERoverControlMode::Manual;
	return SNew(STextBlock).Text(FText::FromString(RoverControlModeToString(Mode)));
}

void FsimulatorEditorModule::OnRoverControlModeSelectionChanged(TSharedPtr<ERoverControlMode> NewSelection,
                                                                ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid())
		return;

	SelectedRoverControlModeOption = NewSelection;
	RoverControlMode = *NewSelection;
}

void FsimulatorEditorModule::OnCustomCaptureHzChanged(float NewValue)
{
	CustomCaptureHz = NormalizeEditorCaptureHz(NewValue);
}

void FsimulatorEditorModule::OnCameraHorizontalFovDegChanged(float NewValue)
{
	CameraHorizontalFovDeg = FCaptureConfig::SanitizeHorizontalFovDeg(NewValue);
}

void FsimulatorEditorModule::OnStereoBaselineCmChanged(float NewValue)
{
	StereoBaselineCm = FCaptureConfig::SanitizeStereoBaselineCm(NewValue);
}

ECheckBoxState FsimulatorEditorModule::GetStereoRosImagesCheckState() const
{
	return bStereoRosImages ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnStereoRosImagesChanged(ECheckBoxState NewState)
{
	bStereoRosImages = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetGroundTruthImagesCheckState() const
{
	return bGroundTruthImages ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnGroundTruthImagesChanged(ECheckBoxState NewState)
{
	bGroundTruthImages = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetGroundTruthRgbCheckState() const
{
	return bGroundTruthRgb ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnGroundTruthRgbChanged(ECheckBoxState NewState)
{
	bGroundTruthRgb = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetGroundTruthDepthCheckState() const
{
	return bGroundTruthDepth ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnGroundTruthDepthChanged(ECheckBoxState NewState)
{
	bGroundTruthDepth = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetGroundTruthSegmentationCheckState() const
{
	return bGroundTruthSegmentation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnGroundTruthSegmentationChanged(ECheckBoxState NewState)
{
	bGroundTruthSegmentation = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetGroundTruthBoundingBoxesCheckState() const
{
	return bGroundTruthBoundingBoxes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnGroundTruthBoundingBoxesChanged(ECheckBoxState NewState)
{
	bGroundTruthBoundingBoxes = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetTrajectoryCsvCheckState() const
{
	return bTrajectoryCsv ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnTrajectoryCsvChanged(ECheckBoxState NewState)
{
	bTrajectoryCsv = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState FsimulatorEditorModule::GetEnableGroundTruthMapsCheckState() const
{
	return bEnableGroundTruthMaps ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnEnableGroundTruthMapsChanged(ECheckBoxState NewState)
{
	bEnableGroundTruthMaps = (NewState == ECheckBoxState::Checked);
}

void FsimulatorEditorModule::OnImuHzChanged(float NewValue)
{
	ImuPublishHz = FMath::Clamp(NewValue, 1.0f, 400.0f);
}

void FsimulatorEditorModule::OnMaxForwardSpeedKmhChanged(float NewValue)
{
	MaxForwardSpeedKmh = ClampForwardRoverSpeedKmh(NewValue);
}

void FsimulatorEditorModule::OnMaxReverseSpeedKmhChanged(float NewValue)
{
	MaxReverseSpeedKmh = ClampReverseRoverSpeedKmh(NewValue);
}

UWorld* FsimulatorEditorModule::GetEditorWorld() const
{
	if (!GEditor)
		return nullptr;
	return GEditor->GetEditorWorldContext().World();
}

void FsimulatorEditorModule::RefreshTargetsFromEditorWorld()
{
	TargetRobotCamRig.Reset();
	TargetRoverActor.Reset();
	TargetImuPublisher.Reset();
	TargetRoverController.Reset();
	TargetCmdVelController.Reset();
	TargetMapPublishers.Empty();

	UWorld* EditorWorld = GetEditorWorld();
	if (!EditorWorld) {
		return;
	}

	for (TActorIterator<AActor> It(EditorWorld); It; ++It) {
		AActor* CandidateRoverActor = *It;
		if (!IsUsableEditorActor(CandidateRoverActor, EditorWorld)) {
			continue;
		}

		FResolvedRoverPipeline Pipeline;
		if (!ResolveCompleteRoverPipeline(CandidateRoverActor, EditorWorld, Pipeline)) {
			continue;
		}

		if (!TargetRoverActor.IsValid()) {
			TargetRoverActor = CandidateRoverActor;
			TargetRobotCamRig = Pipeline.RobotCamRig;
			TargetImuPublisher = Pipeline.ImuPublisher;
			TargetRoverController = Pipeline.RoverController;
			TargetCmdVelController = Pipeline.CmdVelController;
		}
	}

	if (TargetRobotCamRig.IsValid()) {
		LoadConfigFromRobotCamRig();
		LoadConfigFromRoverControl();
		if (UImuSensorPublisherComponent* ImuPublisher = TargetImuPublisher.Get()) {
			ImuPublishHz = ImuPublisher->GetImuPublishHz();
		}
	} else if (AActor* RoverActor = FindActorByLabel<AActor>(EditorWorld, TEXT("ESA_Rover"))) {
		TargetRoverActor = RoverActor;
		TargetRoverController = FindUsableComponentByClass<URoverVehicleControllerComponent>(RoverActor);
	}

	RefreshMapPublisherTargets(EditorWorld);
}

void FsimulatorEditorModule::RefreshMapPublisherTargets(UWorld* EditorWorld)
{
	TargetMapPublishers.Empty();

	if (!EditorWorld)
		return;

	for (TActorIterator<AActor> It(EditorWorld); It; ++It) {
		AActor* Actor = *It;
		if (!IsUsableEditorActor(Actor, EditorWorld))
			continue;

		TArray<UOccupancyMapPublisherComponent*> MapPublishers;
		Actor->GetComponents<UOccupancyMapPublisherComponent>(MapPublishers);
		for (UOccupancyMapPublisherComponent* MapPublisher : MapPublishers) {
			if (!IsUsableComponent(MapPublisher))
				continue;

			TargetMapPublishers.Add(MapPublisher);
		}
	}
}

bool FsimulatorEditorModule::CanApplySettings() const
{
	if (IsEditorPlaySessionRunning()) {
		return false;
	}

	UWorld* EditorWorld = GetEditorWorld();
	AActor* RoverActor = TargetRoverActor.Get();
	FResolvedRoverPipeline Pipeline;

	return ResolveCompleteRoverPipeline(RoverActor, EditorWorld, Pipeline);
}

bool FsimulatorEditorModule::CanEditGroundTruthMaps() const
{
	return TargetMapPublishers.Num() > 0;
}

bool FsimulatorEditorModule::CanEditImuHz() const
{
	return FindUsableComponentByClass<UImuSensorPublisherComponent>(TargetRoverActor.Get()) != nullptr;
}

bool FsimulatorEditorModule::CanEditRoverControl() const
{
	return FindUsableComponentByClass<URoverVehicleControllerComponent>(TargetRoverActor.Get()) != nullptr;
}

bool FsimulatorEditorModule::CanEditGroundTruthOutput() const
{
	return bGroundTruthImages;
}

FCaptureConfig FsimulatorEditorModule::BuildCaptureConfigFromUi(const FCaptureConfig& ExistingConfig) const
{
	FCaptureConfig NewConfig = ExistingConfig;
	NewConfig.bStereoRosImages = bStereoRosImages;
	NewConfig.bGroundTruthImages = bGroundTruthImages;
	NewConfig.bGroundTruthRgb = bGroundTruthRgb;
	NewConfig.bGroundTruthDepth = bGroundTruthDepth;
	NewConfig.bGroundTruthSegmentation = bGroundTruthSegmentation;
	NewConfig.bGroundTruthBoundingBoxes = bGroundTruthBoundingBoxes;
	NewConfig.bTrajectoryCsv = bTrajectoryCsv;
	NewConfig.bGroundTruthMaps = bEnableGroundTruthMaps;
	NewConfig.ResolutionPreset = ResolutionPreset;
	NewConfig.HorizontalFovDeg = FCaptureConfig::SanitizeHorizontalFovDeg(CameraHorizontalFovDeg);
	NewConfig.CustomCaptureHz = NormalizeEditorCaptureHz(CustomCaptureHz);
	NewConfig.StereoBaselineCm = FCaptureConfig::SanitizeStereoBaselineCm(StereoBaselineCm);
	return NewConfig;
}

void FsimulatorEditorModule::OnApplyClicked()
{
	if (IsEditorPlaySessionRunning()) {
		UE_LOG(LogTemp, Warning, TEXT("Simulator Config settings were not applied while PIE/simulation is running."));
		return;
	}

	UWorld* EditorWorld = GetEditorWorld();
	AActor* RoverActor = TargetRoverActor.Get();
	FResolvedRoverPipeline Pipeline;

	if (!IsUsableEditorActor(RoverActor, EditorWorld)) {
		UE_LOG(LogTemp, Warning, TEXT("Simulator Config settings were not applied because no ESA_Rover was found."));
		return;
	}

	if (!FindUsableComponentByClass<URoverVehicleControllerComponent>(RoverActor)) {
		UE_LOG(LogTemp, Warning,
		       TEXT("Simulator Config settings were not applied because RoverVehicleController is missing."));
		return;
	}

	if (!ResolveCompleteRoverPipeline(RoverActor, EditorWorld, Pipeline)) {
		UE_LOG(LogTemp, Warning,
		       TEXT("Simulator Config settings were not applied because the ESA_Rover pipeline is incomplete."));
		return;
	}
	ARobotCamRig* RobotCamRig = Pipeline.RobotCamRig;
	UChildActorComponent* ChildActorComponent = Pipeline.RobotCamRigChildComponent;
	UImuSensorPublisherComponent* ImuPublisher = Pipeline.ImuPublisher;
	URoverVehicleControllerComponent* RoverController = Pipeline.RoverController;
	URoverCmdVelVehicleControllerComponent* CmdVelController = Pipeline.CmdVelController;

	TargetRoverActor = RoverActor;
	TargetRobotCamRig = RobotCamRig;
	TargetImuPublisher = ImuPublisher;
	TargetRoverController = RoverController;
	TargetCmdVelController = CmdVelController;

	const FCaptureConfig NewConfig = BuildCaptureConfigFromUi(RobotCamRig->GetCaptureConfig());
	MaxForwardSpeedKmh = ClampForwardRoverSpeedKmh(MaxForwardSpeedKmh);
	MaxReverseSpeedKmh = ClampReverseRoverSpeedKmh(MaxReverseSpeedKmh);

	const FScopedTransaction ApplyTransaction(LOCTEXT("ApplySimulatorSettingsTransaction", "Apply Simulator Settings"));
	RoverActor->Modify();
	RoverActor->MarkPackageDirty();

	ChildActorComponent->Modify();
	ChildActorComponent->MarkPackageDirty();

	if (ARobotCamRig* TemplateRobotCamRig = Cast<ARobotCamRig>(ChildActorComponent->GetChildActorTemplate())) {
		TemplateRobotCamRig->Modify();
		TemplateRobotCamRig->SetCaptureConfig(NewConfig);
		TemplateRobotCamRig->MarkPackageDirty();
	}

	RobotCamRig->Modify();
	RobotCamRig->SetCaptureConfig(NewConfig);
	RobotCamRig->MarkPackageDirty();

	if (GEditor) {
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(RoverActor, true, true, true);
	}

	ImuPublisher = FindUsableComponentByClass<UImuSensorPublisherComponent>(RoverActor);
	if (ImuPublisher) {
		if (AActor* Owner = ImuPublisher->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		ImuPublisher->Modify();
		ImuPublisher->SetImuPublishHz(ImuPublishHz);
		ImuPublisher->MarkPackageDirty();
	}

	RoverController = FindUsableComponentByClass<URoverVehicleControllerComponent>(RoverActor);
	if (RoverController) {
		if (AActor* Owner = RoverController->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		RoverController->Modify();
		RoverController->SetControlMode(RoverControlMode);
		RoverController->MaxForwardSpeedKmh = MaxForwardSpeedKmh;
		RoverController->MaxReverseSpeedKmh = MaxReverseSpeedKmh;
		RoverController->MarkPackageDirty();
	}

	CmdVelController = FindUsableComponentByClass<URoverCmdVelVehicleControllerComponent>(RoverActor);
	if (CmdVelController) {
		if (AActor* Owner = CmdVelController->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		CmdVelController->Modify();
		CmdVelController->SetSettings(CmdVelSettings);
		CmdVelController->MarkPackageDirty();
	}

	RefreshTargetsFromEditorWorld();
}

void FsimulatorEditorModule::LoadConfigFromRobotCamRig()
{
	ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	if (!RobotCamRig) {
		return;
	}

	const FCaptureConfig CurrentConfig = RobotCamRig->GetCaptureConfig();

	bStereoRosImages = CurrentConfig.bStereoRosImages;
	bGroundTruthImages = CurrentConfig.bGroundTruthImages;
	bGroundTruthRgb = CurrentConfig.bGroundTruthRgb;
	bGroundTruthDepth = CurrentConfig.bGroundTruthDepth;
	bGroundTruthSegmentation = CurrentConfig.bGroundTruthSegmentation;
	bGroundTruthBoundingBoxes = CurrentConfig.bGroundTruthBoundingBoxes;
	bTrajectoryCsv = CurrentConfig.bTrajectoryCsv;
	bEnableGroundTruthMaps = CurrentConfig.bGroundTruthMaps;
	const FIntPoint ResolvedResolution = CurrentConfig.GetResolvedResolution();
	ResolutionPreset =
	    NormalizeEditorResolutionPreset(CurrentConfig.ResolutionPreset, ResolvedResolution.X, ResolvedResolution.Y);
	CameraHorizontalFovDeg = CurrentConfig.GetResolvedHorizontalFovDeg();
	CustomCaptureHz = NormalizeEditorCaptureHz(CurrentConfig.GetResolvedCaptureHz());
	StereoBaselineCm = FCaptureConfig::SanitizeStereoBaselineCm(CurrentConfig.StereoBaselineCm);
	SelectedResolutionPresetOption = FindResolutionPresetOption(ResolutionPreset);
}

void FsimulatorEditorModule::LoadConfigFromRoverControl()
{
	if (URoverVehicleControllerComponent* RoverController = TargetRoverController.Get()) {
		RoverControlMode = RoverController->GetControlMode();
		MaxForwardSpeedKmh = ClampForwardRoverSpeedKmh(RoverController->MaxForwardSpeedKmh);
		MaxReverseSpeedKmh = ClampReverseRoverSpeedKmh(RoverController->MaxReverseSpeedKmh);
	} else {
		RoverControlMode = ERoverControlMode::Manual;
		MaxForwardSpeedKmh = 5.0f;
		MaxReverseSpeedKmh = 3.0f;
	}

	if (URoverCmdVelVehicleControllerComponent* CmdVelController = TargetCmdVelController.Get()) {
		CmdVelSettings = CmdVelController->GetSettings();
	} else {
		CmdVelSettings = FRoverCmdVelControllerSettings();
	}

	SelectedRoverControlModeOption = FindRoverControlModeOption(RoverControlMode);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FsimulatorEditorModule, simulatorEditor)
