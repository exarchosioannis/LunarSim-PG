#include "Robots/RoverCmdVelVehicleControllerComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

URoverCmdVelVehicleControllerComponent::URoverCmdVelVehicleControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URoverCmdVelVehicleControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveVehicleMovement();
	SetupRos();

	if (GetWorld()) {
		LastCmdTime = GetWorld()->GetTimeSeconds();
	}
}

void URoverCmdVelVehicleControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (VehicleMovement) {
		VehicleMovement->SetThrottleInput(0.0f);
		VehicleMovement->SetSteeringInput(0.0f);
		VehicleMovement->SetBrakeInput(1.0f);
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

	UpdateVehicleInputs(DeltaTime);
}

void URoverCmdVelVehicleControllerComponent::ResolveVehicleMovement()
{
	AActor* Owner = GetOwner();
	if (!Owner) {
		return;
	}

	if (VehicleMovementOverride) {
		VehicleMovement = VehicleMovementOverride;
	} else {
		VehicleMovement = Owner->FindComponentByClass<UChaosVehicleMovementComponent>();
	}

	if (!VehicleMovement) {
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RoverCmdVelVehicleControllerComponent: No ChaosVehicleMovementComponent found on %s. Put this component on the rover Pawn, or set VehicleMovementOverride in Details."),
			*Owner->GetName()
		);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("RoverCmdVelVehicleControllerComponent: Using ChaosVehicleMovementComponent on %s."),
		*Owner->GetName()
	);
}

void URoverCmdVelVehicleControllerComponent::SetupRos()
{
	ROSNode = UTempoROSNode::Create(TEXT("rover_cmd_vel_vehicle_controller"), this);

	if (!ROSNode) {
		UE_LOG(LogTemp, Warning, TEXT("RoverCmdVelVehicleControllerComponent: Failed to create ROS node."));
		return;
	}

	ROSNode->AddSubscription<FTwist>(
		*CmdVelTopic,
		TROSSubscriptionDelegate<FTwist>::CreateUObject(
			this,
			&URoverCmdVelVehicleControllerComponent::OnCmdVel
		)
	);

	UE_LOG(LogTemp, Log, TEXT("RoverCmdVelVehicleControllerComponent: Subscribed to %s."), *CmdVelTopic);
}

void URoverCmdVelVehicleControllerComponent::OnCmdVel(const FTwist& Msg)
{
	CurrentTwist = Msg;

	if (GetWorld()) {
		LastCmdTime = GetWorld()->GetTimeSeconds();
	}

	if (bLogReceivedCmdVel) {
		UE_LOG(
			LogTemp,
			Log,
			TEXT("cmd_vel received: linear.x=%.3f angular.z=%.3f"),
			Msg.LinearVelocity.X,
			Msg.AngularVelocity.Z
		);
	}
}

void URoverCmdVelVehicleControllerComponent::RequestGear(int32 GearNum)
{
	if (!VehicleMovement || GearNum == 0) {
		return;
	}

	if (LastRequestedGear == GearNum) {
		return;
	}

	VehicleMovement->SetTargetGear(GearNum, true);
	LastRequestedGear = GearNum;
}

void URoverCmdVelVehicleControllerComponent::UpdateVehicleInputs(float DeltaTime)
{
	if (!VehicleMovement || !GetWorld()) {
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	FTwist SafeTwist = CurrentTwist;
	const bool bTimedOut = (Now - LastCmdTime) > CmdTimeoutSec;

	if (bTimedOut) {
		SafeTwist.LinearVelocity = FVector::ZeroVector;
		SafeTwist.AngularVelocity = FVector::ZeroVector;
	}

	const float LinearX = SafeTwist.LinearVelocity.X;
	const float AngularZ = SafeTwist.AngularVelocity.Z;

	float SignedThrottle = 0.0f;
	float TargetSteering = 0.0f;

	if (!FMath::IsNearlyZero(MaxCmdLinearMps)) {
		SignedThrottle = FMath::Clamp(LinearX / MaxCmdLinearMps, -1.0f, 1.0f);
	}

	if (!FMath::IsNearlyZero(MaxCmdAngularRadps)) {
		TargetSteering = FMath::Clamp(AngularZ / MaxCmdAngularRadps, -1.0f, 1.0f);
	}

	if (bInvertThrottle) {
		SignedThrottle *= -1.0f;
	}

	if (bInvertSteering) {
		TargetSteering *= -1.0f;
	}

	if (FMath::Abs(SignedThrottle) < InputDeadZone) {
		SignedThrottle = 0.0f;
	}

	if (FMath::Abs(TargetSteering) < InputDeadZone) {
		TargetSteering = 0.0f;
	}

	float TargetThrottle = 0.0f;
	float TargetBrake = 0.0f;

	if (SignedThrottle > 0.0f) {
		// Same idea as the Blueprint: SetTargetGear(1, true) -> SetThrottleInput(value)
		RequestGear(1);
		TargetThrottle = SignedThrottle;
		TargetBrake = 0.0f;
	} else if (SignedThrottle < 0.0f) {
		if (bUseReverseGear) {
			// Chaos reverse: gear -1, but throttle stays positive.
			RequestGear(-1);
			TargetThrottle = FMath::Abs(SignedThrottle);
			TargetBrake = 0.0f;
		} else if (bUseBrakeAsReverse) {
			// Fallback for custom Blueprints that treat BrakeInput as reverse.
			TargetThrottle = 0.0f;
			TargetBrake = FMath::Abs(SignedThrottle);
		} else {
			TargetThrottle = 0.0f;
			TargetBrake = 0.0f;
		}
	} else {
		TargetThrottle = 0.0f;
		TargetBrake = bBrakeWhenStopped ? FMath::Clamp(StopBrakeInput, 0.0f, 1.0f) : 0.0f;
	}

	CurrentThrottle = FMath::FInterpTo(CurrentThrottle, TargetThrottle, DeltaTime, ThrottleInterpSpeed);
	CurrentSteering = FMath::FInterpTo(CurrentSteering, TargetSteering, DeltaTime, SteeringInterpSpeed);
	CurrentBrake = FMath::FInterpTo(CurrentBrake, TargetBrake, DeltaTime, BrakeInterpSpeed);

	VehicleMovement->SetBrakeInput(CurrentBrake);
	VehicleMovement->SetSteeringInput(CurrentSteering);
	VehicleMovement->SetThrottleInput(CurrentThrottle);
}