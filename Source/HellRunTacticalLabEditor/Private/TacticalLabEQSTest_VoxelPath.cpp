#include "TacticalLabEQSTest_VoxelPath.h"

#include "EngineUtils.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "HellRunVoxelNavVolume.h"
#include "TacticalLabEQSContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticalLabEQSPathTest,Log,All);

UTacticalLabEQSTest_VoxelPath::UTacticalLabEQSTest_VoxelPath(
    const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    Context=UEnvQueryContext_Querier::StaticClass();
    PathFromContext.DefaultValue=true;
    Cost=EEnvTestCost::High;
    ValidItemType=UEnvQueryItemType_VectorBase::StaticClass();
    TestPurpose=EEnvTestPurpose::Filter;
    FilterType=EEnvTestFilterType::Match;
    BoolValue.DefaultValue=true;
    SetWorkOnFloatValues(false);
}

void UTacticalLabEQSTest_VoxelPath::RunTest(FEnvQueryInstance& QueryInstance) const
{
    UObject* Owner=QueryInstance.Owner.Get();
    if(!Owner||!QueryInstance.World)return;
    BoolValue.BindData(Owner,QueryInstance.QueryID);
    PathFromContext.BindData(Owner,QueryInstance.QueryID);
    const bool bWantsPath=BoolValue.GetValue();
    const bool bFromContext=PathFromContext.GetValue();

    TArray<FVector> ContextLocations;
    if(!QueryInstance.PrepareContext(Context,ContextLocations)||
        ContextLocations.IsEmpty())return;

    const ATacticalLabEQSQuerier* Querier=Cast<ATacticalLabEQSQuerier>(Owner);
    const bool bCanWalk=!Querier||Querier->bCanWalk;
    const bool bCanClimb=Querier&&Querier->bCanClimb;
    const bool bCanMantle=Querier&&Querier->bCanMantle;
    const bool bCanDrop=Querier&&Querier->bCanDrop;
    const bool bCanJump=Querier&&Querier->bCanJump;
    const bool bCanVault=Querier&&Querier->bCanVault;
    const bool bCanFly=Querier&&Querier->bCanFly;

    TArray<FVector> ItemLocations;
    TMap<int32,int32> ItemIndexToResult;
    ItemLocations.Reserve(QueryInstance.Items.Num());
    for(int32 ItemIndex=0;ItemIndex<QueryInstance.Items.Num();++ItemIndex)
    {
        if(!QueryInstance.Items[ItemIndex].IsValid())continue;
        ItemIndexToResult.Add(ItemIndex,ItemLocations.Num());
        ItemLocations.Add(GetItemLocation(QueryInstance,ItemIndex));
    }
    if(ItemLocations.IsEmpty())return;

    TArray<bool> Reachable;
    Reachable.Init(false,ItemLocations.Num());
    for(const FVector& ContextLocation:ContextLocations)
    {
        for(TActorIterator<AHellRunVoxelNavVolume> Volume(QueryInstance.World);Volume;++Volume)
        {
            if(!Volume->HasCurrentBakedNavigationData())continue;
            TArray<bool> VolumeReachable;
            if(bFromContext)
            {
                Volume->TestQueryPathReachability(ContextLocation,ItemLocations,
                    bCanWalk,bCanClimb,bCanMantle,bCanDrop,bCanJump,bCanVault,
                    bCanFly,VolumeReachable);
            }
            else
            {
                // Directed paths to the context require one graph traversal per
                // candidate. Native tactical queries use context-to-item.
                VolumeReachable.Init(false,ItemLocations.Num());
                for(int32 Index=0;Index<ItemLocations.Num();++Index)
                {
                    TArray<FVector> SingleGoal{ContextLocation};
                    TArray<bool> SingleResult;
                    Volume->TestQueryPathReachability(ItemLocations[Index],SingleGoal,
                        bCanWalk,bCanClimb,bCanMantle,bCanDrop,bCanJump,bCanVault,
                        bCanFly,SingleResult);
                    VolumeReachable[Index]=SingleResult.IsValidIndex(0)&&SingleResult[0];
                }
            }
            for(int32 Index=0;Index<Reachable.Num()&&Index<VolumeReachable.Num();++Index)
                Reachable[Index]=Reachable[Index]||VolumeReachable[Index];
        }
    }

    int32 ReachableCount=0;
    for(FEnvQueryInstance::ItemIterator It(this,QueryInstance);It;++It)
    {
        const int32* ResultIndex=ItemIndexToResult.Find(It.GetIndex());
        const bool bReachable=ResultIndex&&Reachable.IsValidIndex(*ResultIndex)
            &&Reachable[*ResultIndex];
        ReachableCount+=bReachable?1:0;
        It.SetScore(TestPurpose,FilterType,bReachable,bWantsPath);
    }
    UE_LOG(LogTacticalLabEQSPathTest,Verbose,
        TEXT("[TacticalLabEQS] voxel path query=%d inputs=%d reachable=%d contexts=%d"),
        QueryInstance.QueryID,ItemLocations.Num(),ReachableCount,ContextLocations.Num());
}

FText UTacticalLabEQSTest_VoxelPath::GetDescriptionTitle() const
{
    return FText::FromString(FString::Printf(TEXT("Voxel path exists %s %s"),
        PathFromContext.DefaultValue?TEXT("from"):TEXT("to"),
        *UEnvQueryTypes::DescribeContext(Context).ToString()));
}

FText UTacticalLabEQSTest_VoxelPath::GetDescriptionDetails() const
{
    return FText::FromString(TEXT("Uses the current directed typed-edge voxel graph and dynamic blockers"));
}
