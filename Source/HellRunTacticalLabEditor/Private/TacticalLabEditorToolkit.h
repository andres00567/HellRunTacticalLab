#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Misc/NotifyHook.h"
#include "Core/AI/TacticalLab/HellRunTacticalLab.h"
#include "TacticalLabPIEDebugTypes.h"

class IDetailsView;
class STacticalLabSurface;
class STacticalLabGoalGraph;
class UTacticalLabScenarioAsset;
class UWorld;
class AActor;
class UEnvQuery;
struct FEnvQueryResult;
struct FStreamableHandle;

class FTacticalLabEditorToolkit final : public FAssetEditorToolkit,
    public FNotifyHook, public FGCObject
{
public:
    virtual ~FTacticalLabEditorToolkit() override;
    void Initialize(UTacticalLabScenarioAsset* InAsset,
        TSharedPtr<IToolkitHost> InToolkitHost);
    /** Renderer-enabled smoke path: schedules the exact toolbar bake after layout. */
    void RunBakeDiagnostic();
    /** Opens/bakes/configures the real panel and exercises its Step workflow. */
    void RunPlaygroundDiagnostic();

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("FTacticalLabEditorToolkit"); }
    virtual void NotifyPostChange(const FPropertyChangedEvent&, FProperty*) override;

private:
    TSharedRef<SDockTab> SpawnTacticalTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnGoalGraphTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnInspectorTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnTimelineTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnLifetimesTab(const FSpawnTabArgs& Args);
    void BindToolbar();
    void BakeCurrentMap();
    bool RenderMapPreview(UWorld& World,FString& OutError);
    FString GetMapPreviewCachePath() const;
    void LoadCachedMapPreview();
    void FrameAllNextTick();
    void FrameAll();
    void ResetSimulation();
    void StepSimulation();
    void RunSimulation();
    void StartPlayback();
    void PausePlayback();
    void StopPlayback();
    EActiveTimerReturnType TickPlayback(double CurrentTime,float DeltaTime);
    void RunBatch(int32 Count);
    void ExportReport();
    void RefreshSimulationViews();
    void RefreshAgentInspector();
    void HandleEntitySelected(int32 EntityIndex);
    void SetSelectedGoal(EHellRunTacticalLabGoal Goal);
    void SetSelectedTarget(FName TargetId);
    void ApplySelectedGoalToSquad();
    void RunFindCoverForSelected();
    FHellRunTacticalLabScenario BuildSimulationScenario() const;
    void HandleScenarioEdited();
    void QueueLiveEQS(int32 EntityIndex,FVector2D Position);
    void LaunchLiveEQS();
    UEnvQuery* GetOrCreateNativeTacticalQuery(EHellRunTacticalLabGoal Goal);
    void HandleLiveEQSFinished(TSharedPtr<FEnvQueryResult> Result);
    EActiveTimerReturnType TickPendingEQS(double CurrentTime,float DeltaTime);
    void AbortLiveEQS();
    void RunEQSAtSelectedEnemy();
    void ActivateSection(FName Section);
    void TogglePIEAttachment();
    void TogglePIEFollow();
    void ReturnToLivePIE();
    void StepPIERecording(int32 DeltaFrames);
    void SelectPIEEvent(int64 Sequence);
    void RefreshPIETimeline();
    EActiveTimerReturnType TickPIEFeed(double CurrentTime,float DeltaTime);
    const FTacticalLabPIEFrame* GetDisplayedPIEFrame() const;
    FText GetStatusText() const;

    static const FName TacticalTabId;
    static const FName GoalGraphTabId;
    static const FName InspectorTabId;
    static const FName TimelineTabId;
    static const FName LifetimesTabId;

    TObjectPtr<UTacticalLabScenarioAsset> Asset;
    TSharedPtr<STacticalLabSurface> TacticalSurface;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<STacticalLabGoalGraph> GoalGraph;
    TSharedPtr<SVerticalBox> TimelineBox;
    TSharedPtr<SVerticalBox> LifetimesBox;
    TSharedPtr<SVerticalBox> AgentInspectorBox;
    TUniquePtr<FHellRunTacticalLab> Simulation;
    TArray<TSharedPtr<FHellRunTacticalLabLifetime>> Lifetimes;
    int32 Seed = 1337;
    FString Status;
    FName ActiveSection = TEXT("Tactical");
    bool bSimulationDirty = false;
    bool bPlaying = false;
    bool bMoveAgentsDuringPlayback = true;
    float PlaybackSpeed = 1.0f;
    float DecisionAccumulator = 0.0f;
    float PlaybackUIAccumulator = 0.0f;
    TObjectPtr<AActor> EQSQueryProxy;
    TMap<EHellRunTacticalLabGoal,TObjectPtr<UEnvQuery>> NativeTacticalQueries;
    TSharedPtr<FStreamableHandle> EQSQueryLoadHandle;
    TOptional<FVector2D> PendingEQSPosition;
    int32 PendingEQSEntityIndex = INDEX_NONE;
    int32 SelectedEntityIndex = INDEX_NONE;
    TArray<FHellRunTacticalLabEntity> TransientEQSCandidates;
    int32 ActiveEQSQueryId = INDEX_NONE;
    TOptional<EHellRunTacticalLabGoal> PendingEQSGoalOverride;
    EHellRunTacticalLabGoal ActiveEQSGoal = EHellRunTacticalLabGoal::Auto;
    bool bEQSDebounceScheduled = false;
    bool bEQSTickScheduled = false;
    bool bPendingManualEQS = false;
    double EQSQueryStartedSeconds = 0.0;
    float ActiveCoverTraceHeight = 60.0f;
    bool bPIEAttached = true;
    bool bPIETickerActive = false;
    bool bFollowPIEPlayers = true;
    FGuid LastPIEFollowSessionId;
    uint64 LastPIERecorderRevision = 0;
    int64 LastPIETimelineSequence = MIN_int64;
    double LastPIEUIRefreshSeconds = -BIG_NUMBER;
    double LastPIEInspectorRefreshSeconds = -BIG_NUMBER;
    int32 PIEFrameCursor = INDEX_NONE;
    double PIEReplayWorldTime = -1.0;
    FGuid SelectedPIEAgentGuid;
    FName SelectedPIEAgentId;
};
