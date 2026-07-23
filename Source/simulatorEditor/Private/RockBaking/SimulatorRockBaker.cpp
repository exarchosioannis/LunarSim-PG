#include "RockBaking/SimulatorRockBaker.h"

#include "RockBaking/SimulatorRockSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CollisionQueryParams.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "SimulatorRockBaker"

DEFINE_LOG_CATEGORY_STATIC(LogSimulatorRockBaking, Log, All);

namespace
{
const FName BakedRockOwnerTag(TEXT("LunarSimPG.SimulatorEditor.BakedRocks"));
const FName BakedRockRootTag(TEXT("LunarSimPG.SimulatorEditor.BakedRockRoot.V1"));
const FName BakedRockComponentTag(TEXT("LunarSimPG.SimulatorEditor.BakedRockComponent.V1"));
const FName LegacyBakedRockTag(TEXT("BakedRock"));
const FName LegacyBakedRockInstancesTag(TEXT("BakedRockInstances"));
const FString LegacyBakedRockIdentity(TEXT("BakedRockInstances"));

constexpr double TraceStartHeightCm = 200000.0;
constexpr double TraceEndDepthCm = 200000.0;

struct FParsedRock
{
	int32 InstanceId = INDEX_NONE;
	FVector2D PositionMeters = FVector2D::ZeroVector;
	double DiameterMeters = 1.0;
	double YawDegrees = 0.0;
	double TiltDegrees = 0.0;
	double TiltAxisDegrees = 0.0;
	double BurialFraction = 0.0;
};

struct FPlannedRock
{
	UStaticMesh* Mesh = nullptr;
	FTransform WorldTransform = FTransform::Identity;
};

enum class EGroundedTransformResult : uint8
{
	Success,
	TraceFailure,
	PlacementFailure
};

FSimulatorRockBakeResult MakeFailure(FSimulatorRockBakeResult Result, const FString& Message)
{
	Result.bSucceeded = false;
	Result.Message = Message;
	return Result;
}

bool ValidateEditorWorldForMutation(UWorld* World, FString& OutError)
{
	if (!GEditor) {
		OutError = TEXT("The Unreal Editor is unavailable.");
		return false;
	}

	if (GEditor->IsPlaySessionInProgress()) {
		OutError = TEXT("Rock actions are locked while PIE/simulation is running.");
		return false;
	}

	if (!IsValid(World) || World->WorldType != EWorldType::Editor) {
		OutError = TEXT("No mutable editor world is available.");
		return false;
	}

	if (!IsValid(World->PersistentLevel)) {
		OutError = TEXT("The editor world has no persistent level.");
		return false;
	}

	return true;
}

bool IsExactLegacyBakedRockActor(const AActor* Actor)
{
	if (!Actor || !Actor->ActorHasTag(LegacyBakedRockTag) || !Actor->ActorHasTag(LegacyBakedRockInstancesTag)) {
		return false;
	}

	bool bHasExactIdentity = Actor->GetName().Equals(LegacyBakedRockIdentity, ESearchCase::CaseSensitive);
#if WITH_EDITOR
	bHasExactIdentity =
	    bHasExactIdentity || Actor->GetActorLabel().Equals(LegacyBakedRockIdentity, ESearchCase::CaseSensitive);
#endif
	return bHasExactIdentity;
}

bool IsOwnedBakedRockActor(const AActor* Actor)
{
	return Actor && ((Actor->ActorHasTag(BakedRockOwnerTag) && Actor->ActorHasTag(BakedRockRootTag)) ||
	                 IsExactLegacyBakedRockActor(Actor));
}

void CollectOwnedBakedRockActors(UWorld* World, TArray<AActor*>& OutActors)
{
	OutActors.Reset();
	if (!World) {
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It) {
		AActor* Actor = *It;
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed() && IsOwnedBakedRockActor(Actor)) {
			OutActors.Add(Actor);
		}
	}
}

int32 ClearOwnedBakedRockActorsInternal(UWorld* World, const TArray<AActor*>& ActorsToClear)
{
	int32 ClearedCount = 0;
	for (AActor* Actor : ActorsToClear) {
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || !IsOwnedBakedRockActor(Actor)) {
			continue;
		}

		Actor->Modify();
		if (World->EditorDestroyActor(Actor, true)) {
			++ClearedCount;
		}
	}
	return ClearedCount;
}

bool IsIntegralInt32(double Value)
{
	return FMath::IsFinite(Value) && Value >= static_cast<double>(MIN_int32) &&
	       Value <= static_cast<double>(MAX_int32) &&
	       FMath::Abs(Value - FMath::RoundToDouble(Value)) <= UE_DOUBLE_SMALL_NUMBER;
}

bool ReadOptionalFiniteNumber(const FJsonObject& Object, const TCHAR* FieldName, double& InOutValue)
{
	if (!Object.HasField(FieldName)) {
		return true;
	}

	double Value = 0.0;
	if (!Object.TryGetNumberField(FieldName, Value) || !FMath::IsFinite(Value)) {
		return false;
	}

	InOutValue = Value;
	return true;
}

bool ValidateSchema(const FJsonObject& RootObject, int32 InputRecordCount, TArray<FString>& OutWarnings,
                    FString& OutError)
{
	TArray<FString> MissingIdentityFields;

	FString Format;
	if (RootObject.HasField(TEXT("format"))) {
		if (!RootObject.TryGetStringField(TEXT("format"), Format)) {
			OutError = TEXT("Rockfield schema field 'format' must be a string.");
			return false;
		}
		if (!Format.Equals(TEXT("MoonSimOfflineRockField"), ESearchCase::IgnoreCase)) {
			OutError = FString::Printf(TEXT("Unsupported rockfield format '%s'."), *Format);
			return false;
		}
	} else {
		MissingIdentityFields.Add(TEXT("format"));
	}

	double Version = 0.0;
	if (RootObject.HasField(TEXT("version"))) {
		if (!RootObject.TryGetNumberField(TEXT("version"), Version) || !FMath::IsFinite(Version) || Version != 1.0) {
			OutError = TEXT("Unsupported rockfield version; expected version 1.");
			return false;
		}
	} else {
		MissingIdentityFields.Add(TEXT("version"));
	}

	FString Units;
	if (RootObject.HasField(TEXT("units"))) {
		if (!RootObject.TryGetStringField(TEXT("units"), Units)) {
			OutError = TEXT("Rockfield schema field 'units' must be a string.");
			return false;
		}
		if (!Units.Equals(TEXT("meters"), ESearchCase::IgnoreCase)) {
			OutError = FString::Printf(TEXT("Unsupported rockfield units '%s'; expected meters."), *Units);
			return false;
		}
	} else {
		MissingIdentityFields.Add(TEXT("units"));
	}

	FString CoordinateFrame;
	if (RootObject.HasField(TEXT("coordinate_frame"))) {
		if (!RootObject.TryGetStringField(TEXT("coordinate_frame"), CoordinateFrame)) {
			OutError = TEXT("Rockfield schema field 'coordinate_frame' must be a string.");
			return false;
		}
		if (!CoordinateFrame.Equals(TEXT("centered_map_meters"), ESearchCase::IgnoreCase)) {
			OutError = FString::Printf(TEXT("Unsupported coordinate frame '%s'; expected centered_map_meters."),
			                           *CoordinateFrame);
			return false;
		}
	} else {
		MissingIdentityFields.Add(TEXT("coordinate_frame"));
	}

	double DeclaredRockCount = 0.0;
	if (RootObject.HasField(TEXT("rock_count"))) {
		if (!RootObject.TryGetNumberField(TEXT("rock_count"), DeclaredRockCount) ||
		    !IsIntegralInt32(DeclaredRockCount) || DeclaredRockCount < 0.0) {
			OutError = TEXT("Rockfield schema field 'rock_count' must be a non-negative integer.");
			return false;
		}

		if (static_cast<int32>(DeclaredRockCount) != InputRecordCount) {
			OutWarnings.Add(
			    FString::Printf(TEXT("Declared rock_count is %d but the rocks array contains %d record(s)."),
			                    static_cast<int32>(DeclaredRockCount), InputRecordCount));
		}
	} else {
		MissingIdentityFields.Add(TEXT("rock_count"));
	}

	if (MissingIdentityFields.Num() > 0) {
		OutWarnings.Add(
		    FString::Printf(TEXT("Legacy rockfield assumptions were used because schema field(s) %s are absent."),
		                    *FString::Join(MissingIdentityFields, TEXT(", "))));
	}

	return true;
}

bool LoadRocksFromJson(const FString& JsonPath, TArray<FParsedRock>& OutRocks, FSimulatorRockBakeResult& InOutResult,
                       TArray<FString>& OutWarnings, FString& OutError)
{
	OutRocks.Reset();

	if (JsonPath.IsEmpty()) {
		OutError = TEXT("Choose a Rock Field JSON file first.");
		return false;
	}

	if (!FPaths::FileExists(JsonPath)) {
		OutError = FString::Printf(TEXT("Rockfield JSON was not found: %s"), *JsonPath);
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonPath)) {
		OutError = FString::Printf(TEXT("Failed to read rockfield JSON: %s"), *JsonPath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) {
		OutError = FString::Printf(TEXT("Failed to parse rockfield JSON: %s"), *JsonPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RocksArray = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("rocks"), RocksArray) || !RocksArray) {
		OutError = TEXT("Rockfield JSON is missing the required 'rocks' array.");
		return false;
	}

	InOutResult.TotalInputRecords = RocksArray->Num();
	if (!ValidateSchema(*RootObject, RocksArray->Num(), OutWarnings, OutError)) {
		return false;
	}

	OutRocks.Reserve(RocksArray->Num());
	TSet<int32> SeenInstanceIds;
	int32 ClampedTiltCount = 0;
	int32 ClampedBurialCount = 0;

	for (const TSharedPtr<FJsonValue>& Value : *RocksArray) {
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid()) {
			++InOutResult.InvalidRecords;
			continue;
		}

		double X = 0.0;
		double Y = 0.0;
		double Diameter = 0.0;
		if (!Object->TryGetNumberField(TEXT("x_m"), X) || !Object->TryGetNumberField(TEXT("y_m"), Y) ||
		    !Object->TryGetNumberField(TEXT("diameter_m"), Diameter) || !FMath::IsFinite(X) || !FMath::IsFinite(Y) ||
		    !FMath::IsFinite(X * 100.0) || !FMath::IsFinite(Y * 100.0) || !FMath::IsFinite(Diameter) ||
		    Diameter <= 0.0) {
			++InOutResult.InvalidRecords;
			continue;
		}

		double Yaw = 0.0;
		double Tilt = 0.0;
		double TiltAxis = 0.0;
		double Burial = 0.0;
		if (!ReadOptionalFiniteNumber(*Object, TEXT("yaw_degrees"), Yaw) ||
		    !ReadOptionalFiniteNumber(*Object, TEXT("tilt_degrees"), Tilt) ||
		    !ReadOptionalFiniteNumber(*Object, TEXT("tilt_axis_degrees"), TiltAxis) ||
		    !ReadOptionalFiniteNumber(*Object, TEXT("burial_fraction"), Burial)) {
			++InOutResult.InvalidRecords;
			continue;
		}

		int32 InstanceId = OutRocks.Num();
		if (Object->HasField(TEXT("instance_id"))) {
			double InstanceIdNumber = 0.0;
			if (!Object->TryGetNumberField(TEXT("instance_id"), InstanceIdNumber) ||
			    !IsIntegralInt32(InstanceIdNumber)) {
				++InOutResult.InvalidRecords;
				continue;
			}
			InstanceId = static_cast<int32>(FMath::RoundToDouble(InstanceIdNumber));
		}

		if (SeenInstanceIds.Contains(InstanceId)) {
			++InOutResult.DuplicateIds;
		} else {
			SeenInstanceIds.Add(InstanceId);
		}

		const double ClampedTilt = FMath::Clamp(Tilt, 0.0, 90.0);
		const double ClampedBurial = FMath::Clamp(Burial, 0.0, 1.0);
		ClampedTiltCount += ClampedTilt != Tilt ? 1 : 0;
		ClampedBurialCount += ClampedBurial != Burial ? 1 : 0;

		FParsedRock& Rock = OutRocks.AddDefaulted_GetRef();
		Rock.InstanceId = InstanceId;
		Rock.PositionMeters = FVector2D(X, Y);
		Rock.DiameterMeters = Diameter;
		Rock.YawDegrees = FMath::Fmod(Yaw, 360.0);
		Rock.TiltDegrees = ClampedTilt;
		Rock.TiltAxisDegrees = FMath::Fmod(TiltAxis, 360.0);
		Rock.BurialFraction = ClampedBurial;
	}

	InOutResult.ValidRecords = OutRocks.Num();
	if (InOutResult.InvalidRecords > 0) {
		OutWarnings.Add(FString::Printf(TEXT("Skipped %d malformed or physically invalid rock record(s)."),
		                                InOutResult.InvalidRecords));
	}
	if (InOutResult.DuplicateIds > 0) {
		OutWarnings.Add(FString::Printf(
		    TEXT("Found %d duplicate instance ID(s); duplicate records retain the same deterministic mesh mapping."),
		    InOutResult.DuplicateIds));
	}
	if (ClampedTiltCount > 0) {
		OutWarnings.Add(FString::Printf(TEXT("Clamped tilt_degrees for %d record(s) to [0, 90]."), ClampedTiltCount));
	}
	if (ClampedBurialCount > 0) {
		OutWarnings.Add(
		    FString::Printf(TEXT("Clamped burial_fraction for %d record(s) to [0, 1]."), ClampedBurialCount));
	}

	if (OutRocks.Num() == 0) {
		OutError = FString::Printf(TEXT("Rockfield contains no valid records (%d input, %d invalid)."),
		                           InOutResult.TotalInputRecords, InOutResult.InvalidRecords);
		return false;
	}

	return true;
}

bool LoadMeshesFromFolder(const USimulatorRockSettings& Settings, TArray<UStaticMesh*>& OutMeshes, FString& OutError)
{
	OutMeshes.Reset();
	const FString& MeshFolderPath = Settings.MeshFolderPath.Path;
	if (MeshFolderPath.IsEmpty()) {
		OutError = TEXT("Choose a rock mesh Content folder first.");
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule =
	    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*MeshFolderPath));
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);
	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B) {
		return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
	});

	for (const FAssetData& AssetData : AssetDataList) {
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset())) {
			OutMeshes.Add(Mesh);
		}
	}

	if (OutMeshes.Num() == 0) {
		OutError = FString::Printf(TEXT("No UStaticMesh assets were found recursively under %s."), *MeshFolderPath);
		return false;
	}

	return true;
}

UStaticMesh* PickMeshForRock(const FParsedRock& Rock, const TArray<UStaticMesh*>& Meshes)
{
	if (Meshes.Num() == 0) {
		return nullptr;
	}

	const uint64 SafeMagnitude = Rock.InstanceId >= 0 ? static_cast<uint64>(Rock.InstanceId)
	                                                  : static_cast<uint64>(-static_cast<int64>(Rock.InstanceId));
	return Meshes[static_cast<int32>(SafeMagnitude % static_cast<uint64>(Meshes.Num()))];
}

EGroundedTransformResult ComputeGroundedTransform(UWorld* World, const FParsedRock& Rock, UStaticMesh* Mesh,
                                                  const TArray<AActor*>& IgnoredActors, FTransform& OutWorldTransform)
{
	if (!World || !Mesh) {
		return EGroundedTransformResult::PlacementFailure;
	}

	const FVector LocalPositionCm(Rock.PositionMeters.X * 100.0, Rock.PositionMeters.Y * 100.0, 0.0);
	if (LocalPositionCm.ContainsNaN()) {
		return EGroundedTransformResult::PlacementFailure;
	}

	const FVector TraceStart = LocalPositionCm + FVector(0.0, 0.0, TraceStartHeightCm);
	const FVector TraceEnd = LocalPositionCm - FVector(0.0, 0.0, TraceEndDepthCm);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RockBakeTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.AddIgnoredActors(IgnoredActors);
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams)) {
		return EGroundedTransformResult::TraceFailure;
	}

	const FVector GroundNormal = Hit.ImpactNormal.GetSafeNormal();
	if (GroundNormal.IsNearlyZero() || GroundNormal.ContainsNaN()) {
		return EGroundedTransformResult::TraceFailure;
	}

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	if (Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN() || !FMath::IsFinite(Bounds.SphereRadius) ||
	    Bounds.SphereRadius <= UE_DOUBLE_SMALL_NUMBER) {
		return EGroundedTransformResult::PlacementFailure;
	}

	const double MeshDiameterMeters = (2.0 * Bounds.SphereRadius) / 100.0;
	const double Scale = Rock.DiameterMeters / MeshDiameterMeters;
	if (!FMath::IsFinite(Scale) || Scale <= 0.0) {
		return EGroundedTransformResult::PlacementFailure;
	}

	const FQuat AlignToGroundQuat = FQuat::FindBetweenNormals(FVector::UpVector, GroundNormal);
	const FQuat YawQuat(GroundNormal, FMath::DegreesToRadians(Rock.YawDegrees));

	FVector TangentX = FVector::CrossProduct(FVector::UpVector, GroundNormal);
	if (!TangentX.Normalize()) {
		TangentX = FVector::ForwardVector;
	}
	TangentX = FVector::VectorPlaneProject(TangentX, GroundNormal).GetSafeNormal();
	if (TangentX.IsNearlyZero()) {
		TangentX = FVector::ForwardVector;
	}

	FVector TangentY = FVector::CrossProduct(GroundNormal, TangentX).GetSafeNormal();
	if (TangentY.IsNearlyZero()) {
		TangentY = FVector::RightVector;
	}

	const double TiltAxisRadians = FMath::DegreesToRadians(Rock.TiltAxisDegrees);
	const FVector TiltDirection =
	    (FMath::Cos(TiltAxisRadians) * TangentX + FMath::Sin(TiltAxisRadians) * TangentY).GetSafeNormal();
	FVector TiltRotationAxis = FVector::CrossProduct(GroundNormal, TiltDirection);
	if (!TiltRotationAxis.Normalize()) {
		TiltRotationAxis = TangentX;
	}

	const FQuat TiltQuat(TiltRotationAxis, FMath::DegreesToRadians(Rock.TiltDegrees));
	FQuat FinalRotationQuat = TiltQuat * YawQuat * AlignToGroundQuat;
	FinalRotationQuat.Normalize();
	if (FinalRotationQuat.ContainsNaN()) {
		return EGroundedTransformResult::PlacementFailure;
	}

	const FVector BoxMin = Bounds.Origin - Bounds.BoxExtent;
	const FVector BoxMax = Bounds.Origin + Bounds.BoxExtent;
	const FVector LocalCorners[8] = {FVector(BoxMin.X, BoxMin.Y, BoxMin.Z), FVector(BoxMin.X, BoxMin.Y, BoxMax.Z),
	                                 FVector(BoxMin.X, BoxMax.Y, BoxMin.Z), FVector(BoxMin.X, BoxMax.Y, BoxMax.Z),
	                                 FVector(BoxMax.X, BoxMin.Y, BoxMin.Z), FVector(BoxMax.X, BoxMin.Y, BoxMax.Z),
	                                 FVector(BoxMax.X, BoxMax.Y, BoxMin.Z), FVector(BoxMax.X, BoxMax.Y, BoxMax.Z)};

	double MinNormalProjectionCm = TNumericLimits<double>::Max();
	double MaxNormalProjectionCm = TNumericLimits<double>::Lowest();
	for (const FVector& LocalCorner : LocalCorners) {
		const FVector RotatedScaledCorner = FinalRotationQuat.RotateVector(LocalCorner * Scale);
		const double ProjectionCm = FVector::DotProduct(RotatedScaledCorner, GroundNormal);
		if (!FMath::IsFinite(ProjectionCm)) {
			return EGroundedTransformResult::PlacementFailure;
		}
		MinNormalProjectionCm = FMath::Min(MinNormalProjectionCm, ProjectionCm);
		MaxNormalProjectionCm = FMath::Max(MaxNormalProjectionCm, ProjectionCm);
	}

	const double RockThicknessAlongNormalCm = FMath::Max(1.0, MaxNormalProjectionCm - MinNormalProjectionCm);
	const double BurialDepthCm = RockThicknessAlongNormalCm * Rock.BurialFraction;
	const FVector FinalLocation = Hit.ImpactPoint + GroundNormal * (-MinNormalProjectionCm - BurialDepthCm);
	if (FinalLocation.ContainsNaN()) {
		return EGroundedTransformResult::PlacementFailure;
	}

	OutWorldTransform = FTransform(FinalRotationQuat, FinalLocation, FVector(Scale));
	return EGroundedTransformResult::Success;
}

UHierarchicalInstancedStaticMeshComponent* CreateRockInstanceComponent(AActor* RootActor, UStaticMesh* Mesh)
{
	if (!RootActor || !Mesh) {
		return nullptr;
	}

	const FString MeshPath = Mesh->GetPathName();
	const FName BaseName(*FString::Printf(TEXT("HISM_%s_%08X"), *Mesh->GetName(), GetTypeHash(MeshPath)));
	const FName ComponentName =
	    MakeUniqueObjectName(RootActor, UHierarchicalInstancedStaticMeshComponent::StaticClass(), BaseName);
	UHierarchicalInstancedStaticMeshComponent* Component =
	    NewObject<UHierarchicalInstancedStaticMeshComponent>(RootActor, ComponentName, RF_Transactional);
	if (!Component) {
		return nullptr;
	}

	Component->CreationMethod = EComponentCreationMethod::Instance;
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetGenerateOverlapEvents(false);
	Component->ComponentTags.AddUnique(BakedRockOwnerTag);
	Component->ComponentTags.AddUnique(BakedRockComponentTag);
	Component->ComponentTags.AddUnique(LegacyBakedRockTag);
	Component->SetupAttachment(RootActor->GetRootComponent());
	RootActor->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

AActor* CreateBakedRockRoot(UWorld* World)
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = World->PersistentLevel;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transactional;

	AActor* RootActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!RootActor) {
		return nullptr;
	}

	RootActor->Modify();
#if WITH_EDITOR
	RootActor->SetActorLabel(LegacyBakedRockIdentity);
#endif
	RootActor->SetFolderPath(FName(TEXT("Rocks")));
	RootActor->Tags.AddUnique(BakedRockOwnerTag);
	RootActor->Tags.AddUnique(BakedRockRootTag);
	RootActor->Tags.AddUnique(LegacyBakedRockTag);
	RootActor->Tags.AddUnique(LegacyBakedRockInstancesTag);

	USceneComponent* RootSceneComponent =
	    NewObject<USceneComponent>(RootActor, TEXT("BakedRockRoot"), RF_Transactional);
	if (!RootSceneComponent) {
		World->EditorDestroyActor(RootActor, true);
		return nullptr;
	}

	RootSceneComponent->CreationMethod = EComponentCreationMethod::Instance;
	RootSceneComponent->SetMobility(EComponentMobility::Static);
	RootSceneComponent->ComponentTags.AddUnique(BakedRockOwnerTag);
	RootSceneComponent->ComponentTags.AddUnique(BakedRockComponentTag);
	RootActor->SetRootComponent(RootSceneComponent);
	RootActor->AddInstanceComponent(RootSceneComponent);
	RootSceneComponent->RegisterComponent();
	return RootActor;
}
}

FSimulatorRockBakeResult FSimulatorRockBaker::BakeRocksToLevel(UWorld* World, const USimulatorRockSettings& Settings)
{
	FSimulatorRockBakeResult Result;
	FString Error;
	if (!ValidateEditorWorldForMutation(World, Error)) {
		return MakeFailure(Result, Error);
	}

	TArray<FString> Warnings;
	TArray<FParsedRock> Rocks;
	if (!LoadRocksFromJson(Settings.RockFieldJsonFile.FilePath, Rocks, Result, Warnings, Error)) {
		return MakeFailure(Result, Error);
	}

	TArray<UStaticMesh*> Meshes;
	if (!LoadMeshesFromFolder(Settings, Meshes, Error)) {
		return MakeFailure(Result, Error);
	}
	Result.LoadedMeshes = Meshes.Num();

	TArray<AActor*> ExistingOwnedActors;
	CollectOwnedBakedRockActors(World, ExistingOwnedActors);

	TArray<FPlannedRock> PlannedRocks;
	PlannedRocks.Reserve(Rocks.Num());
	for (const FParsedRock& Rock : Rocks) {
		UStaticMesh* Mesh = PickMeshForRock(Rock, Meshes);
		FTransform WorldTransform;
		const EGroundedTransformResult TransformResult =
		    ComputeGroundedTransform(World, Rock, Mesh, ExistingOwnedActors, WorldTransform);
		if (TransformResult == EGroundedTransformResult::TraceFailure) {
			++Result.TraceFailures;
			continue;
		}
		if (TransformResult == EGroundedTransformResult::PlacementFailure) {
			++Result.PlacementFailures;
			continue;
		}

		FPlannedRock& PlannedRock = PlannedRocks.AddDefaulted_GetRef();
		PlannedRock.Mesh = Mesh;
		PlannedRock.WorldTransform = WorldTransform;
	}

	if (Result.TraceFailures > 0) {
		Warnings.Add(FString::Printf(TEXT("Ground traces failed for %d valid record(s)."), Result.TraceFailures));
	}
	if (Result.PlacementFailures > 0) {
		Warnings.Add(
		    FString::Printf(TEXT("Transform validation failed for %d valid record(s)."), Result.PlacementFailures));
	}

	if (PlannedRocks.Num() == 0) {
		return MakeFailure(
		    Result,
		    FString::Printf(
		        TEXT(
		            "No valid grounded rock placements were produced (%d valid records, %d trace failures, %d transform failures). The previous bake was retained."),
		        Result.ValidRecords, Result.TraceFailures, Result.PlacementFailures));
	}

	const FScopedTransaction Transaction(LOCTEXT("BakeOfflineRocksToLevel", "Bake Offline Rocks To Level"));
	World->Modify();
	World->PersistentLevel->Modify();
	Result.ClearedActors = ClearOwnedBakedRockActorsInternal(World, ExistingOwnedActors);

	AActor* RockRootActor = CreateBakedRockRoot(World);
	if (!RockRootActor) {
		UE_LOG(LogSimulatorRockBaking, Error,
		       TEXT("Rock bake failed: subsystem=editor mutation, resource=baked-rock root actor, stage=post-clear creation, cause=root actor creation failed, effect=level output incomplete after clearing %d/%d prior actor(s); transaction can be undone."),
		       Result.ClearedActors, ExistingOwnedActors.Num());
		return MakeFailure(Result, TEXT("Bake failed while creating the baked-rock root actor."));
	}

	TMap<UStaticMesh*, UHierarchicalInstancedStaticMeshComponent*> ComponentsByMesh;
	TSet<UStaticMesh*> UsedMeshes;
	for (const FPlannedRock& PlannedRock : PlannedRocks) {
		UsedMeshes.Add(PlannedRock.Mesh);
	}
	for (UStaticMesh* Mesh : Meshes) {
		if (!UsedMeshes.Contains(Mesh)) {
			continue;
		}

		if (UHierarchicalInstancedStaticMeshComponent* Component = CreateRockInstanceComponent(RockRootActor, Mesh)) {
			ComponentsByMesh.Add(Mesh, Component);
		}
	}

	for (const FPlannedRock& PlannedRock : PlannedRocks) {
		UHierarchicalInstancedStaticMeshComponent* const* ComponentPtr = ComponentsByMesh.Find(PlannedRock.Mesh);
		if (!ComponentPtr || !*ComponentPtr) {
			++Result.PlacementFailures;
			continue;
		}

		(*ComponentPtr)->Modify();
		if ((*ComponentPtr)->AddInstance(PlannedRock.WorldTransform, true) != INDEX_NONE) {
			++Result.PlacedRocks;
		} else {
			++Result.PlacementFailures;
		}
	}

	if (Result.PlacedRocks == 0) {
		RockRootActor->Modify();
		World->EditorDestroyActor(RockRootActor, true);
		UE_LOG(LogSimulatorRockBaking, Error,
		       TEXT("Rock bake failed: subsystem=editor mutation, resource=HISM instances, stage=post-clear placement, cause=no instance created, effect=level output incomplete after clearing %d/%d prior actor(s); transaction can be undone."),
		       Result.ClearedActors, ExistingOwnedActors.Num());
		return MakeFailure(Result,
		                   TEXT("Bake failed while creating HISM instances; no empty root actor was retained."));
	}

	RockRootActor->MarkPackageDirty();
	World->PersistentLevel->MarkPackageDirty();
	World->MarkPackageDirty();

	Result.bSucceeded = true;
	Result.bHadWarnings = Warnings.Num() > 0 || Result.ClearedActors != ExistingOwnedActors.Num() ||
	                      Result.PlacedRocks != PlannedRocks.Num();
	Result.Message = FString::Printf(
	    TEXT(
	        "Baked %d rock(s) from %d input record(s) using %d mesh(es). Valid: %d; invalid: %d; trace failures: %d; transform/instance failures: %d; duplicate IDs: %d."),
	    Result.PlacedRocks, Result.TotalInputRecords, Result.LoadedMeshes, Result.ValidRecords, Result.InvalidRecords,
	    Result.TraceFailures, Result.PlacementFailures, Result.DuplicateIds);
	if (Result.bHadWarnings) {
		Result.Message += TEXT(" Completed with warnings; see the Output Log.");
	}

	if (Result.bHadWarnings) {
		UE_LOG(LogSimulatorRockBaking, Warning,
		       TEXT("Rock bake completed with omissions: %s Details: %s Cleared prior actors: %d/%d. Created instances: %d/%d. Review the level before saving."),
		       *Result.Message, *FString::Join(Warnings, TEXT(" ")), Result.ClearedActors,
		       ExistingOwnedActors.Num(), Result.PlacedRocks, PlannedRocks.Num());
	}
	return Result;
}

FSimulatorRockBakeResult FSimulatorRockBaker::ClearBakedRocks(UWorld* World)
{
	FSimulatorRockBakeResult Result;
	FString Error;
	if (!ValidateEditorWorldForMutation(World, Error)) {
		return MakeFailure(Result, Error);
	}

	TArray<AActor*> OwnedActors;
	CollectOwnedBakedRockActors(World, OwnedActors);
	if (OwnedActors.Num() == 0) {
		Result.bSucceeded = true;
		Result.Message = TEXT("No owned baked-rock actors were found to clear.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("ClearBakedRocks", "Clear Baked Rocks"));
	World->Modify();
	World->PersistentLevel->Modify();
	Result.ClearedActors = ClearOwnedBakedRockActorsInternal(World, OwnedActors);
	World->PersistentLevel->MarkPackageDirty();
	World->MarkPackageDirty();

	Result.bSucceeded = Result.ClearedActors == OwnedActors.Num();
	Result.bHadWarnings = !Result.bSucceeded;
	Result.Message = FString::Printf(TEXT("Cleared %d owned baked-rock actor(s)."), Result.ClearedActors);
	if (Result.bSucceeded) {
	} else {
		Result.Message +=
		    FString::Printf(TEXT(" %d actor(s) could not be cleared."), OwnedActors.Num() - Result.ClearedActors);
		UE_LOG(LogSimulatorRockBaking, Error,
		       TEXT("Rock clear incomplete: subsystem=editor mutation, resource=owned baked-rock actors, stage=destruction, cause=%s, effect=level partially cleared; transaction can be undone."),
		       *Result.Message);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
