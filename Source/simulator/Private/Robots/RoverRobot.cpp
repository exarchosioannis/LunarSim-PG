#include "Robots/RoverRobot.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Sensors/RobotCamRig.h"
#include "UObject/ConstructorHelpers.h"

//Constructor
ARoverRobot::ARoverRobot()
{
	PrimaryActorTick.bCanEverTick = true;

	SetupRoverComponents();
	SetupThirdPersonCamera();

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ARoverRobot::BeginPlay()
{
	Super::BeginPlay();
	//ROS Setup
	ROSNode = UTempoROSNode::Create(TEXT("rover_robot"), this);
	ROSNode->AddSubscription<FTwist>(
		TEXT("/cmd_vel"),
		TROSSubscriptionDelegate<FTwist>::CreateUObject(this, &ARoverRobot::OnCmdVel)
	);

	LastCmdTime = GetWorld()->GetTimeSeconds();
	//Camera Rig Setup
	if (CameraRig){
		CameraRig->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
		CameraRig->SetActorRelativeTransform(CameraMount);
	}

	//Player Controller Setup
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()){
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = false;
	}
}

void ARoverRobot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (ROSNode) ROSNode->Tick(DeltaTime);
	
	//Robot Movement
	UpdateMovement(DeltaTime);
}

//apply third person camera controls
void ARoverRobot::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis("TurnCamera", this, &ARoverRobot::LookYaw);
	PlayerInputComponent->BindAxis("LookUpCamera", this, &ARoverRobot::LookPitch);
}

//Rover Setup
void ARoverRobot::SetupRoverComponents()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RoverMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RoverMesh"));
	RoverMesh->SetupAttachment(Root);

	RoverMesh->SetMobility(EComponentMobility::Movable);
	RoverMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RoverMesh->SetCollisionObjectType(ECC_Pawn);
	RoverMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RoverAsset(
		TEXT("StaticMesh'/Game/Assets/Rover/rover.rover'")
	);

	if (RoverAsset.Succeeded())RoverMesh->SetStaticMesh(RoverAsset.Object);
}

//Third Person Camera Setup
void ARoverRobot::SetupThirdPersonCamera()
{
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	SpringArm->TargetArmLength = 1000.0f;
	SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	SpringArm->SetRelativeRotation(FRotator(-20.f, 90.f, 0.f));
	SpringArm->bDoCollisionTest = true;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

//ROS Callback
void ARoverRobot::OnCmdVel(const FTwist& Msg)
{
	CurrentTwist = Msg;
	LastCmdTime = GetWorld()->GetTimeSeconds();
}

//movement Update
void ARoverRobot::UpdateMovement(float DeltaTime)
{
	//Command Timeout
	const float Now = GetWorld()->GetTimeSeconds();
	if ((Now - LastCmdTime) > CmdTimeoutSec){
		CurrentTwist.LinearVelocity = FVector::ZeroVector;
		CurrentTwist.AngularVelocity = FVector::ZeroVector;
	}
	//Target Velocity
	const float TargetVx = FMath::Clamp(CurrentTwist.LinearVelocity.X,-MaxLinearCmPerSec,MaxLinearCmPerSec);
	const float TargetYaw = FMath::Clamp(FMath::RadiansToDegrees(CurrentTwist.AngularVelocity.Z),-MaxYawDegPerSec,MaxYawDegPerSec);

	//Acceleration Limiting
	LinearCmd = FMath::FInterpConstantTo(LinearCmd,TargetVx,DeltaTime,MaxLinearAccelCmPerSec2);
	YawCmd = FMath::FInterpConstantTo(YawCmd,TargetYaw,DeltaTime,MaxYawAccelDegPerSec2);

	//Movement
	const FVector VisualForward = MeshForwardFix.RotateVector(GetActorForwardVector());
	const FVector MoveDelta = VisualForward * (LinearCmd * DeltaTime);

	//Apply Movement
	AddActorWorldOffset(MoveDelta, true);
	AddActorWorldRotation(FRotator(0.f, YawCmd * DeltaTime, 0.f));
}

//move third person camera left and right
void ARoverRobot::LookYaw(float Value)
{
	if (!SpringArm || FMath::IsNearlyZero(Value)) return;

	FRotator Rot = SpringArm->GetRelativeRotation();
	Rot.Yaw += Value * MouseYawSpeed;
	SpringArm->SetRelativeRotation(Rot);
}

//move third person camera up and down
void ARoverRobot::LookPitch(float Value)
{
	if (!SpringArm || FMath::IsNearlyZero(Value)) return;
	
	FRotator Rot = SpringArm->GetRelativeRotation();
	Rot.Pitch = FMath::Clamp(Rot.Pitch - Value * MousePitchSpeed, MinCameraPitch, MaxCameraPitch);
	SpringArm->SetRelativeRotation(Rot);
}