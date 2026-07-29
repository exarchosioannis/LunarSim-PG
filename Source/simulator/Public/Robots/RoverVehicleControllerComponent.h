#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "RoverVehicleControllerComponent.generated.h"

class UChaosVehicleMovementComponent;

UENUM(BlueprintType)
enum class ERoverControlMode : uint8
{
	Manual UMETA(DisplayName = "WASD"),
	RosCmdVel UMETA(DisplayName = "cmd_vel")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API URoverVehicleControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverVehicleControllerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller|Control")
	void SetControlMode(ERoverControlMode NewControlMode);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover Vehicle Controller|Control")
	ERoverControlMode GetControlMode() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover Vehicle Controller|Control")
	bool IsManualInputEnabled() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover Vehicle Controller|Control")
	bool IsCmdVelInputEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller|Control")
	void ApplyNormalizedDriveCommand(float ForwardReverseInput, float SteeringInput);

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller|Control")
	void StopDrive();

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller|Control")
	void EmergencyStop();

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void ApplyForwardThrottle(float InputValue);

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void ApplyReverseThrottle(float InputValue);

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void ApplySteering(float InputValue);

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void StopThrottle();

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void StopSteering();

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void ApplyBrake(float BrakeValue);

	UFUNCTION(BlueprintCallable, Category = "Rover Vehicle Controller")
	void StopBrake();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover Vehicle Controller")
	float GetCurrentSpeedKmh() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover Vehicle Controller")
	bool IsOverForwardSpeedLimit() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover Vehicle Controller")
	bool IsOverReverseSpeedLimit() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "km/h"))
	float MaxForwardSpeedKmh = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "km/h"))
	float MaxReverseSpeedKmh = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AccelerationInputScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ReverseInputScale = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SteeringInputScale = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Steering")
	bool bUseSteeringSmoothing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Steering", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SteeringInterpSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Steering", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SteeringReturnInterpSpeed = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Steering", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SteeringDeadZone = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed Limit")
	bool bUseSoftSpeedLimit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed Limit", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SoftLimitStartRatio = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed Limit")
	bool bUseActiveSpeedBrake = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed Limit", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SpeedLimitBrakeStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Speed Limit", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "km/h"))
	float SpeedLimitHysteresisKmh = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Braking")
	bool bUseIdleBrake = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Braking", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IdleBrakeStrength = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Braking", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "km/h"))
	float IdleBrakeStopSpeedKmh = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover Vehicle Controller|Control")
	ERoverControlMode ControlMode = ERoverControlMode::Manual;

private:
	UPROPERTY(Transient)
	UChaosVehicleMovementComponent* VehicleMovement = nullptr;

	bool bLoggedMissingMovement = false;
	bool bSpeedLimitBrakeActive = false;
	bool bIdleBrakeActive = false;
	float CurrentThrottleInput = 0.0f;
	float TargetSteeringInput = 0.0f;
	float SmoothedSteeringInput = 0.0f;
	int32 LastRequestedGear = 0;
	int32 ActiveThrottleDirection = 0;
	float CoastBrakeSpeedLimitKmh = 0.0f;

	bool CanAcceptManualInput() const;
	bool CanAcceptCmdVelInput() const;
	void ApplyThrottleInternal(float InputValue, int32 Direction);
	void ApplySteeringInternal(float InputValue);
	void StopThrottleInternal();
	void StopSteeringInternal();
	void ApplyBrakeInternal(float BrakeValue);
	void StopBrakeInternal();
	void EmergencyStopInternal();
	void ResolveVehicleMovement();
	bool HasVehicleMovement();
	void RequestGear(int32 GearNum);
	void ApplyDriveOutput();
	void ApplyIdleBrakeOutput();
	void ApplySteeringOutput(float DeltaTime);
	void RefreshTickEnabled();
	bool ShouldApplyIdleBrake() const;
	float CalculateSpeedLimitedThrottle(float DesiredThrottle, float SpeedKmh, float SpeedLimitKmh, float& OutBrakeInput);
};
