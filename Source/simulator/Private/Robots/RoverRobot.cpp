#include "Robots/RoverRobot.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Capture/CapturePoseSourceComponent.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(geometry_msgs::msg::PoseStamped);

//Constructor
ARoverRobot::ARoverRobot()
{
	PrimaryActorTick.bCanEverTick = true;

	SetupRoverComponents();
	SetupThirdPersonCamera();
	RoverPoseSource = CreateDefaultSubobject<UCapturePoseSourceComponent>(TEXT("RoverPoseSource"));

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ARoverRobot::BeginPlay()
{
	Super::BeginPlay();

	SetupRos();

	LastCmdTime = GetWorld()->GetTimeSeconds();

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

void ARoverRobot::SetupRos()
{
	ROSNode = UTempoROSNode::Create(TEXT("rover_robot"), this);

	if (!ROSNode) {
		return;
	}

	FROSQOSProfile DefaultQOS;
	DefaultQOS.CustomQueueSize(10).Reliable();

	ROSNode->AddSubscription<FTwist>(
		*CmdVelTopic,
		TROSSubscriptionDelegate<FTwist>::CreateUObject(this, &ARoverRobot::OnCmdVel)
	);

	ROSNode->AddPublisher<geometry_msgs::msg::PoseStamped>(
		*GroundTruthPoseTopic,
		DefaultQOS,
		false
	);
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

builtin_interfaces::msg::Time ARoverRobot::ToRosTime(double Seconds) const
{
	builtin_interfaces::msg::Time T;

	if (Seconds < 0.0) {
		Seconds = 0.0;
	}

	const int64 Sec = (int64)Seconds;
	const double Frac = Seconds - (double)Sec;

	T.sec = (int32)Sec;
	T.nanosec = (uint32)FMath::Clamp<int64>(
		(int64)(Frac * 1000000000.0),
		0,
		999999999
	);

	return T;
}

FVector ARoverRobot::UnrealLocationToRosMeters(const FVector& UnrealLocation) const
{
	return FVector(
		UnrealLocation.X / 100.0,
		-UnrealLocation.Y / 100.0,
		UnrealLocation.Z / 100.0
	);
}

FQuat ARoverRobot::UnrealYawToRosQuat(const FRotator& UnrealRotation) const
{
	const double RosYawRad = FMath::DegreesToRadians(-UnrealRotation.Yaw);

	const double HalfYaw = RosYawRad * 0.5;
	const double SinHalfYaw = FMath::Sin(HalfYaw);
	const double CosHalfYaw = FMath::Cos(HalfYaw);

	return FQuat(
		0.0,
		0.0,
		SinHalfYaw,
		CosHalfYaw
	);
}

void ARoverRobot::PublishGroundTruthPose(const FCaptureFrameInfo& FrameInfo)
{
	if (!ROSNode) {
		return;
	}

	if (FrameInfo.FrameIndex <= 0) {
		return;
	}

	const FVector RosLocation = UnrealLocationToRosMeters(GetActorLocation());
	const FQuat RosQuat = UnrealYawToRosQuat(GetActorRotation());

	ReusablePoseMsg.header.stamp = ToRosTime(FrameInfo.StampSeconds);
	ReusablePoseMsg.header.frame_id = TCHAR_TO_UTF8(*GroundTruthPoseFrameId);

	ReusablePoseMsg.pose.position.x = RosLocation.X;
	ReusablePoseMsg.pose.position.y = RosLocation.Y;
	ReusablePoseMsg.pose.position.z = RosLocation.Z;

	ReusablePoseMsg.pose.orientation.x = RosQuat.X;
	ReusablePoseMsg.pose.orientation.y = RosQuat.Y;
	ReusablePoseMsg.pose.orientation.z = RosQuat.Z;
	ReusablePoseMsg.pose.orientation.w = RosQuat.W;

	ROSNode->Publish<geometry_msgs::msg::PoseStamped>(
		*GroundTruthPoseTopic,
		ReusablePoseMsg
	);
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