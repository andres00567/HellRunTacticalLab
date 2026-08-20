#include "STacticalLabGoalGraph.h"

#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
struct FBand
{
    const TCHAR* Title;
    FLinearColor Fill;
    FLinearColor Accent;
    TArray<FString> Defaults;
};

const TArray<FBand>& Bands()
{
    static const TArray<FBand> Value = {
        {TEXT("GOALS"),{.12f,.018f,.014f,1},{1,.17f,.12f,1},{TEXT("Eliminate Player Squad"),TEXT("Secure Objective"),TEXT("Regroup")}},
        {TEXT("WORLD FACTS"),{.012f,.065f,.105f,1},{.15f,.55f,1,1},{TEXT("Enemy Spotted"),TEXT("Player In LOS"),TEXT("Cover Available")}},
        {TEXT("APPROVED TACTICAL FACTS"),{.006f,.105f,.105f,1},{.08f,.82f,.85f,1},{TEXT("Firing Lane"),TEXT("Candidate Approved"),TEXT("Route Available")}},
        {TEXT("SQUAD FACTS"),{.07f,.027f,.10f,1},{.63f,.35f,.85f,1},{TEXT("Movement Granted"),TEXT("Squad Role"),TEXT("Reservation Clear")}},
        {TEXT("ACTIONS"),{.105f,.07f,.006f,1},{1,.62f,.08f,1},{TEXT("Move To Candidate"),TEXT("Take Cover"),TEXT("Aim At Target")}},
        {TEXT("RESULTS"),{.018f,.09f,.025f,1},{.25f,.75f,.28f,1},{TEXT("At Candidate"),TEXT("Behind Cover"),TEXT("Goal Progress")}}
    };
    return Value;
}
}

void STacticalLabGoalGraph::Construct(const FArguments&)
{
    SetClipping(EWidgetClipping::ClipToBounds);
}

void STacticalLabGoalGraph::SetLifetime(const FHellRunTacticalLabLifetime* InLifetime,
    const FString& InStatus,const FName InAgentId)
{
    Status = InStatus;AgentId=InAgentId;Plan.Reset();Goal=NAME_None;bHasDecision=false;
    WorldFacts.Reset();CandidateFacts.Reset();SquadFacts.Reset();Results.Reset();
    if (InLifetime && !InLifetime->Decisions.IsEmpty())
    {
        const FHellRunTacticalLabDecision* Decision=nullptr;
        for(int32 Index=InLifetime->Decisions.Num()-1;Index>=0;--Index)
            if(InAgentId.IsNone()||InLifetime->Decisions[Index].AgentId==InAgentId)
            {Decision=&InLifetime->Decisions[Index];break;}
        if(!Decision)Decision=&InLifetime->Decisions.Last();
        bHasDecision=true;
        AgentId=Decision->AgentId;Plan=Decision->GOAPPlan;Goal=Decision->Intent;
        for(const TPair<FName,FString>& Fact:Decision->FactProvenance)
        {
            const FString Label=FString::Printf(TEXT("%s: %s"),*Fact.Key.ToString(),*Fact.Value);
            if(Fact.Key.ToString().Contains(TEXT("Squad"))||Fact.Key.ToString().Contains(TEXT("Movement")))
                SquadFacts.Add(Label);
            else WorldFacts.Add(Label);
        }
        TArray<const FHellRunTacticalLabCandidateRecord*> Ranked;
        for(const FHellRunTacticalLabCandidateRecord& Candidate:InLifetime->Candidates)
            if(Candidate.AgentId==Decision->AgentId&&Candidate.DecisionId==Decision->DecisionId)
                Ranked.Add(&Candidate);
        Ranked.Sort([](const FHellRunTacticalLabCandidateRecord& A,
            const FHellRunTacticalLabCandidateRecord& B)
            {return A.Score.FinalScore>B.Score.FinalScore;});
        for(int32 Index=0;Index<FMath::Min(3,Ranked.Num());++Index)
            CandidateFacts.Add(FString::Printf(TEXT("%s  %.2f  %s"),
                *Ranked[Index]->CandidateId.ToString(),Ranked[Index]->Score.FinalScore,
                Ranked[Index]->Score.bAccepted?TEXT("accepted"):TEXT("rejected")));
        Results={FString::Printf(TEXT("Candidate: %s"),*Decision->SelectedCandidateId.ToString()),
            FString::Printf(TEXT("Route: %s"),*Decision->SelectedRouteId.ToString()),Decision->Reason};
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D STacticalLabGoalGraph::ComputeDesiredSize(float) const
{
    return FVector2D(560,720);
}

int32 STacticalLabGoalGraph::OnPaint(const FPaintArgs&,const FGeometry& G,
    const FSlateRect&,FSlateWindowElementList& E,int32 Layer,
    const FWidgetStyle&,bool)const
{
    const FSlateBrush* White=FAppStyle::GetBrush(TEXT("WhiteBrush"));
    const FVector2D Size=G.GetLocalSize();
    FSlateDrawElement::MakeBox(E,Layer++,G.ToPaintGeometry(),White,
        ESlateDrawEffect::None,FLinearColor(.004f,.008f,.011f,1));
    const float Header=34, BandH=FMath::Max(88.0f,(Size.Y-Header)/Bands().Num());
    const float Left=16, Gap=8;
    const float NodeW=FMath::Max(90.0f,(Size.X-Left*2-Gap*2)/3.0f);
    const float NodeH=FMath::Min(54.0f,BandH-32.0f);
    TArray<FVector2f> PreviousCenters;
    for(int32 Row=0;Row<Bands().Num();++Row)
    {
        const FBand& Band=Bands()[Row]; const float Y=Header+Row*BandH;
        FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(FVector2D(Size.X,BandH-1),
            FSlateLayoutTransform(FVector2D(0,Y))),White,ESlateDrawEffect::None,Band.Fill);
        FSlateDrawElement::MakeText(E,Layer+1,G.ToPaintGeometry(FVector2D(240,18),
            FSlateLayoutTransform(FVector2D(10,Y+5))),Band.Title,
            FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),9),ESlateDrawEffect::None,Band.Accent);
        TArray<FString> Labels=Band.Defaults;
        if(!bHasDecision&&!AgentId.IsNone())
        {
            Labels={Row==0?FString::Printf(TEXT("Agent: %s"),*AgentId.ToString())
                    :TEXT("Waiting for decision data"),
                Row==0?TEXT("Run or Step"):TEXT("—"),
                Row==0?TEXT("No decision yet"):TEXT("—")};
        }
        if(bHasDecision&&Row==0)
        {
            if(!Goal.IsNone())Labels[0]=Goal.ToString();
            if(!AgentId.IsNone())Labels[1]=FString::Printf(TEXT("Agent: %s"),*AgentId.ToString());
            Labels[2]=Plan.IsEmpty()?TEXT("No plan produced"):FString::Printf(TEXT("%d plan actions"),Plan.Num());
        }
        if(bHasDecision&&Row==1)for(int32 I=0;I<FMath::Min(3,WorldFacts.Num());++I)Labels[I]=WorldFacts[I];
        if(bHasDecision&&Row==2)for(int32 I=0;I<FMath::Min(3,CandidateFacts.Num());++I)Labels[I]=CandidateFacts[I];
        if(bHasDecision&&Row==3)for(int32 I=0;I<FMath::Min(3,SquadFacts.Num());++I)Labels[I]=SquadFacts[I];
        if(bHasDecision&&Row==4&&!Plan.IsEmpty())
            for(int32 I=0;I<FMath::Min(3,Plan.Num());++I)Labels[I]=Plan[I].ToString();
        if(bHasDecision&&Row==5)for(int32 I=0;I<FMath::Min(3,Results.Num());++I)Labels[I]=Results[I];
        TArray<FVector2f> Centers;
        for(int32 Col=0;Col<3;++Col)
        {
            const float X=Left+Col*(NodeW+Gap), NodeY=Y+25;
            const FVector2f Center(X+NodeW*.5f,NodeY+NodeH*.5f); Centers.Add(Center);
            if(!PreviousCenters.IsEmpty())
            {
                TArray<FVector2f> Line={PreviousCenters[Col],Center};
                FSlateDrawElement::MakeLines(E,Layer+1,G.ToPaintGeometry(),Line,
                    ESlateDrawEffect::None,FLinearColor(Band.Accent.R,Band.Accent.G,Band.Accent.B,.38f),true,1.2f);
            }
            FSlateDrawElement::MakeBox(E,Layer+2,G.ToPaintGeometry(FVector2D(NodeW,NodeH),
                FSlateLayoutTransform(FVector2D(X,NodeY))),White,ESlateDrawEffect::None,
                FLinearColor(Band.Accent.R*.20f,Band.Accent.G*.20f,Band.Accent.B*.20f,1));
            TArray<FVector2f> Outline={{X,NodeY},{X+NodeW,NodeY},{X+NodeW,NodeY+NodeH},{X,NodeY+NodeH},{X,NodeY}};
            FSlateDrawElement::MakeLines(E,Layer+3,G.ToPaintGeometry(),Outline,
                ESlateDrawEffect::None,Band.Accent,true,1.3f);
            FSlateDrawElement::MakeText(E,Layer+4,G.ToPaintGeometry(FVector2D(NodeW-12,NodeH-8),
                FSlateLayoutTransform(FVector2D(X+7,NodeY+8))),Labels[Col],
                FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9),ESlateDrawEffect::None,FLinearColor(.9f,.93f,.95f));
        }
        PreviousCenters=MoveTemp(Centers);
    }
    FSlateDrawElement::MakeText(E,Layer+5,G.ToPaintGeometry(FVector2D(Size.X-20,20),
        FSlateLayoutTransform(FVector2D(10,8))),Status,
        FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),9),ESlateDrawEffect::None,FLinearColor(.25f,.75f,.78f));
    return Layer+6;
}
