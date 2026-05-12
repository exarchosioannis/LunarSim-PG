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

	UPROPERTY(EditAnywhere, Category = "ROS")
	FString CmdVelTopic = TEXT("/cmd_vel");

	UPROPERTY(EditAnywhere, Category = "Control")
	float MaxCmdLinearMps = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float MaxCmdAngularRadps = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float CmdTimeoutSec = 0.75f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float ThrottleInterpSpeed = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float SteeringInterpSpeed = 3.5f;

	UPROPERTY(EditAnywhere, Category = "Control")
	float BrakeInterpSpeed = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Control")
	bool bInvertSteering = false;

	UPROPERTY(EditAnywhere, Category = "Control")
	bool bInvertThrottle = false;

	FTwist CurrentTwist;

	float LastCmdTime = 0.0f;

	float CurrentThrottle = 0.0f;
	float CurrentSteering = 0.0f;
	float CurrentBrake = 0.0f;

	void SetupRos();
	void ResolveVehicleMovement();
	void OnCmdVel(const FTwist& Msg);

	void UpdateVehicleInputs(float DeltaTime);
};