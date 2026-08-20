#include "AssetDefinition_TacticalLabScenario.h"
#include "TacticalLabEditorToolkit.h"
#include "TacticalLabScenarioAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_TacticalLabScenario)

FText UAssetDefinition_TacticalLabScenario::GetAssetDisplayName() const
{
    return NSLOCTEXT("TacticalLab", "ScenarioAsset", "Tactical AI Scenario");
}

FLinearColor UAssetDefinition_TacticalLabScenario::GetAssetColor() const
{
    return FLinearColor(.85f,.08f,.04f);
}

TSoftClassPtr<UObject> UAssetDefinition_TacticalLabScenario::GetAssetClass() const
{
    return UTacticalLabScenarioAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_TacticalLabScenario::GetAssetCategories() const
{
    static const FAssetCategoryPath Categories[] = {
        FAssetCategoryPath(EAssetCategoryPaths::AI,
            NSLOCTEXT("TacticalLab", "AssetSubmenu", "Tactical Lab"), ECategoryMenuType::Section)};
    return Categories;
}

EAssetCommandResult UAssetDefinition_TacticalLabScenario::OpenAssets(
    const FAssetOpenArgs& OpenArgs) const
{
    for (UTacticalLabScenarioAsset* Asset : OpenArgs.LoadObjects<UTacticalLabScenarioAsset>())
    {
        TSharedRef<FTacticalLabEditorToolkit> Editor = MakeShared<FTacticalLabEditorToolkit>();
        Editor->Initialize(Asset, OpenArgs.ToolkitHost);
    }
    return EAssetCommandResult::Handled;
}
