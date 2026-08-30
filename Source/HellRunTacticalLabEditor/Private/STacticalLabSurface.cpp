#include "STacticalLabSurface.h"

#include "Algo/Reverse.h"
#include "TacticalLabScenarioAsset.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateImageBrush.h"

namespace
{
class FTacticalLabZoomLevels final : public FZoomLevelsContainer
{
public:
    FTacticalLabZoomLevels()
        : Amounts({.05f,.075f,.10f,.125f,.15f,.20f,.25f,.375f,.50f,.675f,
            .75f,.875f,1.0f,1.25f,1.5f,1.75f,2.0f,2.5f,3.0f,4.0f,5.0f,
            6.0f,8.0f,10.0f,12.0f}) {}

    virtual float GetZoomAmount(int32 Level) const override
    { return Amounts[FMath::Clamp(Level,0,Amounts.Num()-1)]; }
    virtual int32 GetNearestZoomLevel(float Amount) const override
    {
        for(int32 I=0;I<Amounts.Num();++I)if(Amount<=Amounts[I])return I;
        return Amounts.Num()-1;
    }
    virtual FText GetZoomText(int32 Level) const override
    {
        const float Amount=GetZoomAmount(Level);
        return FText::FromString(FMath::IsNearlyEqual(Amount,1.0f)
            ?TEXT("1:1"):FString::Printf(TEXT("%.0f%%"),Amount*100.0f));
    }
    virtual int32 GetNumZoomLevels() const override { return Amounts.Num(); }
    virtual int32 GetDefaultZoomLevel() const override { return 12; }
    virtual EGraphRenderingLOD::Type GetLOD(int32 Level) const override
    {
        const float Amount=GetZoomAmount(Level);
        if(Amount<.2f)return EGraphRenderingLOD::LowestDetail;
        if(Amount<.5f)return EGraphRenderingLOD::LowDetail;
        if(Amount<.8f)return EGraphRenderingLOD::MediumDetail;
        if(Amount<1.5f)return EGraphRenderingLOD::DefaultDetail;
        return EGraphRenderingLOD::FullyZoomedIn;
    }
    virtual EGraphZoomLimitHandling GetZoomLimitHandling() const override
    { return EGraphZoomLimitHandling::AllowLimitBreak; }
private:
    TArray<float> Amounts;
};
}

STacticalLabSurface::~STacticalLabSurface() = default;

void STacticalLabSurface::Construct(const FArguments& Args)
{
    SetZoomLevelsContainer<FTacticalLabZoomLevels>();
    SNodePanel::Construct();
    SetClipping(EWidgetClipping::ClipToBounds);
    OnScenarioChanged=Args._OnScenarioChanged;
    OnEntityMoved=Args._OnEntityMoved;
    OnEntitySelected=Args._OnEntitySelected;
    SetAsset(Args._Asset);
}

void STacticalLabSurface::SetEQSResults(TArray<FTacticalLabEQSItem> InItems,
    FString InQueryStatus,TOptional<FVector2D> InQueryOrigin)
{
    if(InItems.IsEmpty())EQSPaths.Reset();
    EQSItems=MoveTemp(InItems);EQSStatus=MoveTemp(InQueryStatus);EQSOrigin=InQueryOrigin;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::SetEQSPaths(TArray<TArray<FVector2D>> InPaths)
{
    EQSPaths=MoveTemp(InPaths);Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::SelectEntity(const int32 EntityIndex)
{
    const bool bValidPIEEntity=PIEFrame.IsSet()
        &&PIEFrame->Agents.IsValidIndex(EntityIndex);
    const bool bValidScenarioEntity=Asset.IsValid()
        &&Asset->Scenario.Entities.IsValidIndex(EntityIndex);
    if(!bValidPIEEntity&&!bValidScenarioEntity)return;
    Selection.Type=ESelectionType::Entity;
    Selection.Index=EntityIndex;
    Selection.SubIndex=INDEX_NONE;
    Selection.Handle=ETransformHandle::None;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::ClearEQSResults()
{
    EQSItems.Reset();EQSPaths.Reset();EQSStatus.Reset();EQSOrigin.Reset();Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::SetRuntimeRoutes(const FHellRunTacticalLabLifetime* Lifetime)
{
    RuntimeRoutes.Reset();RejectedRuntimeRoutes.Reset();
    RuntimeBlockingObstacleIds.Reset();RuntimeCandidates.Reset();
    if(Lifetime)
    {
        TSet<FName> SelectedCandidateIds;
        for(const FHellRunTacticalLabDecision& Decision:Lifetime->Decisions)
            if(!Decision.SelectedCandidateId.IsNone())
                SelectedCandidateIds.Add(Decision.SelectedCandidateId);
        TSet<FName> AddedCandidates;
        TSet<FName> AddedRoutes;
        FName SelectedAgentId;
        if(Selection.Type==ESelectionType::Entity&&Asset.IsValid()&&
            Asset->Scenario.Entities.IsValidIndex(Selection.Index))
            SelectedAgentId=Asset->Scenario.Entities[Selection.Index].Id;
        const auto AddCandidate=[&](const FHellRunTacticalLabCandidateRecord& Candidate)
        {
            if(AddedCandidates.Contains(Candidate.CandidateId))return;
            AddedCandidates.Add(Candidate.CandidateId);
            FTacticalLabEQSItem& Point=RuntimeCandidates.AddDefaulted_GetRef();
            Point.Position=Candidate.Position;
            Point.Label=Candidate.CandidateId.ToString();
            Point.Score=Candidate.Score.bAccepted
                ?FMath::Clamp(.5f+Candidate.Score.FinalScore/20.0f,0.0f,1.0f):0.0f;
            Point.bValid=Candidate.Score.bAccepted;
            const bool bSelected=SelectedCandidateIds.Contains(Candidate.CandidateId);
            if((bSelected||Candidate.Score.bAccepted)&&Candidate.RoutePoints.Num()>1&&
                !AddedRoutes.Contains(Candidate.RouteId))
            {
                RuntimeRoutes.Add(Candidate.RoutePoints);
                AddedRoutes.Add(Candidate.RouteId);
            }
            if(!Candidate.Score.bAccepted&&Candidate.RoutePoints.Num()>1&&
                (SelectedAgentId.IsNone()||Candidate.AgentId==SelectedAgentId))
            {
                // Keep the latest diagnostic set bounded; the canvas must never
                // become a second unfiltered lifetime log.
                if(RejectedRuntimeRoutes.Num()<24)
                    RejectedRuntimeRoutes.Add(Candidate.RoutePoints);
                if(!Candidate.BlockingObstacleId.IsNone())
                    RuntimeBlockingObstacleIds.Add(Candidate.BlockingObstacleId);
            }
        };
        // Selected decision paths must survive candidate display budgeting.
        for(const FHellRunTacticalLabCandidateRecord& Candidate:Lifetime->Candidates)
            if(SelectedCandidateIds.Contains(Candidate.CandidateId))AddCandidate(Candidate);
        for(int32 I=FMath::Max(0,Lifetime->Candidates.Num()-256);
            I<Lifetime->Candidates.Num();++I)AddCandidate(Lifetime->Candidates[I]);
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::SetRuntimeState(const FHellRunTacticalLabScenario* Scenario)
{
    RuntimeEntities=Scenario?Scenario->Entities:TArray<FHellRunTacticalLabEntity>();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::SetRuntimeEntities(
    const TArray<FHellRunTacticalLabEntity>* Entities)
{
    RuntimeEntities=Entities?*Entities:TArray<FHellRunTacticalLabEntity>();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::SetPIEFrame(const FTacticalLabPIEFrame* Frame)
{
    FGuid SelectedGuid;
    FName SelectedId=NAME_None;
    if(PIEFrame.IsSet()&&Selection.Type==ESelectionType::Entity&&
        PIEFrame->Agents.IsValidIndex(Selection.Index))
    {
        SelectedGuid=PIEFrame->Agents[Selection.Index].AgentId;
        SelectedId=PIEFrame->Agents[Selection.Index].Entity.Id;
    }
    PIEFrame=Frame?TOptional<FTacticalLabPIEFrame>(*Frame):TOptional<FTacticalLabPIEFrame>();
    if(PIEFrame.IsSet()&&!SelectedId.IsNone())
    {
        const int32 NewIndex=PIEFrame->Agents.IndexOfByPredicate(
            [SelectedGuid,SelectedId](const FTacticalLabPIEAgentSnapshot& Agent)
            {return SelectedGuid.IsValid()?Agent.AgentId==SelectedGuid:
                Agent.Entity.Id==SelectedId;});
        Selection=NewIndex==INDEX_NONE?FSelection{}:
            FSelection{ESelectionType::Entity,NewIndex,INDEX_NONE};
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::CenterOnWorld(const FVector2D WorldPosition)
{
    const FVector2f Size(GetCachedGeometry().GetLocalSize());
    SetViewOffset(ToGraph(WorldPosition)-Size/(2.0f*GetZoomAmount()));
    Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void STacticalLabSurface::EnsurePIEPlayersVisible(const FTacticalLabPIEFrame& Frame,
    const float EdgePadding)
{
    const FVector2f PanelSize(GetCachedGeometry().GetLocalSize());
    if(PanelSize.X<=1.0f||PanelSize.Y<=1.0f||!Frame.bHasPlayers) return;

    FVector2f BoundsMin(BIG_NUMBER,BIG_NUMBER);
    FVector2f BoundsMax(-BIG_NUMBER,-BIG_NUMBER);
    int32 PlayerCount=0;
    for(const FTacticalLabPIEAgentSnapshot& Agent:Frame.Agents)
    {
        if(Agent.Entity.Kind!=EHellRunTacticalLabEntityKind::Player) continue;
        const FVector2f Point=ToPanel(Agent.Entity.Position);
        BoundsMin.X=FMath::Min(BoundsMin.X,Point.X);
        BoundsMin.Y=FMath::Min(BoundsMin.Y,Point.Y);
        BoundsMax.X=FMath::Max(BoundsMax.X,Point.X);
        BoundsMax.Y=FMath::Max(BoundsMax.Y,Point.Y);
        ++PlayerCount;
    }
    if(PlayerCount==0)
    {
        BoundsMin=BoundsMax=ToPanel(Frame.PlayerGroupCenter);
    }

    const float Padding=FMath::Clamp(EdgePadding,24.0f,
        FMath::Min(PanelSize.X,PanelSize.Y)*0.35f);
    const bool bOutsideDeadZone=BoundsMin.X<Padding||BoundsMin.Y<Padding||
        BoundsMax.X>PanelSize.X-Padding||BoundsMax.Y>PanelSize.Y-Padding;
    if(!bOutsideDeadZone) return;

    // A follow correction is an intentional camera cut: put the complete player
    // group at the center instead of leaving the player pinned to an edge.
    const FVector2f BoundsCenter=(BoundsMin+BoundsMax)*0.5f;
    const FVector2f PanelShift=PanelSize*0.5f-BoundsCenter;

    if(!PanelShift.IsNearlyZero())
    {
        const FVector2f CurrentViewOffset=PanelCoordToGraphCoord(FVector2f::ZeroVector);
        SetViewOffset(CurrentViewOffset-PanelShift/GetZoomAmount());
        Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
    }
}

FVector2D STacticalLabSurface::PanelToWorld(const FGeometry& Geometry,
    FVector2D ScreenPosition) const
{
    const FVector2f Local(Geometry.AbsoluteToLocal(ScreenPosition));
    const FVector2f Graph=PanelCoordToGraphCoord(Local);
    return FVector2D(Graph)/WorldToGraphScale;
}

FReply STacticalLabSurface::OnMouseButtonDown(const FGeometry& Geometry,
    const FPointerEvent& MouseEvent)
{
    if(MouseEvent.GetEffectingButton()==EKeys::RightMouseButton)
        RightMouseDownScreen=MouseEvent.GetScreenSpacePosition();
    else if(MouseEvent.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        LeftMouseDownScreen=MouseEvent.GetScreenSpacePosition();
        LastDragWorld=PanelToWorld(Geometry,LeftMouseDownScreen);
        if(!PendingRoutePoints.IsEmpty())
        {
            ContextWorld=LastDragWorld;
            AddRoutePoint(false);
            return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse);
        }
        Selection=HitTest(Geometry,LeftMouseDownScreen);
        if(TransformMode==ETransformMode::Scale&&!CanScaleSelection())
            TransformMode=ETransformMode::Translate;
        if(TransformMode==ETransformMode::Rotate&&!CanRotateSelection())
            TransformMode=ETransformMode::Translate;
        OnEntitySelected.ExecuteIfBound(Selection.Type==ESelectionType::Entity
            ?Selection.Index:INDEX_NONE);
        if(!PIEFrame.IsSet()&&Asset.IsValid()&&Selection.Type==ESelectionType::Entity&&
            Asset->Scenario.Entities.IsValidIndex(Selection.Index)&&
            Asset->Scenario.Entities[Selection.Index].Kind==EHellRunTacticalLabEntityKind::Enemy)
            OnEntityMoved.ExecuteIfBound(Selection.Index,
                Asset->Scenario.Entities[Selection.Index].Position);
        bDraggingSelection=Selection.IsValid()&&!PIEFrame.IsSet();
        bSelectionMoved=false;
        Invalidate(EInvalidateWidgetReason::Paint);
        if(!bDraggingSelection)
            return SNodePanel::OnMouseButtonDown(Geometry,MouseEvent);
        return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse)
            .CaptureMouse(AsShared());
    }
    return SNodePanel::OnMouseButtonDown(Geometry,MouseEvent);
}

FReply STacticalLabSurface::OnMouseButtonUp(const FGeometry& Geometry,
    const FPointerEvent& MouseEvent)
{
    if(MouseEvent.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        if(!bDraggingSelection&&!DragTransaction&& !bSelectionMoved)
            return SNodePanel::OnMouseButtonUp(Geometry,MouseEvent);
        bDraggingSelection=false;
        DragTransaction.Reset();
        if(bSelectionMoved)Changed();
        bSelectionMoved=false;
        Selection.Handle=ETransformHandle::None;
        return FReply::Handled().ReleaseMouseCapture();
    }
    if(MouseEvent.GetEffectingButton()==EKeys::RightMouseButton&&
        FVector2D::Distance(RightMouseDownScreen,MouseEvent.GetScreenSpacePosition())<5.0)
    {
        ContextWorld=PanelToWorld(Geometry,MouseEvent.GetScreenSpacePosition());
        const FSelection Hit=HitTest(Geometry,MouseEvent.GetScreenSpacePosition());
        // Empty-space context clicks must not operate on a stale selection.
        Selection=Hit;
        OnEntitySelected.ExecuteIfBound(Selection.Type==ESelectionType::Entity
            ?Selection.Index:INDEX_NONE);
        if(PIEFrame.IsSet())
            return FReply::Handled().ReleaseMouseCapture();
        OpenContextMenu(MouseEvent.GetScreenSpacePosition());
        return FReply::Handled().ReleaseMouseCapture();
    }
    return SNodePanel::OnMouseButtonUp(Geometry,MouseEvent);
}

FReply STacticalLabSurface::OnMouseMove(const FGeometry& Geometry,
    const FPointerEvent& MouseEvent)
{
    HoverWorld=PanelToWorld(Geometry,MouseEvent.GetScreenSpacePosition());
    bHasHoverWorld=true;
    if(!PendingRoutePoints.IsEmpty())Invalidate(EInvalidateWidgetReason::Paint);
    if(bDraggingSelection&&MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
    {
        if(!bSelectionMoved&&FVector2D::Distance(LeftMouseDownScreen,
            MouseEvent.GetScreenSpacePosition())>=2.0)
        {
            DragTransaction=MakeUnique<FScopedTransaction>(
                FText::FromString(TransformMode==ETransformMode::Translate
                    ?TEXT("Move Tactical Lab Item"):TransformMode==ETransformMode::Rotate
                    ?TEXT("Rotate Tactical Lab Item"):TEXT("Scale Tactical Lab Item")));
            if(Asset.IsValid())Asset->Modify();
            bSelectionMoved=true;
        }
        if(bSelectionMoved)
        {
            const FVector2D World=PanelToWorld(Geometry,MouseEvent.GetScreenSpacePosition());
            TransformSelection(World,World-LastDragWorld);
            LastDragWorld=World;
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        return FReply::Handled();
    }
    return SNodePanel::OnMouseMove(Geometry,MouseEvent);
}

FReply STacticalLabSurface::OnMouseButtonDoubleClick(const FGeometry& Geometry,
    const FPointerEvent& MouseEvent)
{
    if(MouseEvent.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        ContextWorld=PanelToWorld(Geometry,MouseEvent.GetScreenSpacePosition());
        if(!PendingRoutePoints.IsEmpty())
        {
            AddRoutePoint(false);FinishRoute();
            return FReply::Handled();
        }
        Selection=HitTest(Geometry,MouseEvent.GetScreenSpacePosition());
        if(Selection.Type==ESelectionType::RouteSegment)
        {
            InsertPointOnSelectedRoute();
            return FReply::Handled();
        }
    }
    return SNodePanel::OnMouseButtonDoubleClick(Geometry,MouseEvent);
}

void STacticalLabSurface::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
    bDraggingSelection=false;
    bSelectionMoved=false;
    DragTransaction.Reset();
    SNodePanel::OnMouseCaptureLost(CaptureLostEvent);
}

FReply STacticalLabSurface::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& KeyEvent)
{
    if(KeyEvent.GetKey()==EKeys::W||KeyEvent.GetKey()==EKeys::E||
        KeyEvent.GetKey()==EKeys::R)
    {
        if((KeyEvent.GetKey()==EKeys::E&&!CanRotateSelection())||
            (KeyEvent.GetKey()==EKeys::R&&!CanScaleSelection()))
            return FReply::Handled();
        TransformMode=KeyEvent.GetKey()==EKeys::W?ETransformMode::Translate:
            KeyEvent.GetKey()==EKeys::E?ETransformMode::Rotate:ETransformMode::Scale;
        Selection.Handle=ETransformHandle::None;
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }
    if(KeyEvent.GetKey()==EKeys::Enter&&!PendingRoutePoints.IsEmpty())
    {
        FinishRoute();return FReply::Handled();
    }
    if(KeyEvent.GetKey()==EKeys::BackSpace&&!PendingRoutePoints.IsEmpty())
    {
        PendingRoutePoints.Pop();
        if(PendingRoutePoints.IsEmpty())CancelRoute();
        else Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }
    if(KeyEvent.GetKey()==EKeys::Delete||KeyEvent.GetKey()==EKeys::BackSpace)
    {
        RemoveSelected();
        return FReply::Handled();
    }
    if(KeyEvent.GetKey()==EKeys::Escape)
    {
        bDraggingSelection=false;bSelectionMoved=false;DragTransaction.Reset();
        Selection.Reset();CancelRoute();
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled().ReleaseMouseCapture();
    }
    return SNodePanel::OnKeyDown(Geometry,KeyEvent);
}

STacticalLabSurface::FSelection STacticalLabSurface::HitTest(
    const FGeometry& Geometry,FVector2D ScreenPosition) const
{
    FSelection Best;
    if(!Asset.IsValid())return Best;
    const FVector2f Local(Geometry.AbsoluteToLocal(ScreenPosition));
    float BestPixels=14.0f;
    auto Point=[&](FVector2D World,ESelectionType Type,int32 Index,
        int32 SubIndex=INDEX_NONE,ETransformHandle Handle=ETransformHandle::None)
    {
        const float D=FVector2f::Distance(Local,ToPanel(World));
        if(D<BestPixels){BestPixels=D;Best={Type,Index,SubIndex,Handle};}
    };

    // Match Unreal viewport behavior: the selected object's active W/E/R
    // gizmo receives input before scene geometry beneath it.
    FVector2D GizmoPivot;
    if(Selection.IsValid()&&GetSelectionPivot(GizmoPivot))
    {
        const double AxisWorld=58.0/(WorldToGraphScale*GetZoomAmount());
        if(TransformMode==ETransformMode::Translate)
        {
            Point(GizmoPivot+FVector2D(AxisWorld,0),Selection.Type,
                Selection.Index,Selection.SubIndex,ETransformHandle::MoveX);
            Point(GizmoPivot+FVector2D(0,AxisWorld),Selection.Type,
                Selection.Index,Selection.SubIndex,ETransformHandle::MoveY);
        }
        else if(TransformMode==ETransformMode::Scale&&CanScaleSelection())
        {
            Point(GizmoPivot+FVector2D(AxisWorld,0),Selection.Type,
                Selection.Index,Selection.SubIndex,ETransformHandle::ScaleX);
            Point(GizmoPivot+FVector2D(0,AxisWorld),Selection.Type,
                Selection.Index,Selection.SubIndex,ETransformHandle::ScaleY);
        }
        else if(TransformMode==ETransformMode::Rotate&&CanRotateSelection())
        {
            const float RingRadius=48.0f;
            const float RingDistance=FMath::Abs(
                FVector2f::Distance(Local,ToPanel(GizmoPivot))-RingRadius);
            if(RingDistance<8.0f)
                Best={Selection.Type,Selection.Index,Selection.SubIndex,
                    ETransformHandle::Rotate};
        }
        if(Best.Handle!=ETransformHandle::None)return Best;
    }
    auto Segment=[&](FVector2D WA,FVector2D WB,ESelectionType Type,int32 Index,int32 SubIndex=INDEX_NONE)
    {
        const FVector2f A=ToPanel(WA),B=ToPanel(WB),AB=B-A;
        const float T=FMath::Clamp(FVector2f::DotProduct(Local-A,AB)/FMath::Max(1.0f,AB.SquaredLength()),0.0f,1.0f);
        const float D=FVector2f::Distance(Local,A+AB*T);
        if(D<BestPixels){BestPixels=D;Best={Type,Index,SubIndex};}
    };
    // Scene actors are the primary selection layer. Routes and dense baked
    // geometry must never steal a click from a visible agent marker.
    if(PIEFrame.IsSet()&&Asset->bShowEntities)
        for(int32 I=0;I<PIEFrame->Agents.Num();++I)
            if(FVector2f::Distance(Local,ToPanel(PIEFrame->Agents[I].Entity.Position))<=16.0f)
                return {ESelectionType::Entity,I,INDEX_NONE};
    if(!PIEFrame.IsSet()&&Asset->bShowEntities&&!Asset->bLockEntities)
        for(int32 I=0;I<Asset->Scenario.Entities.Num();++I)
        {
            const FHellRunTacticalLabEntity& Entity=Asset->Scenario.Entities[I];
            if(Entity.Kind==EHellRunTacticalLabEntityKind::Candidate&&
                !Asset->bShowCandidates)continue;
            const FVector2D HitPosition=RuntimeEntities.IsValidIndex(I)&&
                RuntimeEntities[I].Id==Entity.Id?RuntimeEntities[I].Position:Entity.Position;
            if(FVector2f::Distance(Local,ToPanel(HitPosition))<=16.0f)
                return {ESelectionType::Entity,I,INDEX_NONE};
        }
    if(!Asset->bLockGeometry)for(int32 I=0;I<Asset->Scenario.Shapes.Num();++I)
    {
        const auto& Shape=Asset->Scenario.Shapes[I];
        const double Angle=FMath::DegreesToRadians(Shape.RotationDegrees);
        const FVector2D AxisX(FMath::Cos(Angle),FMath::Sin(Angle));
        const FVector2D AxisY(-AxisX.Y,AxisX.X);
        const FVector2D Relative=PanelToWorld(Geometry,ScreenPosition)-Shape.Position;
        const FVector2D ShapeLocal(FVector2D::DotProduct(Relative,AxisX),FVector2D::DotProduct(Relative,AxisY));
        const bool bInside=Shape.Kind==EHellRunTacticalLabShapeKind::Circle
            ?ShapeLocal.SizeSquared()<=FMath::Square(Shape.Extents.X)
            :FMath::Abs(ShapeLocal.X)<=Shape.Extents.X&&FMath::Abs(ShapeLocal.Y)<=Shape.Extents.Y;
        if(bInside&&!Best.IsValid()){Best={ESelectionType::ShapeBody,I,INDEX_NONE};BestPixels=14.0f;}
        if(TransformMode==ETransformMode::Scale)
        {
            if(Shape.Kind==EHellRunTacticalLabShapeKind::Circle)
                Point(Shape.Position+AxisX*Shape.Extents.X,ESelectionType::ShapeResize,I,0);
            else for(int32 Corner=0;Corner<4;++Corner)
            {
                const double SX=(Corner&1)?1.0:-1.0,SY=(Corner&2)?1.0:-1.0;
                Point(Shape.Position+AxisX*Shape.Extents.X*SX+AxisY*Shape.Extents.Y*SY,
                    ESelectionType::ShapeResize,I,Corner);
            }
        }
        if(TransformMode==ETransformMode::Rotate&&
            Shape.Kind!=EHellRunTacticalLabShapeKind::Circle)
        {
            const double HandleOffset=40.0/(WorldToGraphScale*GetZoomAmount());
            Point(Shape.Position-AxisY*(Shape.Extents.Y+HandleOffset),
                ESelectionType::ShapeRotate,I,INDEX_NONE);
        }
    }
    if(!Asset->bLockHazards)for(int32 I=0;I<Asset->Scenario.Hazards.Num();++I)
        Point(Asset->Scenario.Hazards[I].Position,ESelectionType::Hazard,I);
    if(!Asset->bLockGeometry)for(int32 I=0;I<Asset->Scenario.Obstacles.Num();++I)
    {
        const auto& O=Asset->Scenario.Obstacles[I];
        Segment(O.Start,O.End,ESelectionType::ObstacleBody,I);
        Point(O.Start,ESelectionType::ObstacleStart,I);
        Point(O.End,ESelectionType::ObstacleEnd,I);
    }
    if(Asset->bShowRoutes&&!Asset->bLockRoutes)for(int32 I=0;I<Asset->Scenario.Routes.Num();++I)
    {
        const auto& R=Asset->Scenario.Routes[I];
        for(int32 P=1;P<R.Points.Num();++P)
            Segment(R.Points[P-1],R.Points[P],ESelectionType::RouteSegment,I,P);
        for(int32 P=0;P<R.Points.Num();++P)
            Point(R.Points[P],ESelectionType::RoutePoint,I,P);
    }
    return Best;
}

bool STacticalLabSurface::GetSelectionPivot(FVector2D& OutPivot) const
{
    if(!Asset.IsValid()||!Selection.IsValid())return false;
    const auto& S=Asset->Scenario;
    switch(Selection.Type)
    {
    case ESelectionType::Entity:
        if(S.Entities.IsValidIndex(Selection.Index)){OutPivot=S.Entities[Selection.Index].Position;return true;}break;
    case ESelectionType::Hazard:
        if(S.Hazards.IsValidIndex(Selection.Index)){OutPivot=S.Hazards[Selection.Index].Position;return true;}break;
    case ESelectionType::ObstacleStart:case ESelectionType::ObstacleEnd:case ESelectionType::ObstacleBody:
        if(S.Obstacles.IsValidIndex(Selection.Index))
        {const auto& O=S.Obstacles[Selection.Index];OutPivot=Selection.Type==ESelectionType::ObstacleStart
            ?O.Start:Selection.Type==ESelectionType::ObstacleEnd?O.End:(O.Start+O.End)*.5;return true;}break;
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:case ESelectionType::ShapeMoveY:
    case ESelectionType::ShapeResize:case ESelectionType::ShapeRotate:
        if(S.Shapes.IsValidIndex(Selection.Index)){OutPivot=S.Shapes[Selection.Index].Position;return true;}break;
    case ESelectionType::RoutePoint:
        if(S.Routes.IsValidIndex(Selection.Index)&&S.Routes[Selection.Index].Points.IsValidIndex(Selection.SubIndex))
        {OutPivot=S.Routes[Selection.Index].Points[Selection.SubIndex];return true;}break;
    case ESelectionType::RouteSegment:
        if(S.Routes.IsValidIndex(Selection.Index)&&!S.Routes[Selection.Index].Points.IsEmpty())
        {OutPivot=FVector2D::ZeroVector;for(const FVector2D& P:S.Routes[Selection.Index].Points)OutPivot+=P;
            OutPivot/=S.Routes[Selection.Index].Points.Num();return true;}break;
    default:break;
    }
    return false;
}

bool STacticalLabSurface::CanRotateSelection() const
{
    switch(Selection.Type)
    {
    case ESelectionType::Entity:
    case ESelectionType::ObstacleBody:
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:
    case ESelectionType::ShapeMoveY:case ESelectionType::ShapeResize:
    case ESelectionType::ShapeRotate:
    case ESelectionType::RouteSegment:return true;
    default:return false;
    }
}

bool STacticalLabSurface::CanScaleSelection() const
{
    switch(Selection.Type)
    {
    case ESelectionType::Hazard:
    case ESelectionType::ObstacleBody:
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:
    case ESelectionType::ShapeMoveY:case ESelectionType::ShapeResize:
    case ESelectionType::ShapeRotate:
    case ESelectionType::RouteSegment:return true;
    default:return false;
    }
}

void STacticalLabSurface::TransformSelection(const FVector2D& World,const FVector2D& Delta)
{
    if(!Asset.IsValid()||!Selection.IsValid())return;
    FVector2D AppliedDelta=Delta;
    FVector2D Pivot;
    if(GetSelectionPivot(Pivot))
    {
        const ETransformHandle Active=Selection.Handle!=ETransformHandle::None
            ?Selection.Handle:TransformMode==ETransformMode::Rotate
            ?ETransformHandle::Rotate:TransformMode==ETransformMode::Scale
            ?ETransformHandle::ScaleUniform:ETransformHandle::None;
        if(Active==ETransformHandle::Rotate)
        {
            const FVector2D Previous=LastDragWorld-Pivot,Current=World-Pivot;
            if(!Previous.IsNearlyZero()&&!Current.IsNearlyZero())
                RotateSelection(Pivot,FMath::RadiansToDegrees(FMath::Atan2(Current.Y,Current.X)-
                    FMath::Atan2(Previous.Y,Previous.X)));
            return;
        }
        if(Active==ETransformHandle::ScaleX||Active==ETransformHandle::ScaleY||
            Active==ETransformHandle::ScaleUniform)
        {ScaleSelection(Pivot,LastDragWorld,World,Active);return;}
        if(Active==ETransformHandle::MoveX)AppliedDelta.Y=0;
        if(Active==ETransformHandle::MoveY)AppliedDelta.X=0;
    }
    auto& S=Asset->Scenario;
    switch(Selection.Type)
    {
    case ESelectionType::Entity:
        if(S.Entities.IsValidIndex(Selection.Index))
        {
            S.Entities[Selection.Index].Position+=AppliedDelta;
            if(RuntimeEntities.IsValidIndex(Selection.Index)&&
                RuntimeEntities[Selection.Index].Id==S.Entities[Selection.Index].Id)
                RuntimeEntities[Selection.Index].Position=S.Entities[Selection.Index].Position;
            if(S.Entities[Selection.Index].Kind==EHellRunTacticalLabEntityKind::Enemy)
                OnEntityMoved.ExecuteIfBound(Selection.Index,S.Entities[Selection.Index].Position);
        }break;
    case ESelectionType::Hazard: if(S.Hazards.IsValidIndex(Selection.Index))S.Hazards[Selection.Index].Position+=AppliedDelta;break;
    case ESelectionType::ObstacleStart: if(S.Obstacles.IsValidIndex(Selection.Index))S.Obstacles[Selection.Index].Start+=AppliedDelta;break;
    case ESelectionType::ObstacleEnd: if(S.Obstacles.IsValidIndex(Selection.Index))S.Obstacles[Selection.Index].End+=AppliedDelta;break;
    case ESelectionType::ObstacleBody: if(S.Obstacles.IsValidIndex(Selection.Index)){S.Obstacles[Selection.Index].Start+=AppliedDelta;S.Obstacles[Selection.Index].End+=AppliedDelta;}break;
    case ESelectionType::ShapeBody:if(S.Shapes.IsValidIndex(Selection.Index))S.Shapes[Selection.Index].Position+=AppliedDelta;break;
    case ESelectionType::ShapeMoveX:
    case ESelectionType::ShapeMoveY:
        if(S.Shapes.IsValidIndex(Selection.Index))
        {
            const double A=FMath::DegreesToRadians(S.Shapes[Selection.Index].RotationDegrees);
            const FVector2D X(FMath::Cos(A),FMath::Sin(A)),Y(-X.Y,X.X);
            const FVector2D Axis=Selection.Type==ESelectionType::ShapeMoveX?X:Y;
            S.Shapes[Selection.Index].Position+=Axis*FVector2D::DotProduct(AppliedDelta,Axis);
        }break;
    case ESelectionType::ShapeResize:
        if(S.Shapes.IsValidIndex(Selection.Index))
        {
            auto& Shape=S.Shapes[Selection.Index];const FVector2D Relative=World-Shape.Position;
            if(Shape.Kind==EHellRunTacticalLabShapeKind::Circle)
            {const double Radius=FMath::Max(25.0,Relative.Size());Shape.Extents=FVector2D(Radius);}
            else
            {
                const double A=FMath::DegreesToRadians(Shape.RotationDegrees);
                const FVector2D X(FMath::Cos(A),FMath::Sin(A)),Y(-X.Y,X.X);
                Shape.Extents=FVector2D(FMath::Max(25.0,FMath::Abs(FVector2D::DotProduct(Relative,X))),
                    FMath::Max(25.0,FMath::Abs(FVector2D::DotProduct(Relative,Y))));
            }
        }break;
    case ESelectionType::ShapeRotate:
        if(S.Shapes.IsValidIndex(Selection.Index))
        {const FVector2D D=World-S.Shapes[Selection.Index].Position;S.Shapes[Selection.Index].RotationDegrees=FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X))+90.0f;}break;
    case ESelectionType::RoutePoint: if(S.Routes.IsValidIndex(Selection.Index)&&S.Routes[Selection.Index].Points.IsValidIndex(Selection.SubIndex))S.Routes[Selection.Index].Points[Selection.SubIndex]+=AppliedDelta;break;
    case ESelectionType::RouteSegment: if(S.Routes.IsValidIndex(Selection.Index)){for(FVector2D& P:S.Routes[Selection.Index].Points)P+=AppliedDelta;}break;
    default:break;
    }
}

void STacticalLabSurface::RotateSelection(const FVector2D& Pivot,const double DeltaDegrees)
{
    if(!Asset.IsValid()||FMath::IsNearlyZero(DeltaDegrees))return;
    auto& S=Asset->Scenario;
    const double A=FMath::DegreesToRadians(DeltaDegrees),C=FMath::Cos(A),N=FMath::Sin(A);
    const auto Rotate=[&](const FVector2D& P)
    {const FVector2D D=P-Pivot;return Pivot+FVector2D(D.X*C-D.Y*N,D.X*N+D.Y*C);};
    switch(Selection.Type)
    {
    case ESelectionType::Entity:if(S.Entities.IsValidIndex(Selection.Index))
        {auto& E=S.Entities[Selection.Index];const FVector2D F=E.Facing.GetSafeNormal();
            E.Facing=FVector2D(F.X*C-F.Y*N,F.X*N+F.Y*C).GetSafeNormal();}break;
    case ESelectionType::ObstacleBody:if(S.Obstacles.IsValidIndex(Selection.Index))
        {auto& O=S.Obstacles[Selection.Index];O.Start=Rotate(O.Start);O.End=Rotate(O.End);}break;
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:case ESelectionType::ShapeMoveY:
    case ESelectionType::ShapeResize:case ESelectionType::ShapeRotate:
        if(S.Shapes.IsValidIndex(Selection.Index))S.Shapes[Selection.Index].RotationDegrees=
            FMath::UnwindDegrees(S.Shapes[Selection.Index].RotationDegrees+DeltaDegrees);break;
    case ESelectionType::RouteSegment:if(S.Routes.IsValidIndex(Selection.Index))
        for(FVector2D& P:S.Routes[Selection.Index].Points)P=Rotate(P);break;
    default:break;
    }
}

void STacticalLabSurface::ScaleSelection(const FVector2D& Pivot,
    const FVector2D& Previous,const FVector2D& Current,const ETransformHandle Handle)
{
    if(!Asset.IsValid())return;
    double ScaleX=1.0,ScaleY=1.0;
    if(Handle==ETransformHandle::ScaleX)
        ScaleX=FMath::Clamp(FMath::Abs(Current.X-Pivot.X)/
            FMath::Max(1.0,FMath::Abs(Previous.X-Pivot.X)),.02,50.0);
    else if(Handle==ETransformHandle::ScaleY)
        ScaleY=FMath::Clamp(FMath::Abs(Current.Y-Pivot.Y)/
            FMath::Max(1.0,FMath::Abs(Previous.Y-Pivot.Y)),.02,50.0);
    else
    {
        const double Uniform=FMath::Clamp((Current-Pivot).Size()/
            FMath::Max(1.0,(Previous-Pivot).Size()),.02,50.0);
        ScaleX=ScaleY=Uniform;
    }
    auto& S=Asset->Scenario;
    const auto ScalePoint=[&](const FVector2D& P)
    {const FVector2D D=P-Pivot;return Pivot+FVector2D(D.X*ScaleX,D.Y*ScaleY);};
    switch(Selection.Type)
    {
    case ESelectionType::Hazard:if(S.Hazards.IsValidIndex(Selection.Index))
        S.Hazards[Selection.Index].Radius=FMath::Max(25.0f,S.Hazards[Selection.Index].Radius*
            static_cast<float>(FMath::Max(ScaleX,ScaleY)));break;
    case ESelectionType::ObstacleBody:if(S.Obstacles.IsValidIndex(Selection.Index))
        {auto& O=S.Obstacles[Selection.Index];O.Start=ScalePoint(O.Start);O.End=ScalePoint(O.End);}break;
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:case ESelectionType::ShapeMoveY:
    case ESelectionType::ShapeResize:case ESelectionType::ShapeRotate:
        if(S.Shapes.IsValidIndex(Selection.Index))
        {auto& Shape=S.Shapes[Selection.Index];Shape.Extents.X=FMath::Max(25.0,Shape.Extents.X*ScaleX);
            Shape.Extents.Y=FMath::Max(25.0,Shape.Extents.Y*ScaleY);
            if(Shape.Kind==EHellRunTacticalLabShapeKind::Circle)
                Shape.Extents=FVector2D(FMath::Max(Shape.Extents.X,Shape.Extents.Y));}break;
    case ESelectionType::RouteSegment:if(S.Routes.IsValidIndex(Selection.Index))
        for(FVector2D& P:S.Routes[Selection.Index].Points)P=ScalePoint(P);break;
    default:break;
    }
}

void STacticalLabSurface::RemoveSelected()
{
    if(!Asset.IsValid()||!Selection.IsValid())return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Tactical Lab Item")));
    Asset->Modify();auto& S=Asset->Scenario;
    switch(Selection.Type)
    {
    case ESelectionType::Entity: if(S.Entities.IsValidIndex(Selection.Index))S.Entities.RemoveAt(Selection.Index);break;
    case ESelectionType::Hazard: if(S.Hazards.IsValidIndex(Selection.Index))S.Hazards.RemoveAt(Selection.Index);break;
    case ESelectionType::ObstacleStart:case ESelectionType::ObstacleEnd:case ESelectionType::ObstacleBody:
        if(S.Obstacles.IsValidIndex(Selection.Index))S.Obstacles.RemoveAt(Selection.Index);break;
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:case ESelectionType::ShapeMoveY:case ESelectionType::ShapeResize:case ESelectionType::ShapeRotate:
        if(S.Shapes.IsValidIndex(Selection.Index))S.Shapes.RemoveAt(Selection.Index);break;
    case ESelectionType::RoutePoint:
        if(S.Routes.IsValidIndex(Selection.Index)&&S.Routes[Selection.Index].Points.IsValidIndex(Selection.SubIndex))
        {S.Routes[Selection.Index].Points.RemoveAt(Selection.SubIndex);if(S.Routes[Selection.Index].Points.Num()<2)S.Routes.RemoveAt(Selection.Index);}break;
    case ESelectionType::RouteSegment:if(S.Routes.IsValidIndex(Selection.Index))S.Routes.RemoveAt(Selection.Index);break;
    default:break;
    }
    Selection.Reset();Changed();
}

void STacticalLabSurface::InsertPointOnSelectedRoute()
{
    if(!Asset.IsValid()||Selection.Type!=ESelectionType::RouteSegment||
        !Asset->Scenario.Routes.IsValidIndex(Selection.Index))return;
    auto& Points=Asset->Scenario.Routes[Selection.Index].Points;
    if(Selection.SubIndex<=0||Selection.SubIndex>Points.Num())return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Insert Tactical Route Point")));
    Asset->Modify();Points.Insert(ContextWorld,Selection.SubIndex);
    Selection.Type=ESelectionType::RoutePoint;Changed();
}

FString STacticalLabSurface::GetSelectionLabel() const
{
    if(!PendingRoutePoints.IsEmpty())return FString::Printf(
        TEXT("DRAWING ROUTE  |  LMB add point  |  Double-click/Enter finish  |  Backspace undo point  |  Esc cancel  |  %d points"),
        PendingRoutePoints.Num());
    if(!Asset.IsValid()||!Selection.IsValid())return TEXT("LMB select/drag  |  W move  E rotate  R scale  |  RMB pan/menu  |  Wheel zoom");
    const auto& S=Asset->Scenario;
    const TCHAR* Mode=TransformMode==ETransformMode::Translate?TEXT("MOVE [W]"):
        TransformMode==ETransformMode::Rotate?TEXT("ROTATE [E]"):TEXT("SCALE [R]");
    FString Name;
    switch(Selection.Type)
    {
    case ESelectionType::Entity:Name=S.Entities.IsValidIndex(Selection.Index)?S.Entities[Selection.Index].Id.ToString():TEXT("Entity");break;
    case ESelectionType::Hazard:Name=S.Hazards.IsValidIndex(Selection.Index)?S.Hazards[Selection.Index].Id.ToString():TEXT("Hazard");break;
    case ESelectionType::ObstacleStart:case ESelectionType::ObstacleEnd:case ESelectionType::ObstacleBody:Name=S.Obstacles.IsValidIndex(Selection.Index)?S.Obstacles[Selection.Index].Id.ToString():TEXT("Obstacle");break;
    case ESelectionType::ShapeBody:case ESelectionType::ShapeMoveX:case ESelectionType::ShapeMoveY:case ESelectionType::ShapeResize:case ESelectionType::ShapeRotate:Name=S.Shapes.IsValidIndex(Selection.Index)?S.Shapes[Selection.Index].Id.ToString():TEXT("Shape");break;
    case ESelectionType::RoutePoint:case ESelectionType::RouteSegment:Name=S.Routes.IsValidIndex(Selection.Index)?S.Routes[Selection.Index].RouteId.ToString():TEXT("Route");break;
    default:Name=TEXT("Selection");break;
    }
    const FString Shortcuts=FString::Printf(TEXT("W move%s%s"),
        CanRotateSelection()?TEXT("  E rotate"):TEXT(""),
        CanScaleSelection()?TEXT("  R scale"):TEXT(""));
    return FString::Printf(TEXT("%s  |  %s  |  %s"),*Name,Mode,*Shortcuts);
}

void STacticalLabSurface::OpenContextMenu(const FVector2D& ScreenPosition)
{
    FMenuBuilder Menu(true,nullptr);
    auto Add=[&](const TCHAR* Label,const TCHAR* Tip,FExecuteAction Action)
    {Menu.AddMenuEntry(FText::FromString(Label),FText::FromString(Tip),FSlateIcon(),FUIAction(Action));};
    if(Selection.IsValid())
    {
        Menu.BeginSection(TEXT("Selection"),FText::FromString(TEXT("Selection")));
        const FString DeleteLabel=FString::Printf(TEXT("Delete %s"),*GetSelectionLabel());
        Add(*DeleteLabel,TEXT("Delete the selected item (Delete/Backspace)"),
            FExecuteAction::CreateSP(this,&STacticalLabSurface::RemoveSelected));
        if(Selection.Type==ESelectionType::RouteSegment)
            Add(TEXT("Insert Route Point Here"),TEXT("Insert a draggable route control point"),
                FExecuteAction::CreateSP(this,&STacticalLabSurface::InsertPointOnSelectedRoute));
        if(Selection.Type==ESelectionType::RouteSegment||Selection.Type==ESelectionType::RoutePoint)
        {
            Add(TEXT("Reverse Route Direction"),TEXT("Reverse this route and its traversal segment order"),
                FExecuteAction::CreateSP(this,&STacticalLabSurface::ReverseSelectedRoute));
            Menu.AddSubMenu(FText::FromString(TEXT("Assign Route to Agent")),
                FText::FromString(TEXT("Use this authored path as an agent's simulation candidate")),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& AgentsMenu)
                {
                    if(!Asset.IsValid())return;
                    for(const FHellRunTacticalLabEntity& Entity:Asset->Scenario.Entities)
                        if(Entity.Kind==EHellRunTacticalLabEntityKind::Enemy)
                            AgentsMenu.AddMenuEntry(FText::FromName(Entity.Id),FText::GetEmpty(),FSlateIcon(),
                                FUIAction(FExecuteAction::CreateSP(this,
                                    &STacticalLabSurface::AssignSelectedRouteToAgent,Entity.Id)));
                }));
        }
        Menu.EndSection();
    }
    Menu.BeginSection(TEXT("Actors"),FText::FromString(TEXT("Place Actor")));
    Add(TEXT("Add Player Here"),TEXT("Place a player threat at this position"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddEntity,
            static_cast<uint8>(EHellRunTacticalLabEntityKind::Player)));
    Add(TEXT("Add Enemy Here"),TEXT("Place an enemy agent at this position"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddEntity,
            static_cast<uint8>(EHellRunTacticalLabEntityKind::Enemy)));
    Add(TEXT("Add Friendly Here"),TEXT("Place a friendly agent at this position"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddEntity,
            static_cast<uint8>(EHellRunTacticalLabEntityKind::Friendly)));
    Add(TEXT("Add Candidate Here"),TEXT("Place a tactical candidate"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddEntity,
            static_cast<uint8>(EHellRunTacticalLabEntityKind::Candidate)));
    Menu.EndSection();
    Menu.BeginSection(TEXT("Authoring"),FText::FromString(TEXT("Author Geometry")));
    Add(TEXT("Add Hazard Here"),TEXT("Place a circular tactical hazard"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddHazard));
    Add(TEXT("Add Rectangle"),TEXT("Place a resizable and rotatable 2D obstacle"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddShape,
            static_cast<uint8>(EHellRunTacticalLabShapeKind::Rectangle)));
    Add(TEXT("Add Circle"),TEXT("Place a resizable circular 2D obstacle"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::AddShape,
            static_cast<uint8>(EHellRunTacticalLabShapeKind::Circle)));
    if(PendingRoutePoints.IsEmpty())
        Add(TEXT("Begin Route Here"),TEXT("Start authoring a spline route"),
            FExecuteAction::CreateSP(this,&STacticalLabSurface::AddRoutePoint,false));
    else
    {
        Add(TEXT("Add Route Point Here"),TEXT("Append a spline control point (or left-click the canvas)"),
            FExecuteAction::CreateSP(this,&STacticalLabSurface::AddRoutePoint,false));
        Add(TEXT("Finish Route Here"),TEXT("Append this point and save the route"),
            FExecuteAction::CreateSP(this,&STacticalLabSurface::AddRoutePoint,true));
        Add(TEXT("Cancel Route"),TEXT("Discard the route currently being drawn"),
            FExecuteAction::CreateSP(this,&STacticalLabSurface::CancelRoute));
    }
    Menu.EndSection();
    Menu.BeginSection(TEXT("Remove"),FText::FromString(TEXT("Remove")));
    Add(TEXT("Remove Nearest Item"),TEXT("Remove the nearest authored actor, hazard, obstacle, or route"),
        FExecuteAction::CreateSP(this,&STacticalLabSurface::RemoveNearest));
    Menu.EndSection();
    FSlateApplication::Get().PushMenu(AsShared(),FWidgetPath(),Menu.MakeWidget(),
        ScreenPosition,FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

void STacticalLabSurface::Changed()
{
    // Authored edits invalidate prior simulation overlays. Keeping them alive
    // made moved actors appear duplicated and left stale failure rays behind.
    RuntimeEntities.Reset();RuntimeRoutes.Reset();RejectedRuntimeRoutes.Reset();
    RuntimeCandidates.Reset();RuntimeBlockingObstacleIds.Reset();
    if(Asset.IsValid()){Asset->Modify();Asset->MarkPackageDirty();}
    OnScenarioChanged.ExecuteIfBound();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::AssignSelectedRouteToAgent(const FName AgentId)
{
    if(!Asset.IsValid()||!Asset->Scenario.Routes.IsValidIndex(Selection.Index))return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Assign Tactical Route to Agent")));
    Asset->Modify();
    Asset->Scenario.Routes[Selection.Index].AgentId=AgentId;
    Changed();
}

void STacticalLabSurface::AddEntity(const uint8 KindValue)
{
    if(!Asset.IsValid())return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Add Tactical Lab Entity")));
    Asset->Modify();
    auto Kind=static_cast<EHellRunTacticalLabEntityKind>(KindValue);
    FHellRunTacticalLabEntity& Entity=Asset->Scenario.Entities.AddDefaulted_GetRef();
    const TCHAR* Prefix=Kind==EHellRunTacticalLabEntityKind::Player?TEXT("Player"):
        Kind==EHellRunTacticalLabEntityKind::Enemy?TEXT("Enemy"):
        Kind==EHellRunTacticalLabEntityKind::Friendly?TEXT("Friendly"):TEXT("Candidate");
    Entity.Id=*FString::Printf(TEXT("%s_%03d"),Prefix,Asset->Scenario.Entities.Num());
    Entity.Kind=Kind; Entity.Position=ContextWorld;
    Entity.Team=Kind==EHellRunTacticalLabEntityKind::Enemy?TEXT("Enemies"):
        Kind==EHellRunTacticalLabEntityKind::Candidate?TEXT("Neutral"):TEXT("Players");
    if(Kind==EHellRunTacticalLabEntityKind::Enemy)
        Entity.ArchetypeId=Asset->Scenario.Profiles.IsEmpty()?NAME_None:
            Asset->Scenario.Profiles[0].ArchetypeId;
    Selection={ESelectionType::Entity,Asset->Scenario.Entities.Num()-1,INDEX_NONE};Changed();
}

void STacticalLabSurface::AddHazard()
{
    if(!Asset.IsValid())return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Add Tactical Lab Hazard")));
    Asset->Modify();
    FHellRunTacticalLabHazard& H=Asset->Scenario.Hazards.AddDefaulted_GetRef();
    H.Id=*FString::Printf(TEXT("Hazard_%03d"),Asset->Scenario.Hazards.Num());
    H.Position=ContextWorld;Selection={ESelectionType::Hazard,Asset->Scenario.Hazards.Num()-1,INDEX_NONE};Changed();
}

void STacticalLabSurface::AddShape(const uint8 KindValue)
{
    if(!Asset.IsValid())return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Add Tactical Lab Shape")));
    Asset->Modify();
    auto& Shape=Asset->Scenario.Shapes.AddDefaulted_GetRef();
    Shape.Kind=static_cast<EHellRunTacticalLabShapeKind>(KindValue);
    Shape.Id=Shape.Kind==EHellRunTacticalLabShapeKind::Circle
        ?FName(*FString::Printf(TEXT("Circle_%03d"),Asset->Scenario.Shapes.Num()))
        :FName(*FString::Printf(TEXT("Rectangle_%03d"),Asset->Scenario.Shapes.Num()));
    Shape.Position=ContextWorld;
    if(Shape.Kind==EHellRunTacticalLabShapeKind::Circle)Shape.Extents=FVector2D(250.0);
    Selection={ESelectionType::ShapeBody,Asset->Scenario.Shapes.Num()-1,INDEX_NONE};Changed();
}

void STacticalLabSurface::AddRoutePoint(bool bFinish)
{
    if(!Asset.IsValid())return;
    if(PendingRoutePoints.IsEmpty()||FVector2D::Distance(PendingRoutePoints.Last(),ContextWorld)>1.0)
        PendingRoutePoints.Add(ContextWorld);
    if(bFinish)FinishRoute();
    else Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::FinishRoute()
{
    if(!Asset.IsValid()||PendingRoutePoints.Num()<2)return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Add Tactical Lab Spline Route")));
    Asset->Modify();
    FHellRunTacticalLabRoute& R=Asset->Scenario.Routes.AddDefaulted_GetRef();
    R.RouteId=*FString::Printf(TEXT("Route_%04d"),Asset->Scenario.Routes.Num());
    R.Points=MoveTemp(PendingRoutePoints);PendingRoutePoints.Reset();
    R.SegmentTypes.Init(EHellRunTacticalLabTraversal::Walk,FMath::Max(0,R.Points.Num()-1));
    Selection={ESelectionType::RouteSegment,Asset->Scenario.Routes.Num()-1,1};Changed();
}

void STacticalLabSurface::CancelRoute()
{
    PendingRoutePoints.Reset();bHasHoverWorld=false;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void STacticalLabSurface::ReverseSelectedRoute()
{
    if(!Asset.IsValid()||
        (Selection.Type!=ESelectionType::RoutePoint&&Selection.Type!=ESelectionType::RouteSegment)||
        !Asset->Scenario.Routes.IsValidIndex(Selection.Index))return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Reverse Tactical Lab Route")));
    Asset->Modify();auto& R=Asset->Scenario.Routes[Selection.Index];
    Algo::Reverse(R.Points);Algo::Reverse(R.SegmentTypes);
    if(Selection.Type==ESelectionType::RoutePoint)
        Selection.SubIndex=R.Points.Num()-1-Selection.SubIndex;
    Changed();
}

void STacticalLabSurface::RemoveNearest()
{
    if(!Asset.IsValid())return;
    if(Selection.IsValid()){RemoveSelected();return;}
    float Best=250.0f; enum class EType{None,Entity,Hazard,Obstacle,Route} Type=EType::None;int32 Index=-1;
    for(int32 I=0;I<Asset->Scenario.Entities.Num();++I){float D=FVector2D::Distance(ContextWorld,Asset->Scenario.Entities[I].Position);if(D<Best){Best=D;Type=EType::Entity;Index=I;}}
    for(int32 I=0;I<Asset->Scenario.Hazards.Num();++I){float D=FVector2D::Distance(ContextWorld,Asset->Scenario.Hazards[I].Position);if(D<Best){Best=D;Type=EType::Hazard;Index=I;}}
    auto SegmentDistance=[](FVector2D P,FVector2D A,FVector2D B){const FVector2D AB=B-A;const double T=FMath::Clamp(FVector2D::DotProduct(P-A,AB)/FMath::Max(1.0,AB.SquaredLength()),0.0,1.0);return FVector2D::Distance(P,A+AB*T);};
    for(int32 I=0;I<Asset->Scenario.Obstacles.Num();++I){const auto& O=Asset->Scenario.Obstacles[I];float D=SegmentDistance(ContextWorld,O.Start,O.End);if(D<Best){Best=D;Type=EType::Obstacle;Index=I;}}
    for(int32 I=0;I<Asset->Scenario.Routes.Num();++I)for(int32 P=1;P<Asset->Scenario.Routes[I].Points.Num();++P){float D=SegmentDistance(ContextWorld,Asset->Scenario.Routes[I].Points[P-1],Asset->Scenario.Routes[I].Points[P]);if(D<Best){Best=D;Type=EType::Route;Index=I;}}
    if(Index<0)return;
    const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Tactical Lab Item")));
    Asset->Modify();
    switch(Type){case EType::Entity:Asset->Scenario.Entities.RemoveAt(Index);break;case EType::Hazard:Asset->Scenario.Hazards.RemoveAt(Index);break;case EType::Obstacle:Asset->Scenario.Obstacles.RemoveAt(Index);break;case EType::Route:Asset->Scenario.Routes.RemoveAt(Index);break;default:break;}
    Changed();
}

void STacticalLabSurface::SetAsset(UTacticalLabScenarioAsset* InAsset)
{
    if(MapLoadHandle)MapLoadHandle->CancelHandle();
    MapLoadHandle.Reset();
    Asset=InAsset;
    MapBrush=FSlateBrush();
    if(InAsset&&IsValid(InAsset->GeneratedMapPreview))
    {
        MapBrush=FSlateImageBrush(InAsset->GeneratedMapPreview,
            FVector2D(InAsset->GeneratedMapPreview->GetSizeX(),
                InAsset->GeneratedMapPreview->GetSizeY()));
    }
    else if(InAsset&&!InAsset->TacticalMapTexture.IsNull())
    {
        if(InAsset->TacticalMapTexture.IsValid())ApplyMapTexture();
        else MapLoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(
            InAsset->TacticalMapTexture.ToSoftObjectPath(),
            FStreamableDelegate::CreateSP(this,&STacticalLabSurface::ApplyMapTexture));
    }
    Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void STacticalLabSurface::ApplyMapTexture()
{
    if(!Asset.IsValid())return;
    if(UTexture2D* Texture=Asset->TacticalMapTexture.Get())
    {
        MapBrush=FSlateImageBrush(Texture,
            FVector2D(Texture->GetSizeX(),Texture->GetSizeY()));
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2f STacticalLabSurface::ToGraph(const FVector2D& World) const
{
    return FVector2f(UE_REAL_TO_FLOAT(World.X*WorldToGraphScale),
        UE_REAL_TO_FLOAT(World.Y*WorldToGraphScale));
}

FVector2f STacticalLabSurface::ToPanel(const FVector2D& World) const
{
    return GraphCoordToPanelCoord(ToGraph(World));
}

void STacticalLabSurface::FrameAll()
{
    if(!Asset.IsValid())return;
    const auto& Metadata=Asset->Scenario.BakeMetadata;
    FVector2D WorldMin=Metadata.bBakedFromProductionMap?Metadata.BoundsMin:FVector2D(-2500);
    FVector2D WorldMax=Metadata.bBakedFromProductionMap?Metadata.BoundsMax:FVector2D(2500);
    TArray<double> X,Y,CandidateX,CandidateY;
    const int32 CoordinateReserve=Asset->Scenario.Obstacles.Num()*2+
        Asset->Scenario.Entities.Num();
    X.Reserve(CoordinateReserve);Y.Reserve(CoordinateReserve);
    for(const auto& O:Asset->Scenario.Obstacles){X.Add(O.Start.X);X.Add(O.End.X);Y.Add(O.Start.Y);Y.Add(O.End.Y);}
    for(const auto& Shape:Asset->Scenario.Shapes)
    {X.Add(Shape.Position.X-Shape.Extents.Size());X.Add(Shape.Position.X+Shape.Extents.Size());Y.Add(Shape.Position.Y-Shape.Extents.Size());Y.Add(Shape.Position.Y+Shape.Extents.Size());}
    for(const auto& E:Asset->Scenario.Entities)
        if(E.Kind==EHellRunTacticalLabEntityKind::Candidate)
        {CandidateX.Add(E.Position.X);CandidateY.Add(E.Position.Y);}
        else {X.Add(E.Position.X);Y.Add(E.Position.Y);}
    if(CandidateX.Num()>=10)
    {
        CandidateX.Sort();CandidateY.Sort();
        const int32 Low=FMath::FloorToInt(CandidateX.Num()*.01f);
        const int32 High=FMath::Clamp(FMath::CeilToInt(CandidateX.Num()*.99f),0,CandidateX.Num()-1);
        WorldMin=FVector2D(CandidateX[Low],CandidateY[Low]);
        WorldMax=FVector2D(CandidateX[High],CandidateY[High]);
    }
    else if(X.Num()>100)
    {
        X.Sort();Y.Sort();const int32 Low=FMath::FloorToInt(X.Num()*.015f);
        const int32 High=FMath::Clamp(FMath::CeilToInt(X.Num()*.985f),0,X.Num()-1);
        WorldMin=FVector2D(X[Low],Y[Low]);WorldMax=FVector2D(X[High],Y[High]);
    }
    FVector2f Min=ToGraph(WorldMin),Max=ToGraph(WorldMax);
    const FVector2f Padding(60.0f);
    ZoomToTarget(Min-Padding,Max+Padding);
}

void STacticalLabSurface::SetViewMode(FName InMode)
{
    ViewMode=InMode;Invalidate(EInvalidateWidgetReason::Paint);
}

FLinearColor STacticalLabSurface::EntityColor(const uint8 Kind) const
{
    switch(static_cast<EHellRunTacticalLabEntityKind>(Kind))
    {
    case EHellRunTacticalLabEntityKind::Player:return FLinearColor(.1f,.55f,1.0f);
    case EHellRunTacticalLabEntityKind::Friendly:return FLinearColor(.16f,.9f,.34f);
    case EHellRunTacticalLabEntityKind::Enemy:return FLinearColor(1.0f,.16f,.12f);
    case EHellRunTacticalLabEntityKind::Candidate:return FLinearColor(.1f,.85f,.9f);
    default:return FLinearColor(.8f,.84f,.88f);
    }
}

int32 STacticalLabSurface::OnPaint(const FPaintArgs&,const FGeometry& Geometry,
    const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,
    const FWidgetStyle&,bool)const
{
    const FSlateBrush* White=FAppStyle::GetBrush(TEXT("WhiteBrush"));
    FSlateDrawElement::MakeBox(Elements,Layer++,Geometry.ToPaintGeometry(),White,
        ESlateDrawEffect::None,FLinearColor(.006f,.01f,.014f));
    if(!Asset.IsValid())return Layer;
    const FHellRunTacticalLabScenario& Scenario=Asset->Scenario;
    const FVector2f PanelSize(Geometry.GetLocalSize());
    auto VisibleSegment=[PanelSize](const FVector2f A,const FVector2f B)
    {
        const FVector2f Min(FMath::Min(A.X,B.X),FMath::Min(A.Y,B.Y));
        const FVector2f Max(FMath::Max(A.X,B.X),FMath::Max(A.Y,B.Y));
        return Max.X>=-32&&Max.Y>=-32&&Min.X<=PanelSize.X+32&&Min.Y<=PanelSize.Y+32;
    };
    const FVector2D BoundsMin=Scenario.BakeMetadata.bBakedFromProductionMap
        ?Scenario.BakeMetadata.BoundsMin:FVector2D(-2500);
    const FVector2D BoundsMax=Scenario.BakeMetadata.bBakedFromProductionMap
        ?Scenario.BakeMetadata.BoundsMax:FVector2D(2500);

    if(MapBrush.GetResourceObject())
    {
        const FVector2f A=ToPanel(BoundsMin),B=ToPanel(BoundsMax);
        FSlateDrawElement::MakeBox(Elements,Layer++,Geometry.ToPaintGeometry(
            FVector2D(B-A),FSlateLayoutTransform(A)),&MapBrush,
            ESlateDrawEffect::None,FLinearColor::White);
    }

    if(Asset->bShowGrid)
    {
        const FVector2D VisibleA=FVector2D(PanelCoordToGraphCoord(FVector2f::ZeroVector))/WorldToGraphScale;
        const FVector2D VisibleB=FVector2D(PanelCoordToGraphCoord(PanelSize))/WorldToGraphScale;
        const FVector2D VisibleMin(FMath::Min(VisibleA.X,VisibleB.X),FMath::Min(VisibleA.Y,VisibleB.Y));
        const FVector2D VisibleMax(FMath::Max(VisibleA.X,VisibleB.X),FMath::Max(VisibleA.Y,VisibleB.Y));
        int32 Step=100;
        while(Step*WorldToGraphScale*GetZoomAmount()<36.0f)Step*=2;
        const FLinearColor Grid(.045f,.075f,.09f,.72f);
        for(int32 X=FMath::FloorToInt(VisibleMin.X/Step)*Step;X<=VisibleMax.X;X+=Step)
        {
            TArray<FVector2f> Points={ToPanel(FVector2D(X,VisibleMin.Y)),ToPanel(FVector2D(X,VisibleMax.Y))};
            const bool bMajor=(X%(Step*5))==0;
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,
                bMajor?FLinearColor(.075f,.13f,.15f,.85f):Grid,true,bMajor?1.2f:1.0f);
        }
        for(int32 Y=FMath::FloorToInt(VisibleMin.Y/Step)*Step;Y<=VisibleMax.Y;Y+=Step)
        {
            TArray<FVector2f> Points={ToPanel(FVector2D(VisibleMin.X,Y)),ToPanel(FVector2D(VisibleMax.X,Y))};
            const bool bMajor=(Y%(Step*5))==0;
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,
                bMajor?FLinearColor(.075f,.13f,.15f,.85f):Grid,true,bMajor?1.2f:1.0f);
        }
        ++Layer;
    }

    if(Asset->bShowGeometry)
    {
        for(int32 ShapeIndex=0;ShapeIndex<Scenario.Shapes.Num();++ShapeIndex)
        {
            const auto& Shape=Scenario.Shapes[ShapeIndex];
            const double A=FMath::DegreesToRadians(Shape.RotationDegrees);
            const FVector2D AxisX(FMath::Cos(A),FMath::Sin(A)),AxisY(-AxisX.Y,AxisX.X);
            const bool bSelected=Selection.IsValid()&&Selection.Index==ShapeIndex&&
                (Selection.Type==ESelectionType::ShapeBody||Selection.Type==ESelectionType::ShapeMoveX||Selection.Type==ESelectionType::ShapeMoveY||Selection.Type==ESelectionType::ShapeResize||Selection.Type==ESelectionType::ShapeRotate);
            const FLinearColor ShapeColor=bSelected?FLinearColor(1,.65f,.08f,1):FLinearColor(.45f,.53f,.58f,.9f);
            TArray<FVector2f> Outline;
            if(Shape.Kind==EHellRunTacticalLabShapeKind::Circle)
            {
                Outline.Reserve(33);
                for(int32 Point=0;Point<=32;++Point)
                {const double T=2.0*PI*Point/32.0;Outline.Add(ToPanel(Shape.Position+FVector2D(FMath::Cos(T),FMath::Sin(T))*Shape.Extents.X));}
            }
            else
            {
                const FVector2D C0=Shape.Position-AxisX*Shape.Extents.X-AxisY*Shape.Extents.Y;
                const FVector2D C1=Shape.Position+AxisX*Shape.Extents.X-AxisY*Shape.Extents.Y;
                const FVector2D C2=Shape.Position+AxisX*Shape.Extents.X+AxisY*Shape.Extents.Y;
                const FVector2D C3=Shape.Position-AxisX*Shape.Extents.X+AxisY*Shape.Extents.Y;
                Outline={ToPanel(C0),ToPanel(C1),ToPanel(C2),ToPanel(C3),ToPanel(C0)};
            }
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Outline,
                ESlateDrawEffect::None,ShapeColor,true,bSelected?3.0f:2.0f);
        }
        ++Layer;
        const float Thickness=FMath::Clamp(GetZoomAmount()*.8f,1.0f,3.0f);
        const int32 MaxObstacles=GetZoomAmount()<.4f?300:GetZoomAmount()<.8f?700:1600;
        const int32 ObstacleStride=FMath::Max(1,FMath::CeilToInt(
            Scenario.Obstacles.Num()/static_cast<float>(MaxObstacles)));
        for(int32 Index=0;Index<Scenario.Obstacles.Num();Index+=ObstacleStride)
        {
            const auto& Obstacle=Scenario.Obstacles[Index];
            const FVector2f A=ToPanel(Obstacle.Start),B=ToPanel(Obstacle.End);
            if(!VisibleSegment(A,B))continue;
            TArray<FVector2f> Points={A,B};
            const bool bBlocking=RuntimeBlockingObstacleIds.Contains(Obstacle.Id);
            const FLinearColor Color=bBlocking?FLinearColor(1.0f,.08f,.02f,1.0f):Obstacle.bVaultable
                ?FLinearColor(.78f,.43f,.12f,.82f):FLinearColor(.46f,.5f,.55f,.72f);
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);
        }
        ++Layer;
    }

    if(Asset->bShowTraversal||ViewMode==TEXT("Routes"))
    {
        const int32 MaxEdges=GetZoomAmount()<.5f?600:2200;
        const int32 Stride=FMath::Max(1,FMath::CeilToInt(Scenario.TraversalEdges.Num()/static_cast<float>(MaxEdges)));
        for(int32 Index=0;Index<Scenario.TraversalEdges.Num();Index+=Stride)
        {
            const auto& Edge=Scenario.TraversalEdges[Index];
            const FVector2f A=ToPanel(Edge.Start),B=ToPanel(Edge.End);
            if(!VisibleSegment(A,B))continue;
            TArray<FVector2f> Points={A,B};
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,
                ESlateDrawEffect::None,FLinearColor(.65f,.25f,.9f,.55f),true,1);
        }
        ++Layer;
    }

    for(const FHellRunTacticalLabHazard& Hazard:Scenario.Hazards)
    {
        const FVector2f Center=ToPanel(Hazard.Position);
        const float Radius=Hazard.Radius*WorldToGraphScale*GetZoomAmount();
        if(Center.X+Radius<0||Center.Y+Radius<0||Center.X-Radius>PanelSize.X||Center.Y-Radius>PanelSize.Y)continue;
        TArray<FVector2f> Ring;Ring.Reserve(25);
        for(int32 I=0;I<=24;++I){const float A=2*PI*I/24.0f;Ring.Add(Center+FVector2f(FMath::Cos(A),FMath::Sin(A))*Radius);}
        FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Ring,
            ESlateDrawEffect::None,FLinearColor(1,.16f,.05f,.75f),true,2);
    }
    ++Layer;

    // The baked fixture may show theoretical configuration, but it is not an
    // authoritative LOS source. Resolved rays are supplied only by a PIE frame.
    if(!PIEFrame.IsSet()&&Asset->bShowVisionCones&&Asset->bShowConfiguredFOV)
    {
        for(int32 EntityIndex=0;EntityIndex<Scenario.Entities.Num();++EntityIndex)
        {
            const FHellRunTacticalLabEntity& Authored=Scenario.Entities[EntityIndex];
            if(Authored.Kind!=EHellRunTacticalLabEntityKind::Enemy&&
                Authored.Kind!=EHellRunTacticalLabEntityKind::Player&&
                Authored.Kind!=EHellRunTacticalLabEntityKind::Friendly)continue;
            const FHellRunTacticalLabEntity* Entity=&Authored;
            if(RuntimeEntities.IsValidIndex(EntityIndex)&&RuntimeEntities[EntityIndex].Id==Authored.Id)
                Entity=&RuntimeEntities[EntityIndex];
            FVector2D Forward=Entity->Facing.GetSafeNormal();
            if(Forward.IsNearlyZero())Forward=FVector2D(1,0);
            const FHellRunEnemySimulationProfile* Profile=Scenario.Profiles.FindByPredicate(
                [Entity](const FHellRunEnemySimulationProfile& P){return P.ArchetypeId==Entity->ArchetypeId;});
            // Baked PlayerStarts and generic fixture entities do not own a
            // perception configuration. Drawing scenario defaults for them
            // creates a convincing but completely synthetic vision shape.
            if(!Profile)continue;
            const float VisionRange=Profile->VisionRange;
            const float VisionHalfAngle=Profile->VisionHalfAngleDegrees;
            const int32 RayCount=FMath::Clamp(Profile->VisionRayCount,3,128);
            TArray<FVector2f> ConfiguredOutline;ConfiguredOutline.Reserve(RayCount+3);
            ConfiguredOutline.Add(ToPanel(Entity->Position));
            for(int32 RayIndex=0;RayIndex<=RayCount;++RayIndex)
            {
                const double Alpha=RayIndex/static_cast<double>(RayCount);
                const double Angle=FMath::DegreesToRadians(FMath::Lerp(
                    -VisionHalfAngle,VisionHalfAngle,
                    static_cast<float>(Alpha)));
                const FVector2D Direction(
                    Forward.X*FMath::Cos(Angle)-Forward.Y*FMath::Sin(Angle),
                    Forward.X*FMath::Sin(Angle)+Forward.Y*FMath::Cos(Angle));
                ConfiguredOutline.Add(ToPanel(Entity->Position+Direction*VisionRange));
            }
            ConfiguredOutline.Add(ToPanel(Entity->Position));
            const bool bSelected=Selection.Type==ESelectionType::Entity&&Selection.Index==EntityIndex;
            const FLinearColor Base=Authored.Kind==EHellRunTacticalLabEntityKind::Enemy
                ?FLinearColor(1.0f,.12f,.08f,bSelected?.88f:.42f)
                :FLinearColor(.1f,.55f,1.0f,bSelected?.75f:.28f);
            if(Asset->bShowConfiguredFOV)
                FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),ConfiguredOutline,
                    ESlateDrawEffect::None,FLinearColor(Base.R,Base.G,Base.B,bSelected?.42f:.18f),true,1.0f);
        }
        ++Layer;
    }

    auto DrawSplineRoute=[&](const TArray<FVector2D>& Points,const FLinearColor& Color,
        float Thickness,bool bDrawDirection)
    {
        if(Points.Num()<2)return;
        const int32 ControlPointBudget=GetZoomAmount()<.55f?160:
            GetZoomAmount()<1.5f?320:640;
        const int32 ControlStride=FMath::Max(1,FMath::CeilToInt(
            Points.Num()/static_cast<float>(ControlPointBudget)));
        TArray<FVector2f> Smooth;
        Smooth.Reserve(FMath::Min(Points.Num(),ControlPointBudget)*8);
        for(int32 I=ControlStride;I<Points.Num();I+=ControlStride)
        {
            const int32 P1Index=I-ControlStride;
            const int32 P2Index=FMath::Min(I,Points.Num()-1);
            const FVector2D P0=Points[FMath::Max(0,P1Index-ControlStride)];
            const FVector2D P1=Points[P1Index];
            const FVector2D P2=Points[P2Index];
            const FVector2D P3=Points[FMath::Min(Points.Num()-1,P2Index+ControlStride)];
            const int32 Samples=FMath::Clamp(FMath::CeilToInt(
                FVector2f::Distance(ToPanel(P1),ToPanel(P2))/24.0f),2,8);
            for(int32 Step=I==ControlStride?0:1;Step<=Samples;++Step)
            {
                const double T=Step/static_cast<double>(Samples),T2=T*T,T3=T2*T;
                const FVector2D P=.5*((2.0*P1)+(-P0+P2)*T+
                    (2.0*P0-5.0*P1+4.0*P2-P3)*T2+
                    (-P0+3.0*P1-3.0*P2+P3)*T3);
                Smooth.Add(ToPanel(P));
            }
        }
        if(Smooth.IsEmpty()||Smooth.Last()!=ToPanel(Points.Last()))
            Smooth.Add(ToPanel(Points.Last()));
        FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Smooth,
            ESlateDrawEffect::None,Color,true,Thickness);
        if(bDrawDirection&&Smooth.Num()>5&&GetZoomAmount()>.35f)
        {
            const int32 ArrowCount=FMath::Clamp(Smooth.Num()/24,1,6);
            for(int32 Arrow=1;Arrow<=ArrowCount;++Arrow)
            {
                const int32 At=FMath::Clamp(Arrow*Smooth.Num()/(ArrowCount+1),1,Smooth.Num()-2);
                const FVector2f Direction=(Smooth[At+1]-Smooth[At-1]).GetSafeNormal();
                const FVector2f Normal(-Direction.Y,Direction.X),Tip=Smooth[At]+Direction*7;
                TArray<FVector2f> ArrowLines={Tip-Direction*9+Normal*5,Tip,Tip-Direction*9-Normal*5};
                FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),ArrowLines,
                    ESlateDrawEffect::None,Color,true,FMath::Max(1.5f,Thickness));
            }
        }
    };
    if(PIEFrame.IsSet()&&Asset->bShowVisionCones)
    {
        for(int32 AgentIndex=0;AgentIndex<PIEFrame->Agents.Num();++AgentIndex)
        {
            const FTacticalLabPIEAgentSnapshot& Agent=PIEFrame->Agents[AgentIndex];
            if(Agent.VisionRange<=0.0f||Agent.VisionRayCount<2)continue;
            const bool bSelected=Selection.Type==ESelectionType::Entity&&Selection.Index==AgentIndex;
            const FLinearColor Base=Agent.Entity.Kind==EHellRunTacticalLabEntityKind::Enemy
                ?FLinearColor(1.0f,.12f,.08f,bSelected?.9f:.3f)
                :FLinearColor(.1f,.55f,1.0f,bSelected?.8f:.25f);
            if(Asset->bShowConfiguredFOV)
            {
                TArray<FVector2f> Outline;Outline.Reserve(Agent.VisionRayCount+2);
                Outline.Add(ToPanel(Agent.VisionOrigin));
                FVector2D Forward=Agent.Entity.Facing.GetSafeNormal();
                for(int32 RayIndex=0;RayIndex<Agent.VisionRayCount;++RayIndex)
                {
                    const float Alpha=RayIndex/static_cast<float>(FMath::Max(1,Agent.VisionRayCount-1));
                    const float Angle=FMath::DegreesToRadians(FMath::Lerp(
                        -Agent.VisionHalfAngle,Agent.VisionHalfAngle,Alpha));
                    const FVector2D Direction(Forward.X*FMath::Cos(Angle)-Forward.Y*FMath::Sin(Angle),
                        Forward.X*FMath::Sin(Angle)+Forward.Y*FMath::Cos(Angle));
                    Outline.Add(ToPanel(Agent.VisionOrigin+Direction*Agent.VisionRange));
                }
                Outline.Add(ToPanel(Agent.VisionOrigin));
                FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Outline,
                    ESlateDrawEffect::None,FLinearColor(Base.R,Base.G,Base.B,bSelected?.72f:.34f),true,
                    bSelected?2.5f:1.5f);
            }
            if(Asset->bShowResolvedVisibility&&bSelected)
                for(const FTacticalLabPIEVisionRay& Ray:Agent.VisionRays)
                {
                    TArray<FVector2f> Line={ToPanel(Agent.VisionOrigin),ToPanel(Ray.End)};
                    FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Line,
                        ESlateDrawEffect::None,FLinearColor(Base.R,Base.G,Base.B,.55f),true,1.25f);
                    if(Ray.bBlocked)
                    {
                        const FVector2f End=ToPanel(Ray.End);
                        FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(
                            FVector2D(4),FSlateLayoutTransform(End-FVector2f(2))),White,
                            ESlateDrawEffect::None,FLinearColor(1,.72f,.08f,.95f));
                        if(Asset->bShowLabels&&!Ray.BlockingActor.IsNone())
                            FSlateDrawElement::MakeText(Elements,Layer+3,Geometry.ToPaintGeometry(
                                FVector2D(180,16),FSlateLayoutTransform(End+FVector2f(5,-7))),
                                Ray.BlockingActor.ToString(),FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),7),
                                ESlateDrawEffect::None,FLinearColor(1,.72f,.2f,.9f));
                    }
                }
        }
        Layer+=4;
    }
    if(PIEFrame.IsSet()&&Asset->bShowRoutes)
    {
        for(const FTacticalLabPIEAgentSnapshot& Agent:PIEFrame->Agents)
            if(Agent.MovementPath.Num()>1)
                DrawSplineRoute(Agent.MovementPath,
                    Agent.Entity.Kind==EHellRunTacticalLabEntityKind::Friendly
                        ?FLinearColor(.16f,.9f,.34f,.95f)
                        :FLinearColor(.1f,.85f,1.0f,.95f),3.0f,true);
        Layer+=2;
    }
    if(Asset->bShowRoutes)for(int32 RouteIndex=0;RouteIndex<Scenario.Routes.Num();++RouteIndex)
    {
        const bool bSelected=Selection.IsValid()&&Selection.Index==RouteIndex&&
            (Selection.Type==ESelectionType::RoutePoint||Selection.Type==ESelectionType::RouteSegment);
        DrawSplineRoute(Scenario.Routes[RouteIndex].Points,bSelected
            ?FLinearColor(1,.65f,.08f,1):FLinearColor(.16f,.45f,1,.9f),bSelected?4.0f:2.5f,true);
    }
    if(Asset->bShowRoutes)
    {
        for(const TArray<FVector2D>& Route:RuntimeRoutes)
            DrawSplineRoute(Route,FLinearColor(.08f,.9f,.82f,.78f),2.5f,true);
        for(const TArray<FVector2D>& Route:RejectedRuntimeRoutes)
            DrawSplineRoute(Route,FLinearColor(1.0f,.08f,.03f,.72f),2.0f,false);
    }
    if(Asset->bShowEQS)for(const TArray<FVector2D>& Route:EQSPaths)
    {
        DrawSplineRoute(Route,FLinearColor(.9f,.2f,1.0f,.95f),3.5f,true);
        if(Asset->bShowLabels&&GetZoomAmount()>.8f)
        {
            const int32 PointStride=FMath::Max(1,FMath::CeilToInt(Route.Num()/32.0f));
            for(int32 PointIndex=0;PointIndex<Route.Num();PointIndex+=PointStride)
            {
                const FVector2f Point=ToPanel(Route[PointIndex]);
                FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(5),
                    FSlateLayoutTransform(Point-FVector2f(2.5f))),White,ESlateDrawEffect::None,
                    FLinearColor(.95f,.35f,1.0f,.95f));
                FSlateDrawElement::MakeText(Elements,Layer+3,Geometry.ToPaintGeometry(FVector2D(54,16),
                    FSlateLayoutTransform(Point+FVector2f(6,-8))),FString::Printf(TEXT("P%d"),PointIndex),
                    FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),8),ESlateDrawEffect::None,
                    FLinearColor(.95f,.55f,1.0f,.95f));
            }
        }
    }
    if(!PendingRoutePoints.IsEmpty())
    {
        TArray<FVector2D> Preview=PendingRoutePoints;
        if(bHasHoverWorld&&FVector2D::Distance(Preview.Last(),HoverWorld)>1.0)Preview.Add(HoverWorld);
        if(Preview.Num()>1)DrawSplineRoute(Preview,FLinearColor(1,.65f,.08f,.95f),3.0f,true);
        for(const FVector2D& Point:PendingRoutePoints)
        {
            const FVector2f C=ToPanel(Point);const float Size=8;
            FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(Size),
                FSlateLayoutTransform(C-FVector2f(Size*.5f))),White,ESlateDrawEffect::None,
                FLinearColor(1,.72f,.12f,1));
        }
    }
    Layer+=3;

    const bool bOverview=GetZoomAmount()<.55f;
    auto DrawLiveAgentIcon=[&](const FHellRunTacticalLabEntity& Entity,
        const FVector2f Center,const bool bSelected,const int32 IconLayer)
    {
        const float Radius=bSelected?11.0f:8.5f;
        const FLinearColor Color=EntityColor(static_cast<uint8>(Entity.Kind));
        TArray<FVector2f> Outline;
        if(Entity.Kind==EHellRunTacticalLabEntityKind::Player||
            Entity.Kind==EHellRunTacticalLabEntityKind::Friendly)
        {
            FVector2D WorldForward=Entity.Facing.GetSafeNormal();
            if(WorldForward.IsNearlyZero()) WorldForward=FVector2D(1,0);
            const FVector2f Forward(WorldForward),Right(-Forward.Y,Forward.X);
            const FVector2f Rear=Center-Forward*Radius*.68f;
            Outline={Center+Forward*Radius,Rear+Right*Radius*.72f,
                Rear-Right*Radius*.72f,Center+Forward*Radius};
        }
        else if(Entity.Kind==EHellRunTacticalLabEntityKind::Enemy)
        {
            Outline.Reserve(21);
            for(int32 Point=0;Point<=20;++Point)
            {
                const float Angle=2.0f*PI*Point/20.0f;
                Outline.Add(Center+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
            }
        }
        else
            Outline={Center+FVector2f(0,-Radius),Center+FVector2f(Radius,0),
                Center+FVector2f(0,Radius),Center+FVector2f(-Radius,0),
                Center+FVector2f(0,-Radius)};
        FSlateDrawElement::MakeLines(Elements,IconLayer,Geometry.ToPaintGeometry(),Outline,
            ESlateDrawEffect::None,Color,true,bSelected?3.5f:2.5f);
        FSlateDrawElement::MakeBox(Elements,IconLayer,Geometry.ToPaintGeometry(FVector2D(3),
            FSlateLayoutTransform(Center-FVector2f(1.5f))),White,ESlateDrawEffect::None,Color);
    };
    int32 CandidateCount=0;
    for(const auto& Entity:Scenario.Entities)
        if(Entity.Kind==EHellRunTacticalLabEntityKind::Candidate)++CandidateCount;
    const int32 CandidateBudget=bOverview?260:GetZoomAmount()<1.5f?600:1000;
    const int32 CandidateStride=FMath::Max(1,
        FMath::CeilToInt(FMath::Max(1,CandidateCount)/static_cast<float>(CandidateBudget)));
    int32 CandidateOrdinal=0;
    if(Asset->bShowEntities)for(int32 EntityIndex=0;EntityIndex<Scenario.Entities.Num();++EntityIndex)
    {
        const auto& Entity=Scenario.Entities[EntityIndex];
        const bool bCandidate=Entity.Kind==EHellRunTacticalLabEntityKind::Candidate;
        if(PIEFrame.IsSet()&&!bCandidate)continue;
        if(bCandidate&&(!Asset->bShowCandidates||ViewMode==TEXT("Agents")||ViewMode==TEXT("Squads")))continue;
        if(bCandidate&&(CandidateOrdinal++%CandidateStride)!=0)continue;
        const FVector2D DisplayPosition=RuntimeEntities.IsValidIndex(EntityIndex)
            &&RuntimeEntities[EntityIndex].Id==Entity.Id
            ?RuntimeEntities[EntityIndex].Position:Entity.Position;
        const FVector2f Center=ToPanel(DisplayPosition);
        if(Center.X<-24||Center.Y<-24||Center.X>PanelSize.X+24||Center.Y>PanelSize.Y+24)continue;
        const float Size=bCandidate?(bOverview?2.5f:6.0f):16.0f;
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(FVector2D(Size),
            FSlateLayoutTransform(Center-FVector2f(Size*.5f))),White,ESlateDrawEffect::None,
            bCandidate&&bOverview?FLinearColor(.05f,.65f,.7f,.38f):EntityColor(static_cast<uint8>(Entity.Kind)));
        if(Asset->bShowLabels&&(!bCandidate||GetZoomAmount()>2.5f))
            FSlateDrawElement::MakeText(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2D(180,20),
                FSlateLayoutTransform(Center+FVector2f(10,-9))),Entity.Id.ToString(),
                FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10),ESlateDrawEffect::None,FLinearColor(.9f,.94f,.97f));
    }

    if(Asset->bShowEntities&&PIEFrame.IsSet())
        for(int32 AgentIndex=0;AgentIndex<PIEFrame->Agents.Num();++AgentIndex)
        {
            const FHellRunTacticalLabEntity& Entity=PIEFrame->Agents[AgentIndex].Entity;
            const FVector2f Center=ToPanel(Entity.Position);
            if(Center.X<-24||Center.Y<-24||Center.X>PanelSize.X+24||Center.Y>PanelSize.Y+24)continue;
            const bool bSelected=Selection.Type==ESelectionType::Entity&&Selection.Index==AgentIndex;
            DrawLiveAgentIcon(Entity,Center,bSelected,Layer);
            if(Asset->bShowLabels)
                FSlateDrawElement::MakeText(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2D(220,20),
                    FSlateLayoutTransform(Center+FVector2f(11,-9))),Entity.Id.ToString(),
                    FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10),ESlateDrawEffect::None,FLinearColor::White);
        }


    TArray<FTacticalLabEQSItem> DisplayCandidateItems=RuntimeCandidates;
    DisplayCandidateItems.Append(EQSItems);
    if(Asset->bShowEQS&&Asset->bShowCandidates&&!DisplayCandidateItems.IsEmpty())
    {
        float BestScore=-BIG_NUMBER;
        for(const FTacticalLabEQSItem& Item:DisplayCandidateItems)if(Item.bValid)BestScore=FMath::Max(BestScore,Item.Score);
        for(const FTacticalLabEQSItem& Item:DisplayCandidateItems)
        {
            const FVector2f Center=ToPanel(Item.Position);
            if(Center.X<-16||Center.Y<-16||Center.X>PanelSize.X+16||Center.Y>PanelSize.Y+16)continue;
            const float Normalized=FMath::Clamp(Item.Score,0.0f,1.0f);
            const FLinearColor Color=Item.bValid
                ?FLinearColor::LerpUsingHSV(FLinearColor(.95f,.12f,.08f),FLinearColor(.15f,1,.3f),Normalized)
                :FLinearColor(.95f,.16f,.1f,.72f);
            const bool bBest=Item.bValid&&FMath::IsNearlyEqual(Item.Score,BestScore);
            const float Size=bBest?13.0f:7.0f;
            FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(Size),
                FSlateLayoutTransform(Center-FVector2f(Size*.5f))),White,ESlateDrawEffect::None,Color);
            if(bBest)
            {
                TArray<FVector2f> Ring;Ring.Reserve(17);
                for(int32 I=0;I<=16;++I){const float A=2*PI*I/16.0f;Ring.Add(Center+FVector2f(FMath::Cos(A),FMath::Sin(A))*10.0f);}
                FSlateDrawElement::MakeLines(Elements,Layer+3,Geometry.ToPaintGeometry(),Ring,
                    ESlateDrawEffect::None,FLinearColor::White,true,2);
            }
            if(Asset->bShowLabels&&GetZoomAmount()>1.0f)
                FSlateDrawElement::MakeText(Elements,Layer+3,Geometry.ToPaintGeometry(FVector2D(60,16),
                    FSlateLayoutTransform(Center+FVector2f(7,-8))),FString::Printf(TEXT("%s %.2f"),
                        Item.Label.IsEmpty()?TEXT("C"):*Item.Label,Item.Score),
                    FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),8),ESlateDrawEffect::None,Color);
        }
        if(EQSPaths.IsEmpty()&&EQSOrigin.IsSet()&&!EQSItems.IsEmpty())
        {
            const FTacticalLabEQSItem* BestItem=nullptr;
            for(const FTacticalLabEQSItem& Item:EQSItems)
                if(Item.bValid&&(!BestItem||Item.Score>BestItem->Score))BestItem=&Item;
            if(BestItem)
            {
                TArray<FVector2f> BestLink={ToPanel(EQSOrigin.GetValue()),ToPanel(BestItem->Position)};
                FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),BestLink,
                    ESlateDrawEffect::None,FLinearColor(.9f,.2f,1.0f,.7f),true,2.0f);
            }
        }
        Layer+=4;
    }

    // Authoring handles remain screen-sized so they are usable at every zoom level.
    auto DrawHandle=[&](const FVector2D& World,const FLinearColor& Color,float Size)
    {
        const FVector2f Center=ToPanel(World);
        if(Center.X<-20||Center.Y<-20||Center.X>PanelSize.X+20||Center.Y>PanelSize.Y+20)return;
        FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(Size),
            FSlateLayoutTransform(Center-FVector2f(Size*.5f))),White,ESlateDrawEffect::None,Color);
    };
    if(Selection.IsValid())
    {
        const FLinearColor Handle(.98f,.72f,.12f,1.0f);
        FVector2D SelectedWorld=FVector2D::ZeroVector;bool bHasSelectedWorld=false;
        if(Selection.Type==ESelectionType::Entity&&Scenario.Entities.IsValidIndex(Selection.Index))
        {SelectedWorld=Scenario.Entities[Selection.Index].Position;bHasSelectedWorld=true;}
        else if(Selection.Type==ESelectionType::Hazard&&Scenario.Hazards.IsValidIndex(Selection.Index))
        {SelectedWorld=Scenario.Hazards[Selection.Index].Position;bHasSelectedWorld=true;}
        else if((Selection.Type==ESelectionType::ObstacleStart||Selection.Type==ESelectionType::ObstacleEnd||
            Selection.Type==ESelectionType::ObstacleBody)&&Scenario.Obstacles.IsValidIndex(Selection.Index))
        {
            const auto& O=Scenario.Obstacles[Selection.Index];
            DrawHandle(O.Start,Selection.Type==ESelectionType::ObstacleStart?Handle:FLinearColor(.7f,.75f,.8f),9);
            DrawHandle(O.End,Selection.Type==ESelectionType::ObstacleEnd?Handle:FLinearColor(.7f,.75f,.8f),9);
            SelectedWorld=Selection.Type==ESelectionType::ObstacleStart?O.Start:
                Selection.Type==ESelectionType::ObstacleEnd?O.End:(O.Start+O.End)*.5;
            bHasSelectedWorld=true;
        }
        else if((Selection.Type==ESelectionType::ShapeBody||Selection.Type==ESelectionType::ShapeMoveX||Selection.Type==ESelectionType::ShapeMoveY||Selection.Type==ESelectionType::ShapeResize||
            Selection.Type==ESelectionType::ShapeRotate)&&Scenario.Shapes.IsValidIndex(Selection.Index))
        {
            const auto& Shape=Scenario.Shapes[Selection.Index];
            SelectedWorld=Shape.Position;bHasSelectedWorld=true;
        }
        else if((Selection.Type==ESelectionType::RoutePoint||Selection.Type==ESelectionType::RouteSegment)&&
            Scenario.Routes.IsValidIndex(Selection.Index))
        {
            const auto& R=Scenario.Routes[Selection.Index];
            for(int32 P=0;P<R.Points.Num();++P)
                DrawHandle(R.Points[P],Selection.Type==ESelectionType::RoutePoint&&P==Selection.SubIndex
                    ?Handle:FLinearColor(.25f,.58f,1,.95f),8);
            if(Selection.Type==ESelectionType::RoutePoint&&R.Points.IsValidIndex(Selection.SubIndex))
            {SelectedWorld=R.Points[Selection.SubIndex];bHasSelectedWorld=true;}
            else if(Selection.SubIndex>0&&R.Points.IsValidIndex(Selection.SubIndex))
            {SelectedWorld=(R.Points[Selection.SubIndex-1]+R.Points[Selection.SubIndex])*.5;bHasSelectedWorld=true;}
        }
        if(bHasSelectedWorld)
        {
            const FVector2f C=ToPanel(SelectedWorld);const float R=13;
            TArray<FVector2f> Outline={C+FVector2f(-R,-R),C+FVector2f(R,-R),
                C+FVector2f(R,R),C+FVector2f(-R,R),C+FVector2f(-R,-R)};
            FSlateDrawElement::MakeLines(Elements,Layer+3,Geometry.ToPaintGeometry(),Outline,
                ESlateDrawEffect::None,Handle,true,2);

            // Screen-sized 2D equivalent of Unreal's W/E/R transform widget.
            constexpr float AxisLength=58.0f;
            const FLinearColor AxisX(.95f,.12f,.08f,1),AxisY(.12f,.85f,.3f,1);
            if(Selection.Type==ESelectionType::Entity&&
                Scenario.Entities.IsValidIndex(Selection.Index))
            {
                FVector2D Forward=Scenario.Entities[Selection.Index].Facing.GetSafeNormal();
                if(Forward.IsNearlyZero())Forward=FVector2D(1,0);
                const FVector2f Direction(static_cast<float>(Forward.X),static_cast<float>(Forward.Y));
                const FVector2f Normal(-Direction.Y,Direction.X),Tip=C+Direction*44.0f;
                TArray<FVector2f> ForwardArrow={C,Tip,Tip-Direction*10.0f+Normal*6.0f,
                    Tip,Tip-Direction*10.0f-Normal*6.0f};
                FSlateDrawElement::MakeLines(Elements,Layer+5,Geometry.ToPaintGeometry(),
                    ForwardArrow,ESlateDrawEffect::None,FLinearColor(1,.75f,.12f,1),true,3.0f);
            }
            if(TransformMode==ETransformMode::Translate||
                (TransformMode==ETransformMode::Scale&&CanScaleSelection()))
            {
                const FVector2f XEnd=C+FVector2f(AxisLength,0),YEnd=C+FVector2f(0,AxisLength);
                TArray<FVector2f> XLine={C,XEnd},YLine={C,YEnd};
                FSlateDrawElement::MakeLines(Elements,Layer+4,Geometry.ToPaintGeometry(),XLine,
                    ESlateDrawEffect::None,AxisX,true,3);
                FSlateDrawElement::MakeLines(Elements,Layer+4,Geometry.ToPaintGeometry(),YLine,
                    ESlateDrawEffect::None,AxisY,true,3);
                DrawHandle(SelectedWorld+FVector2D(AxisLength/(WorldToGraphScale*GetZoomAmount()),0),
                    AxisX,TransformMode==ETransformMode::Translate?11:9);
                DrawHandle(SelectedWorld+FVector2D(0,AxisLength/(WorldToGraphScale*GetZoomAmount())),
                    AxisY,TransformMode==ETransformMode::Translate?11:9);
            }
            else if(TransformMode==ETransformMode::Rotate&&CanRotateSelection())
            {
                TArray<FVector2f> Ring;Ring.Reserve(41);
                for(int32 I=0;I<=40;++I)
                {const float A=2*PI*I/40.0f;Ring.Add(C+FVector2f(FMath::Cos(A),FMath::Sin(A))*48.0f);}
                FSlateDrawElement::MakeLines(Elements,Layer+4,Geometry.ToPaintGeometry(),Ring,
                    ESlateDrawEffect::None,FLinearColor(.25f,.55f,1,1),true,3);
            }
        }
    }

    if(ViewMode==TEXT("Squads"))
    {
        TMap<FName,FVector2D> SquadAnchors;
        for(const auto& Entity:Scenario.Entities)
        {
            if(Entity.SquadId.IsNone()||Entity.Kind==EHellRunTacticalLabEntityKind::Candidate)continue;
            if(const FVector2D* Anchor=SquadAnchors.Find(Entity.SquadId))
            {
                TArray<FVector2f> Link={ToPanel(*Anchor),ToPanel(Entity.Position)};
                FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Link,
                    ESlateDrawEffect::None,FLinearColor(.18f,.65f,1,.7f),true,2);
            }
            else SquadAnchors.Add(Entity.SquadId,Entity.Position);
        }
    }

    FSlateDrawElement::MakeText(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(260,22),
        FSlateLayoutTransform(FVector2f(14,12))),FString::Printf(TEXT("%s  |  %s"),
            *GetZoomText().ToString(),*Scenario.ScenarioId.ToString()),
        FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10),ESlateDrawEffect::None,FLinearColor(.55f,.66f,.74f));
    FSlateDrawElement::MakeText(Elements,Layer+4,Geometry.ToPaintGeometry(FVector2D(440,22),
        FSlateLayoutTransform(FVector2f(14,68))),GetSelectionLabel(),
        FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10),ESlateDrawEffect::None,
        Selection.IsValid()?FLinearColor(.98f,.72f,.12f):FLinearColor(.5f,.6f,.66f));
    if(!EQSStatus.IsEmpty())
        FSlateDrawElement::MakeText(Elements,Layer+4,Geometry.ToPaintGeometry(FVector2D(700,22),
            FSlateLayoutTransform(FVector2f(14,88))),EQSStatus,
            FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),9),ESlateDrawEffect::None,
            FLinearColor(.2f,.9f,.72f));
    const float ScalePixels=1000.0f*WorldToGraphScale*GetZoomAmount();
    const FVector2f ScaleStart(16,42),ScaleEnd(16+ScalePixels,42);
    TArray<FVector2f> ScaleLine={ScaleStart,ScaleEnd};
    FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),ScaleLine,
        ESlateDrawEffect::None,FLinearColor(.8f,.87f,.9f),true,2);
    FSlateDrawElement::MakeText(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(70,18),
        FSlateLayoutTransform(FVector2f(16,47))),TEXT("10 m"),
        FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),9),ESlateDrawEffect::None,FLinearColor(.8f,.87f,.9f));

    // Screen-space legend remains readable regardless of map zoom and pan.
    const FVector2f LegendOrigin(FMath::Max(12.0f,PanelSize.X-220.0f),12.0f);
    FSlateDrawElement::MakeBox(Elements,Layer+5,Geometry.ToPaintGeometry(FVector2D(208,126),
        FSlateLayoutTransform(LegendOrigin)),White,ESlateDrawEffect::None,FLinearColor(.015f,.025f,.04f,.88f));
    FSlateDrawElement::MakeText(Elements,Layer+6,Geometry.ToPaintGeometry(FVector2D(190,18),
        FSlateLayoutTransform(LegendOrigin+FVector2f(10,7))),TEXT("LIVE MAP LEGEND"),
        FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),9),ESlateDrawEffect::None,FLinearColor(.78f,.86f,.92f));
    struct FLegendEntry { EHellRunTacticalLabEntityKind Kind; const TCHAR* Label; };
    const FLegendEntry LegendEntries[]={
        {EHellRunTacticalLabEntityKind::Player,TEXT("Player")},
        {EHellRunTacticalLabEntityKind::Friendly,TEXT("Friendly bot")},
        {EHellRunTacticalLabEntityKind::Enemy,TEXT("Enemy")}};
    for(int32 EntryIndex=0;EntryIndex<UE_ARRAY_COUNT(LegendEntries);++EntryIndex)
    {
        const FVector2f Row=LegendOrigin+FVector2f(20,34+EntryIndex*22);
        FHellRunTacticalLabEntity LegendEntity;
        LegendEntity.Kind=LegendEntries[EntryIndex].Kind;
        LegendEntity.Facing=FVector2D(1,0);
        DrawLiveAgentIcon(LegendEntity,Row,false,Layer+7);
        FSlateDrawElement::MakeText(Elements,Layer+7,Geometry.ToPaintGeometry(FVector2D(155,18),
            FSlateLayoutTransform(Row+FVector2f(17,-8))),LegendEntries[EntryIndex].Label,
            FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9),ESlateDrawEffect::None,FLinearColor(.82f,.88f,.92f));
    }
    TArray<FVector2f> ConeKey={LegendOrigin+FVector2f(12,112),LegendOrigin+FVector2f(28,103),
        LegendOrigin+FVector2f(28,121),LegendOrigin+FVector2f(12,112)};
    FSlateDrawElement::MakeLines(Elements,Layer+7,Geometry.ToPaintGeometry(),ConeKey,
        ESlateDrawEffect::None,FLinearColor(.25f,.72f,1,.8f),true,1.5f);
    FSlateDrawElement::MakeText(Elements,Layer+7,Geometry.ToPaintGeometry(FVector2D(155,18),
        FSlateLayoutTransform(LegendOrigin+FVector2f(37,104))),TEXT("Configured view cone"),
        FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9),ESlateDrawEffect::None,FLinearColor(.82f,.88f,.92f));

    if(PIEFrame.IsSet())
    {
        const FTacticalLabPIEAgentSnapshot* SelectedAgent=
            Selection.Type==ESelectionType::Entity
            ?PIEFrame->Agents.FindByPredicate([this](const FTacticalLabPIEAgentSnapshot& Agent)
                {return PIEFrame->Agents.IsValidIndex(Selection.Index)&&
                    Agent.AgentId==PIEFrame->Agents[Selection.Index].AgentId;})
            :nullptr;
        const FVector2f DebugOrigin(14.0f,FMath::Max(110.0f,PanelSize.Y-142.0f));
        FSlateDrawElement::MakeBox(Elements,Layer+5,Geometry.ToPaintGeometry(FVector2D(520,128),
            FSlateLayoutTransform(DebugOrigin)),White,ESlateDrawEffect::None,FLinearColor(.015f,.025f,.04f,.88f));
        FString DebugText=FString::Printf(TEXT("LIVE  t=%.2fs  |  %d controlled pawns\nSelect an agent for GOAP, navigation, and perception diagnostics."),
            PIEFrame->WorldTime,PIEFrame->Agents.Num());
        if(SelectedAgent)
        {
            const FString Kind=UEnum::GetValueAsString(SelectedAgent->Entity.Kind);
            if(SelectedAgent->bHasGOAP)
            {
                const FGOAPBrainDebugSnapshot& Debug=SelectedAgent->GOAP;
                const FString Plan=FString::JoinBy(Debug.RemainingPlan,TEXT(" -> "),
                    [](const FName Name){return Name.ToString();});
                DebugText=FString::Printf(TEXT("%s  |  %s  |  team %s\nGoal: %s  |  Action: %s (%s)\nPlan: %s\nLast solve %s  |  cost %.2f  |  %d expanded / %d visited\nSpeed %.0f cm/s  |  path %d pts  |  sight %.0f cm / %.0f deg\nFacts %d  |  goals %d  |  world rev %d  |  replan: %s"),
                    *SelectedAgent->Entity.Id.ToString(),*Kind,*SelectedAgent->Entity.Team.ToString(),
                    *Debug.ActiveGoal.ToString(),*Debug.ActiveAction.ToString(),
                    *UEnum::GetValueAsString(Debug.ActionStatus),Plan.IsEmpty()?TEXT("none"):*Plan,
                    Debug.LastPlan.bSucceeded?TEXT("success"):TEXT("failure"),Debug.LastPlan.Cost,
                    Debug.LastPlan.ExpandedNodes,Debug.LastPlan.VisitedStates,
                    SelectedAgent->Entity.Velocity.Size(),SelectedAgent->MovementPath.Num(),
                    SelectedAgent->VisionRange,SelectedAgent->VisionHalfAngle,
                    Debug.Facts.Num(),Debug.GoalScores.Num(),Debug.WorldStateRevision,*Debug.LastReplanReason);
            }
            else
                DebugText=FString::Printf(TEXT("%s  |  %s  |  team %s\nPosition %.0f, %.0f  |  speed %.0f cm/s\nPath %d pts  |  sight %.0f cm / %.0f deg\nNo GOAP brain is attached."),
                    *SelectedAgent->Entity.Id.ToString(),*Kind,*SelectedAgent->Entity.Team.ToString(),
                    SelectedAgent->Entity.Position.X,SelectedAgent->Entity.Position.Y,
                    SelectedAgent->Entity.Velocity.Size(),SelectedAgent->MovementPath.Num(),
                    SelectedAgent->VisionRange,SelectedAgent->VisionHalfAngle);
        }
        FSlateDrawElement::MakeText(Elements,Layer+6,Geometry.ToPaintGeometry(FVector2D(500,112),
            FSlateLayoutTransform(DebugOrigin+FVector2f(10,8))),DebugText,
            FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9),ESlateDrawEffect::None,FLinearColor(.82f,.9f,.94f));
        if(PIEFrame->bHasDirectorDebug)
        {
            const FTacticalLabDirectorDebugSnapshot& Director=PIEFrame->Director;
            const FVector2f DirectorOrigin(FMath::Max(14.0f,PanelSize.X-534.0f),
                FMath::Max(150.0f,PanelSize.Y-174.0f));
            FSlateDrawElement::MakeBox(Elements,Layer+5,
                Geometry.ToPaintGeometry(FVector2D(520,160),
                    FSlateLayoutTransform(DirectorOrigin)),White,ESlateDrawEffect::None,
                FLinearColor(.015f,.025f,.04f,.9f));
            const FString DirectorText=FString::Printf(
                TEXT("AI DIRECTOR  |  %s  |  intensity %.2f  enemy %.2f  team %.2f\nCadence: %s / %s  (%.1fs)  %s\nPopulation: %d live, %d engaged, %d survivors, %d spawned  |  cap %.1f\nStates: %d idle  %d investigate  %d chase  %d attack  %d stunned\nTargets: %d players  |  slots %d, max %d/%d  |  in range %d, ready %d\nGates: %d no controller  %d no target  %d missing montage\nRecycle: %d/%d pressure, %d stale, %d pending, %d last/%d total  %s\nPlanner: %d native  |  last batch %d in %.2fms%s"),
                *Director.Phase,Director.Intensity,Director.EnemyPressure,Director.TeamPressure,
                *Director.CadenceAction,*Director.CadenceGoal.ToString(),
                Director.CadenceCommitmentRemaining,*Director.CadenceReason,
                Director.LiveEnemyCount,Director.EngagedEnemyCount,
                Director.AliveSurvivorCount,Director.TotalEnemiesSpawned,Director.SpawnCapacity,
                Director.IdleEnemyCount,Director.InvestigatingEnemyCount,
                Director.ChasingEnemyCount,Director.AttackingEnemyCount,Director.StunnedEnemyCount,
                Director.TargetedPlayerCount,Director.ActiveAttackReservationCount,
                Director.MaxAttackReservationsOnSingleTarget,Director.MaxAttackersPerTarget,
                Director.TargetInRangeEnemyCount,Director.MeleeReadyEnemyCount,
                Director.NoControllerEnemyCount,Director.NoTargetEnemyCount,
                Director.MissingAttackMontageEnemyCount,Director.UsefulPressureEnemyCount,
                Director.DesiredPressureEnemyCount,Director.StaleTailEnemyCount,
                Director.PendingRecycleEnemyCount,Director.LastRecycledEnemyCount,
                Director.TotalRecycledEnemyCount,*Director.RecycleBlockedReason,
                Director.NativePlannerEnemyCount,Director.LastPlannerBatchAgentCount,
                Director.LastPlannerBatchMilliseconds,
                Director.bPlannerBatchActive?TEXT("  ACTIVE"):TEXT(""));
            FSlateDrawElement::MakeText(Elements,Layer+6,
                Geometry.ToPaintGeometry(FVector2D(500,144),
                    FSlateLayoutTransform(DirectorOrigin+FVector2f(10,8))),DirectorText,
                FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),8),
                ESlateDrawEffect::None,FLinearColor(.8f,.9f,.94f));
        }
    }
    return Layer+8;
}
