#pragma once

#include "CoreMinimal.h"
#include "RockBaking/SimulatorRockSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UWorld;

class SRockBakingPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRockBakingPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	enum class EStatusSeverity : uint8
	{
		Info,
		Success,
		Warning,
		Error
	};

	FReply OnBrowseRockJsonClicked();
	FReply OnBakeRocksClicked();
	FReply OnClearRocksClicked();

	bool CanExecuteRockActions() const;
	UWorld* GetEditorWorld() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	void SetStatus(const FString& InMessage, EStatusSeverity InSeverity);

	TStrongObjectPtr<USimulatorRockSettings> RockSettings;
	FString StatusMessage;
	EStatusSeverity StatusSeverity = EStatusSeverity::Info;
};
