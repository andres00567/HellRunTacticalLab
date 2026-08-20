#pragma once

#include "CoreMinimal.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"

/** Pure deterministic tactical evaluator shared by the production policy
 * boundary and the editor/headless Lab world adapter. */
class HELLRUNTACTICALLAB_API FHellRunTacticalEvaluator
{
public:
    static bool ApplyPolicy(FHellRunTacticalScore& Score,
        const FHellRunTacticalPolicy& Policy,
        const FString& FriendlyLaneReason = FString());

    static FHellRunTacticalLabCandidateRecord EvaluateRoute(
        const FHellRunTacticalLabScenario& Scenario,
        const FHellRunTacticalLabEntity& Agent,
        const FHellRunTacticalLabEntity& Threat,
        const FHellRunTacticalLabRoute& Route,
        const FHellRunEnemySimulationProfile& Profile,
        FName DecisionId);

    static void GenerateCandidateRoutes(
        const FHellRunTacticalLabScenario& Scenario,
        const FHellRunTacticalLabEntity& Agent,
        const FHellRunTacticalLabEntity& Threat,
        FRandomStream& Random,
        TArray<FHellRunTacticalLabRoute>& OutRoutes);
};
