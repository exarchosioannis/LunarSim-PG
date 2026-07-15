// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "GTGeneratorReference.h"
#include "Generators/GTDataGeneratorComponent.h"

#include "GTGeneratorTrigger.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNREALGT_API UGTGeneratorTrigger : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  UGTGeneratorTrigger();

  UFUNCTION(BlueprintCallable, Category = Triggers)
  virtual void Trigger();

  UFUNCTION(BlueprintCallable, Category = Triggers)
  virtual void TriggerWithSession(int32 SessionId, UObject* CaptureManager);
  
  UFUNCTION(BlueprintCallable, Category = Triggers)
  virtual void TriggerWithFrame(
      int32 FrameIndex,
      double StampSeconds,
      int32 SessionId,
      UObject* CaptureManager);

  UFUNCTION(BlueprintCallable, Category = TriggerSettings)
  void SetEnabledGeneratorComponentProperties(const TArray<FName>& InEnabledGeneratorComponentProperties);

  UFUNCTION(BlueprintCallable, Category = TriggerSettings)
  void ClearEnabledGeneratorComponentProperties();

  bool ResolveEnabledGeneratorComponents(
      TArray<UGTDataGeneratorComponent*>& OutGeneratorComponents,
      TArray<FName>& OutGeneratorComponentProperties,
      FString& OutError) const;

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

  UPROPERTY(EditAnywhere, Category = TriggerSettings, meta = (EditCondition = "!bTriggerAllGeneratorComponents"))
  TArray<FGTGeneratorReference> DataGenerators;

private:
  bool IsGeneratorReferenceEnabled(const FGTGeneratorReference& GeneratorReference) const;

  UPROPERTY(Transient)
  bool bUseGeneratorComponentFilter = false;

  UPROPERTY(Transient)
  TArray<FName> EnabledGeneratorComponentProperties;

};
