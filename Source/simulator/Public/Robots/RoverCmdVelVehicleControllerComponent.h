#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "TempoROSNode.h"
#include "TempoROSTypes.h"

#include "RoverCmdVelVehicleControllerComponent.generated.h"

class UChaosVehicleMovementComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API URoverCmdVelVehicleControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverCmdVelVehicleControllerComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

private:
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	UPROPERTY()
	UChaosVehicleMovementComponent* VehicleMovement = nullptr;

	/*
		Optional manual reference.
		Use this only if this component is not placed on the same Actor/Pawn
		as the Chaos Vehicle Movement Component.
	*/
	UPROPERTY(EditAnywhere, Category = "Vehicle")
	UChaosVehicleMovementComponent* VehicleMovementOverride = nullptr;

	UPROPERTY(EditAnywhere, Category = "ROS")
	FString CmdVelTopic = TEXT("/cmd_vel");

	/*
		ROS cmd_vel scaling:
		linear.x = MaxCmdLinearMps gives full throttle.
		angular.z = MaxCmdAngularRadps gives full steering.
	*/
	UPROPERTY(EditAnywhere, Category = "Control")
	float MaxCmdLinearMps = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float MaxCmdAngularRadps = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float CmdTimeoutSec = 0.75f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float InputDeadZone = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float ThrottleInterpSpeed = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float SteeringInterpSpeed = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float BrakeInterpSpeed = 8.0f;

	/*
		When cmd_vel goes to zero, ROS usually means "stop".
		Keep this true for rover control. If the rover feels too abrupt,
		lower StopBrakeInput or disable this.
	*/
	UPROPERTY(EditAnywhere, Category = "Control")
	bool bBrakeWhenStopped = true;

	UPROPERTY(EditAnywhere, Category = "Control", meta = (EditCondition = "bBrakeWhenStopped"))
	float StopBrakeInput = 1.0f;

	/*
		Default mode for negative linear.x:
		linear.x < 0 -> SetTargetGear(-1, true) + positive throttle.
		This matches Chaos Vehicle better than using brake as reverse.
	*/
	UPROPERTY(EditAnywhere, Category = "Control")
	bool bUseReverseGear = true;

	/*
		Fallback only. Enable this if your specific vehicle Blueprint uses
		BrakeInput as reverse instead of Gear -1.
	*/
	UPROPERTY(EditAnywhere, Category = "Control", meta = (EditCondition = "!bUseReverseGear"))
	bool bUseBrakeAsReverse = false;

	UPROPERTY(EditAnywhere, Category = "Control")
	bool bInvertSteering = false;

	UPROPERTY(EditAnywhere, Category = "Control")
	bool bInvertThrottle = false;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bLogReceivedCmdVel = false;

	FTwist CurrentTwist;

	float LastCmdTime = 0.0f;

	float CurrentThrottle = 0.0f;
	float CurrentSteering = 0.0f;
	float CurrentBrake = 0.0f;

	int32 LastRequestedGear = 0;

	void SetupRos();
	void ResolveVehicleMovement();
	void OnCmdVel(const FTwist& Msg);

	void UpdateVehicleInputs(float DeltaTime);
	void RequestGear(int32 GearNum);
};