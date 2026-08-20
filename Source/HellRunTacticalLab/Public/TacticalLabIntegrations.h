#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

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

/** Optional host-game integrations keep the commercial lab independent of any
 * particular planner implementation. */
class HELLRUNTACTICALLAB_API FTacticalLabIntegrations
{
public:
    using FPlanHandler = TFunction<bool(const FTacticalLabPlanRequest&,
        FTacticalLabPlanResult&)>;
    using FActionProducesFactHandler = TFunction<bool(const FSoftObjectPath&,
        FName /* action */, FName /* fact */)>;

    static void SetPlanHandler(FPlanHandler InHandler);
    static void SetActionProducesFactHandler(FActionProducesFactHandler InHandler);
    static bool BuildPlan(const FTacticalLabPlanRequest& Request,
        FTacticalLabPlanResult& OutResult);
    static bool ActionProducesFact(const FSoftObjectPath& Domain,
        FName Action, FName Fact);
};
