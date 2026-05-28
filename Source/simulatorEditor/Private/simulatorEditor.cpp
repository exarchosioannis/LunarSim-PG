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
	RefreshTargetsFromEditorWorld();
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
				SNew(SGridPanel)

				+ SGridPanel::Slot(0, 0)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CaptureModeLabel", "Capture Mode"))
				]

				+ SGridPanel::Slot(1, 0)
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

				+ SGridPanel::Slot(0, 1)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PublishHzLabel", "Publish Hz"))
				]

				+ SGridPanel::Slot(1, 1)
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

				+ SGridPanel::Slot(0, 2)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StereoBaselineCmLabel", "Stereo Baseline Cm"))
				]

				+ SGridPanel::Slot(1, 2)
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

				+ SGridPanel::Slot(0, 3)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableGroundTruthMapsLabel", "Enable Ground Truth Maps"))
				]

				+ SGridPanel::Slot(1, 3)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsEnabled_Raw(this, &FsimulatorEditorModule::CanEditGroundTruthMaps)
					.IsChecked_Raw(this, &FsimulatorEditorModule::GetEnableGroundTruthMapsCheckState)
					.OnCheckStateChanged_Raw(this, &FsimulatorEditorModule::OnEnableGroundTruthMapsChanged)
				]

				+ SGridPanel::Slot(0, 4)
				.Padding(4.f, 6.f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ImuHzLabel", "IMU Hz"))
				]

				+ SGridPanel::Slot(1, 4)
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

void FsimulatorEditorModule::OnPublishHzChanged(int32 NewValue)
{
	PublishHz = FMath::Clamp(NewValue, 1, 24);
}

void FsimulatorEditorModule::OnStereoBaselineCmChanged(float NewValue)
{
	StereoBaselineCm = FMath::Clamp(NewValue, 1.0f, 200.0f);
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

	ARobotCamRig* RobotCamRig = TargetRobotCamRig.Get();
	if (!RobotCamRig) return;

	AActor* RoverActor = RobotCamRig->GetRoverActor();
	if (!RoverActor) return;

	TargetRoverActor = RoverActor;

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
	NewConfig.PublishHz = FMath::Clamp(PublishHz, 1, 24);
	NewConfig.CaptureMode = CaptureMode;

	RobotCamRig->Modify();
	RobotCamRig->SetCaptureConfig(NewConfig);
	RobotCamRig->SetStereoBaselineCm(StereoBaselineCm);
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

	FString Status = TEXT("Applied settings.");

	UE_LOG(LogTemp, Display,
		TEXT("Simulator config applied to %s: CaptureMode=%s, PublishHz=%d, StereoBaselineCm=%.2f, GroundTruthMaps=%s, MapPublishers=%d, ImuHz=%s"),
		*RobotCamRig->GetActorLabel(),
		*CaptureModeToString(NewConfig.CaptureMode),
		NewConfig.PublishHz,
		StereoBaselineCm,
		bEnableGroundTruthMaps ? TEXT("true") : TEXT("false"),
		MapsApplied,
		bImuApplied ? *FString::Printf(TEXT("%.2f"), ImuPublishHz) : TEXT("not applied")
	);

	if (!bImuApplied && TargetRoverActor.IsValid()) {
		Status = TEXT("Applied settings. IMU Hz was skipped.");
	} else if (MapsApplied == 0 && GroundTruthMapPublisherCount == 0) {
		Status = TEXT("Applied settings. Map setting was skipped.");
	} else if (RobotCamRigCount > 1) {
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

	PublishHz = FMath::Clamp(CurrentConfig.PublishHz, 1, 24);
	CaptureMode = CurrentConfig.CaptureMode;
	StereoBaselineCm = FMath::Clamp(RobotCamRig->GetStereoBaselineCm(), 1.0f, 200.0f);
	SelectedCaptureModeOption = FindCaptureModeOption(CaptureMode);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FsimulatorEditorModule, simulatorEditor)
