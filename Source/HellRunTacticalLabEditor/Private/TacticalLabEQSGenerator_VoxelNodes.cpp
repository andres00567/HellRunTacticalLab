#include "TacticalLabEQSGenerator_VoxelNodes.h"

#include "EngineUtils.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "HellRunVoxelNavVolume.h"
#include "TacticalLabEQSContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticalLabEQSGenerator,Log,All);

UTacticalLabEQSGenerator_VoxelNodes::UTacticalLabEQSGenerator_VoxelNodes(
    const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    ItemType=UEnvQueryItemType_Point::StaticClass();
    GenerateAround=UEnvQueryContext_Querier::StaticClass();
    CoverAgainst=UTacticalLabEQSContext_Threat::StaticClass();
    SearchRadius.DefaultValue=3000.0f;
    MaximumItems.DefaultValue=500;
    bAutoSortTests=true;
}

void UTacticalLabEQSGenerator_VoxelNodes::GenerateItems(
    FEnvQueryInstance& QueryInstance) const
{
    UObject* Owner=QueryInstance.Owner.Get();
    SearchRadius.BindData(Owner,QueryInstance.QueryID);
    MaximumItems.BindData(Owner,QueryInstance.QueryID);
    const float Radius=FMath::Max(100.0f,SearchRadius.GetValue());
    const int32 Limit=FMath::Clamp(MaximumItems.GetValue(),1,5000);
    TArray<FVector> Centers;
    if(!QueryInstance.PrepareContext(GenerateAround,Centers)||Centers.IsEmpty())return;
    TArray<FVector> CoverContexts;
    if(bCoverOnly&&(!QueryInstance.PrepareContext(CoverAgainst,CoverContexts)||
        CoverContexts.IsEmpty()))return;
    UWorld* World=Owner?Owner->GetWorld():nullptr;
    if(!World)return;
    TArray<FVector> Items;
    Items.Reserve(Limit);
    const ATacticalLabEQSQuerier* TacticalQuerier=Cast<ATacticalLabEQSQuerier>(Owner);
    const bool bCanWalk=!TacticalQuerier||TacticalQuerier->bCanWalk;
    const bool bCanClimb=TacticalQuerier&&TacticalQuerier->bCanClimb;
    const bool bCanFly=TacticalQuerier&&TacticalQuerier->bCanFly;
    for(const FVector& Center:Centers)
    {
        const int32 Remaining=Limit-Items.Num();
        if(Remaining<=0)break;
        for(TActorIterator<AHellRunVoxelNavVolume> It(World);It&&Items.Num()<Limit;++It)
        {
            if(!It->HasCurrentBakedNavigationData())continue;
            TArray<FVector> VolumeItems;
            if(bCoverOnly)
            {
                const FVector* NearestContext=&CoverContexts[0];
                for(const FVector& CandidateContext:CoverContexts)
                    if(FVector::DistSquared(Center,CandidateContext)<
                        FVector::DistSquared(Center,*NearestContext))
                        NearestContext=&CandidateContext;
                It->GetQueryCoverLocationsInRange(Center,*NearestContext,0.0f,
                    Radius,VolumeItems,Remaining,MaximumCoverFacingDot);
            }
            else
                It->GetQueryNodeLocationsInRange(Center,0.0f,Radius,
                    bCanWalk,bCanClimb,bCanFly,VolumeItems,Remaining);
            Items.Append(VolumeItems);
        }
    }
    UE_LOG(LogTacticalLabEQSGenerator,Verbose,
        TEXT("[TacticalLabEQS] generator query=%d centers=%d cover=%s items=%d radius=%.0f"),
        QueryInstance.QueryID,Centers.Num(),bCoverOnly?TEXT("true"):TEXT("false"),
        Items.Num(),Radius);
    QueryInstance.AddItemData<UEnvQueryItemType_Point>(Items);
}

FText UTacticalLabEQSGenerator_VoxelNodes::GetDescriptionTitle() const
{
    return FText::FromString(TEXT("Tactical Lab: baked voxel navigation nodes"));
}

FText UTacticalLabEQSGenerator_VoxelNodes::GetDescriptionDetails() const
{
    return FText::FromString(FString::Printf(TEXT("radius %s, maximum %s, %s"),
        *SearchRadius.ToString(),*MaximumItems.ToString(),
        bCoverOnly?TEXT("baked wall-adjacent ground nodes only"):TEXT("current voxel bake nodes")));
}
