#pragma once

#include "AssetDefinitionDefault.h"
#include "AssetDefinition_TacticalLabScenario.generated.h"

UCLASS()
class UAssetDefinition_TacticalLabScenario final : public UAssetDefinitionDefault
{
    GENERATED_BODY()
public:
    virtual FText GetAssetDisplayName() const override;
    virtual FLinearColor GetAssetColor() const override;
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
    virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
