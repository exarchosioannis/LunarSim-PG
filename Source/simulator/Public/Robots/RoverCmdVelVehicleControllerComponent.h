#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Robots/RoverVehicleControllerComponent.h"

#include "TempoROSNode.h"
#include "TempoROSTypes.h"
#include "Utils/LunarSimRosInterface.h"

#include "RoverCmdVelVehicleControllerComponent.generated.h"

UENUM(BlueprintType)
enum class ERoverCmdVelStopMode : uint8
{
	RoverStop UMETA(DisplayName = "Rover stop / idle brake"),
	EmergencyStop UMETA(DisplayName = "Full brake")
};

USTRUCT(BlueprintType)
struct SIMULATOR_API FRoverCmdVelControllerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ROS")
	FString CmdVelTopic = LunarSimRosTopics::CmdVel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.01", UIMin = "0.01", Units = "m/s"))
	float MaxCmdLinearMps = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.01", UIMin = "0.01", Units = "rad/s"))
	float MaxCmdAngularRadps = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float CmdTimeoutSec = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25"))
	float InputDeadZone = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	bool bInvertThrottle = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	bool bInvertSteering = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	ERoverCmdVelStopMode StopMode = ERoverCmdVelStopMode::RoverStop;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SIMULATOR_API URoverCmdVelVehicleControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverCmdVelVehicleControllerComponent();

	UFUNCTION(BlueprintCallable, Category = "Rover CmdVel Controller")
	void SetSettings(const FRoverCmdVelControllerSettings& NewSettings);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover CmdVel Controller")
	FRoverCmdVelControllerSettings GetSettings() const;

	UFUNCTION(BlueprintCallable, Category = "Rover CmdVel Controller")
	void SetCmdVelTopic(const FString& NewCmdVelTopic);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Rover CmdVel Controller")
	FString GetCmdVelTopic() const;

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
	URoverVehicleControllerComponent* RoverController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Vehicle")
	URoverVehicleControllerComponent* RoverControllerOverride = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rover CmdVel Controller", meta = (AllowPrivateAccess = "true"))
	FRoverCmdVelControllerSettings Settings;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bLogReceivedCmdVel = false;

	FTwist CurrentUnrealTwist;

	FString SubscribedCmdVelTopic;
	float LastCmdTime = -1000000.0f;
	bool bHasReceivedCommand = false;
	bool bLoggedMissingRoverController = false;

	void SetupRos();
	void ResolveRoverController();
	void OnCmdVel(const FTwist& Msg);

	void UpdateRoverCommand();
	void ApplyStopCommand();
	FRoverCmdVelControllerSettings MakeSanitizedSettings(const FRoverCmdVelControllerSettings& InSettings) const;
};
