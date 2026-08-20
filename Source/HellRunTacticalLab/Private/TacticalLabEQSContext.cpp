#include "TacticalLabEQSContext.h"

#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"

void UTacticalLabEQSContext_Threat::ProvideContext(FEnvQueryInstance& QueryInstance,
    FEnvQueryContextData& ContextData) const
{
    if(const ATacticalLabEQSQuerier* Querier=
        Cast<ATacticalLabEQSQuerier>(QueryInstance.Owner.Get()))
        UEnvQueryItemType_Point::SetContextHelper(ContextData,Querier->ThreatLocation);
}
