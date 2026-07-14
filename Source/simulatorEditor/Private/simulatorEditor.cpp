#include "simulatorEditor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Components/ChildActorComponent.h"
#include "GameFramework/Actor.h"
#include "Maps/OccupancyMapPublisherComponent.h"
#include "Robots/RoverGroundTruthPublisherComponent.h"
#include "Sensors/ImuSensorPublisherComponent.h"
#include "Sensors/RobotCamRig.h"
#include "Capture/CaptureTypes.h"

#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FsimulatorEditorModule"

const FName FsimulatorEditorModule::SimulatorConfigTabName(TEXT("SimulatorConfigTab"));

namespace
{
ERoverControlMode NormalizeEditorRoverControlMode(ERoverControlMode InMode)
{
	return InMode == ERoverControlMode::RosCmdVel ? ERoverControlMode::RosCmdVel : ERoverControlMode::Manual;
}

ELunarSimResolutionPreset NormalizeEditorResolutionPreset(ELunarSimResolutionPreset InPreset, int32 Width, int32 Height)
{
	switch (InPreset)
	{
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

	if (Width == 640 && Height == 360) return ELunarSimResolutionPreset::R640x360;
	if (Width == 1024 && Height == 576) return ELunarSimResolutionPreset::R1024x576;
	if (Width == 1280 && Height == 720) return ELunarSimResolutionPreset::R1280x720;
	if (Width == 1920 && Height == 1080) return ELunarSimResolutionPreset::R1920x1080;
	if (Width == 640 && Height == 640) return ELunarSimResolutionPreset::R640x640;
	if (Width == 1024 && Height == 1024) return ELunarSimResolutionPreset::R1024x1024;

	return ELunarSimResolutionPreset::R1024x1024;
}

float NormalizeEditorCaptureHz(float InCaptureHz)
{
	return FMath::Clamp(
		FCaptureConfig::SanitizeCameraCaptureHz(InCaptureHz),
		FCaptureConfig::GetMinCameraCaptureHz(),
		FCaptureConfig::GetMaxCameraCaptureHz());
}

bool IsEditorPlaySessionRunning()
{
	return GEditor && GEditor->IsPlaySessionInProgress();
}

bool HasInvalidEditorFlags(const UObject* Object)
{
	return !Object
		|| Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed | RF_FinishDestroyed);
}

bool IsUsableEditorActor(AActor* Actor, const UWorld* ExpectedWorld)
{
	return IsValid(Actor)
		&& Actor->GetWorld() == ExpectedWorld
		&& !Actor->IsActorBeingDestroyed()
		&& !HasInvalidEditorFlags(Actor);
}

bool IsUsableRobotCamRig(ARobotCamRig* RobotCamRig, const UWorld* ExpectedWorld)
{
	return IsUsableEditorActor(RobotCamRig, ExpectedWorld)
		&& IsValid(RobotCamRig->GetRootComponent())
		&& !HasInvalidEditorFlags(RobotCamRig->GetRootComponent());
}

bool IsUsableComponent(const UActorComponent* Component)
{
	return IsValid(Component) && !HasInvalidEditorFlags(Component);
}

template <typename ComponentType>
ComponentType* FindUsableComponentByClass(AActor* Actor)
{
	ComponentType* Component = Actor ? Actor->FindComponentByClass<ComponentType>() : nullptr;
	return IsUsableComponent(Component) ? Component : nullptr;
}

ARobotCamRig* FindUsableRobotCamRigChildActor(AActor* RoverActor, const UWorld* EditorWorld, UChildActorComponent*& OutChildActorComponent)
{
	OutChildActorComponent = nullptr;
	if (!IsUsableEditorActor(RoverActor, EditorWorld)) {
		return nullptr;
	}

	TArray<UChildActorComponent*> ChildActorComponents;
	RoverActor->GetComponents<UChildActorComponent>(ChildActorComponents);
	for (UChildActorComponent* ChildActorComponent : ChildActorComponents)
	{
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

bool ResolveCompleteRoverPipeline(
	AActor* RoverActor,
	UWorld* EditorWorld,
	ARobotCamRig*& OutRobotCamRig,
	UChildActorComponent*& OutRobotCamRigChildComponent,
	URoverGroundTruthPublisherComponent*& OutGroundTruthPublisher,
	UImuSensorPublisherComponent*& OutImuPublisher,
	URoverVehicleControllerComponent*& OutRoverController,
	URoverCmdVelVehicleControllerComponent*& OutCmdVelController)
{
	OutRobotCamRig = nullptr;
	OutRobotCamRigChildComponent = nullptr;
	OutGroundTruthPublisher = nullptr;
	OutImuPublisher = nullptr;
	OutRoverController = nullptr;
	OutCmdVelController = nullptr;

	if (!IsUsableEditorActor(RoverActor, EditorWorld)) {
		return false;
	}

	OutGroundTruthPublisher = FindUsableComponentByClass<URoverGroundTruthPublisherComponent>(RoverActor);
	OutImuPublisher = FindUsableComponentByClass<UImuSensorPublisherComponent>(RoverActor);
	OutRoverController = FindUsableComponentByClass<URoverVehicleControllerComponent>(RoverActor);
	OutCmdVelController = FindUsableComponentByClass<URoverCmdVelVehicleControllerComponent>(RoverActor);
	OutRobotCamRig = FindUsableRobotCamRigChildActor(RoverActor, EditorWorld, OutRobotCamRigChildComponent);

	return OutGroundTruthPublisher
		&& OutImuPublisher
		&& OutRoverController
		&& OutCmdVelController
		&& OutRobotCamRig
		&& OutRobotCamRigChildComponent;
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
	InitRoverControlOptions();
	SelectedRunModeOption = FindRunModeOption(RunMode);
	SelectedResolutionPresetOption = FindResolutionPresetOption(ResolutionPreset);
	SelectedRoverControlModeOption = FindRoverControlModeOption(RoverControlMode);

	return SNew(SBox)
		.Padding(12.0f)
		.MinDesiredWidth(760.f)
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
			.Padding(0.f, 0.f, 0.f, 10.f)
			[
				MakeSimulatorConfigSection(
					LOCTEXT("ModeSectionLabel", "Mode"),
					MakeSimulatorConfigFormRow(
						LOCTEXT("RunModeLabel", "Run Mode"),
						SNew(SBox)
						.WidthOverride(220.f)
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
						]))
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
								[
									SNew(STextBlock)
									.Text(LOCTEXT("GroundTruthBoundingBoxesLabel", "Bounding Boxes"))
								],
								18.f)
						])
				]
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
									.Value_Lambda([this]() -> TOptional<float>
									{
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
									.Value_Lambda([this]() -> TOptional<float>
									{
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
									.Value_Lambda([this]() -> TOptional<float>
									{
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
								])
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
						.Padding(0.f, 8.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ImuHzNote", "Note: IMU rate capped by FPS"))
							.AutoWrapText(true)
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
		return TEXT("ROS2Live");
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

	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R640x360));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1024x576));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1280x720));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1920x1080));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R640x640));
	ResolutionPresetOptions.Add(MakeShared<ELunarSimResolutionPreset>(ELunarSimResolutionPreset::R1024x1024));
}

TSharedPtr<ELunarSimResolutionPreset> FsimulatorEditorModule::FindResolutionPresetOption(ELunarSimResolutionPreset InPreset) const
{
	TSharedPtr<ELunarSimResolutionPreset> DefaultOption;
	for (const TSharedPtr<ELunarSimResolutionPreset>& Option : ResolutionPresetOptions)
	{
		if (Option.IsValid() && *Option == ELunarSimResolutionPreset::R1024x1024)
		{
			DefaultOption = Option;
		}
		if (Option.IsValid() && *Option == InPreset)
		{
			return Option;
		}
	}
	return DefaultOption.IsValid() ? DefaultOption : (ResolutionPresetOptions.Num() > 0 ? ResolutionPresetOptions[0] : nullptr);
}

FString FsimulatorEditorModule::ResolutionPresetToString(ELunarSimResolutionPreset InPreset) const
{
	switch (InPreset)
	{
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
		return TEXT("WASD");
	case ERoverControlMode::RosCmdVel:
		return TEXT("cmd_vel");
	default:
		return TEXT("WASD");
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

UWorld* FsimulatorEditorModule::GetEditorWorld() const
{
	if (!GEditor) return nullptr;
	return GEditor->GetEditorWorldContext().World();
}

void FsimulatorEditorModule::RefreshTargetsFromEditorWorld()
{
	TargetRobotCamRig.Reset();
	TargetRobotCamRigChildComponent.Reset();
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

	for (TActorIterator<AActor> It(EditorWorld); It; ++It)
	{
		AActor* CandidateRoverActor = *It;
		if (!IsUsableEditorActor(CandidateRoverActor, EditorWorld)) {
			continue;
		}

		URoverGroundTruthPublisherComponent* GroundTruthPublisher = nullptr;
		UImuSensorPublisherComponent* ImuPublisher = nullptr;
		URoverVehicleControllerComponent* RoverController = nullptr;
		URoverCmdVelVehicleControllerComponent* CmdVelController = nullptr;
		UChildActorComponent* RobotCamRigChildComponent = nullptr;
		ARobotCamRig* RobotCamRig = nullptr;
		if (!ResolveCompleteRoverPipeline(
			CandidateRoverActor,
			EditorWorld,
			RobotCamRig,
			RobotCamRigChildComponent,
			GroundTruthPublisher,
			ImuPublisher,
			RoverController,
			CmdVelController)) {
			continue;
		}

		++RobotCamRigCount;
		if (!TargetRoverActor.IsValid()) {
			TargetRoverActor = CandidateRoverActor;
			TargetRobotCamRig = RobotCamRig;
			TargetRobotCamRigChildComponent = RobotCamRigChildComponent;
			TargetImuPublisher = ImuPublisher;
			TargetRoverController = RoverController;
			TargetCmdVelController = CmdVelController;
		}
	}

	if (TargetRobotCamRig.IsValid()) {
		LoadConfigFromRobotCamRig();
		LoadConfigFromRoverControl();
		if (UImuSensorPublisherComponent* ImuPublisher = TargetImuPublisher.Get()) {
			ImuPublishHz = ImuPublisher->GetImuPublishHz();
		}
	}
	else {
		LastApplyStatus = LOCTEXT("NoCompleteRoverPipelineStatus", "No complete ESA_Rover pipeline found in the level. Place an ESA_Rover in the level.");
	}

	RefreshMapPublisherTargets(EditorWorld);
}

void FsimulatorEditorModule::RefreshMapPublisherTargets(UWorld* EditorWorld)
{
	TargetMapPublishers.Empty();
	GroundTruthMapPublisherCount = 0;

	if (!EditorWorld) return;

	for (TActorIterator<AActor> It(EditorWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsUsableEditorActor(Actor, EditorWorld)) continue;

		TArray<UOccupancyMapPublisherComponent*> MapPublishers;
		Actor->GetComponents<UOccupancyMapPublisherComponent>(MapPublishers);
		for (UOccupancyMapPublisherComponent* MapPublisher : MapPublishers)
		{
			if (!IsUsableComponent(MapPublisher)) continue;

			++GroundTruthMapPublisherCount;
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
	ARobotCamRig* RobotCamRig = nullptr;
	UChildActorComponent* RobotCamRigChildComponent = nullptr;
	URoverGroundTruthPublisherComponent* GroundTruthPublisher = nullptr;
	UImuSensorPublisherComponent* ImuPublisher = nullptr;
	URoverVehicleControllerComponent* RoverController = nullptr;
	URoverCmdVelVehicleControllerComponent* CmdVelController = nullptr;

	return ResolveCompleteRoverPipeline(
		RoverActor,
		EditorWorld,
		RobotCamRig,
		RobotCamRigChildComponent,
		GroundTruthPublisher,
		ImuPublisher,
		RoverController,
		CmdVelController);
}

bool FsimulatorEditorModule::CanSelectTargetRobotCamRig() const
{
	return TargetRoverActor.IsValid();
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

bool FsimulatorEditorModule::CanEditCustomCaptureHz() const
{
	return CaptureRatePreset == ELunarSimCaptureRatePreset::Custom;
}

bool FsimulatorEditorModule::CanEditGroundTruthOutput() const
{
	return bGroundTruthImages;
}

FText FsimulatorEditorModule::GetTargetStatusText() const
{
	if (RobotCamRigCount <= 0) {
		return LOCTEXT("NoRoverPipelineFoundStatus", "No complete ESA_Rover pipeline found in the level.");
	}

	if (RobotCamRigCount == 1) {
		return LOCTEXT("OneRoverPipelineFoundStatus", "One complete ESA_Rover pipeline found.");
	}

	return FText::Format(
		LOCTEXT("MultipleRoverPipelinesFoundStatus", "Multiple ESA_Rover pipelines found ({0}); using the first one."),
		FText::AsNumber(RobotCamRigCount)
	);
}

FText FsimulatorEditorModule::GetTargetActorText() const
{
	const AActor* RoverActor = TargetRoverActor.Get();
	if (!RoverActor) {
		return LOCTEXT("NoTargetRoverActorText", "Target ESA_Rover pipeline: none");
	}

	return FText::FromString(FString::Printf(TEXT("Target ESA_Rover pipeline: %s"), *RoverActor->GetActorLabel()));
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
	AActor* RoverActor = TargetRoverActor.Get();
	if (!RoverActor) {
		return LOCTEXT("NoImuRoverPipelineStatus", "IMU: no complete ESA_Rover pipeline target.");
	}

	if (!FindUsableComponentByClass<UImuSensorPublisherComponent>(RoverActor)) {
		return FText::FromString(FString::Printf(
			TEXT("IMU: rover actor %s has no ImuSensorPublisherComponent; IMU Hz will not be applied."),
			*RoverActor->GetActorLabel()
		));
	}

	return FText::FromString(FString::Printf(TEXT("IMU: editing %s."), *RoverActor->GetActorLabel()));
}

FText FsimulatorEditorModule::GetApplyStatusText() const
{
	if (IsEditorPlaySessionRunning()) {
		return LOCTEXT("ApplyDisabledDuringPlayStatus", "Settings are locked while PIE/simulation is running.");
	}

	if (!LastApplyStatus.IsEmpty()) {
		return LastApplyStatus;
	}

	if (RobotCamRigCount <= 0) {
		return LOCTEXT("SimpleNoRoverPipelineStatus", "No complete ESA_Rover pipeline found in the level.");
	}

	if (RobotCamRigCount > 1) {
		return LOCTEXT("SimpleMultipleRoverPipelinesStatus", "Multiple ESA_Rover pipelines found; using the first.");
	}

	return LOCTEXT("SimpleReadyStatus", "Ready.");
}

void FsimulatorEditorModule::SelectTargetRobotCamRig()
{
	if (!GEditor || !TargetRoverActor.IsValid()) return;

	AActor* RoverActor = TargetRoverActor.Get();
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(RoverActor, true, true, true);
	GEditor->MoveViewportCamerasToActor(*RoverActor, false);

	LastApplyStatus = FText::FromString(FString::Printf(TEXT("Selected target ESA_Rover pipeline: %s"), *RoverActor->GetActorLabel()));
}

void FsimulatorEditorModule::OnApplyClicked()
{
	if (IsEditorPlaySessionRunning()) {
		LastApplyStatus = LOCTEXT("ApplyDuringPlayStatus", "Settings were not applied because PIE/simulation is running.");
		UE_LOG(LogTemp, Warning, TEXT("Simulator config was not applied because capture configuration is locked while PIE/simulation is running."));
		return;
	}

	UWorld* EditorWorld = GetEditorWorld();
	AActor* RoverActor = TargetRoverActor.Get();
	ARobotCamRig* RobotCamRig = nullptr;
	UChildActorComponent* ChildActorComponent = nullptr;
	URoverGroundTruthPublisherComponent* GroundTruthPublisher = nullptr;
	UImuSensorPublisherComponent* ImuPublisher = nullptr;
	URoverVehicleControllerComponent* RoverController = nullptr;
	URoverCmdVelVehicleControllerComponent* CmdVelController = nullptr;

	if (!ResolveCompleteRoverPipeline(
		RoverActor,
		EditorWorld,
		RobotCamRig,
		ChildActorComponent,
		GroundTruthPublisher,
		ImuPublisher,
		RoverController,
		CmdVelController)) {
		LastApplyStatus = LOCTEXT("ApplyNoRoverPipelineStatus", "Settings were not applied because no complete ESA_Rover pipeline was found.");
		UE_LOG(LogTemp, Warning, TEXT("Simulator config was not applied because no complete ESA_Rover pipeline was found in the editor world."));
		return;
	}

	TargetRoverActor = RoverActor;
	TargetRobotCamRig = RobotCamRig;
	TargetRobotCamRigChildComponent = ChildActorComponent;
	TargetImuPublisher = ImuPublisher;
	TargetRoverController = RoverController;
	TargetCmdVelController = CmdVelController;

	FCaptureConfig NewConfig = RobotCamRig->GetCaptureConfig();
	NewConfig.RunMode = RunMode;
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
	NewConfig.CaptureRatePreset = ELunarSimCaptureRatePreset::Custom;
	NewConfig.CustomCaptureHz = NormalizeEditorCaptureHz(CustomCaptureHz);
	NewConfig.StereoBaselineCm = FCaptureConfig::SanitizeStereoBaselineCm(StereoBaselineCm);

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

	int32 MapsApplied = 0;
	for (TWeakObjectPtr<UOccupancyMapPublisherComponent>& MapPublisherPtr : TargetMapPublishers)
	{
		UOccupancyMapPublisherComponent* MapPublisher = MapPublisherPtr.Get();
		if (!IsUsableComponent(MapPublisher)) continue;

		// Map publishers consume NewConfig from the frozen dataset-run config at
		// runtime. Count targets for status reporting; do not maintain a second flag.
		++MapsApplied;
	}

	bool bImuApplied = false;
	ImuPublisher = FindUsableComponentByClass<UImuSensorPublisherComponent>(RoverActor);
	if (ImuPublisher) {
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
	RoverController = FindUsableComponentByClass<URoverVehicleControllerComponent>(RoverActor);
	if (RoverController) {
		if (AActor* Owner = RoverController->GetOwner()) {
			Owner->Modify();
			Owner->MarkPackageDirty();
		}

		RoverController->Modify();
		RoverController->SetControlMode(RoverControlMode);
		RoverController->MarkPackageDirty();
		bRoverModeApplied = true;
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

	FString Status = TEXT("Applied settings.");

	UE_LOG(LogTemp, Display,
		TEXT("Simulator config applied to ESA_Rover pipeline %s / %s: RunMode=%s, StereoRosImages=%s, GroundTruthImages=%s, GroundTruthRGB=%s, GroundTruthDepth=%s, GroundTruthSegmentation=%s, GroundTruthBoundingBoxes=%s, TrajectoryCsv=%s, Resolution=%s (%dx%d), HorizontalFovDeg=%.2f, CaptureHz=%.3f, StereoBaselineCm=%.2f, GroundTruthMaps=%s, MapPublishers=%d, ImuHz=%s, RoverControlMode=%s"),
		*RoverActor->GetActorLabel(),
		*RobotCamRig->GetActorLabel(),
		*RunModeToString(NewConfig.RunMode),
		NewConfig.bStereoRosImages ? TEXT("true") : TEXT("false"),
		NewConfig.bGroundTruthImages ? TEXT("true") : TEXT("false"),
		NewConfig.bGroundTruthRgb ? TEXT("true") : TEXT("false"),
		NewConfig.bGroundTruthDepth ? TEXT("true") : TEXT("false"),
		NewConfig.bGroundTruthSegmentation ? TEXT("true") : TEXT("false"),
		NewConfig.bGroundTruthBoundingBoxes ? TEXT("true") : TEXT("false"),
		NewConfig.bTrajectoryCsv ? TEXT("true") : TEXT("false"),
		*ResolutionPresetToString(NewConfig.ResolutionPreset),
		NewConfig.GetResolvedWidth(),
		NewConfig.GetResolvedHeight(),
		NewConfig.GetResolvedHorizontalFovDeg(),
		NewConfig.GetResolvedCaptureHz(),
		NewConfig.StereoBaselineCm,
		NewConfig.IsGroundTruthMapsEnabled() ? TEXT("true") : TEXT("false"),
		MapsApplied,
		bImuApplied ? *FString::Printf(TEXT("%.2f"), ImuPublishHz) : TEXT("not applied"),
		bRoverModeApplied ? *RoverControlModeToString(RoverControlMode) : TEXT("not applied")
	);

	TArray<FString> SkippedSettings;
	if (!bImuApplied) {
		SkippedSettings.Add(TEXT("IMU Hz"));
	}
	if (!bRoverModeApplied) {
		SkippedSettings.Add(TEXT("rover control mode"));
	}
	if (MapsApplied == 0 && GroundTruthMapPublisherCount == 0 && bEnableGroundTruthMaps) {
		SkippedSettings.Add(TEXT("map setting"));
	}

	if (SkippedSettings.Num() > 0) {
		Status = FString::Printf(TEXT("Applied settings. Skipped %s."), *FString::Join(SkippedSettings, TEXT(", ")));
	}
	else if (NewConfig.bGroundTruthImages && !NewConfig.HasAnyGroundTruthOutputType()) {
		Status = TEXT("Applied settings. Ground Truth Images has no selected outputs.");
	}
	else if (RobotCamRigCount > 1) {
		Status = TEXT("Applied settings to first ESA_Rover pipeline.");
	}

	UE_LOG(LogTemp, Display, TEXT("%s"), *Status);

	const FText StatusText = FText::FromString(Status);
	RefreshTargetsFromEditorWorld();
	LastApplyStatus = StatusText;
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
	bGroundTruthRgb = CurrentConfig.bGroundTruthRgb;
	bGroundTruthDepth = CurrentConfig.bGroundTruthDepth;
	bGroundTruthSegmentation = CurrentConfig.bGroundTruthSegmentation;
	bGroundTruthBoundingBoxes = CurrentConfig.bGroundTruthBoundingBoxes;
	bTrajectoryCsv = CurrentConfig.bTrajectoryCsv;
	bEnableGroundTruthMaps = CurrentConfig.bGroundTruthMaps;
	const int32 ResolvedWidth = CurrentConfig.GetResolvedWidth();
	const int32 ResolvedHeight = CurrentConfig.GetResolvedHeight();
	ResolutionPreset = NormalizeEditorResolutionPreset(CurrentConfig.ResolutionPreset, ResolvedWidth, ResolvedHeight);
	CameraHorizontalFovDeg = CurrentConfig.GetResolvedHorizontalFovDeg();
	CaptureRatePreset = ELunarSimCaptureRatePreset::Custom;
	CustomCaptureHz = NormalizeEditorCaptureHz(CurrentConfig.GetResolvedCaptureHz());
	StereoBaselineCm = FCaptureConfig::SanitizeStereoBaselineCm(CurrentConfig.StereoBaselineCm);
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
