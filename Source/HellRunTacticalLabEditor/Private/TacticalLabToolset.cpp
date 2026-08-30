#include "TacticalLabToolset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/AI/TacticalLab/HellRunTacticalLab.h"
#include "Modules/ModuleManager.h"
#include "TacticalLabScenarioAsset.h"

FString UTacticalLabToolset::ListScenarios(const int32 MaxResults)
{
    const int32 Limit = FMath::Clamp(MaxResults, 1, 200);
    TArray<FAssetData> Assets;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get()
        .GetAssetsByClass(UTacticalLabScenarioAsset::StaticClass()->GetClassPathName(), Assets, true);
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
    FString Result = FString::Printf(TEXT("Tactical Lab Scenarios: %d total; returning up to %d\n"), Assets.Num(), Limit);
    for (int32 Index = 0; Index < FMath::Min(Limit, Assets.Num()); ++Index) Result += Assets[Index].GetObjectPathString() + TEXT("\n");
    return Result;
}

FString UTacticalLabToolset::InspectScenario(const FString& AssetPath)
{
    const UTacticalLabScenarioAsset* Asset = LoadObject<UTacticalLabScenarioAsset>(nullptr, *AssetPath);
    if (!Asset) return FString::Printf(TEXT("ERROR: Tactical Lab Scenario not found: %s"), *AssetPath);
    const FHellRunTacticalLabScenario& Scenario = Asset->Scenario;
    return FString::Printf(TEXT("Asset=%s Scenario=%s Version=%d Seed=%d MaxDuration=%.2f SourceMap=%s "
        "Profiles=%d Entities=%d Obstacles=%d Hazards=%d Shapes=%d Routes=%d TraversalEdges=%d Assertions=%d ExpectedTags=%d"),
        *Asset->GetPathName(), *Scenario.ScenarioId.ToString(), Scenario.ScenarioVersion, Scenario.DefaultSeed,
        Scenario.MaximumDurationSeconds, *Asset->SourceMap.ToString(), Scenario.Profiles.Num(), Scenario.Entities.Num(),
        Scenario.Obstacles.Num(), Scenario.Hazards.Num(), Scenario.Shapes.Num(), Scenario.Routes.Num(),
        Scenario.TraversalEdges.Num(), Scenario.Assertions.Num(), Scenario.ExpectedBehaviorTags.Num());
}

FString UTacticalLabToolset::RunScenarioLifetime(const FString& AssetPath, const int32 Seed, const int32 MaxDecisions)
{
    const UTacticalLabScenarioAsset* Asset = LoadObject<UTacticalLabScenarioAsset>(nullptr, *AssetPath);
    if (!Asset) return FString::Printf(TEXT("ERROR: Tactical Lab Scenario not found: %s"), *AssetPath);
    const FHellRunTacticalLabLifetime Lifetime = FHellRunTacticalLab::RunLifetime(Asset->Scenario, Seed);
    const int32 Limit = FMath::Clamp(MaxDecisions, 0, 100);
    FString Result = FString::Printf(TEXT("Scenario=%s Seed=%d Result=%s Duration=%.3f Decisions=%d Candidates=%d Failures=%s\n"),
        *Lifetime.ScenarioId.ToString(), Lifetime.Seed,
        *StaticEnum<EHellRunTacticalLabResult>()->GetNameStringByValue(static_cast<int64>(Lifetime.Result)),
        Lifetime.DurationSeconds, Lifetime.Decisions.Num(), Lifetime.Candidates.Num(),
        *FString::JoinBy(Lifetime.FailureTags, TEXT(","), [](const FName Name) { return Name.ToString(); }));
    for (int32 Index = 0; Index < FMath::Min(Limit, Lifetime.Decisions.Num()); ++Index)
    {
        const FHellRunTacticalLabDecision& Decision = Lifetime.Decisions[Index];
        Result += FString::Printf(TEXT("- t=%.2f agent=%s intent=%s candidate=%s route=%s reason=%s\n"),
            Decision.Time, *Decision.AgentId.ToString(), *Decision.Intent.ToString(), *Decision.SelectedCandidateId.ToString(),
            *Decision.SelectedRouteId.ToString(), *Decision.Reason);
    }
    return Result;
}
