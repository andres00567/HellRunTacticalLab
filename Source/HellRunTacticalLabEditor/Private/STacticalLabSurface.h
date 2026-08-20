#pragma once

#include "CoreMinimal.h"
#include "SNodePanel.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"
#include "TacticalLabPIEDebugTypes.h"

class UTacticalLabScenarioAsset;
class FScopedTransaction;
struct FStreamableHandle;
struct FHellRunTacticalLabLifetime;

struct FTacticalLabEQSItem
{
    FVector2D Position = FVector2D::ZeroVector;
    FString Label;
    float Score = 0.0f;
    bool bValid = false;
};

DECLARE_DELEGATE_TwoParams(FOnTacticalEntityMoved,int32,FVector2D);
DECLARE_DELEGATE_OneParam(FOnTacticalEntitySelected,int32);

/** Native graph-style tactical surface. SNodePanel owns zoom, pan and LOD. */
class STacticalLabSurface final : public SNodePanel
{
public:
    virtual ~STacticalLabSurface() override;

    SLATE_BEGIN_ARGS(STacticalLabSurface){}
        SLATE_ARGUMENT(UTacticalLabScenarioAsset*, Asset)
        SLATE_EVENT(FSimpleDelegate, OnScenarioChanged)
        SLATE_EVENT(FOnTacticalEntityMoved, OnEntityMoved)
        SLATE_EVENT(FOnTacticalEntitySelected, OnEntitySelected)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    void SetAsset(UTacticalLabScenarioAsset* InAsset);
    void FrameAll();
    void SetViewMode(FName InMode);
    void SetRuntimeRoutes(const FHellRunTacticalLabLifetime* Lifetime);
    void SetRuntimeState(const FHellRunTacticalLabScenario* Scenario);
    void SetRuntimeEntities(const TArray<FHellRunTacticalLabEntity>* Entities);
    void SelectEntity(int32 EntityIndex);
    void SetPIEFrame(const FTacticalLabPIEFrame* Frame);
    void CenterOnWorld(FVector2D WorldPosition);
    void SetEQSResults(TArray<FTacticalLabEQSItem> InItems,
        FString InQueryStatus,TOptional<FVector2D> InQueryOrigin=TOptional<FVector2D>());
    void SetEQSPaths(TArray<TArray<FVector2D>> InPaths);
    void ClearEQSResults();

    virtual FReply OnMouseButtonDown(const FGeometry& Geometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(const FGeometry& Geometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry,
        const FPointerEvent& MouseEvent) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
    virtual FReply OnKeyDown(const FGeometry& Geometry,
        const FKeyEvent& KeyEvent) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual int32 OnPaint(const FPaintArgs& Args,
        const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId,
        const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    enum class ETransformMode : uint8 { Translate,Rotate,Scale };
    enum class ETransformHandle : uint8
    {
        None,MoveX,MoveY,Rotate,ScaleX,ScaleY,ScaleUniform
    };

    enum class ESelectionType : uint8
    {
        None,
        Entity,
        Hazard,
        ObstacleStart,
        ObstacleEnd,
        ObstacleBody,
        ShapeBody,
        ShapeMoveX,
        ShapeMoveY,
        ShapeResize,
        ShapeRotate,
        RoutePoint,
        RouteSegment
    };

    struct FSelection
    {
        ESelectionType Type = ESelectionType::None;
        int32 Index = INDEX_NONE;
        int32 SubIndex = INDEX_NONE;
        ETransformHandle Handle = ETransformHandle::None;
        bool IsValid() const { return Type != ESelectionType::None && Index != INDEX_NONE; }
        void Reset() { Type = ESelectionType::None; Index = SubIndex = INDEX_NONE;
            Handle=ETransformHandle::None; }
    };

    static constexpr float WorldToGraphScale = 0.05f;
    FVector2f ToGraph(const FVector2D& World) const;
    FVector2f ToPanel(const FVector2D& World) const;
    FLinearColor EntityColor(uint8 Kind) const;
    FVector2D PanelToWorld(const FGeometry& Geometry,
        FVector2D ScreenPosition) const;
    FSelection HitTest(const FGeometry& Geometry, FVector2D ScreenPosition) const;
    bool GetSelectionPivot(FVector2D& OutPivot) const;
    bool CanRotateSelection() const;
    bool CanScaleSelection() const;
    void TransformSelection(const FVector2D& World,const FVector2D& Delta);
    void RotateSelection(const FVector2D& Pivot,double DeltaDegrees);
    void ScaleSelection(const FVector2D& Pivot,const FVector2D& Previous,
        const FVector2D& Current,ETransformHandle Handle);
    void RemoveSelected();
    void InsertPointOnSelectedRoute();
    FString GetSelectionLabel() const;
    void ApplyMapTexture();
    void OpenContextMenu(const FVector2D& ScreenPosition);
    void AddEntity(uint8 Kind);
    void AddHazard();
    void AddShape(uint8 Kind);
    void AddRoutePoint(bool bFinish);
    void FinishRoute();
    void CancelRoute();
    void ReverseSelectedRoute();
    void AssignSelectedRouteToAgent(FName AgentId);
    void RemoveNearest();
    void Changed();

    TWeakObjectPtr<UTacticalLabScenarioAsset> Asset;
    mutable FSlateBrush MapBrush;
    TSharedPtr<FStreamableHandle> MapLoadHandle;
    FSimpleDelegate OnScenarioChanged;
    FOnTacticalEntityMoved OnEntityMoved;
    FOnTacticalEntitySelected OnEntitySelected;
    FVector2D ContextWorld = FVector2D::ZeroVector;
    FVector2D RightMouseDownScreen = FVector2D::ZeroVector;
    FVector2D LeftMouseDownScreen = FVector2D::ZeroVector;
    FVector2D LastDragWorld = FVector2D::ZeroVector;
    FVector2D HoverWorld = FVector2D::ZeroVector;
    bool bHasHoverWorld = false;
    FSelection Selection;
    ETransformMode TransformMode = ETransformMode::Translate;
    bool bDraggingSelection = false;
    bool bSelectionMoved = false;
    TUniquePtr<FScopedTransaction> DragTransaction;
    TArray<FVector2D> PendingRoutePoints;
    TArray<TArray<FVector2D>> RuntimeRoutes;
    TArray<TArray<FVector2D>> RejectedRuntimeRoutes;
    TSet<FName> RuntimeBlockingObstacleIds;
    TArray<FTacticalLabEQSItem> RuntimeCandidates;
    TArray<FHellRunTacticalLabEntity> RuntimeEntities;
    TOptional<FTacticalLabPIEFrame> PIEFrame;
    TArray<FTacticalLabEQSItem> EQSItems;
    TArray<TArray<FVector2D>> EQSPaths;
    FString EQSStatus;
    TOptional<FVector2D> EQSOrigin;
    FName ViewMode = TEXT("Tactical");
};
