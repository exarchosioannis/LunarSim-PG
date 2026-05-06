#pragma once

#include "CoreMinimal.h"
#include "Capture/CaptureTypes.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

class SDockTab;
class FSpawnTabArgs;

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

	ECheckBoxState GetEnableRosRoverGtPoseCheckState() const;
	void OnEnableRosRoverGtPoseChanged(ECheckBoxState NewState);
	void OnPublishHzChanged(int32 NewValue);

	void OnApplyClicked();
	void LoadConfigFromRobotCamRig();

private:
	static const FName SimulatorConfigTabName;

	TArray<TSharedPtr<ECaptureMode>> CaptureModeOptions;
	TSharedPtr<ECaptureMode> SelectedCaptureModeOption;

	ECaptureMode CaptureMode = ECaptureMode::MonoRosGroundTruth;
	bool bEnableRosRoverGtPose = true;
	int32 PublishHz = 6;
};