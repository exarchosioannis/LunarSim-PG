#include "Robots/RoverCmdVelVehicleControllerComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
constexpr float MetersPerCentimeter = 0.01f;
}

URoverCmdVelVehicleControllerComponent::URoverCmdVelVehicleControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URoverCmdVelVehicleControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	Settings = MakeSanitizedSettings(Settings);
	ResolveRoverController();
	SetupRos();
}

void URoverCmdVelVehicleControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RoverController) {
		RoverController->EmergencyStop();
	}

	Super::EndPlay(EndPlayReason);
}

void URoverCmdVelVehicleControllerComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ROSNode) {
		ROSNode->Tick(DeltaTime);
	}

	UpdateRoverCommand();
}

void URoverCmdVelVehicleControllerComponent::SetSettings(const FRoverCmdVelControllerSettings& NewSettings)
{
	const FString PreviousTopic = Settings.CmdVelTopic;
	Settings = MakeSanitizedSettings(NewSettings);

	if (ROSNode && PreviousTopic != Settings.CmdVelTopic) {
		SetupRos();
	}
}

FRoverCmdVelControllerSettings URoverCmdVelVehicleControllerComponent::GetSettings() const
{
	return Settings;
}

void URoverCmdVelVehicleControllerComponent::SetCmdVelTopic(const FString& NewCmdVelTopic)
{
	FRoverCmdVelControllerSettings NewSettings = Settings;
	NewSettings.CmdVelTopic = NewCmdVelTopic;
	SetSettings(NewSettings);
}

FString URoverCmdVelVehicleControllerComponent::GetCmdVelTopic() const
{
	return Settings.CmdVelTopic;
}

void URoverCmdVelVehicleControllerComponent::ResolveRoverController()
{
	AActor* Owner = GetOwner();
	if (!Owner) {
		return;
	}

	RoverController = RoverControllerOverride
		? RoverControllerOverride
		: Owner->FindComponentByClass<URoverVehicleControllerComponent>();

	if (!RoverController) {
		if (!bLoggedMissingRoverController) {
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("RoverCmdVelVehicleControllerComponent: No RoverVehicleControllerComponent found on %s. cmd_vel will be ignored until the rover controller exists."),
				*Owner->GetName()
			);
			bLoggedMissingRoverController = true;
		}
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("RoverCmdVelVehicleControllerComponent: Commanding RoverVehicleControllerComponent on %s."),
		*Owner->GetName()
	);
}

void URoverCmdVelVehicleControllerComponent::SetupRos()
{
	if (!ROSNode) {
		ROSNode = UTempoROSNode::Create(TEXT("rover_cmd_vel_vehicle_controller"), this, false);
		if (!ROSNode) {
			UE_LOG(LogTemp, Warning, TEXT("RoverCmdVelVehicleControllerComponent: Failed to create ROS node."));
			return;
		}
	}

	const FString DesiredTopic = Settings.CmdVelTopic.IsEmpty() ? TEXT("/cmd_vel") : Settings.CmdVelTopic;
	if (SubscribedCmdVelTopic == DesiredTopic) {
		return;
	}

	if (!SubscribedCmdVelTopic.IsEmpty()) {
		ROSNode->RemoveSubscriptions(SubscribedCmdVelTopic);
		SubscribedCmdVelTopic.Empty();
	}

	if (!ROSNode->AddSubscription<FTwist>(
		DesiredTopic,
		TROSSubscriptionDelegate<FTwist>::CreateUObject(
			this,
			&URoverCmdVelVehicleControllerComponent::OnCmdVel
		)
	)) {
		UE_LOG(LogTemp, Warning, TEXT("RoverCmdVelVehicleControllerComponent: Failed to subscribe to %s."), *DesiredTopic);
		return;
	}

	SubscribedCmdVelTopic = DesiredTopic;
	bHasReceivedCommand = false;
	LastCmdTime = -1000000.0f;

	UE_LOG(LogTemp, Log, TEXT("RoverCmdVelVehicleControllerComponent: Subscribed to %s."), *SubscribedCmdVelTopic);
}

void URoverCmdVelVehicleControllerComponent::OnCmdVel(const FTwist& Msg)
{
	CurrentUnrealTwist = Msg;
	bHasReceivedCommand = true;

	if (GetWorld()) {
		LastCmdTime = GetWorld()->GetTimeSeconds();
	}

	if (bLogReceivedCmdVel) {
		const float RosLinearMps = Msg.LinearVelocity.X * MetersPerCentimeter;
		const float RosAngularRadps = -FMath::DegreesToRadians(Msg.AngularVelocity.Z);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("cmd_vel received (ROS SI): linear.x=%.3f m/s angular.z=%.3f rad/s"),
			RosLinearMps,
			RosAngularRadps
		);
	}
}

void URoverCmdVelVehicleControllerComponent::UpdateRoverCommand()
{
	if (!RoverController) {
		ResolveRoverController();
		if (!RoverController) {
			return;
		}
	}

	if (!GetWorld()) {
		return;
	}

	const ERoverControlMode ControlMode = RoverController->GetControlMode();
	if (ControlMode == ERoverControlMode::Disabled) {
		RoverController->EmergencyStop();
		return;
	}

	if (ControlMode != ERoverControlMode::RosCmdVel) {
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const bool bTimedOut = !bHasReceivedCommand || (Now - LastCmdTime) > Settings.CmdTimeoutSec;
	if (bTimedOut) {
		ApplyStopCommand();
		return;
	}

	float ForwardReverseInput = 0.0f;
	float SteeringInput = 0.0f;

	const float LinearMps = CurrentUnrealTwist.LinearVelocity.X * MetersPerCentimeter;
	const float UnrealAngularRadps = FMath::DegreesToRadians(CurrentUnrealTwist.AngularVelocity.Z);

	if (!FMath::IsNearlyZero(Settings.MaxCmdLinearMps)) {
		ForwardReverseInput = FMath::Clamp(LinearMps / Settings.MaxCmdLinearMps, -1.0f, 1.0f);
	}

	if (!FMath::IsNearlyZero(Settings.MaxCmdAngularRadps)) {
		SteeringInput = FMath::Clamp(UnrealAngularRadps / Settings.MaxCmdAngularRadps, -1.0f, 1.0f);
	}

	if (Settings.bInvertThrottle) {
		ForwardReverseInput *= -1.0f;
	}

	if (Settings.bInvertSteering) {
		SteeringInput *= -1.0f;
	}

	const float DeadZone = FMath::Clamp(Settings.InputDeadZone, 0.0f, 1.0f);
	if (FMath::Abs(ForwardReverseInput) < DeadZone) {
		ForwardReverseInput = 0.0f;
	}

	if (FMath::Abs(SteeringInput) < DeadZone) {
		SteeringInput = 0.0f;
	}

	if (FMath::IsNearlyZero(ForwardReverseInput) && FMath::IsNearlyZero(SteeringInput)) {
		ApplyStopCommand();
		return;
	}

	RoverController->ApplyNormalizedDriveCommand(ForwardReverseInput, SteeringInput);
}

void URoverCmdVelVehicleControllerComponent::ApplyStopCommand()
{
	if (!RoverController) {
		return;
	}

	if (Settings.StopMode == ERoverCmdVelStopMode::EmergencyStop) {
		RoverController->EmergencyStop();
		return;
	}

	RoverController->StopDrive();
}

FRoverCmdVelControllerSettings URoverCmdVelVehicleControllerComponent::MakeSanitizedSettings(
	const FRoverCmdVelControllerSettings& InSettings
) const
{
	FRoverCmdVelControllerSettings Sanitized = InSettings;
	Sanitized.CmdVelTopic = Sanitized.CmdVelTopic.TrimStartAndEnd();
	if (Sanitized.CmdVelTopic.IsEmpty()) {
		Sanitized.CmdVelTopic = TEXT("/cmd_vel");
	}
	Sanitized.MaxCmdLinearMps = FMath::Max(Sanitized.MaxCmdLinearMps, 0.01f);
	Sanitized.MaxCmdAngularRadps = FMath::Max(Sanitized.MaxCmdAngularRadps, 0.01f);
	Sanitized.CmdTimeoutSec = FMath::Max(Sanitized.CmdTimeoutSec, 0.0f);
	Sanitized.InputDeadZone = FMath::Clamp(Sanitized.InputDeadZone, 0.0f, 1.0f);
	return Sanitized;
}
