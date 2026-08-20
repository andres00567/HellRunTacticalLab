#include "TacticalLabIntegrations.h"

namespace
{
FTacticalLabIntegrations::FPlanHandler GPlanHandler;
FTacticalLabIntegrations::FActionProducesFactHandler GActionProducesFactHandler;
}

void FTacticalLabIntegrations::SetPlanHandler(FPlanHandler InHandler)
{
    GPlanHandler = MoveTemp(InHandler);
}

void FTacticalLabIntegrations::SetActionProducesFactHandler(
    FActionProducesFactHandler InHandler)
{
    GActionProducesFactHandler = MoveTemp(InHandler);
}

bool FTacticalLabIntegrations::BuildPlan(const FTacticalLabPlanRequest& Request,
    FTacticalLabPlanResult& OutResult)
{
    return GPlanHandler && GPlanHandler(Request, OutResult);
}

bool FTacticalLabIntegrations::ActionProducesFact(const FSoftObjectPath& Domain,
    FName Action, FName Fact)
{
    return GActionProducesFactHandler &&
        GActionProducesFactHandler(Domain, Action, Fact);
}
