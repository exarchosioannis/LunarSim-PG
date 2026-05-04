#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TempoROSNode.h"
#include "TempoROSTypes.h"

#include "RoverRobot.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputComponent;
class UCapturePoseSourceComponent;

UCLASS()
class SIMULATOR_API ARoverRobot : public APawn
{
	GENERATED_BODY()

public:
	ARoverRobot();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture|Pose")
	UCapturePoseSourceComponent* RoverPoseSource;

private:
	//Components
	UPROPERTY(VisibleAnywhere, Category = "Components")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UStaticMeshComponent* RoverMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Camera")
	USpringArmComponent* SpringArm = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Camera")
	UCameraComponent* FollowCamera = nullptr;

	//ROS
	UPROPERTY()
	UTempoROSNode* ROSNode = nullptr;

	//Movement State
	FTwist CurrentTwist;
	float LastCmdTime = 0.0f;
	float LinearCmd = 0.0f;
	float YawCmd = 0.0f;

	//Movement Settings
	UPROPERTY(EditAnywhere, Category = "Robot|Limits")
	float MaxLinearCmPerSec = 200.0f;

	UPROPERTY(EditAnywhere, Category = "Robot|Limits")
	float MaxYawDegPerSec = 30.0f;

	UPROPERTY(EditAnywhere, Category = "Robot|Limits")
	float MaxLinearAccelCmPerSec2 = 200.0f;

	UPROPERTY(EditAnywhere, Category = "Robot|Limits")
	float MaxYawAccelDegPerSec2 = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Robot|Safety")
	float CmdTimeoutSec = 0.75f;

	//Mesh Settings
	UPROPERTY(EditAnywhere, Category = "Robot|Frame")
	FRotator MeshForwardFix = FRotator(0.f, 90.f, 0.f);

	//Camera Control Settings
	UPROPERTY(EditAnywhere, Category = "Robot|Camera")
	float MouseYawSpeed = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Robot|Camera")
	float MousePitchSpeed = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Robot|Camera")
	float MinCameraPitch = -60.0f;

	UPROPERTY(EditAnywhere, Category = "Robot|Camera")
	float MaxCameraPitch = -5.0f;

	//ROS Callbacks
	void OnCmdVel(const FTwist& Msg);

	//Camera Input
	void LookYaw(float Value);
	void LookPitch(float Value);

	//Movement Helpers
	void UpdateMovement(float DeltaTime);

	//Setup
	void SetupRoverComponents();
	void SetupThirdPersonCamera();
};