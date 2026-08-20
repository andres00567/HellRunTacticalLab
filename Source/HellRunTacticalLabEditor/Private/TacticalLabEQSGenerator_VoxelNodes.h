#pragma once

#include "CoreMinimal.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "TacticalLabEQSGenerator_VoxelNodes.generated.h"

/** Generates real EQS point items from Hell Run's baked voxel graph. */
UCLASS(EditInlineNew, meta=(DisplayName="Tactical Lab: Voxel Navigation Nodes"))
class UTacticalLabEQSGenerator_VoxelNodes final : public UEnvQueryGenerator
{
    GENERATED_BODY()
public:
    UTacticalLabEQSGenerator_VoxelNodes(const FObjectInitializer& ObjectInitializer);
    virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;

    UPROPERTY(EditDefaultsOnly,Category="Generator")
    FAIDataProviderFloatValue SearchRadius;

    UPROPERTY(EditDefaultsOnly,Category="Generator")
    FAIDataProviderIntValue MaximumItems;

    UPROPERTY(EditDefaultsOnly,Category="Generator")
    TSubclassOf<UEnvQueryContext> GenerateAround;

    /** Threat/observer context used to select the protected side of cover. */
    UPROPERTY(EditDefaultsOnly,Category="Generator")
    TSubclassOf<UEnvQueryContext> CoverAgainst;

    UPROPERTY(EditDefaultsOnly,Category="Generator",meta=(ClampMin="-1.0",ClampMax="0.0"))
    float MaximumCoverFacingDot = -0.1f;

    /** Restrict generation to ground nodes carrying a baked wall normal. */
    UPROPERTY(EditDefaultsOnly,Category="Generator")
    bool bCoverOnly = false;
};
