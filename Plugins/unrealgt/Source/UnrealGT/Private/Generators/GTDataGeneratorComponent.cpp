// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/GTDataGeneratorComponent.h"

// Sets default values for this component's properties
UGTDataGeneratorComponent::UGTDataGeneratorComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}

void UGTDataGeneratorComponent::DrawDebug(FViewport* Viewport, FCanvas* Canvas)
{
}

void UGTDataGeneratorComponent::GenerateData(const FDateTime& TimeStamp)
{
}

void UGTDataGeneratorComponent::GenerateData(const FDateTime& TimeStamp, int32 SessionId, FSessionValidationFunc SessionValidator)
{
    // Default implementation falls back to legacy method
    GenerateData(TimeStamp);
}

void UGTDataGeneratorComponent::GenerateData(
    const FDateTime& TimeStamp,
    int32 SessionId,
    FSessionValidationFunc SessionValidator,
    int32 FrameIndex,
    double StampSeconds)
{
    // Default implementation falls back to session-aware method
    GenerateData(TimeStamp, SessionId, MoveTemp(SessionValidator));
}

// Called when the game starts
void UGTDataGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void UGTDataGeneratorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
