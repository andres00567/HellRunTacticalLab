#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"

class UWorld;

class HELLRUNTACTICALLABEDITOR_API IHellRunTacticalLabEditorModule : public IModuleInterface
{
public:
    using FBakeProgress = TFunction<bool(float,const FText&)>;
    using FBakeWorldHandler = TFunction<bool(UWorld&, FHellRunTacticalLabScenario&,
        FString& /* summary */, FString& /* error */, const FBakeProgress&)>;

    static IHellRunTacticalLabEditorModule& Get();
    static bool IsAvailable();

    virtual void SetBakeWorldHandler(FBakeWorldHandler InHandler) = 0;
    virtual void OpenLab() = 0;
    virtual bool BakeWorld(UWorld& World, FHellRunTacticalLabScenario& OutScenario,
        FString& OutSummary, FString& OutError,const FBakeProgress& Progress) = 0;
};
