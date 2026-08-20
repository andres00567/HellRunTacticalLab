#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

struct FHellRunTacticalLabLifetime;

/** Compact, clipped live provenance graph matching the visual language of the lab. */
class STacticalLabGoalGraph final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(STacticalLabGoalGraph) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&);
    void SetLifetime(const FHellRunTacticalLabLifetime* InLifetime,
        const FString& InStatus,FName InAgentId=NAME_None);
    virtual FVector2D ComputeDesiredSize(float) const override;
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry,
        const FSlateRect&, FSlateWindowElementList&, int32 Layer,
        const FWidgetStyle&, bool) const override;

private:
    TArray<FName> Plan;
    FName Goal;
    FName AgentId;
    TArray<FString> WorldFacts;
    TArray<FString> CandidateFacts;
    TArray<FString> SquadFacts;
    TArray<FString> Results;
    bool bHasDecision = false;
    FString Status;
};
