#include "Robots/RoverVehicleControllerComponent.h"
#include "simulator.h"

#include "ChaosVehicleMovementComponent.h"
#include "GameFramework/Actor.h"

namespace
{
constexpr float CmPerSecondToKmh = 0.036f;
}

URoverVehicleControllerComponent::URoverVehicleControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URoverVehicleControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveVehicleMovement();
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
	}
}

void URoverVehicleControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EmergencyStopInternal();
	Super::EndPlay(EndPlayReason);
}

void URoverVehicleControllerComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
		return;
	}

	ApplyDriveOutput();
	ApplyIdleBrakeOutput();
	ApplySteeringOutput(DeltaTime);
	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::SetControlMode(ERoverControlMode NewControlMode)
{
	if (ControlMode == NewControlMode) {
		if (ControlMode == ERoverControlMode::Disabled) {
			EmergencyStopInternal();
		}
		RefreshTickEnabled();
		return;
	}

	ControlMode = NewControlMode;
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
	}
	else {
		StopDrive();
	}

	RefreshTickEnabled();
}

ERoverControlMode URoverVehicleControllerComponent::GetControlMode() const
{
	return ControlMode;
}

bool URoverVehicleControllerComponent::IsManualInputEnabled() const
{
	return CanAcceptManualInput();
}

bool URoverVehicleControllerComponent::IsCmdVelInputEnabled() const
{
	return CanAcceptCmdVelInput();
}

void URoverVehicleControllerComponent::SetManualInputEnabled(bool bEnabled)
{
	SetControlMode(bEnabled ? ERoverControlMode::Manual : ERoverControlMode::Disabled);
}

void URoverVehicleControllerComponent::SetCmdVelInputEnabled(bool bEnabled)
{
	SetControlMode(bEnabled ? ERoverControlMode::RosCmdVel : ERoverControlMode::Disabled);
}

void URoverVehicleControllerComponent::ApplyNormalizedDriveCommand(float ForwardReverseInput, float SteeringInput)
{
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
		return;
	}

	if (!CanAcceptCmdVelInput()) {
		return;
	}

	const float DriveInput = FMath::Clamp(ForwardReverseInput, -1.0f, 1.0f);
	if (DriveInput > KINDA_SMALL_NUMBER) {
		ApplyThrottleInternal(DriveInput, 1);
	}
	else if (DriveInput < -KINDA_SMALL_NUMBER) {
		ApplyThrottleInternal(-DriveInput, -1);
	}
	else {
		StopThrottleInternal();
	}

	ApplySteeringInternal(SteeringInput);
}

void URoverVehicleControllerComponent::StopDrive()
{
	StopThrottleInternal();
	StopSteeringInternal();
}

void URoverVehicleControllerComponent::EmergencyStop()
{
	EmergencyStopInternal();
}

void URoverVehicleControllerComponent::ApplyForwardThrottle(float InputValue)
{
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
		return;
	}

	if (!CanAcceptManualInput()) {
		return;
	}

	ApplyThrottleInternal(InputValue, 1);
}

void URoverVehicleControllerComponent::ApplyReverseThrottle(float InputValue)
{
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
		return;
	}

	if (!CanAcceptManualInput()) {
		return;
	}

	ApplyThrottleInternal(InputValue, -1);
}

void URoverVehicleControllerComponent::ApplySteering(float InputValue)
{
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
		return;
	}

	if (!CanAcceptManualInput()) {
		return;
	}

	ApplySteeringInternal(InputValue);
}

void URoverVehicleControllerComponent::StopThrottle()
{
	if (!CanAcceptManualInput()) {
		return;
	}

	StopThrottleInternal();
}

void URoverVehicleControllerComponent::StopSteering()
{
	if (!CanAcceptManualInput()) {
		return;
	}

	StopSteeringInternal();
}

void URoverVehicleControllerComponent::ApplyBrake(float BrakeValue)
{
	if (ControlMode == ERoverControlMode::Disabled) {
		EmergencyStopInternal();
		return;
	}

	if (!CanAcceptManualInput()) {
		return;
	}

	ApplyBrakeInternal(BrakeValue);
}

void URoverVehicleControllerComponent::StopBrake()
{
	if (!CanAcceptManualInput()) {
		return;
	}

	StopBrakeInternal();
}

float URoverVehicleControllerComponent::GetCurrentSpeedKmh() const
{
	return VehicleMovement ? VehicleMovement->GetForwardSpeed() * CmPerSecondToKmh : 0.0f;
}

bool URoverVehicleControllerComponent::IsOverForwardSpeedLimit() const
{
	return MaxForwardSpeedKmh > 0.0f && GetCurrentSpeedKmh() >= MaxForwardSpeedKmh;
}

bool URoverVehicleControllerComponent::IsOverReverseSpeedLimit() const
{
	return MaxReverseSpeedKmh > 0.0f && -GetCurrentSpeedKmh() >= MaxReverseSpeedKmh;
}

bool URoverVehicleControllerComponent::CanAcceptManualInput() const
{
	return ControlMode == ERoverControlMode::Manual;
}

bool URoverVehicleControllerComponent::CanAcceptCmdVelInput() const
{
	return ControlMode == ERoverControlMode::RosCmdVel;
}

void URoverVehicleControllerComponent::ApplyThrottleInternal(float InputValue, int32 Direction)
{
	if (!HasVehicleMovement()) {
		return;
	}

	const int32 RequestedDirection = Direction < 0 ? -1 : 1;
	RequestGear(RequestedDirection);

	if (ActiveThrottleDirection != RequestedDirection) {
		bSpeedLimitBrakeActive = false;
	}

	CurrentThrottleInput = FMath::Clamp(FMath::Abs(InputValue), 0.0f, 1.0f);
	ActiveThrottleDirection = CurrentThrottleInput > KINDA_SMALL_NUMBER ? RequestedDirection : 0;
	if (ActiveThrottleDirection == 0) {
		bSpeedLimitBrakeActive = false;
		bIdleBrakeActive = ShouldApplyIdleBrake();
		VehicleMovement->SetThrottleInput(0.0f);
		ApplyIdleBrakeOutput();
		RefreshTickEnabled();
		return;
	}

	bIdleBrakeActive = false;
	ApplyDriveOutput();
	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::ApplySteeringInternal(float InputValue)
{
	if (!HasVehicleMovement()) {
		return;
	}

	const float ClampedInput = FMath::Clamp(InputValue, -1.0f, 1.0f);
	TargetSteeringInput = FMath::Abs(ClampedInput) <= FMath::Clamp(SteeringDeadZone, 0.0f, 1.0f)
		? 0.0f
		: ClampedInput;

	if (!bUseSteeringSmoothing) {
		ApplySteeringOutput(0.0f);
	}
	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::StopThrottleInternal()
{
	if (!HasVehicleMovement()) {
		return;
	}

	const int32 ReleasedThrottleDirection = ActiveThrottleDirection;
	const float ReleasedSpeedLimitKmh = ReleasedThrottleDirection < 0 ? MaxReverseSpeedKmh : MaxForwardSpeedKmh;
	const float CurrentAbsSpeedKmh = FMath::Abs(GetCurrentSpeedKmh());

	CurrentThrottleInput = 0.0f;
	ActiveThrottleDirection = 0;
	CoastBrakeSpeedLimitKmh = FMath::Max(ReleasedSpeedLimitKmh, 0.0f);

	VehicleMovement->SetThrottleInput(0.0f);

	// If the rover is released while still above the active cap, keep the stronger
	// speed-limit brake instead of immediately falling back to the weaker idle brake.
	bSpeedLimitBrakeActive = bUseActiveSpeedBrake
		&& CoastBrakeSpeedLimitKmh > 0.0f
		&& CurrentAbsSpeedKmh >= CoastBrakeSpeedLimitKmh;

	bIdleBrakeActive = !bSpeedLimitBrakeActive && ShouldApplyIdleBrake();
	ApplyIdleBrakeOutput();

	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::StopSteeringInternal()
{
	if (!HasVehicleMovement()) {
		return;
	}

	TargetSteeringInput = 0.0f;
	if (!bUseSteeringSmoothing) {
		SmoothedSteeringInput = 0.0f;
		VehicleMovement->SetSteeringInput(0.0f);
	}

	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::ApplyBrakeInternal(float BrakeValue)
{
	if (!HasVehicleMovement()) {
		return;
	}

	CurrentThrottleInput = 0.0f;
	ActiveThrottleDirection = 0;
	bSpeedLimitBrakeActive = false;
	bIdleBrakeActive = false;
	CoastBrakeSpeedLimitKmh = 0.0f;

	const float AppliedBrake = FMath::Clamp(FMath::Abs(BrakeValue), 0.0f, 1.0f);
	VehicleMovement->SetThrottleInput(0.0f);
	VehicleMovement->SetBrakeInput(AppliedBrake);

	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::StopBrakeInternal()
{
	if (!HasVehicleMovement()) {
		return;
	}

	bIdleBrakeActive = ShouldApplyIdleBrake();
	ApplyIdleBrakeOutput();

	if (!bIdleBrakeActive && !bSpeedLimitBrakeActive) {
		VehicleMovement->SetBrakeInput(0.0f);
	}

	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::EmergencyStopInternal()
{
	if (!HasVehicleMovement()) {
		return;
	}

	CurrentThrottleInput = 0.0f;
	TargetSteeringInput = 0.0f;
	SmoothedSteeringInput = 0.0f;
	ActiveThrottleDirection = 0;
	bSpeedLimitBrakeActive = false;
	bIdleBrakeActive = false;
	CoastBrakeSpeedLimitKmh = 0.0f;

	VehicleMovement->SetThrottleInput(0.0f);
	VehicleMovement->SetSteeringInput(0.0f);
	VehicleMovement->SetBrakeInput(1.0f);

	RefreshTickEnabled();
}

void URoverVehicleControllerComponent::ResolveVehicleMovement()
{
	if (VehicleMovement) {
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner) {
		return;
	}

	VehicleMovement = Owner->FindComponentByClass<UChaosVehicleMovementComponent>();

	if (!VehicleMovement && !bLoggedMissingMovement) {
		UE_LOG(
			LogLunarSimROS,
			Warning,
			TEXT("RoverVehicleControllerComponent: No ChaosVehicleMovementComponent found on %s. Add this component to the rover vehicle Pawn that owns the Chaos movement component."),
			*Owner->GetName()
		);
		bLoggedMissingMovement = true;
	}
}

bool URoverVehicleControllerComponent::HasVehicleMovement()
{
	ResolveVehicleMovement();
	return VehicleMovement != nullptr;
}

void URoverVehicleControllerComponent::RequestGear(int32 GearNum)
{
	if (!VehicleMovement || GearNum == 0 || LastRequestedGear == GearNum) {
		return;
	}

	VehicleMovement->SetTargetGear(GearNum, true);
	LastRequestedGear = GearNum;
}

void URoverVehicleControllerComponent::ApplyDriveOutput()
{
	if (!HasVehicleMovement() || ActiveThrottleDirection == 0 || CurrentThrottleInput <= KINDA_SMALL_NUMBER) {
		return;
	}

	bIdleBrakeActive = false;

	const bool bReverse = ActiveThrottleDirection < 0;
	RequestGear(bReverse ? -1 : 1);

	const float RawThrottle = FMath::Clamp(CurrentThrottleInput, 0.0f, 1.0f);
	const float InputScale = bReverse ? ReverseInputScale : AccelerationInputScale;
	const float SpeedLimitKmh = bReverse ? MaxReverseSpeedKmh : MaxForwardSpeedKmh;

	// Use speed magnitude for limiting. Some vehicle meshes / Chaos setups report
	// forward travel with the opposite sign, and using only the signed value can
	// make the limiter think the rover is standing still.
	const float SpeedAlongDirectionKmh = FMath::Abs(GetCurrentSpeedKmh());

	const float DesiredThrottle = FMath::Clamp(RawThrottle * InputScale, 0.0f, 1.0f);
	float AppliedBrake = 0.0f;
	const float AppliedThrottle = CalculateSpeedLimitedThrottle(
		DesiredThrottle,
		SpeedAlongDirectionKmh,
		SpeedLimitKmh,
		AppliedBrake
	);

	VehicleMovement->SetBrakeInput(AppliedBrake);
	VehicleMovement->SetThrottleInput(AppliedThrottle);
}

void URoverVehicleControllerComponent::ApplyIdleBrakeOutput()
{
	if (!HasVehicleMovement() || ActiveThrottleDirection != 0 || CurrentThrottleInput > KINDA_SMALL_NUMBER) {
		return;
	}

	VehicleMovement->SetThrottleInput(0.0f);

	const float CurrentAbsSpeedKmh = FMath::Abs(GetCurrentSpeedKmh());
	const float Hysteresis = FMath::Max(SpeedLimitHysteresisKmh, 0.0f);

	if (bSpeedLimitBrakeActive) {
		const float ActiveCoastLimitKmh = FMath::Max(CoastBrakeSpeedLimitKmh, 0.0f);
		const bool bStillAboveLimitBand = ActiveCoastLimitKmh > 0.0f
			&& CurrentAbsSpeedKmh > FMath::Max(ActiveCoastLimitKmh - Hysteresis, 0.0f);

		if (bStillAboveLimitBand) {
			bIdleBrakeActive = false;
			VehicleMovement->SetBrakeInput(FMath::Clamp(SpeedLimitBrakeStrength, 0.0f, 1.0f));
			return;
		}

		bSpeedLimitBrakeActive = false;
		CoastBrakeSpeedLimitKmh = 0.0f;
	}

	if (ShouldApplyIdleBrake()) {
		bIdleBrakeActive = true;
		VehicleMovement->SetBrakeInput(FMath::Clamp(IdleBrakeStrength, 0.0f, 1.0f));
		return;
	}

	bIdleBrakeActive = false;
	VehicleMovement->SetBrakeInput(0.0f);
}

void URoverVehicleControllerComponent::ApplySteeringOutput(float DeltaTime)
{
	if (!HasVehicleMovement()) {
		return;
	}

	if (!bUseSteeringSmoothing) {
		SmoothedSteeringInput = TargetSteeringInput;
		const float AppliedSteering = FMath::Clamp(SmoothedSteeringInput * SteeringInputScale, -1.0f, 1.0f);
		VehicleMovement->SetSteeringInput(AppliedSteering);
		return;
	}

	const bool bReturningToCenter = FMath::IsNearlyZero(TargetSteeringInput);
	const float InterpSpeed = FMath::Max(bReturningToCenter ? SteeringReturnInterpSpeed : SteeringInterpSpeed, 0.0f);
	if (InterpSpeed <= 0.0f) {
		SmoothedSteeringInput = TargetSteeringInput;
	}
	else {
		SmoothedSteeringInput = FMath::FInterpTo(
			SmoothedSteeringInput,
			TargetSteeringInput,
			FMath::Max(DeltaTime, 0.0f),
			InterpSpeed
		);
	}

	if (bReturningToCenter && FMath::IsNearlyZero(SmoothedSteeringInput, FMath::Max(SteeringDeadZone, 0.001f))) {
		SmoothedSteeringInput = 0.0f;
		VehicleMovement->SetSteeringInput(0.0f);
		return;
	}

	const float RawSteering = FMath::Clamp(SmoothedSteeringInput, -1.0f, 1.0f);
	const float AppliedSteering = FMath::Clamp(RawSteering * SteeringInputScale, -1.0f, 1.0f);

	VehicleMovement->SetSteeringInput(AppliedSteering);
}

void URoverVehicleControllerComponent::RefreshTickEnabled()
{
	const bool bNeedsDisabledTick = ControlMode == ERoverControlMode::Disabled;
	const bool bNeedsDriveTick = ActiveThrottleDirection != 0 && CurrentThrottleInput > KINDA_SMALL_NUMBER;
	const bool bNeedsSteeringTick = bUseSteeringSmoothing
		? !FMath::IsNearlyZero(TargetSteeringInput) || !FMath::IsNearlyZero(SmoothedSteeringInput)
		: !FMath::IsNearlyZero(TargetSteeringInput);
	const bool bNeedsIdleBrakeTick = bIdleBrakeActive && bUseIdleBrake;

	SetComponentTickEnabled(bNeedsDisabledTick || bNeedsDriveTick || bNeedsSteeringTick || bSpeedLimitBrakeActive || bNeedsIdleBrakeTick);
}

bool URoverVehicleControllerComponent::ShouldApplyIdleBrake() const
{
	if (!bUseIdleBrake || !VehicleMovement) {
		return false;
	}

	const float StopSpeedKmh = FMath::Max(IdleBrakeStopSpeedKmh, 0.0f);
	return FMath::Abs(GetCurrentSpeedKmh()) > StopSpeedKmh;
}

float URoverVehicleControllerComponent::CalculateSpeedLimitedThrottle(
	float DesiredThrottle,
	float SpeedKmh,
	float SpeedLimitKmh,
	float& OutBrakeInput
)
{
	OutBrakeInput = 0.0f;

	const float ClampedSpeedLimit = FMath::Max(SpeedLimitKmh, 0.0f);
	if (ClampedSpeedLimit <= 0.0f) {
		bSpeedLimitBrakeActive = false;
		return 0.0f;
	}

	const float SpeedAlongDirection = FMath::Max(SpeedKmh, 0.0f);
	const float Hysteresis = FMath::Max(SpeedLimitHysteresisKmh, 0.0f);

	if (bSpeedLimitBrakeActive) {
		bSpeedLimitBrakeActive = SpeedAlongDirection > ClampedSpeedLimit - Hysteresis;
	}
	else {
		bSpeedLimitBrakeActive = SpeedAlongDirection >= ClampedSpeedLimit;
	}

	if (bSpeedLimitBrakeActive) {
		OutBrakeInput = bUseActiveSpeedBrake ? FMath::Clamp(SpeedLimitBrakeStrength, 0.0f, 1.0f) : 0.0f;
		return 0.0f;
	}

	if (!bUseSoftSpeedLimit) {
		return FMath::Clamp(DesiredThrottle, 0.0f, 1.0f);
	}

	const float SoftStartSpeed = ClampedSpeedLimit * FMath::Clamp(SoftLimitStartRatio, 0.0f, 1.0f);
	if (SpeedAlongDirection <= SoftStartSpeed) {
		return FMath::Clamp(DesiredThrottle, 0.0f, 1.0f);
	}

	const float RemainingThrottleRatio = FMath::GetMappedRangeValueClamped(
		FVector2D(SoftStartSpeed, ClampedSpeedLimit),
		FVector2D(1.0f, 0.0f),
		SpeedAlongDirection
	);
	return FMath::Clamp(DesiredThrottle, 0.0f, 1.0f) * RemainingThrottleRatio;
}
