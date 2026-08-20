#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "TacticalLabEQSTest_CoverLOS.generated.h"

/**
 * Requires real geometry between a voxel cover candidate and the threat.
 * Pawns are deliberately ignored so a target capsule cannot masquerade as cover.
 */
UCLASS(meta=(DisplayName="Hell Run Cover Occlusion"))
class UTacticalLabEQSTest_CoverLOS final : public UEnvQueryTest
{
    GENERATED_BODY()
public:
    UTacticalLabEQSTest_CoverLOS(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditDefaultsOnly,Category="Cover")
    TSubclassOf<UEnvQueryContext> ThreatContext;

    UPROPERTY(EditDefaultsOnly,Category="Cover")
    FAIDataProviderFloatValue TraceHeight;

    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;
};
