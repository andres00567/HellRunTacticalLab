#include "TacticalLabEQSTest_CoverLOS.h"

#include "EngineUtils.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "GameFramework/Pawn.h"
#include "TacticalLabEQSContext.h"

UTacticalLabEQSTest_CoverLOS::UTacticalLabEQSTest_CoverLOS(
    const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    Cost=EEnvTestCost::High;
    ValidItemType=UEnvQueryItemType_VectorBase::StaticClass();
    TestPurpose=EEnvTestPurpose::FilterAndScore;
    FilterType=EEnvTestFilterType::Match;
    BoolValue.DefaultValue=true;
    SetWorkOnFloatValues(false);
    ThreatContext=UTacticalLabEQSContext_Threat::StaticClass();
    TraceHeight.DefaultValue=60.0f;
}

void UTacticalLabEQSTest_CoverLOS::RunTest(FEnvQueryInstance& QueryInstance) const
{
    UObject* Owner=QueryInstance.Owner.Get();
    if(!Owner||!QueryInstance.World)return;
    BoolValue.BindData(Owner,QueryInstance.QueryID);
    TraceHeight.BindData(Owner,QueryInstance.QueryID);
    const bool bWantsOcclusion=BoolValue.GetValue();
    const float Height=TraceHeight.GetValue();
    TArray<FVector> ThreatLocations;
    if(!QueryInstance.PrepareContext(ThreatContext,ThreatLocations)||ThreatLocations.IsEmpty())return;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalLabCoverLOS),false);
    if(const AActor* OwnerActor=Cast<AActor>(Owner))Params.AddIgnoredActor(OwnerActor);
    // Characters are not cover. Ignoring every pawn also prevents the target
    // capsule at the trace endpoint from turning every clear item into a hit.
    for(TActorIterator<APawn> It(QueryInstance.World);It;++It)Params.AddIgnoredActor(*It);

    for(FEnvQueryInstance::ItemIterator It(this,QueryInstance);It;++It)
    {
        const FVector Item=GetItemLocation(QueryInstance,It.GetIndex())+FVector(0,0,Height);
        bool bOccluded=false;
        for(const FVector& Threat:ThreatLocations)
        {
            FHitResult Hit;
            if(QueryInstance.World->LineTraceSingleByChannel(Hit,Item,
                Threat+FVector(0,0,Height),ECC_Visibility,Params))
            {bOccluded=true;break;}
        }
        It.SetScore(TestPurpose,FilterType,bOccluded,bWantsOcclusion);
    }
}

FText UTacticalLabEQSTest_CoverLOS::GetDescriptionTitle() const
{
    return FText::FromString(TEXT("World geometry occludes threat at crouch height"));
}

FText UTacticalLabEQSTest_CoverLOS::GetDescriptionDetails() const
{
    return FText::FromString(TEXT("ECC_Visibility line trace; ignores all pawn capsules"));
}
