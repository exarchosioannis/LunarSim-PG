#pragma once

#include "CoreMinimal.h"
#include "Capture/CaptureTypes.h"
#include "Modules/ModuleManager.h"
#include "Robots/RoverCmdVelVehicleControllerComponent.h"
#include "Robots/RoverVehicleControllerComponent.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

class AActor;
class ARobotCamRig;
class SDockTab;
class FSpawnTabArgs;
class UImuSensorPublisherComponent;
class UOccupancyMapPublisherComponent;
class URoverGroundTruthPublisherComponent;
class URoverCmdVelVehicleControllerComponent;
class URoverVehicleControllerComponent;
class UWorld;

class FsimulatorEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenSimulatorConfigTab();
	TSharedRef<SDockTab> OnSpawnSimulatorConfigTab(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<class SWidget> BuildSimulatorConfigPanel();

	void InitResolutionPresetOptions();
	TSharedPtr<ELunarSimResolutionPreset> FindResolutionPresetOption(ELunarSimResolutionPreset InPreset) const;
	FText GetResolutionPresetText() const;
	TSharedRef<SWidget> MakeResolutionPresetComboWidget(TSharedPtr<ELunarSimResolutionPreset> InOption) const;
	void OnResolutionPresetSelectionChanged(TSharedPtr<ELunarSimResolutionPreset> NewSelection,
	                                        ESelectInfo::Type SelectInfo);
	FString ResolutionPresetToString(ELunarSimResolutionPreset InPreset) const;

	void InitRoverControlOptions();
	TSharedPtr<ERoverControlMode> FindRoverControlModeOption(ERoverControlMode InMode) const;
	FText GetRoverControlModeText() const;
	TSharedRef<SWidget> MakeRoverControlModeComboWidget(TSharedPtr<ERoverControlMode> InOption) const;
	void OnRoverControlModeSelectionChanged(TSharedPtr<ERoverControlMode> NewSelection, ESelectInfo::Type SelectInfo);
	FString RoverControlModeToString(ERoverControlMode InMode) const;

	ECheckBoxState GetStereoRosImagesCheckState() const;
	void OnStereoRosImagesChanged(ECheckBoxState NewState);
	ECheckBoxState GetGroundTruthImagesCheckState() const;
	void OnGroundTruthImagesChanged(ECheckBoxState NewState);
	ECheckBoxState GetGroundTruthRgbCheckState() const;
	void OnGroundTruthRgbChanged(ECheckBoxState NewState);
	ECheckBoxState GetGroundTruthDepthCheckState() const;
	void OnGroundTruthDepthChanged(ECheckBoxState NewState);
	ECheckBoxState GetGroundTruthSegmentationCheckState() const;
	void OnGroundTruthSegmentationChanged(ECheckBoxState NewState);
	ECheckBoxState GetGroundTruthBoundingBoxesCheckState() const;
	void OnGroundTruthBoundingBoxesChanged(ECheckBoxState NewState);
	ECheckBoxState GetTrajectoryCsvCheckState() const;
	void OnTrajectoryCsvChanged(ECheckBoxState NewState);
	ECheckBoxState GetEnableGroundTruthMapsCheckState() const;
	void OnEnableGroundTruthMapsChanged(ECheckBoxState NewState);
	void OnCustomCaptureHzChanged(float NewValue);
	void OnCameraHorizontalFovDegChanged(float NewValue);
	void OnStereoBaselineCmChanged(float NewValue);
	void OnImuHzChanged(float NewValue);

	void RefreshTargetsFromEditorWorld();
	bool CanApplySettings() const;
	bool CanEditGroundTruthMaps() const;
	bool CanEditImuHz() const;
	bool CanEditRoverControl() const;
	bool CanEditGroundTruthOutput() const;
	FText GetApplyStatusText() const;

	void OnApplyClicked();
	FCaptureConfig BuildCaptureConfigFromUi(const FCaptureConfig& ExistingConfig) const;
	void LoadConfigFromRobotCamRig();
	void LoadConfigFromRoverControl();
	UWorld* GetEditorWorld() const;
	void RefreshMapPublisherTargets(UWorld* EditorWorld);

private:
	static const FName SimulatorConfigTabName;

	TArray<TSharedPtr<ELunarSimResolutionPreset>> ResolutionPresetOptions;
	TSharedPtr<ELunarSimResolutionPreset> SelectedResolutionPresetOption;
	TArray<TSharedPtr<ERoverControlMode>> RoverControlModeOptions;
	TSharedPtr<ERoverControlMode> SelectedRoverControlModeOption;

	TWeakObjectPtr<ARobotCamRig> TargetRobotCamRig;
	TWeakObjectPtr<AActor> TargetRoverActor;
	TWeakObjectPtr<UImuSensorPublisherComponent> TargetImuPublisher;
	TWeakObjectPtr<URoverVehicleControllerComponent> TargetRoverController;
	TWeakObjectPtr<URoverCmdVelVehicleControllerComponent> TargetCmdVelController;
	TArray<TWeakObjectPtr<UOccupancyMapPublisherComponent>> TargetMapPublishers;

	ELunarSimResolutionPreset ResolutionPreset = ELunarSimResolutionPreset::R1024x1024;
	ERoverControlMode RoverControlMode = ERoverControlMode::Manual;
	FRoverCmdVelControllerSettings CmdVelSettings;
	float CustomCaptureHz = 6.0f;
	float CameraHorizontalFovDeg = 90.0f;
	float StereoBaselineCm = 20.0f;
	bool bStereoRosImages = true;
	bool bGroundTruthImages = true;
	bool bGroundTruthRgb = true;
	bool bGroundTruthDepth = true;
	bool bGroundTruthSegmentation = true;
	bool bGroundTruthBoundingBoxes = true;
	bool bTrajectoryCsv = true;
	bool bEnableGroundTruthMaps = true;
	float ImuPublishHz = 100.0f;
	int32 RobotCamRigCount = 0;
	int32 GroundTruthMapPublisherCount = 0;
	FText LastApplyStatus;
};
