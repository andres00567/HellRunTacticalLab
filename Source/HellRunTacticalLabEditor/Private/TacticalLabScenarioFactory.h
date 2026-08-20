#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "TacticalLabScenarioFactory.generated.h"

UCLASS()
class UTacticalLabScenarioFactory final : public UFactory
{
    GENERATED_BODY()
public:
    UTacticalLabScenarioFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent,
        FName Name, EObjectFlags Flags, UObject* Context,
        FFeedbackContext* Warn) override;
};
