#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "TacticalLabEQSTest_VoxelPath.generated.h"

/** Filters EQS items using Hell Run's authoritative voxel navigation graph. */
UCLASS(meta=(DisplayName="Hell Run Voxel Path Exists"))
class UTacticalLabEQSTest_VoxelPath final : public UEnvQueryTest
{
    GENERATED_BODY()
public:
    UTacticalLabEQSTest_VoxelPath(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditDefaultsOnly,Category="Voxel Navigation")
    TSubclassOf<UEnvQueryContext> Context;

    UPROPERTY(EditDefaultsOnly,Category="Voxel Navigation")
    FAIDataProviderBoolValue PathFromContext;

    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;
};
