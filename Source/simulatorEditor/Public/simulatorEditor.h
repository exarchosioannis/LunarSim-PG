#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SCheckBox.h"

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

	ECheckBoxState GetEnableGtCheckState() const;
	ECheckBoxState GetEnableRosRgbCheckState() const;

	void OnEnableGtChanged(ECheckBoxState NewState);
	void OnEnableRosRgbChanged(ECheckBoxState NewState);
	void OnPublishHzChanged(int32 NewValue);

    //buttons
	void OnApplyClicked();

private:
	static const FName SimulatorConfigTabName;
    void LoadConfigFromRobotCamRig();

	bool bEnableGt = true;
	bool bEnableRosRgb = true;
	int32 PublishHz = 6;
};