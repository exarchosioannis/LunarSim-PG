#pragma once

#include "CoreMinimal.h"
#include "Capture/CaptureTypes.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

class AActor;
class ARobotCamRig;
class SDockTab;
class FSpawnTabArgs;
class UImuSensorPublisherComponent;
class UOccupancyMapPublisherComponent;
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

	void InitCaptureModeOptions();
	TSharedPtr<ECaptureMode> FindCaptureModeOption(ECaptureMode InMode) const;
	FText GetCaptureModeText() const;
	TSharedRef<SWidget> MakeCaptureModeComboWidget(TSharedPtr<ECaptureMode> InOption) const;
	void OnCaptureModeSelectionChanged(TSharedPtr<ECaptureMode> NewSelection, ESelectInfo::Type SelectInfo);
	FString CaptureModeToString(ECaptureMode InMode) const;

	ECheckBoxState GetEnableGroundTruthMapsCheckState() const;
	void OnEnableGroundTruthMapsChanged(ECheckBoxState NewState);
	void OnPublishHzChanged(int32 NewValue);
	void OnStereoBaselineCmChanged(float NewValue);
	void OnImuHzChanged(float NewValue);

	void RefreshTargetsFromEditorWorld();
	void SelectTargetRobotCamRig();
	bool CanApplySettings() const;
	bool CanSelectTargetRobotCamRig() const;
	bool CanEditGroundTruthMaps() const;
	bool CanEditImuHz() const;
	FText GetTargetStatusText() const;
	FText GetTargetActorText() const;
	FText GetMapStatusText() const;
	FText GetImuStatusText() const;
	FText GetApplyStatusText() const;

	void OnApplyClicked();
	void LoadConfigFromRobotCamRig();
	UWorld* GetEditorWorld() const;
	void RefreshMapPublisherTargets(UWorld* EditorWorld);
	void RefreshImuTarget();

private:
	static const FName SimulatorConfigTabName;

	TArray<TSharedPtr<ECaptureMode>> CaptureModeOptions;
	TSharedPtr<ECaptureMode> SelectedCaptureModeOption;

	TWeakObjectPtr<ARobotCamRig> TargetRobotCamRig;
	TWeakObjectPtr<AActor> TargetRoverActor;
	TWeakObjectPtr<UImuSensorPublisherComponent> TargetImuPublisher;
	TArray<TWeakObjectPtr<UOccupancyMapPublisherComponent>> TargetMapPublishers;

	ECaptureMode CaptureMode = ECaptureMode::MonoRosGroundTruth;
	int32 PublishHz = 6;
	float StereoBaselineCm = 20.0f;
	bool bEnableGroundTruthMaps = true;
	float ImuPublishHz = 100.0f;
	int32 RobotCamRigCount = 0;
	int32 GroundTruthMapPublisherCount = 0;
	FText LastApplyStatus;
};
