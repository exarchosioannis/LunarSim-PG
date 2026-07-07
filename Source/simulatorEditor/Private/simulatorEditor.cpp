#include "simulatorEditor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Maps/OccupancyMapPublisherComponent.h"
#include "Sensors/ImuSensorPublisherComponent.h"
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
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FsimulatorEditorModule"

const FName FsimulatorEditorModule::SimulatorConfigTabName(TEXT("SimulatorConfigTab"));

namespace
{
ERoverControlMode NormalizeEditorRoverControlMode(ERoverControlMode InMode)
{
	return InMode == ERoverControlMode::RosCmdVel ? ERoverControlMode::RosCmdVel : ERoverControlMode::Manual;
}
}

void FsimulatorEditorModule::StartupModule()
{
	InitRunModeOptions();
	InitResolutionPresetOptions();
	InitCaptureRatePresetOptions();
	InitRoverControlOptions();

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
	RefreshTargetsFromEditorWorld();
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			BuildSimulatorConfigPanel()
		];
}

TSharedRef<SWidget> FsimulatorEditorModule::BuildSimulatorConfigPanel()
{
	InitRunModeOptions();
	InitResolutionPresetOptions();
	InitCaptureRatePresetOptions();
	InitRoverControlOptions();
	SelectedRunModeOption = FindRunModeOption(RunMode);
	SelectedResolutionPresetOption = FindResolutionPresetOption(ResolutionPreset);
	SelectedCaptureRatePresetOption = FindCaptureRatePresetOption(CaptureRatePreset);
	SelectedRoverControlModeOption = FindRoverControlModeOption(RoverControlMode);

	return SNew(SBox)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SGridPanel)

				+ SGridPanel::Slot(0, 0)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RunModeLabel", "Run Mode"))
				]

				+ SGridPanel::Slot(1, 0)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(240.f)
					[
						SNew(SComboBox<TSharedPtr<ELunarSimRunMode>>)
						.OptionsSource(&RunModeOptions)
						.InitiallySelectedItem(SelectedRunModeOption)
						.OnGenerateWidget_Raw(this, &FsimulatorEditorModule::MakeRunModeComboWidget)
						.OnSelectionChanged_Raw(this, &FsimulatorEditorModule::OnRunModeSelectionChanged)
						[
							SNew(STextBlock)
							.Text_Raw(this, &FsimulatorEditorModule::GetRunModeText)
						]
					]
				]

				+ SGridPanel::Slot(0, 1)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StereoRosImagesLabel", "Stereo ROS Images + CameraInfo"))
				]

				+ SGridPanel::Slot(1, 1)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Raw(this, &FsimulatorEditorModule::GetStereoRosImagesCheckState)
					.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnStereoRosImagesChanged)
				]

				+ SGridPanel::Slot(0, 2)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GroundTruthImagesLabel", "Ground Truth Images"))
				]

				+ SGridPanel::Slot(1, 2)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Raw(this, &FsimulatorEditorModule::GetGroundTruthImagesCheckState)
					.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnGroundTruthImagesChanged)
				]

				+ SGridPanel::Slot(0, 3)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TrajectoryCsvLabel", "Trajectory CSV"))
				]

				+ SGridPanel::Slot(1, 3)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Raw(this, &FsimulatorEditorModule::GetTrajectoryCsvCheckState)
					.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnTrajectoryCsvChanged)
				]

				+ SGridPanel::Slot(0, 4)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ResolutionPresetLabel", "Resolution Preset"))
				]

				+ SGridPanel::Slot(1, 4)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(240.f)
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
					]
				]

				+ SGridPanel::Slot(0, 5)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CustomWidthLabel", "Custom Width"))
				]

				+ SGridPanel::Slot(1, 5)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(120.f)
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditCustomResolution)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32>
						{
							return CustomWidth;
						})
						.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnCustomWidthChanged)
						.MinValue(1)
						.MinSliderValue(1)
						.AllowSpin(true)
					]
				]

				+ SGridPanel::Slot(0, 6)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CustomHeightLabel", "Custom Height"))
				]

				+ SGridPanel::Slot(1, 6)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(120.f)
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditCustomResolution)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32>
						{
							return CustomHeight;
						})
						.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnCustomHeightChanged)
						.MinValue(1)
						.MinSliderValue(1)
						.AllowSpin(true)
					]
				]

				+ SGridPanel::Slot(0, 7)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CaptureRatePresetLabel", "Capture Rate"))
				]

				+ SGridPanel::Slot(1, 7)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(240.f)
					[
						SNew(SComboBox<TSharedPtr<ELunarSimCaptureRatePreset>>)
						.OptionsSource(&CaptureRatePresetOptions)
						.InitiallySelectedItem(SelectedCaptureRatePresetOption)
						.OnGenerateWidget_Raw(this, &FsimulatorEditorModule::MakeCaptureRatePresetComboWidget)
						.OnSelectionChanged_Raw(this, &FsimulatorEditorModule::OnCaptureRatePresetSelectionChanged)
						[
							SNew(STextBlock)
							.Text_Raw(this, &FsimulatorEditorModule::GetCaptureRatePresetText)
						]
					]
				]

				+ SGridPanel::Slot(0, 8)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CustomCaptureHzLabel", "Custom Capture Hz"))
				]

				+ SGridPanel::Slot(1, 8)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(120.f)
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditCustomCaptureHz)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32>
						{
							return CustomCaptureHz;
						})
						.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnCustomCaptureHzChanged)
						.MinValue(1)
						.MaxValue(60)
						.MinSliderValue(1)
						.MaxSliderValue(60)
						.AllowSpin(true)
					]
				]

				+ SGridPanel::Slot(0, 9)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StereoBaselineCmLabel", "Stereo Baseline Cm"))
				]

				+ SGridPanel::Slot(1, 9)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(120.f)
					[
						SNew(SNumericEntryBox<float>)
						.Value_Lambda([this]() -> TOptional<float>
						{
							return StereoBaselineCm;
						})
						.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnStereoBaselineCmChanged)
						.MinValue(1.0f)
						.MaxValue(200.0f)
						.MinSliderValue(1.0f)
						.MaxSliderValue(100.0f)
						.AllowSpin(true)
					]
				]

				+ SGridPanel::Slot(0, 10)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableGroundTruthMapsLabel", "Enable Ground Truth Maps"))
				]

				+ SGridPanel::Slot(1, 10)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthMaps)
					.IsChecked_Raw(this, &FsimulatorEditorModule::GetEnableGroundTruthMapsCheckState)
					.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnEnableGroundTruthMapsChanged)
				]

				+ SGridPanel::Slot(0, 11)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ImuHzLabel", "IMU Hz"))
				]

				+ SGridPanel::Slot(1, 11)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(120.f)
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditImuHz)
					[
						SNew(SNumericEntryBox<float>)
						.Value_Lambda([this]() -> TOptional<float>
						{
							return ImuPublishHz;
						})
						.OnValueChanged_Raw(this, &FsimulatorEditorModule::OnImuHzChanged)
						.MinValue(1.0f)
						.MaxValue(400.0f)
						.MinSliderValue(1.0f)
						.MaxSliderValue(200.0f)
						.AllowSpin(true)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 14.f, 0.f, 8.f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f, 0.f, 4.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RoverControlSectionLabel", "Rover Control"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SGridPanel)

				+ SGridPanel::Slot(0, 0)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RoverControlModeLabel", "Control Mode"))
				]

				+ SGridPanel::Slot(1, 0)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(240.f)
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
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 14.f, 0.f, 0.f)
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyButtonLabel", "Apply Settings"))
				.IsEnabled_Raw(this, &FsimulatorEditorModule::CanApplySettings)
				.OnClicked_Lambda([this]()
				{
					OnApplyClicked();
					return FReply::Handled();
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 10.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text_Raw(this, &FsimulatorEditorModule::GetApplyStatusText)
			]
		];
}

void FsimulatorEditorModule::InitRunModeOptions()
{
	if (RunModeOptions.Num() > 0) return;

	RunModeOptions.Add(MakeShared<ELunarSimRunMode>(ELunarSimRunMode::Dataset));
	RunModeOptions.Add(MakeShared<ELunarSimRunMode>(ELunarSimRunMode::Ros2Live));
}

TSharedPtr<ELunarSimRunMode> FsimulatorEditorModule::FindRunModeOption(ELunarSimRunMode InMode) const
{
	for (const TSharedPtr<ELunarSimRunMode>& Option : RunModeOptions)
	{
		if (Option.IsValid() && *Option == InMode)
		{
			return Option;
		}
	}
	return nullptr;
}

FString FsimulatorEditorModule::RunModeToString(ELunarSimRunMode InMode) const
{
	switch (InMode)
	{
	case ELunarSimRunMode::Dataset:
		return TEXT("Dataset");
	case ELunarSimRunMode::Ros2Live:
		return TEXT("ROS2 Live");
	default:
		return TEXT("Dataset");
	}
}

FText FsimulatorEditorModule::GetRunModeText() const
{
	return FText::FromString(RunModeToString(RunMode));
}

TSharedRef<SWidget> FsimulatorEditorModule::MakeRunModeComboWidget(TSharedPtr<ELunarSimRunMode> InOption) const
{
	const ELunarSimRunMode Mode = InOption.IsValid() ? *InOption : ELunarSimRunMode::Dataset;
	return SNew(STextBlock).Text(FText::FromString(RunModeToString(Mode)));
}

void FsimulatorEditorModule::OnRunModeSelectionChanged(TSharedPtr<ELunarSimRunMode> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid()) return;

	SelectedRunModeOption = NewSelection;
	RunMode = *NewSelection;
}

void FsimulatorEditorModule::InitResolutionPresetOptions()
{
	if (ResolutionPresetOptions.Num() > 0) return;

	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R640x480));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1280x720));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1024x1024));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::Custom));
}

TSharedPtr<ELunarSimResolutionPreset> FsimulatorEditorModule::FindResolutionPresetOption(ELunarSimResolutionPreset InPreset) const
{
	for (const TSharedPtr<ELunarSimResolutionPreset>& Option : ResolutionPresetOptions)
	{
		if (Option.IsValid() && *Option == InPreset)
		{
			return Option;
		}
	}
	return nullptr;
}

FString FsimulatorEditorModule::ResolutionPresetToString(ELunarSimResolutionPreset InPreset) const
{
	switch (InPreset)
	{
	case ELunarSimResolutionPreset::R640x480:
		return TEXT("640x480");
	case ELunarSimResolutionPreset::R1280x720:
		return TEXT("1280x720");
	case ELunarSimResolutionPreset::R1024x1024:
		return TEXT("1024x1024");
	case ELunarSimResolutionPreset::Custom:
		return TEXT("Custom");
	default:
		return TEXT("1024x1024");
	}
}

FText FsimulatorEditorModule::GetResolutionPresetText() const
{
	return FText::FromString(ResolutionPresetToString(ResolutionPreset));
}

TSharedRef<SWidget> FsimulatorEditorModule::MakeResolutionPresetComboWidget(TSharedPtr<ELunarSimResolutionPreset> InOption) const
{
	const ELunarSimResolutionPreset Preset = InOption.IsValid() ? *InOption : ELunarSimResolutionPreset::R1024x1024;
	return SNew(STextBlock).Text(FText::FromString(ResolutionPresetToString(Preset)));
}

void FsimulatorEditorModule::OnResolutionPresetSelectionChanged(TSharedPtr<ELunarSimResolutionPreset> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid()) return;

	SelectedResolutionPresetOption = NewSelection;
	ResolutionPreset = *NewSelection;
}

void FsimulatorEditorModule::InitCaptureRatePresetOptions()
{
	if (CaptureRatePresetOptions.Num() > 0) return;

	CaptureRatePresetOptions.Add(MakeShared<ELunarSimCaptureRatePreset>(ELunarSimCaptureRatePreset::Hz6));
	CaptureRatePresetOptions.Add(MakeShared<ELunarSimCaptureRatePreset>(ELunarSimCaptureRatePreset::Hz10));
	CaptureRatePresetOptions.Add(MakeShared<ELunarSimCaptureRatePreset>(ELunarSimCaptureRatePreset::Custom));
}

TSharedPtr<ELunarSimCaptureRatePreset> FsimulatorEditorModule::FindCaptureRatePresetOption(ELunarSimCaptureRatePreset InPreset) const
{
	for (const TSharedPtr<ELunarSimCaptureRatePreset>& Option : CaptureRatePresetOptions)
	{
		if (Option.IsValid() && *Option == InPreset)
		{
			return Option;
		}
	}
	return nullptr;
}

FString FsimulatorEditorModule::CaptureRatePresetToString(ELunarSimCaptureRatePreset InPreset) const
{
	switch (InPreset)
	{
	case ELunarSimCaptureRatePreset::Hz6:
		return TEXT("6 Hz");
	case ELunarSimCaptureRatePreset::Hz10:
		return TEXT("10 Hz");
	case ELunarSimCaptureRatePreset::Custom:
		return TEXT("Custom");
	default:
		return TEXT("6 Hz");
	}
}

FText FsimulatorEditorModule::GetCaptureRatePresetText() const
{
	return FText::FromString(CaptureRatePresetToString(CaptureRatePreset));
}

TSharedRef<SWidget> FsimulatorEditorModule::MakeCaptureRatePresetComboWidget(TSharedPtr<ELunarSimCaptureRatePreset> InOption) const
{
	const ELunarSimCaptureRatePreset Preset = InOption.IsValid() ? *InOption : ELunarSimCaptureRatePreset::Hz6;
	return SNew(STextBlock).Text(FText::FromString(CaptureRatePresetToString(Preset)));
}

void FsimulatorEditorModule::OnCaptureRatePresetSelectionChanged(TSharedPtr<ELunarSimCaptureRatePreset> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid()) return;

	SelectedCaptureRatePresetOption = NewSelection;
	CaptureRatePreset = *NewSelection;
}

void FsimulatorEditorModule::InitRoverControlOptions()
{
	if (RoverControlModeOptions.Num() > 0) return;

	RoverControlModeOptions.Add(MakeShared<ERoverControlMode>(ERoverControlMode::Manual));
	RoverControlModeOptions.Add(MakeShared<ERoverControlMode>(ERoverControlMode::RosCmdVel));
}

TSharedPtr<ERoverControlMode> FsimulatorEditorModule::FindRoverControlModeOption(ERoverControlMode InMode) const
{
	for (const TSharedPtr<ERoverControlMode>& Option : RoverControlModeOptions)
	{
		if (Option.IsValid() && *Option == InMode)
		{
			return Option;
		}
	}
	return nullptr;
}

FString FsimulatorEditorModule::RoverControlModeToString(ERoverControlMode InMode) const
{
	switch (InMode)
	{
	case ERoverControlMode::Manual:
		return TEXT("Manual");
	case ERoverControlMode::RosCmdVel:
		return TEXT("ROS cmd_vel");
	default:
		return TEXT("Manual");
	}
}

FText FsimulatorEditorModule::GetRoverControlModeText() const
{
	return FText::FromString(RoverControlModeToString(NormalizeEditorRoverControlMode(RoverControlMode)));
}

TSharedRef<SWidget> FsimulatorEditorModule::MakeRoverControlModeComboWidget(TSharedPtr<ERoverControlMode> InOption) const
{
	const ERoverControlMode Mode = InOption.IsValid() ? *InOption : ERoverControlMode::Manual;
	return SNew(STextBlock).Text(FText::FromString(RoverControlModeToString(Mode)));
}

void FsimulatorEditorModule::OnRoverControlModeSelectionChanged(TSharedPtr<ERoverControlMode> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid()) return;

	SelectedRoverControlModeOption = NewSelection;
	RoverControlMode = NormalizeEditorRoverControlMode(*NewSelection);
}

void FsimulatorEditorModule::OnCustomWidthChanged(int32 NewValue)
{
	CustomWidth = FMath::Max(1, NewValue);
}

void FsimulatorEditorModule::OnCustomHeightChanged(int32 NewValue)
{
	CustomHeight = FMath::Max(1, NewValue);
}

void FsimulatorEditorModule::OnCustomCaptureHzChanged(int32 NewValue)
{
	CustomCaptureHz = FMath::Clamp(NewValue, 1, 60);
}

void FsimulatorEditorModule::OnStereoBaselineCmChanged(float NewValue)
{
	StereoBaselineCm = FMath::Clamp(NewValue, 1.0f, 200.0f);
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

UWorld* FsimulatorEditorModule::GetEditorWorld() const
{
	if (!GEditor) return nullptr;
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
	RobotCamRigCount = 0;
	GroundTruthMapPublisherCount = 0;
	LastApplyStatus = FText::GetEmpty();

	UWorld* EditorWorld = GetEditorWorld();
	if (!EditorWorld) {
		LastApplyStatus = LOCTEXT("NoEditorWorldStatus", "No editor world available.");
		return;
	}

	for (TActorIterator<ARobotCamRig> It(EditorWorld); It; ++It)
	{
		ARobotCamRig* RobotCamRig = *It;
		if (!RobotCamRig) continue;

		++RobotCamRigCount;
		if (!TargetRobotCamRig.IsValid()) {
			TargetRobotCamRig = RobotCamRig;
		}
	}

	if (TargetRobotCamRig.IsValid()) {
		LoadConfigFromRobotCamRig();
	}

	RefreshMapPublisherTargets(EditorWorld);
	RefreshImuTarget();
}

void FsimulatorEditorModule::RefreshMapPublisherTargets(UWorld* EditorWorld)
{
	TargetMapPublishers.Empty();
	GroundTruthMapPublisherCount = 0;

	if (!EditorWorld) return;

	for (TActorIterator<AActor> It(EditorWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		TArray<UOccupancyMapPublisherComponent*> MapPublishers;
		Actor->GetComponents<UOccupancyMapPublisherComponent>(MapPublishers);
		for (UOccupancyMapPublisherComponent* MapPublisher : MapPublishers)
		{
			if (!MapPublisher) continue;

			++GroundTruthMapPublisherCount;
			TargetMapPublishers.Add(MapPublisher);
		}
	}

	if (TargetMapPublishers.Num() > 0) {
		if (UOccupancyMapPublisherComponent* MapPublisher = TargetMapPublishers[0].Get()) {
			bEnableGroundTruthMaps = MapPublisher->bEnableGroundTruthMaps;
		}
	}
}

void FsimulatorEditorModule::RefreshImuTarget()
{
	TargetRoverActor.Reset();
	TargetImuPublisher.Reset();
	TargetRoverController.Reset();
	TargetCmdVelController.Reset();

	ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	if (!RobotCamRig) return;

	AActor* RoverActor = RobotCamRig->GetRoverActor();
	if (!RoverActor) return;

	TargetRoverActor = RoverActor;
	TargetRoverController = RoverActor->FindComponentByClass<URoverVehicleControllerComponent>();
	TargetCmdVelController = RoverActor->FindComponentByClass<URoverCmdVelVehicleControllerComponent>();
	LoadConfigFromRoverControl();

	UImuSensorPublisherComponent* ImuPublisher = RoverActor->FindComponentByClass<UImuSensorPublisherComponent>();
	if (ImuPublisher) {
		TargetImuPublisher = ImuPublisher;
		ImuPublishHz = ImuPublisher->GetImuPublishHz();
	}
}

bool FsimulatorEditorModule::CanApplySettings() const
{
	return TargetRobotCamRig.IsValid();
}

bool FsimulatorEditorModule::CanSelectTargetRobotCamRig() const
{
	return TargetRobotCamRig.IsValid();
}

bool FsimulatorEditorModule::CanEditGroundTruthMaps() const
{
	return TargetMapPublishers.Num() > 0;
}

bool FsimulatorEditorModule::CanEditImuHz() const
{
	return TargetImuPublisher.IsValid();
}

bool FsimulatorEditorModule::CanEditRoverControl() const
{
	return TargetRoverController.IsValid();
}

bool FsimulatorEditorModule::CanEditCustomResolution() const
{
	return ResolutionPreset == ELunarSimResolutionPreset::Custom;
}

bool FsimulatorEditorModule::CanEditCustomCaptureHz() const
{
	return CaptureRatePreset == ELunarSimCaptureRatePreset::Custom;
}

FText FsimulatorEditorModule::GetTargetStatusText() const
{
	if (RobotCamRigCount <= 0) {
		return LOCTEXT("NoRobotCamRigFoundStatus", "No RobotCamRig found.");
	}

	if (RobotCamRigCount == 1) {
		return LOCTEXT("OneRobotCamRigFoundStatus", "One RobotCamRig found.");
	}

	return FText::Format(
		LOCTEXT("MultipleRobotCamRigsFoundStatus", "Multiple RobotCamRigs found ({0}); using the first one for now."),
		FText::AsNumber(RobotCamRigCount)
	);
}

FText FsimulatorEditorModule::GetTargetActorText() const
{
	const ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	if (!RobotCamRig) {
		return LOCTEXT("NoTargetActorText", "Target RobotCamRig: none");
	}

	return FText::FromString(FString::Printf(TEXT("Target RobotCamRig: %s"), *RobotCamRig->GetActorLabel()));
}

FText FsimulatorEditorModule::GetMapStatusText() const
{
	if (GroundTruthMapPublisherCount <= 0) {
		return LOCTEXT("NoGroundTruthMapPublisherStatus", "Ground Truth Maps: no map publisher found; map setting will be ignored.");
	}

	if (GroundTruthMapPublisherCount == 1) {
		return LOCTEXT("OneGroundTruthMapPublisherStatus", "Ground Truth Maps: one map publisher found.");
	}

	return FText::Format(
		LOCTEXT("MultipleGroundTruthMapPublishersStatus", "Ground Truth Maps: {0} map publishers found; applying to all."),
		FText::AsNumber(GroundTruthMapPublisherCount)
	);
}

FText FsimulatorEditorModule::GetImuStatusText() const
{
	if (!TargetRobotCamRig.IsValid()) {
		return LOCTEXT("NoImuRobotCamRigTargetStatus", "IMU: no RobotCamRig target.");
	}

	const AActor* RoverActor = TargetRoverActor.Get();
	if (!RoverActor) {
		return LOCTEXT("NoRoverActorStatus", "IMU: target RobotCamRig has no RoverActor assigned; IMU Hz will not be applied.");
	}

	if (!TargetImuPublisher.IsValid()) {
		return FText::FromString(FString::Printf(
			TEXT("IMU: rover actor %s has no ImuSensorPublisherComponent; IMU Hz will not be applied."),
			*RoverActor->GetActorLabel()
		));
	}

	return FText::FromString(FString::Printf(TEXT("IMU: editing %s."), *RoverActor->GetActorLabel()));
}

FText FsimulatorEditorModule::GetApplyStatusText() const
{
	if (!LastApplyStatus.IsEmpty()) {
		return LastApplyStatus;
	}

	if (RobotCamRigCount <= 0) {
		return LOCTEXT("SimpleNoRobotCamRigStatus", "No RobotCamRig found.");
	}

	if (RobotCamRigCount > 1) {
		return LOCTEXT("SimpleMultipleRobotCamRigsStatus", "Multiple RobotCamRigs found; using the first.");
	}

	return LOCTEXT("SimpleReadyStatus", "Ready.");
}

void FsimulatorEditorModule::SelectTargetRobotCamRig()
{
	if (!GEditor || !TargetRobotCamRig.IsValid()) return;

	ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(RobotCamRig, true, true, true);
	GEditor->MoveViewportCamerasToActor(*RobotCamRig, false);

	LastApplyStatus = FText::FromString(FString::Printf(TEXT("Selected target RobotCamRig: %s"), *RobotCamRig->GetActorLabel()));
}

void FsimulatorEditorModule::OnApplyClicked()
{
	ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	if (!RobotCamRig) {
		LastApplyStatus = LOCTEXT("ApplyNoRobotCamRigStatus", "Settings were not applied because no RobotCamRig was found.");
		UE_LOG(LogTemp, Warning, TEXT("Simulator config was not applied because no RobotCamRig was found in the editor world."));
		return;
	}

	FCaptureConfig NewConfig = RobotCamRig->GetCaptureConfig();
	NewConfig.RunMode = RunMode;
	NewConfig.bStereoRosImages = bStereoRosImages;
	NewConfig.bGroundTruthImages = bGroundTruthImages;
	NewConfig.bTrajectoryCsv = bTrajectoryCsv;
	NewConfig.ResolutionPreset = ResolutionPreset;
	NewConfig.CustomWidth = FMath::Max(1, CustomWidth);
	NewConfig.CustomHeight = FMath::Max(1, CustomHeight);
	NewConfig.CaptureRatePreset = CaptureRatePreset;
	NewConfig.CustomCaptureHz = FMath::Clamp(CustomCaptureHz, 1, 60);
	NewConfig.StereoBaselineCm = FMath::Clamp(StereoBaselineCm, 1.0f, 200.0f);

	RobotCamRig->Modify();
	RobotCamRig->SetCaptureConfig(NewConfig);
	RobotCamRig->PostEditChange();
	RobotCamRig->MarkPackageDirty();

	if (GEditor) {
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(RobotCamRig, true, true, true);
	}

	int32 MapsApplied = 0;
	for (TWeakObjectPtr<UOccupancyMapPublisherComponent>& MapPublisherPtr : TargetMapPublishers)
	{
		UOccupancyMapPublisherComponent* MapPublisher = MapPublisherPtr.Get();
		if (!MapPublisher) continue;

		if (AActor* Owner = MapPublisher->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		MapPublisher->Modify();
		MapPublisher->bEnableGroundTruthMaps = bEnableGroundTruthMaps;
		MapPublisher->MarkPackageDirty();
		++MapsApplied;
	}

	bool bImuApplied = false;
	if (UImuSensorPublisherComponent* ImuPublisher = TargetImuPublisher.Get()) {
		if (AActor* Owner = ImuPublisher->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		ImuPublisher->Modify();
		ImuPublisher->SetImuPublishHz(ImuPublishHz);
		ImuPublisher->MarkPackageDirty();
		bImuApplied = true;
	}

	bool bRoverModeApplied = false;
	if (URoverVehicleControllerComponent* RoverController = TargetRoverController.Get()) {
		if (AActor* Owner = RoverController->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		RoverController->Modify();
		RoverController->SetControlMode(RoverControlMode);
		RoverController->MarkPackageDirty();
		bRoverModeApplied = true;
	}

	if (URoverCmdVelVehicleControllerComponent* CmdVelController = TargetCmdVelController.Get()) {
		if (AActor* Owner = CmdVelController->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		CmdVelController->Modify();
		CmdVelController->SetSettings(CmdVelSettings);
		CmdVelController->MarkPackageDirty();
	}

	FString Status = TEXT("Applied settings.");

	UE_LOG(LogTemp, Display,
		TEXT("Simulator config applied to %s: RunMode=%s, StereoRosImages=%s, GroundTruthImages=%s, TrajectoryCsv=%s, Resolution=%s (%dx%d), CaptureRate=%s (%d Hz), StereoBaselineCm=%.2f, GroundTruthMaps=%s, MapPublishers=%d, ImuHz=%s, RoverControlMode=%s"),
		*RobotCamRig->GetActorLabel(),
		*RunModeToString(NewConfig.RunMode),
		NewConfig.bStereoRosImages ? TEXT("true") : TEXT("false"),
		NewConfig.bGroundTruthImages ? TEXT("true") : TEXT("false"),
		NewConfig.bTrajectoryCsv ? TEXT("true") : TEXT("false"),
		*ResolutionPresetToString(NewConfig.ResolutionPreset),
		NewConfig.GetResolvedWidth(),
		NewConfig.GetResolvedHeight(),
		*CaptureRatePresetToString(NewConfig.CaptureRatePreset),
		NewConfig.GetResolvedCaptureHz(),
		NewConfig.StereoBaselineCm,
		bEnableGroundTruthMaps ? TEXT("true") : TEXT("false"),
		MapsApplied,
		bImuApplied ? *FString::Printf(TEXT("%.2f"), ImuPublishHz) : TEXT("not applied"),
		bRoverModeApplied ? *RoverControlModeToString(RoverControlMode) : TEXT("not applied")
	);

	TArray<FString> SkippedSettings;
	if (!bImuApplied && TargetRoverActor.IsValid()) {
		SkippedSettings.Add(TEXT("IMU Hz"));
	}
	if (!bRoverModeApplied && TargetRoverActor.IsValid()) {
		SkippedSettings.Add(TEXT("rover control mode"));
	}
	if (MapsApplied == 0 && GroundTruthMapPublisherCount == 0) {
		SkippedSettings.Add(TEXT("map setting"));
	}

	if (SkippedSettings.Num() > 0) {
		Status = FString::Printf(TEXT("Applied settings. Skipped %s."), *FString::Join(SkippedSettings, TEXT(", ")));
	}
	else if (RobotCamRigCount > 1) {
		Status = TEXT("Applied settings to first RobotCamRig.");
	}

	LastApplyStatus = FText::FromString(Status);

	UE_LOG(LogTemp, Display, TEXT("%s"), *Status);
}

void FsimulatorEditorModule::LoadConfigFromRobotCamRig()
{
	ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	if (!RobotCamRig) {
		UE_LOG(LogTemp, Warning, TEXT("Simulator config window refreshed, but no RobotCamRig was found in the editor world. Showing default settings."));
		return;
	}

	const FCaptureConfig CurrentConfig = RobotCamRig->GetCaptureConfig();

	RunMode = CurrentConfig.RunMode;
	bStereoRosImages = CurrentConfig.bStereoRosImages;
	bGroundTruthImages = CurrentConfig.bGroundTruthImages;
	bTrajectoryCsv = CurrentConfig.bTrajectoryCsv;
	ResolutionPreset = CurrentConfig.ResolutionPreset;
	CustomWidth = FMath::Max(1, CurrentConfig.CustomWidth);
	CustomHeight = FMath::Max(1, CurrentConfig.CustomHeight);
	CaptureRatePreset = CurrentConfig.CaptureRatePreset;
	CustomCaptureHz = FMath::Clamp(CurrentConfig.CustomCaptureHz, 1, 60);
	StereoBaselineCm = FMath::Clamp(CurrentConfig.StereoBaselineCm, 1.0f, 200.0f);
	SelectedRunModeOption = FindRunModeOption(RunMode);
	SelectedResolutionPresetOption = FindResolutionPresetOption(ResolutionPreset);
	SelectedCaptureRatePresetOption = FindCaptureRatePresetOption(CaptureRatePreset);
}

void FsimulatorEditorModule::LoadConfigFromRoverControl()
{
	if (URoverVehicleControllerComponent* RoverController = TargetRoverController.Get()) {
		RoverControlMode = NormalizeEditorRoverControlMode(RoverController->GetControlMode());
	}
	else {
		RoverControlMode = ERoverControlMode::Manual;
	}

	if (URoverCmdVelVehicleControllerComponent* CmdVelController = TargetCmdVelController.Get()) {
		CmdVelSettings = CmdVelController->GetSettings();
	}
	else {
		CmdVelSettings = FRoverCmdVelControllerSettings();
	}

	SelectedRoverControlModeOption = FindRoverControlModeOption(RoverControlMode);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FsimulatorEditorModule, simulatorEditor)
