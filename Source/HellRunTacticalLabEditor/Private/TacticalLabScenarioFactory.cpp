#include "TacticalLabScenarioFactory.h"
#include "TacticalLabScenarioAsset.h"
#include "Core/AI/TacticalLab/HellRunTacticalLab.h"

UTacticalLabScenarioFactory::UTacticalLabScenarioFactory()
{
    SupportedClass = UTacticalLabScenarioAsset::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

UObject* UTacticalLabScenarioFactory::FactoryCreateNew(UClass* Class,
    UObject* InParent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    UTacticalLabScenarioAsset* Asset = NewObject<UTacticalLabScenarioAsset>(
        InParent, Class, Name, Flags | RF_Transactional);
    Asset->Scenario.ScenarioId = Name;
    Asset->Scenario.Profiles = FHellRunTacticalLab::GetBuiltInProfiles();
    return Asset;
}
