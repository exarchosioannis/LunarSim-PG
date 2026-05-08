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
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FsimulatorEditorModule"

const FName FsimulatorEditorModule::SimulatorConfigTabName(TEXT("SimulatorConfigTab"));

void FsimulatorEditorModule::StartupModule()
{
	InitCaptureModeOptions();

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
	InitCaptureModeOptions();
	SelectedCaptureModeOption = FindCaptureModeOption(CaptureMode);

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
						.Text(LOCTEXT("PublishHzLabel", "Publish Hz"))
					]

					+ SGridPanel::Slot(1, 0)
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

					+ SGridPanel::Slot(0, 1)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CaptureModeLabel", "Capture Mode"))
					]

					+ SGridPanel::Slot(1, 1)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(240.f)
						[
							SNew(SComboBox<TSharedPtr<ECaptureMode>>)
							.OptionsSource(&CaptureModeOptions)
							.InitiallySelectedItem(SelectedCaptureModeOption)
							.OnGenerateWidget_Raw(this, &FsimulatorEditorModule::MakeCaptureModeComboWidget)
							.OnSelectionChanged_Raw(this, &FsimulatorEditorModule::OnCaptureModeSelectionChanged)
							[
								SNew(STextBlock)
								.Text_Raw(this, &FsimulatorEditorModule::GetCaptureModeText)
							]
						]
					]

					+ SGridPanel::Slot(0, 2)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EnableRosRoverGtPoseLabel", "/rover/gt/pose"))
					]

					+ SGridPanel::Slot(1, 2)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Raw(this, &FsimulatorEditorModule::GetEnableRosRoverGtPoseCheckState)
						.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnEnableRosRoverGtPoseChanged)
					]

					+ SGridPanel::Slot(0, 3)
					.Padding(4.f, 6.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("StereoBaselineCmLabel", "Stereo Baseline Cm"))
					]

					+ SGridPanel::Slot(1, 3)
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

void FsimulatorEditorModule::InitCaptureModeOptions()
{
	if (CaptureModeOptions.Num() > 0) return;

	CaptureModeOptions.Add(MakeShared<ECaptureMode>(ECaptureMode::MonoRos));
	CaptureModeOptions.Add(MakeShared<ECaptureMode>(ECaptureMode::GroundTruth));
	CaptureModeOptions.Add(MakeShared<ECaptureMode>(ECaptureMode::StereoRos));
	CaptureModeOptions.Add(MakeShared<ECaptureMode>(ECaptureMode::MonoRosGroundTruth));
	CaptureModeOptions.Add(MakeShared<ECaptureMode>(ECaptureMode::StereoRosGroundTruth));
}

TSharedPtr<ECaptureMode> FsimulatorEditorModule::FindCaptureModeOption(ECaptureMode InMode) const
{
	for (const TSharedPtr<ECaptureMode>& Option : CaptureModeOptions)
	{
		if (Option.IsValid() && *Option == InMode)
		{
			return Option;
		}
	}
	return nullptr;
}

FString FsimulatorEditorModule::CaptureModeToString(ECaptureMode InMode) const
{
	switch (InMode)
	{
	case ECaptureMode::MonoRos:
		return TEXT("Mono ROS");
	case ECaptureMode::GroundTruth:
		return TEXT("Ground Truth");
	case ECaptureMode::StereoRos:
		return TEXT("Stereo ROS");
	case ECaptureMode::MonoRosGroundTruth:
		return TEXT("Mono ROS + Ground Truth");
	case ECaptureMode::StereoRosGroundTruth:
		return TEXT("Stereo ROS + Ground Truth");
	default:
		return TEXT("Unknown");
	}
}

FText FsimulatorEditorModule::GetCaptureModeText() const
{
	return FText::FromString(CaptureModeToString(CaptureMode));
}

TSharedRef<SWidget> FsimulatorEditorModule::MakeCaptureModeComboWidget(TSharedPtr<ECaptureMode> InOption) const
{
	const ECaptureMode Mode = InOption.IsValid() ? *InOption : ECaptureMode::MonoRosGroundTruth;
	return SNew(STextBlock).Text(FText::FromString(CaptureModeToString(Mode)));
}

void FsimulatorEditorModule::OnCaptureModeSelectionChanged(TSharedPtr<ECaptureMode> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid()) return;

	SelectedCaptureModeOption = NewSelection;
	CaptureMode = *NewSelection;
}

ECheckBoxState FsimulatorEditorModule::GetEnableRosRoverGtPoseCheckState() const
{
	return bEnableRosRoverGtPose ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FsimulatorEditorModule::OnEnableRosRoverGtPoseChanged(ECheckBoxState NewState)
{
	bEnableRosRoverGtPose = (NewState == ECheckBoxState::Checked);
}

void FsimulatorEditorModule::OnPublishHzChanged(int32 NewValue)
{
	PublishHz = FMath::Clamp(NewValue, 1, 24);
}

void FsimulatorEditorModule::OnStereoBaselineCmChanged(float NewValue)
{
	StereoBaselineCm = FMath::Clamp(NewValue, 1.0f, 200.0f);
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
		NewConfig.PublishHz = FMath::Clamp(PublishHz, 1, 24);
		NewConfig.CaptureMode = CaptureMode;
		NewConfig.bEnableRosRoverGtPose = bEnableRosRoverGtPose;

		RobotCamRig->Modify();
		RobotCamRig->SetCaptureConfig(NewConfig);
		RobotCamRig->SetStereoBaselineCm(StereoBaselineCm);
		RobotCamRig->PostEditChange();
		RobotCamRig->MarkPackageDirty();

		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(RobotCamRig, true, true, true);

		UE_LOG(LogTemp, Display, TEXT("Simulator config applied to %s: PublishHz=%d, CaptureMode=%s, RosRoverGtPose=%s, StereoBaselineCm=%.2f"),
			*RobotCamRig->GetName(),
			NewConfig.PublishHz,
			*CaptureModeToString(NewConfig.CaptureMode),
			NewConfig.bEnableRosRoverGtPose ? TEXT("true") : TEXT("false"),
			StereoBaselineCm);

		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Simulator config was not applied because no RobotCamRig was found in the editor world."));
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

		PublishHz = FMath::Clamp(CurrentConfig.PublishHz, 1, 24);
		CaptureMode = CurrentConfig.CaptureMode;
		bEnableRosRoverGtPose = CurrentConfig.bEnableRosRoverGtPose;
		StereoBaselineCm = FMath::Clamp(RobotCamRig->GetStereoBaselineCm(), 1.0f, 200.0f);
		SelectedCaptureModeOption = FindCaptureModeOption(CaptureMode);

		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Simulator config window opened, but no RobotCamRig was found in the editor world. Showing default settings."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FsimulatorEditorModule, simulatorEditor)