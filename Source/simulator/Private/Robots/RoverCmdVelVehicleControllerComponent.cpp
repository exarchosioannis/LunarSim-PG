#include "Robots/RoverCmdVelVehicleControllerComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

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

	VehicleMovement = Owner->FindComponentByClass<UChaosVehicleMovementComponent>();

	if (!VehicleMovement) {
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RoverCmdVelVehicleControllerComponent: No ChaosVehicleMovementComponent found on %s."),
			*Owner->GetName()
		);
	}
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
}

void URoverCmdVelVehicleControllerComponent::OnCmdVel(const FTwist& Msg)
{
	CurrentTwist = Msg;

	if (GetWorld()) {
		LastCmdTime = GetWorld()->GetTimeSeconds();
	}
}

void URoverCmdVelVehicleControllerComponent::UpdateVehicleInputs(float DeltaTime)
{
	if (!VehicleMovement || !GetWorld()) {
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	FTwist SafeTwist = CurrentTwist;

	if ((Now - LastCmdTime) > CmdTimeoutSec) {
		SafeTwist.LinearVelocity = FVector::ZeroVector;
		SafeTwist.AngularVelocity = FVector::ZeroVector;
	}

	float TargetThrottle = 0.0f;
	float TargetBrake = 0.0f;
	float TargetSteering = 0.0f;

	const float LinearX = SafeTwist.LinearVelocity.X;
	const float AngularZ = SafeTwist.AngularVelocity.Z;

	if (!FMath::IsNearlyZero(MaxCmdLinearMps)) {
		TargetThrottle = FMath::Clamp(LinearX / MaxCmdLinearMps, -1.0f, 1.0f);
	}

	if (!FMath::IsNearlyZero(MaxCmdAngularRadps)) {
		TargetSteering = FMath::Clamp(AngularZ / MaxCmdAngularRadps, -1.0f, 1.0f);
	}

	if (bInvertThrottle) {
		TargetThrottle *= -1.0f;
	}

	if (bInvertSteering) {
		TargetSteering *= -1.0f;
	}

	/*
		Important:
		In your Blueprint, the "Break" axis is connected to SetBrakeInput.
		But you said it behaves like reverse for this rover.
		So for ROS:
		  linear.x > 0  -> positive throttle
		  linear.x < 0  -> brake/reverse input
	*/
	if (TargetThrottle >= 0.0f) {
		TargetBrake = 0.0f;
	} else {
		TargetBrake = FMath::Abs(TargetThrottle);
		TargetThrottle = 0.0f;
	}

	CurrentThrottle = FMath::FInterpTo(CurrentThrottle, TargetThrottle, DeltaTime, ThrottleInterpSpeed);
	CurrentSteering = FMath::FInterpTo(CurrentSteering, TargetSteering, DeltaTime, SteeringInterpSpeed);
	CurrentBrake = FMath::FInterpTo(CurrentBrake, TargetBrake, DeltaTime, BrakeInterpSpeed);

	VehicleMovement->SetThrottleInput(CurrentThrottle);
	VehicleMovement->SetSteeringInput(CurrentSteering);
	VehicleMovement->SetBrakeInput(CurrentBrake);
}