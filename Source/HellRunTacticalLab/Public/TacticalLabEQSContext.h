#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "GameFramework/Actor.h"
#include "TacticalLabEQSContext.generated.h"

/** Transient bridge between the 2D lab entity and Unreal's real EQS runtime. */
UCLASS(Transient, NotPlaceable)
class HELLRUNTACTICALLAB_API ATacticalLabEQSQuerier : public AActor
{
    GENERATED_BODY()
public:
    FVector ThreatLocation = FVector::ZeroVector;
    TWeakObjectPtr<AActor> ThreatActor;
    bool bCanWalk = true;
    bool bCanClimb = false;
    bool bCanMantle = false;
    bool bCanDrop = false;
    bool bCanJump = false;
    bool bCanVault = false;
    bool bCanFly = false;
};

/** Supplies the selected simulated target to trace/visibility EQS tests. */
UCLASS()
class HELLRUNTACTICALLAB_API UTacticalLabEQSContext_Threat : public UEnvQueryContext
{
    GENERATED_BODY()
public:
    virtual void ProvideContext(FEnvQueryInstance& QueryInstance,
        FEnvQueryContextData& ContextData) const override;
};
