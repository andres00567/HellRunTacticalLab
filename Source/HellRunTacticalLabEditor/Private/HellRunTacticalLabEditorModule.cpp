#include "IHellRunTacticalLabEditorModule.h"
#include "TacticalLabEditorToolkit.h"
#include "TacticalLabScenarioAsset.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FHellRunTacticalLabEditorModule final : public IHellRunTacticalLabEditorModule
{
public:
    virtual void StartupModule() override
    {
        DiagnosticBakeCommand=IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("HellRun.TacticalLab.BakeDiagnostic"),
            TEXT("Opens the real Tactical Lab editor and invokes its Bake Map action."),
            FConsoleCommandDelegate::CreateRaw(this,
                &FHellRunTacticalLabEditorModule::OpenLabAndBakeDiagnostic),
            ECVF_Default);
        DiagnosticPlaygroundCommand=IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("HellRun.TacticalLab.PlaygroundDiagnostic"),
            TEXT("Opens the real lab, bakes the map, configures goals, and exercises Step."),
            FConsoleCommandDelegate::CreateRaw(this,
                &FHellRunTacticalLabEditorModule::OpenLabAndPlaygroundDiagnostic),ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if(DiagnosticBakeCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(DiagnosticBakeCommand);
            DiagnosticBakeCommand=nullptr;
        }
        if(DiagnosticPlaygroundCommand)
        {IConsoleManager::Get().UnregisterConsoleObject(DiagnosticPlaygroundCommand);
            DiagnosticPlaygroundCommand=nullptr;}
    }

    virtual void SetBakeWorldHandler(FBakeWorldHandler InHandler) override
    {
        BakeWorldHandler = MoveTemp(InHandler);
    }

    virtual void OpenLab() override
    {
        UTacticalLabScenarioAsset* Asset = NewObject<UTacticalLabScenarioAsset>(
            GetTransientPackage(), TEXT("TacticalAILabSession"), RF_Transient|RF_Transactional);
        Asset->Scenario.ScenarioId = TEXT("Unsaved_Scenario");
        Asset->Scenario.Profiles = FHellRunTacticalLab::GetBuiltInProfiles();
        TSharedRef<FTacticalLabEditorToolkit> Editor =
            MakeShared<FTacticalLabEditorToolkit>();
        Editor->Initialize(Asset, nullptr);
    }

    virtual bool BakeWorld(UWorld& World, FHellRunTacticalLabScenario& OutScenario,
        FString& OutSummary, FString& OutError,const FBakeProgress& Progress) override
    {
        if (!BakeWorldHandler)
        {
            OutError = TEXT("No production-map baker is registered for this project.");
            return false;
        }
        return BakeWorldHandler(World, OutScenario, OutSummary, OutError,Progress);
    }

private:
    void OpenLabAndBakeDiagnostic()
    {
        UTacticalLabScenarioAsset* Asset=NewObject<UTacticalLabScenarioAsset>(
            GetTransientPackage(),TEXT("TacticalAILabDiagnosticSession"),
            RF_Transient|RF_Transactional);
        Asset->Scenario.ScenarioId=TEXT("Unsaved_Scenario");
        Asset->Scenario.Profiles=FHellRunTacticalLab::GetBuiltInProfiles();
        TSharedRef<FTacticalLabEditorToolkit> Editor=MakeShared<FTacticalLabEditorToolkit>();
        Editor->Initialize(Asset,nullptr);
        Editor->RunBakeDiagnostic();
    }

    void OpenLabAndPlaygroundDiagnostic()
    {
        UTacticalLabScenarioAsset* Asset=NewObject<UTacticalLabScenarioAsset>(
            GetTransientPackage(),TEXT("TacticalAILabPlaygroundDiagnostic"),
            RF_Transient|RF_Transactional);
        Asset->Scenario.ScenarioId=TEXT("Unsaved_Scenario");
        Asset->Scenario.Profiles=FHellRunTacticalLab::GetBuiltInProfiles();
        TSharedRef<FTacticalLabEditorToolkit> Editor=MakeShared<FTacticalLabEditorToolkit>();
        Editor->Initialize(Asset,nullptr);Editor->RunPlaygroundDiagnostic();
    }

    FBakeWorldHandler BakeWorldHandler;
    IConsoleObject* DiagnosticBakeCommand=nullptr;
    IConsoleObject* DiagnosticPlaygroundCommand=nullptr;
};

IMPLEMENT_MODULE(FHellRunTacticalLabEditorModule, HellRunTacticalLabEditor)

IHellRunTacticalLabEditorModule& IHellRunTacticalLabEditorModule::Get()
{
    return FModuleManager::LoadModuleChecked<IHellRunTacticalLabEditorModule>(
        TEXT("HellRunTacticalLabEditor"));
}

bool IHellRunTacticalLabEditorModule::IsAvailable()
{
    return FModuleManager::Get().IsModuleLoaded(TEXT("HellRunTacticalLabEditor"));
}
