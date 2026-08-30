#include "TacticalLabIntegrations.h"

namespace
{
FTacticalLabIntegrations::FPlanHandler GPlanHandler;
FTacticalLabIntegrations::FActionProducesFactHandler GActionProducesFactHandler;
FTacticalLabIntegrations::FDirectorDebugHandler GDirectorDebugHandler;
FTacticalLabIntegrations::FAgentRuntimeDebugHandler GAgentRuntimeDebugHandler;
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

void FTacticalLabIntegrations::SetDirectorDebugHandler(
    FDirectorDebugHandler InHandler)
{
    GDirectorDebugHandler = MoveTemp(InHandler);
}

void FTacticalLabIntegrations::SetAgentRuntimeDebugHandler(
    FAgentRuntimeDebugHandler InHandler)
{
    GAgentRuntimeDebugHandler=MoveTemp(InHandler);
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

bool FTacticalLabIntegrations::CaptureDirectorDebug(UWorld& World,
    FTacticalLabDirectorDebugSnapshot& OutSnapshot)
{
    OutSnapshot={};
    return GDirectorDebugHandler&&GDirectorDebugHandler(World,OutSnapshot);
}

bool FTacticalLabIntegrations::CaptureAgentRuntimeDebug(const APawn& Pawn,
    FTacticalLabAgentRuntimeDebugSnapshot& OutSnapshot)
{
    OutSnapshot={};
    return GAgentRuntimeDebugHandler&&GAgentRuntimeDebugHandler(Pawn,OutSnapshot);
}
