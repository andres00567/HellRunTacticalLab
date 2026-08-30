#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "TacticalLabToolset.generated.h"

/** Tactical AI Lab scenario inspection and deterministic simulation operations. */
UCLASS(BlueprintType, Hidden)
class HELLRUNTACTICALLABEDITOR_API UTacticalLabToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Lists bounded Tactical Lab Scenario asset paths. */
    UFUNCTION(meta=(AICallable), Category="Tactical Lab|Tools")
    static FString ListScenarios(int32 MaxResults = 50);

    /** Inspects the authored entities, geometry, routes, hazards, traversal, and assertions of one scenario. */
    UFUNCTION(meta=(AICallable), Category="Tactical Lab|Tools")
    static FString InspectScenario(const FString& AssetPath);

    /** Runs one deterministic lifetime from an exact scenario asset and returns bounded decision evidence. */
    UFUNCTION(meta=(AICallable), Category="Tactical Lab|Tools")
    static FString RunScenarioLifetime(const FString& AssetPath, int32 Seed = 1337, int32 MaxDecisions = 25);
};
