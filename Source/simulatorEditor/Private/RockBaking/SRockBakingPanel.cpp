#include "RockBaking/SRockBakingPanel.h"

#include "RockBaking/SimulatorRockBaker.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "IDesktopPlatform.h"
#include "ISinglePropertyView.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SRockBakingPanel"

void SRockBakingPanel::Construct(const FArguments& InArgs)
{
	(void)InArgs;

	RockSettings.Reset(NewObject<USimulatorRockSettings>());
	RockSettings->MeshFolderPath.Path = TEXT("/Game/Meshes/Rocks");
	StatusMessage = TEXT("Select a rockfield JSON and rock mesh folder, then bake.");

	FPropertyEditorModule& PropertyEditorModule =
	    FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedPtr<ISinglePropertyView> MeshFolderPropertyView = PropertyEditorModule.CreateSingleProperty(
	    RockSettings.Get(), GET_MEMBER_NAME_CHECKED(USimulatorRockSettings, MeshFolderPath), FSinglePropertyParams());

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RockBakingTitle", "Rock Field Baking"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"RockBakingDescription",
				"Load an offline rockfield JSON, choose a Content folder of rock meshes, "
				"then bake deterministic, terrain-aligned HISM instances into the level."))
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(10.f, 8.f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RockInputsHeader", "Inputs"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RockFieldJsonLabel", "Rock Field JSON"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 10.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SEditableTextBox)
						.HintText(LOCTEXT("RockJsonPathHint", "Select an offline rockfield .json file"))
						.Text_Lambda([this]() {
							return RockSettings.IsValid()
								? FText::FromString(RockSettings->RockFieldJsonFile.FilePath)
								: FText::GetEmpty();
						})
						.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type) {
							if (RockSettings.IsValid()) {
								RockSettings->RockFieldJsonFile.FilePath = NewText.ToString();
							}
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("BrowseRockJsonButton", "Browse..."))
						.OnClicked_Raw(this, &SRockBakingPanel::OnBrowseRockJsonClicked)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RockMeshFolderLabel", "Rock Mesh Folder"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MeshFolderPropertyView.IsValid()
						? MeshFolderPropertyView.ToSharedRef()
						: SNullWidget::NullWidget
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BakeRocksButton", "Bake Rocks"))
				.IsEnabled_Raw(this, &SRockBakingPanel::CanExecuteRockActions)
				.OnClicked_Raw(this, &SRockBakingPanel::OnBakeRocksClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearRocksButton", "Clear Baked Rocks"))
				.IsEnabled_Raw(this, &SRockBakingPanel::CanExecuteRockActions)
				.OnClicked_Raw(this, &SRockBakingPanel::OnClearRocksClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(10.f, 8.f))
			[
				SNew(STextBlock)
				.Text_Raw(this, &SRockBakingPanel::GetStatusText)
				.ColorAndOpacity_Raw(this, &SRockBakingPanel::GetStatusColor)
				.AutoWrapText(true)
			]
		]
	];
}

FReply SRockBakingPanel::OnBrowseRockJsonClicked()
{
	if (!RockSettings.IsValid()) {
		SetStatus(TEXT("JSON selection failed: rock settings are unavailable."), EStatusSeverity::Error);
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) {
		SetStatus(TEXT("JSON selection failed: the desktop platform is unavailable."), EStatusSeverity::Error);
		return FReply::Handled();
	}

	FString StartDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
	    FPaths::ProjectDir(), TEXT("Tools"), TEXT("Terrain_Generation"), TEXT("unreal_import"), TEXT("rockfields")));
	FPaths::NormalizeDirectoryName(StartDirectory);

	TArray<FString> SelectedFiles;
	const bool bSelected =
	    DesktopPlatform->OpenFileDialog(nullptr, TEXT("Select LunarSim-PG Rockfield JSON"), StartDirectory, TEXT(""),
	                                    TEXT("JSON files (*.json)|*.json"), EFileDialogFlags::None, SelectedFiles);
	if (bSelected && SelectedFiles.Num() > 0) {
		const FString SelectedPath = FPaths::ConvertRelativePathToFull(SelectedFiles[0]);
		RockSettings->RockFieldJsonFile.FilePath = SelectedPath;
		SetStatus(FString::Printf(TEXT("Selected rockfield: %s"), *SelectedPath), EStatusSeverity::Info);
	}

	return FReply::Handled();
}

FReply SRockBakingPanel::OnBakeRocksClicked()
{
	if (!CanExecuteRockActions()) {
		SetStatus(TEXT("Bake was blocked because PIE/simulation is running or no editor world is available."),
		          EStatusSeverity::Warning);
		return FReply::Handled();
	}

	const FSimulatorRockBakeResult Result = FSimulatorRockBaker::BakeRocksToLevel(GetEditorWorld(), *RockSettings);
	SetStatus(Result.Message, Result.bSucceeded
	                              ? (Result.bHadWarnings ? EStatusSeverity::Warning : EStatusSeverity::Success)
	                              : EStatusSeverity::Error);
	return FReply::Handled();
}

FReply SRockBakingPanel::OnClearRocksClicked()
{
	if (!CanExecuteRockActions()) {
		SetStatus(TEXT("Clear was blocked because PIE/simulation is running or no editor world is available."),
		          EStatusSeverity::Warning);
		return FReply::Handled();
	}

	const FSimulatorRockBakeResult Result = FSimulatorRockBaker::ClearBakedRocks(GetEditorWorld());
	SetStatus(Result.Message, Result.bSucceeded
	                              ? (Result.bHadWarnings ? EStatusSeverity::Warning : EStatusSeverity::Success)
	                              : EStatusSeverity::Error);
	return FReply::Handled();
}

bool SRockBakingPanel::CanExecuteRockActions() const
{
	UWorld* World = GetEditorWorld();
	return RockSettings.IsValid() && GEditor && !GEditor->IsPlaySessionInProgress() && IsValid(World) &&
	       World->WorldType == EWorldType::Editor;
}

UWorld* SRockBakingPanel::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

FText SRockBakingPanel::GetStatusText() const
{
	if (GEditor && GEditor->IsPlaySessionInProgress()) {
		return LOCTEXT("RockActionsDisabledDuringPlay", "Rock actions are locked while PIE/simulation is running.");
	}
	return FText::FromString(StatusMessage);
}

FSlateColor SRockBakingPanel::GetStatusColor() const
{
	if (GEditor && GEditor->IsPlaySessionInProgress()) {
		return FAppStyle::Get().GetSlateColor("Colors.Warning");
	}

	switch (StatusSeverity) {
	case EStatusSeverity::Success:
		return FAppStyle::Get().GetSlateColor("Colors.AccentGreen");
	case EStatusSeverity::Warning:
		return FAppStyle::Get().GetSlateColor("Colors.Warning");
	case EStatusSeverity::Error:
		return FAppStyle::Get().GetSlateColor("Colors.AccentRed");
	case EStatusSeverity::Info:
	default:
		return FAppStyle::Get().GetSlateColor("Colors.Foreground");
	}
}

void SRockBakingPanel::SetStatus(const FString& InMessage, EStatusSeverity InSeverity)
{
	StatusMessage = InMessage;
	StatusSeverity = InSeverity;
}

#undef LOCTEXT_NAMESPACE
