#pragma once

#include "CoreMinimal.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"

class HELLRUNTACTICALLAB_API FHellRunTacticalLab
{
public:
    FHellRunTacticalLab();

    static TArray<FHellRunEnemySimulationProfile> GetBuiltInProfiles();
    static bool LoadScenario(const FString& Filename,
        FHellRunTacticalLabScenario& OutScenario, FString& OutError);
    static bool SaveScenario(const FString& Filename,
        const FHellRunTacticalLabScenario& Scenario, FString& OutError);
    static bool SaveLifetime(const FString& Filename,
        const FHellRunTacticalLabLifetime& Lifetime, FString& OutError);
    static bool SaveSummary(const FString& Filename,
        const FHellRunTacticalLabBatchSummary& Summary, FString& OutError);
    static bool SaveFailureReport(const FString& Filename,
        const FHellRunTacticalLabLifetime& Lifetime, FString& OutError);

    bool Initialize(const FHellRunTacticalLabScenario& InScenario,
        int32 Seed, int32 LifetimeIndex, FString& OutError);
    bool StepDecision();
    /** Advances the simulation clock. Movement can be frozen for analysis and
     * interactive playback can opt out of the one-maneuver batch terminal. */
    bool StepTick(float DeltaSeconds, bool bMoveAgents = true,
        bool bCompleteWhenMovementFinishes = true);
    FHellRunTacticalLabLifetime Finish();
    const FHellRunTacticalLabScenario& GetState() const { return State; }
    const FHellRunTacticalLabLifetime& GetLifetime() const { return Lifetime; }
    bool IsComplete() const { return bComplete; }
    bool HasActiveMovement() const { return HasMovement(); }

    static FHellRunTacticalLabLifetime RunLifetime(
        const FHellRunTacticalLabScenario& Scenario, int32 Seed,
        int32 LifetimeIndex = 0);
    static FHellRunTacticalLabBatchSummary RunBatch(
        const FHellRunTacticalLabScenario& Scenario, int32 BaseSeed,
        int32 Count, const FString& OutputDirectory = FString());

private:
    const FHellRunEnemySimulationProfile* FindProfile(FName ArchetypeId) const;
    const FHellRunTacticalLabEntity* FindThreat(
        const FHellRunTacticalLabEntity& Agent) const;
    void RecordEvent(FName Type, FName AgentId, FName DecisionId,
        const FString& Detail);
    void EvaluateAssertions();
    void AddFailure(FName Tag);
    bool HasMovement() const;

    FHellRunTacticalLabScenario InitialScenario;
    FHellRunTacticalLabScenario State;
    FHellRunTacticalLabLifetime Lifetime;
    FRandomStream Random;
    TMap<FName, FVector2D> ActiveDestinations;
    TMap<FName, FName> ActiveRouteIds;
    TMap<FName, TArray<FVector2D>> ActiveRoutes;
    TMap<FName, int32> ActiveRoutePointIndices;
    float Time = 0.0f;
    int32 DecisionEpoch = 0;
    int32 IdleDecisionCount = 0;
    bool bComplete = false;
};
