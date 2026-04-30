#include "simulatorEditor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Sensors/RobotCamRig.h"
#include "Capture/CaptureTypes.h"

#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FsimulatorEditorModule"

const FName FsimulatorEditorModule::SimulatorConfigTabName(TEXT("SimulatorConfigTab"));

void FsimulatorEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		SimulatorConfigTabName,
		FOnSpawnTab::CreateRaw(this, &FsimulatorEditorModule::OnSpawnSimulatorConfigTab)
	)
	.SetDisplayName(LOCTEXT("SimulatorConfigTabTitle", "Simulator Config"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FsimulatorEditorModule::RegisterMenus)
		);
	}
	
}

void FsimulatorEditorModule::ShutdownModule()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SimulatorConfigTabName);
}

void FsimulatorEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	if (!Menu) return;

	FToolMenuSection& Section = Menu->AddSection("SimulatorConfigSection", LOCTEXT("SimulatorConfigSection", "Simulator"));

	Section.AddMenuEntry(
		"OpenSimulatorConfig",
		LOCTEXT("OpenSimulatorConfigLabel", "Simulator Config"),
		LOCTEXT("OpenSimulatorConfigTooltip", "Open the Simulator Config window"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FsimulatorEditorModule::OpenSimulatorConfigTab))
	);
}

void FsimulatorEditorModule::OpenSimulatorConfigTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(SimulatorConfigTabName);
}

TSharedRef<SDockTab> FsimulatorEditorModule::OnSpawnSimulatorConfigTab(const FSpawnTabArgs& SpawnTabArgs)
{
	LoadConfigFromRobotCamRig();
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			BuildSimulatorConfigPanel()
		];
}

TSharedRef<SWidget> FsimulatorEditorModule::BuildSimulatorConfigPanel()
{
	return SNew(SBox)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("CaptureSettingsAreaTitle", "Capture Settings"))
				.InitiallyCollapsed(false)
				.Padding(8.0f)
				.BodyContent()
				[
					SNew(SGridPanel)

					+ SGridPanel::Slot(0, 0)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EnableGtLabel", "Enable GT"))
					]

					+ SGridPanel::Slot(1, 0)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Raw(this, &FsimulatorEditorModule::GetEnableGtCheckState)
						.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnEnableGtChanged)
					]

					+ SGridPanel::Slot(0, 1)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EnableRosRgbLabel", "Enable ROS RGB"))
					]

					+ SGridPanel::Slot(1, 1)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Raw(this, &FsimulatorEditorModule::GetEnableRosRgbCheckState)
						.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnEnableRosRgbChanged)
					]

					+ SGridPanel::Slot(0, 2)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PublishHzLabel", "Publish Hz"))
					]

					+ SGridPanel::Slot(1, 2)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(120.f)
						[
							SNew(SNumericEntryBox<int32>)
							.Value_Lambda([this]() -> TOptional<int32>
							{
								return PublishHz;
							})
							.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnPublishHzChanged)
							.MinValue(1)
							.MaxValue(24)
							.MinSliderValue(1)
							.MaxSliderValue(24)
							.AllowSpin(true)
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 14.f, 0.f, 0.f)
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(140.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyButtonLabel", "Apply Settings"))
					.OnClicked_Lambda([this]()
					{
						OnApplyClicked();
						return FReply::Handled();
					})
				]
			]
		];
}

ECheckBoxState FsimulatorEditorModule::GetEnableGtCheckState() const
{
	return bEnableGt ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FsimulatorEditorModule::GetEnableRosRgbCheckState() const
{
	return bEnableRosRgb ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnEnableGtChanged(ECheckBoxState NewState)
{
	bEnableGt = (NewState == ECheckBoxState::Checked);
}

void FsimulatorEditorModule::OnEnableRosRgbChanged(ECheckBoxState NewState)
{
	bEnableRosRgb = (NewState == ECheckBoxState::Checked);
}

void FsimulatorEditorModule::OnPublishHzChanged(int32 NewValue)
{
	PublishHz = NewValue;
}

void FsimulatorEditorModule::OnApplyClicked()
{
	if (!GEditor) return;

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld) return;

	for (TActorIterator<ARobotCamRig> It(EditorWorld); It; ++It)
	{
		ARobotCamRig* RobotCamRig = *It;
		if (!RobotCamRig) continue;

		FCaptureConfig NewConfig;
		NewConfig.bEnableGt = bEnableGt;
		NewConfig.bEnableRosRgb = bEnableRosRgb;
		NewConfig.PublishHz = PublishHz;

		RobotCamRig->Modify();
		RobotCamRig->SetCaptureConfig(NewConfig);
		RobotCamRig->PostEditChange();
		RobotCamRig->MarkPackageDirty();

		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(RobotCamRig, true, true, true);

		break;
	}
}

void FsimulatorEditorModule::LoadConfigFromRobotCamRig()
{
	if (!GEditor) return;

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld) return;

	for (TActorIterator<ARobotCamRig> It(EditorWorld); It; ++It)
	{
		ARobotCamRig* RobotCamRig = *It;
		if (!RobotCamRig) continue;

		const FCaptureConfig CurrentConfig = RobotCamRig->GetCaptureConfig();

		bEnableGt = CurrentConfig.bEnableGt;
		bEnableRosRgb = CurrentConfig.bEnableRosRgb;
		PublishHz = CurrentConfig.PublishHz;

		break;
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FsimulatorEditorModule, simulatorEditor)