// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "GTDataGeneratorComponent.generated.h"

class FViewport;
class FCanvas;

// Session validation function type
using FSessionValidationFunc = TFunction<bool(int32)>;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGTDataReadySignature,
                                             const TArray<uint8> &, Data,
                                             const FDateTime &, TimeStamp,
                                             int32, FrameIndex);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNREALGT_API UGTDataGeneratorComponent : public USceneComponent {
  GENERATED_BODY()

public:
  FGTDataReadySignature DataReadyDelegate;

  // Sets default values for this component's properties
  UGTDataGeneratorComponent();

  virtual void DrawDebug(FViewport *Viewport, FCanvas *Canvas);

  virtual void GenerateData(const FDateTime &TimeStamp);
  
  virtual void GenerateData(const FDateTime &TimeStamp, int32 SessionId, FSessionValidationFunc SessionValidator);
  
  virtual void GenerateData(
      const FDateTime& TimeStamp,
      int32 SessionId,
      FSessionValidationFunc SessionValidator,
      int32 FrameIndex,
      double StampSeconds);

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;
};
