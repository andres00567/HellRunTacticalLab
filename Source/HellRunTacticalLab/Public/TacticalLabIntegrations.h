#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UWorld;
class APawn;

struct HELLRUNTACTICALLAB_API FTacticalLabPlanRequest
{
    FSoftObjectPath Domain;
    FName CaseName;
    TMap<FName, bool> BoolFacts;
    TMap<FName, float> FloatFacts;
};

struct HELLRUNTACTICALLAB_API FTacticalLabPlanResult
{
    bool bSucceeded = false;
    FName SelectedGoal;
    TArray<FName> Plan;
    float Cost = 0.0f;
    int32 ExpandedNodes = 0;
    FString FailureReason;
    TMap<FName,float> GoalScores;
    TMap<FName,FString> GoalReasons;
};

/** Bounded, host-neutral projection of the runtime encounter Director. */
struct HELLRUNTACTICALLAB_API FTacticalLabDirectorDebugSnapshot
{
    FString Phase;
    FString CadenceAction;
    FName CadenceGoal;
    FString CadenceReason;
    float CadenceCommitmentRemaining = 0.0f;
    float Intensity = 0.0f;
    float EnemyPressure = 0.0f;
    float TeamPressure = 0.0f;
    float SpawnCapacity = 0.0f;
    float SpawnDelayRemaining = 0.0f;
    float RecycleDelayRemaining = 0.0f;
    int32 AliveSurvivorCount = 0;
    int32 LiveEnemyCount = 0;
    int32 EngagedEnemyCount = 0;
    int32 TotalEnemiesSpawned = 0;
    int32 TargetedPlayerCount = 0;
    int32 ActiveAttackReservationCount = 0;
    int32 MaxAttackReservationsOnSingleTarget = 0;
    int32 MaxAttackersPerTarget = 0;
    int32 IdleEnemyCount = 0;
    int32 InvestigatingEnemyCount = 0;
    int32 ChasingEnemyCount = 0;
    int32 AttackingEnemyCount = 0;
    int32 StunnedEnemyCount = 0;
    int32 StateTreeRunningEnemyCount = 0;
    int32 StateTreeStoppedEnemyCount = 0;
    int32 NoControllerEnemyCount = 0;
    int32 NoTargetEnemyCount = 0;
    int32 TargetInRangeEnemyCount = 0;
    int32 HasAttackSlotEnemyCount = 0;
    int32 MeleeReadyEnemyCount = 0;
    int32 MissingAttackMontageEnemyCount = 0;
    int32 UsefulPressureEnemyCount = 0;
    int32 DesiredPressureEnemyCount = 0;
    int32 StaleTailEnemyCount = 0;
    int32 PendingRecycleEnemyCount = 0;
    int32 LastRecycledEnemyCount = 0;
    int32 TotalRecycledEnemyCount = 0;
    int32 NativePlannerEnemyCount = 0;
    int32 LastPlannerBatchAgentCount = 0;
    float LastPlannerBatchMilliseconds = 0.0f;
    bool bPlannerBatchActive = false;
    bool bHordeActive = false;
    bool bMobActive = false;
    FString RecycleBlockedReason;
};

/** Optional host-owned navigation diagnostics not exposed by PathFollowing. */
struct HELLRUNTACTICALLAB_API FTacticalLabAgentRuntimeDebugSnapshot
{
    TArray<FVector> RetainedRoute;
    FString RouteProvider;
    FString RouteAdmission;
    float RouteExposure = -1.0f;
};

/** Optional host-game integrations keep the commercial lab independent of any
 * particular planner implementation. */
class HELLRUNTACTICALLAB_API FTacticalLabIntegrations
{
public:
    using FPlanHandler = TFunction<bool(const FTacticalLabPlanRequest&,
        FTacticalLabPlanResult&)>;
    using FActionProducesFactHandler = TFunction<bool(const FSoftObjectPath&,
        FName /* action */, FName /* fact */)>;
    using FDirectorDebugHandler = TFunction<bool(UWorld&,
        FTacticalLabDirectorDebugSnapshot&)>;
    using FAgentRuntimeDebugHandler = TFunction<bool(const APawn&,
        FTacticalLabAgentRuntimeDebugSnapshot&)>;

    static void SetPlanHandler(FPlanHandler InHandler);
    static void SetActionProducesFactHandler(FActionProducesFactHandler InHandler);
    static void SetDirectorDebugHandler(FDirectorDebugHandler InHandler);
    static void SetAgentRuntimeDebugHandler(FAgentRuntimeDebugHandler InHandler);
    static bool BuildPlan(const FTacticalLabPlanRequest& Request,
        FTacticalLabPlanResult& OutResult);
    static bool ActionProducesFact(const FSoftObjectPath& Domain,
        FName Action, FName Fact);
    static bool CaptureDirectorDebug(UWorld& World,
        FTacticalLabDirectorDebugSnapshot& OutSnapshot);
    static bool CaptureAgentRuntimeDebug(const APawn& Pawn,
        FTacticalLabAgentRuntimeDebugSnapshot& OutSnapshot);
};
