#include "TacticalLabEditorToolkit.h"

#include "Algo/Reverse.h"
#include "IHellRunTacticalLabEditorModule.h"
#include "STacticalLabGoalGraph.h"
#include "STacticalLabSurface.h"
#include "TacticalLabEQSGenerator_VoxelNodes.h"
#include "TacticalLabEQSTest_VoxelPath.h"
#include "TacticalLabEQSTest_CoverLOS.h"
#include "TacticalLabScenarioAsset.h"
#include "TacticalLabEQSContext.h"
#include "DataProviders/AIDataProvider_QueryParams.h"
#include "Editor.h"
#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GOAPBrainComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "GameFramework/Actor.h"
#include "HellRunVoxelNavVolume.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "TacticalLabEditor"

DEFINE_LOG_CATEGORY_STATIC(LogHellRunTacticalLabEditor,Log,All);

const FName FTacticalLabEditorToolkit::TacticalTabId(TEXT("TacticalLab_Tactical"));
const FName FTacticalLabEditorToolkit::GoalGraphTabId(TEXT("TacticalLab_GOAP"));
const FName FTacticalLabEditorToolkit::InspectorTabId(TEXT("TacticalLab_Inspector"));
const FName FTacticalLabEditorToolkit::TimelineTabId(TEXT("TacticalLab_Timeline"));
const FName FTacticalLabEditorToolkit::LifetimesTabId(TEXT("TacticalLab_Lifetimes"));

FTacticalLabEditorToolkit::~FTacticalLabEditorToolkit()
{
    AbortLiveEQS();
    if(IsValid(EQSQueryProxy))EQSQueryProxy->Destroy();
}

void FTacticalLabEditorToolkit::Initialize(UTacticalLabScenarioAsset* InAsset,
    TSharedPtr<IToolkitHost> InToolkitHost)
{
    Asset = InAsset;
    LoadCachedMapPreview();
    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<
        FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs Args;
    Args.NotifyHook = this;
    Args.bHideSelectionTip = true;
    DetailsView = PropertyEditor.CreateDetailView(Args);
    // Raw baked arrays can contain thousands of records and are edited on the
    // tactical canvas. Reflecting them into the details tree causes runaway
    // widget allocation after a production-map bake.
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(
        [](const FPropertyAndParent& PropertyAndParent)
        {
            return PropertyAndParent.Property.GetFName()!=
                GET_MEMBER_NAME_CHECKED(UTacticalLabScenarioAsset,Scenario);
        }));
    DetailsView->SetObject(Asset);
    BindToolbar();

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(
        TEXT("HellRunTacticalLabEditor_v3"))
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
        ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
            ->SetSizeCoefficient(.68f)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.52f)
                ->AddTab(TacticalTabId, ETabState::OpenedTab))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.32f)
                ->AddTab(GoalGraphTabId, ETabState::OpenedTab))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.16f)
                ->AddTab(InspectorTabId, ETabState::OpenedTab)))
        ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
            ->SetSizeCoefficient(.32f)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.72f)
                ->AddTab(TimelineTabId, ETabState::OpenedTab))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.28f)
                ->AddTab(LifetimesTabId, ETabState::OpenedTab))));

    InitAssetEditor(EToolkitMode::Standalone, InToolkitHost,
        TEXT("HellRunTacticalLabEditor"), Layout, true, true, Asset);
    RegenerateMenusAndToolbars();
    Status=TEXT("Ready — use Bake Map or load a scenario fixture");
    ResetSimulation();
}

void FTacticalLabEditorToolkit::RunBakeDiagnostic()
{
    if(!TacticalSurface)
    {
        UE_LOG(LogHellRunTacticalLabEditor,Error,
            TEXT("[TacticalLabPanel] Diagnostic bake could not start: tactical surface is missing"));
        return;
    }
    TWeakPtr<FTacticalLabEditorToolkit> WeakThis=SharedThis(this);
    TacticalSurface->RegisterActiveTimer(.75f,
        FWidgetActiveTimerDelegate::CreateLambda([WeakThis](double,float)
        {
            if(const TSharedPtr<FTacticalLabEditorToolkit> Self=WeakThis.Pin())
            {
                UE_LOG(LogHellRunTacticalLabEditor,Display,
                    TEXT("[TacticalLabPanel] Diagnostic invoking the real Bake Map action"));
                Self->BakeCurrentMap();
            }
            return EActiveTimerReturnType::Stop;
        }));
}

void FTacticalLabEditorToolkit::RunPlaygroundDiagnostic()
{
    if(!TacticalSurface)return;
    TWeakPtr<FTacticalLabEditorToolkit> WeakThis=SharedThis(this);
    TacticalSurface->RegisterActiveTimer(.75f,FWidgetActiveTimerDelegate::CreateLambda(
        [WeakThis](double,float)
        {
            const TSharedPtr<FTacticalLabEditorToolkit> Self=WeakThis.Pin();
            if(!Self)return EActiveTimerReturnType::Stop;
            Self->BakeCurrentMap();
            int32 PlayerIndex=INDEX_NONE;
            for(int32 I=0;I<Self->Asset->Scenario.Entities.Num();++I)
                if(Self->Asset->Scenario.Entities[I].Kind==EHellRunTacticalLabEntityKind::Player)
                {PlayerIndex=I;break;}
            if(PlayerIndex!=INDEX_NONE&&!Self->Asset->Scenario.Entities.ContainsByPredicate(
                [](const FHellRunTacticalLabEntity& E){return E.Kind==EHellRunTacticalLabEntityKind::Enemy;}))
            {
                const FVector2D PlayerPosition=Self->Asset->Scenario.Entities[PlayerIndex].Position;
                const FName Archetype=Self->Asset->Scenario.Profiles.IsEmpty()
                    ?FName(TEXT("MeleeZombie")):Self->Asset->Scenario.Profiles[0].ArchetypeId;
                for(int32 Enemy=0;Enemy<3;++Enemy)
                {
                    FHellRunTacticalLabEntity& Added=Self->Asset->Scenario.Entities.AddDefaulted_GetRef();
                    Added.Id=FName(*FString::Printf(TEXT("DiagnosticEnemy_%d"),Enemy+1));
                    Added.Kind=EHellRunTacticalLabEntityKind::Enemy;Added.Team=TEXT("Enemies");
                    Added.ArchetypeId=Archetype;
                    Added.Position=PlayerPosition+FVector2D(-1800.0-Enemy*350.0,(Enemy-1)*450.0);
                }
            }
            int32 EnemyOrdinal=0,FirstEnemyIndex=INDEX_NONE;
            for(int32 I=0;I<Self->Asset->Scenario.Entities.Num();++I)
            {
                FHellRunTacticalLabEntity& Entity=Self->Asset->Scenario.Entities[I];
                if(Entity.Kind!=EHellRunTacticalLabEntityKind::Enemy)continue;
                Entity.Goal=EHellRunTacticalLabGoal::FindCover;
                Entity.SquadId=TEXT("DiagnosticSquad");
                if(PlayerIndex!=INDEX_NONE)
                {Entity.GoalTargetId=Self->Asset->Scenario.Entities[PlayerIndex].Id;
                    Entity.TargetId=Entity.GoalTargetId;}
                if(EnemyOrdinal++==0)FirstEnemyIndex=I;
            }
            Self->HandleEntitySelected(FirstEnemyIndex);Self->ResetSimulation();
            for(int32 Step=0;Step<24&&Self->Simulation&&!Self->Simulation->IsComplete();++Step)
                Self->StepSimulation();
            if(Self->Simulation)
            {
                const FHellRunTacticalLabLifetime& Lifetime=Self->Simulation->GetLifetime();
                int32 AcceptedCandidates=0;
                for(const FHellRunTacticalLabCandidateRecord& Candidate:Lifetime.Candidates)
                    AcceptedCandidates+=Candidate.Score.bAccepted?1:0;
                UE_LOG(LogHellRunTacticalLabEditor,Display,
                    TEXT("[TacticalLabPanel] Playground diagnostic | decisions=%d candidates=%d accepted=%d time=%.2f complete=%s"),
                    Lifetime.Decisions.Num(),Lifetime.Candidates.Num(),
                    AcceptedCandidates,Lifetime.DurationSeconds,
                    Self->Simulation->IsComplete()?TEXT("yes"):TEXT("no"));
            }
            return EActiveTimerReturnType::Stop;
        }));
}

FName FTacticalLabEditorToolkit::GetToolkitFName() const { return TEXT("HellRunTacticalLabEditor"); }
FText FTacticalLabEditorToolkit::GetBaseToolkitName() const { return LOCTEXT("Name", "Hell Run Tactical AI Lab"); }
FString FTacticalLabEditorToolkit::GetWorldCentricTabPrefix() const { return TEXT("Tactical Lab "); }
FLinearColor FTacticalLabEditorToolkit::GetWorldCentricTabColorScale() const { return FLinearColor(.8f,.06f,.035f); }

void FTacticalLabEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    FAssetEditorToolkit::RegisterTabSpawners(Manager);
    WorkspaceMenuCategory = Manager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "Tactical AI Lab"));
    Manager->RegisterTabSpawner(TacticalTabId, FOnSpawnTab::CreateSP(this, &FTacticalLabEditorToolkit::SpawnTacticalTab)).SetDisplayName(LOCTEXT("Tactical", "Tactical View"));
    Manager->RegisterTabSpawner(GoalGraphTabId, FOnSpawnTab::CreateSP(this, &FTacticalLabEditorToolkit::SpawnGoalGraphTab)).SetDisplayName(LOCTEXT("GOAP", "GOAP Graph"));
    Manager->RegisterTabSpawner(InspectorTabId, FOnSpawnTab::CreateSP(this, &FTacticalLabEditorToolkit::SpawnInspectorTab)).SetDisplayName(LOCTEXT("Inspector", "Inspector"));
    Manager->RegisterTabSpawner(TimelineTabId, FOnSpawnTab::CreateSP(this, &FTacticalLabEditorToolkit::SpawnTimelineTab)).SetDisplayName(LOCTEXT("Timeline", "Timeline / Lifetime"));
    Manager->RegisterTabSpawner(LifetimesTabId, FOnSpawnTab::CreateSP(this, &FTacticalLabEditorToolkit::SpawnLifetimesTab)).SetDisplayName(LOCTEXT("Lifetimes", "Lifetime Browser"));
}

void FTacticalLabEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    Manager->UnregisterTabSpawner(TacticalTabId); Manager->UnregisterTabSpawner(GoalGraphTabId);
    Manager->UnregisterTabSpawner(InspectorTabId); Manager->UnregisterTabSpawner(TimelineTabId);
    Manager->UnregisterTabSpawner(LifetimesTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

void FTacticalLabEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(Asset);
    Collector.AddReferencedObject(EQSQueryProxy);
    for(TPair<EHellRunTacticalLabGoal,TObjectPtr<UEnvQuery>>& Query:NativeTacticalQueries)
        Collector.AddReferencedObject(Query.Value);
}

void FTacticalLabEditorToolkit::NotifyPostChange(const FPropertyChangedEvent&, FProperty*)
{
    if (TacticalSurface) TacticalSurface->SetAsset(Asset);
    ResetSimulation();
}

void FTacticalLabEditorToolkit::BindToolbar()
{
    TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
    Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After,
        GetToolkitCommands(), FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& B)
    {
        auto Add = [&B](const TCHAR* Label, const TCHAR* Tip,const TCHAR* Icon, FExecuteAction Action)
        { B.AddToolBarButton(FUIAction(Action), NAME_None, FText::FromString(Label), FText::FromString(Tip),
            FSlateIcon(FAppStyle::GetAppStyleSetName(),Icon)); };
        Add(TEXT("Bake Map"), TEXT("Convert the current production map into a deterministic 2D tactical fixture"),TEXT("Icons.Level"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::BakeCurrentMap));
        Add(TEXT("Frame All"), TEXT("Frame the complete baked map"),TEXT("Icons.FrameActor"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::FrameAll));
        B.AddSeparator();
        Add(TEXT("Attach PIE"), TEXT("Attach or detach the tactical debugger from the active PIE world"),TEXT("Icons.Pinned"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::TogglePIEAttachment));
        Add(TEXT("Follow Players"), TEXT("Toggle automatic centering on the live PIE player group; turn off to pan freely"),TEXT("Icons.Camera"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::TogglePIEFollow));
        Add(TEXT("Rewind"), TEXT("Move one recorded PIE snapshot backward"),TEXT("Icons.Undo"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::StepPIERecording,-1));
        Add(TEXT("Forward"), TEXT("Move one recorded PIE snapshot forward"),TEXT("Icons.Redo"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::StepPIERecording,1));
        Add(TEXT("Live"), TEXT("Return to the newest recorded PIE snapshot"),TEXT("Icons.Refresh"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::ReturnToLivePIE));
        B.AddSeparator();
        Add(TEXT("Play"), TEXT("Start or resume real-time interactive playback"),TEXT("Icons.Toolbar.Play"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::StartPlayback));
        Add(TEXT("Pause"), TEXT("Freeze simulation time while preserving state and editor queries"),TEXT("Icons.Toolbar.Pause"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::PausePlayback));
        Add(TEXT("Stop"), TEXT("Stop playback without resetting the current simulation state"),TEXT("Icons.Toolbar.Stop"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::StopPlayback));
        Add(TEXT("Run"), TEXT("Run this lifetime to completion"),TEXT("Icons.Play"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::RunSimulation));
        Add(TEXT("Step"), TEXT("Advance one tactical decision"),TEXT("Icons.Next"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::StepSimulation));
        Add(TEXT("Reset"), TEXT("Reset using the current seed"),TEXT("Icons.Refresh"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::ResetSimulation));
        Add(TEXT("Run 100"), TEXT("Run 100 deterministic lifetimes"),TEXT("Icons.CircleArrowRight"), FExecuteAction::CreateLambda([this]{RunBatch(100);}));
        Add(TEXT("Run 1000"), TEXT("Run 1000 deterministic lifetimes"),TEXT("Icons.Launch"), FExecuteAction::CreateLambda([this]{RunBatch(1000);}));
        B.AddSeparator();
        Add(TEXT("Export Report"), TEXT("Export the selected/current lifetime as JSON"),TEXT("Icons.Export"), FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::ExportReport));
        B.AddSeparator();
        Add(TEXT("Run EQS"),TEXT("Run the configured live EQS query at the selected or last-dragged enemy"),TEXT("Icons.Search"),
            FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::RunEQSAtSelectedEnemy));
        Add(TEXT("Find Cover"),TEXT("Evaluate cover and voxel paths without moving the simulation"),
            TEXT("Icons.Visibility"),FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::RunFindCoverForSelected));
        B.AddComboButton(FUIAction(),FOnGetContent::CreateLambda([this]()
        {
            FMenuBuilder Menu(true,nullptr);
            Menu.AddMenuEntry(FText::FromString(TEXT("Movement Enabled")),
                FText::FromString(TEXT("Agents follow accepted routes during playback")),FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this]{bMoveAgentsDuringPlayback=true;}),
                    FCanExecuteAction(),FIsActionChecked::CreateLambda([this]{return bMoveAgentsDuringPlayback;})),
                NAME_None,EUserInterfaceActionType::RadioButton);
            Menu.AddMenuEntry(FText::FromString(TEXT("Analysis Frozen")),
                FText::FromString(TEXT("Clock and decisions advance while agent transforms stay fixed")),FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this]{bMoveAgentsDuringPlayback=false;}),
                    FCanExecuteAction(),FIsActionChecked::CreateLambda([this]{return !bMoveAgentsDuringPlayback;})),
                NAME_None,EUserInterfaceActionType::RadioButton);
            Menu.AddMenuSeparator();
            for(const float Speed:{.25f,.5f,1.0f,2.0f,4.0f})
                Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%.2gx speed"),Speed)),FText(),FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this,Speed]{PlaybackSpeed=Speed;}),
                        FCanExecuteAction(),FIsActionChecked::CreateLambda([this,Speed]{return FMath::IsNearlyEqual(PlaybackSpeed,Speed);})),
                    NAME_None,EUserInterfaceActionType::RadioButton);
            return Menu.MakeWidget();
        }),FText::FromString(TEXT("Execution")),FText::FromString(TEXT("Playback mode and simulation speed")),FSlateIcon(FAppStyle::GetAppStyleSetName(),TEXT("Icons.Settings")));
    }));
    AddToolbarExtender(Extender);
}

TSharedRef<SDockTab> FTacticalLabEditorToolkit::SpawnTacticalTab(const FSpawnTabArgs&)
{
    auto RailButton=[this](const TCHAR* Label,const TCHAR* Section)
    {
        const FName SectionName(Section);
        return StaticCastSharedRef<SWidget>(SNew(SBox).HeightOverride(50).MinDesiredWidth(112)
        [SNew(SButton).ContentPadding(FMargin(10,8)).HAlign(HAlign_Fill)
            .ButtonColorAndOpacity_Lambda([this,SectionName]
            {return ActiveSection==SectionName?FLinearColor(.025f,.18f,.31f,1):FLinearColor(.006f,.012f,.017f,1);})
            .OnClicked_Lambda([this,SectionName]
            {ActivateSection(SectionName);return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(Label)).Justification(ETextJustify::Center)
                .Clipping(EWidgetClipping::ClipToBoundsAlways)
                .ColorAndOpacity_Lambda([this,SectionName]
                {return ActiveSection==SectionName?FSlateColor(FLinearColor(.3f,.75f,1)):FSlateColor(FLinearColor(.55f,.63f,.68f));})
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))]]);
    };
    auto LayerRow=[this](const TCHAR* Label,bool* Visible,bool* Locked)
    {
        return StaticCastSharedRef<SWidget>(SNew(SBox).HeightOverride(28)
        [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)
        [SNew(STextBlock).Text(FText::FromString(Label)).Font(
            FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9))]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0)
        [SNew(SCheckBox).ToolTipText(LOCTEXT("LayerVisibleTip","Show or hide this layer"))
            .IsChecked_Lambda([Visible]{return *Visible?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Visible](ECheckBoxState State)
            {*Visible=State==ECheckBoxState::Checked;if(TacticalSurface)TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("LayerVisible","V"))]]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
        [Locked?StaticCastSharedRef<SWidget>(SNew(SCheckBox)
            .ToolTipText(LOCTEXT("LayerLockTip","Locked layers remain visible but cannot be selected"))
            .IsChecked_Lambda([Locked]{return *Locked?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Locked](ECheckBoxState State)
            {*Locked=State==ECheckBoxState::Checked;if(TacticalSurface)TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("LayerLocked","L"))])
            :StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(18))]]);
    };
    return SNew(SDockTab)
    [SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth()
    [SNew(SBox).WidthOverride(176).MinDesiredWidth(176)
    [SNew(SBorder).Padding(6).BorderBackgroundColor(FLinearColor(.006f,.012f,.017f))
    [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("SCENARIO"),TEXT("Scenario"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("SIMULATION"),TEXT("Simulation"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("AGENTS"),TEXT("Agents"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("SQUADS"),TEXT("Squads"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("GOAP"),TEXT("GOAP"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("TACTICAL"),TEXT("Tactical"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("ROUTES"),TEXT("Routes"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("REPLAY"),TEXT("Replay"))]
        +SVerticalBox::Slot().AutoHeight()[RailButton(TEXT("REPORTS"),TEXT("Reports"))]
        +SVerticalBox::Slot().AutoHeight().Padding(6,10,6,3)
        [SNew(STextBlock).Text(LOCTEXT("LayerHeader","LAYERS  VISIBLE / LOCK"))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),9))
            .ColorAndOpacity(FLinearColor(.35f,.72f,.85f))]
        +SVerticalBox::Slot().AutoHeight()[LayerRow(TEXT("Agents"),&Asset->bShowEntities,&Asset->bLockEntities)]
        +SVerticalBox::Slot().AutoHeight()[LayerRow(TEXT("Geometry"),&Asset->bShowGeometry,&Asset->bLockGeometry)]
        +SVerticalBox::Slot().AutoHeight()[LayerRow(TEXT("Routes"),&Asset->bShowRoutes,&Asset->bLockRoutes)]
        +SVerticalBox::Slot().AutoHeight()[LayerRow(TEXT("EQS"),&Asset->bShowEQS,nullptr)]
        +SVerticalBox::Slot().AutoHeight()[LayerRow(TEXT("Vision"),&Asset->bShowVisionCones,nullptr)]]]]
    +SHorizontalBox::Slot().FillWidth(1)
    [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()
    [SNew(SBorder).Padding(8,5).BorderBackgroundColor(FLinearColor(.01f,.018f,.024f))
    [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [SNew(STextBlock).Text(LOCTEXT("TacticalHeader","TACTICAL VIEW"))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))]
        +SHorizontalBox::Slot().FillWidth(1).Padding(12,0).VAlign(VAlign_Center)
        [SNew(STextBlock).Text(this,&FTacticalLabEditorToolkit::GetStatusText)
            .Clipping(EWidgetClipping::ClipToBoundsAlways)
            .ColorAndOpacity(FLinearColor(.25f,.78f,.9f))]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowGrid?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowGrid=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("Grid","Grid"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowGeometry?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowGeometry=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("Geometry","Geometry"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowCandidates?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowCandidates=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("Candidates","Candidates"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowLabels?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowLabels=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("Labels","Labels"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowVisionCones?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowVisionCones=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("Vision","FOV"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowConfiguredFOV?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowConfiguredFOV=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("ConfiguredFOV","Configured"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bShowResolvedVisibility?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Asset->bShowResolvedVisibility=V==ECheckBoxState::Checked;TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);})
            [SNew(STextBlock).Text(LOCTEXT("ResolvedFOV","Resolved"))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)
        [SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->bEnableLiveEQS?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V)
            {Asset->bEnableLiveEQS=V==ECheckBoxState::Checked;if(!Asset->bEnableLiveEQS){AbortLiveEQS();if(TacticalSurface)TacticalSurface->ClearEQSResults();}})
            [SNew(STextBlock).Text(LOCTEXT("LiveEQS","Live EQS"))]]]]
    +SVerticalBox::Slot().FillHeight(1)
    [SAssignNew(TacticalSurface, STacticalLabSurface).Asset(Asset)
        .OnScenarioChanged(FSimpleDelegate::CreateSP(this,
            &FTacticalLabEditorToolkit::HandleScenarioEdited))
        .OnEntityMoved(FOnTacticalEntityMoved::CreateSP(this,
            &FTacticalLabEditorToolkit::QueueLiveEQS))
        .OnEntitySelected(FOnTacticalEntitySelected::CreateSP(this,
            &FTacticalLabEditorToolkit::HandleEntitySelected))]]];
}

TSharedRef<SDockTab> FTacticalLabEditorToolkit::SpawnGoalGraphTab(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SAssignNew(GoalGraph,STacticalLabGoalGraph)];
}

TSharedRef<SDockTab> FTacticalLabEditorToolkit::SpawnInspectorTab(const FSpawnTabArgs&)
{
    TSharedRef<SDockTab> Tab=SNew(SDockTab)
    [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(6)
        [SAssignNew(AgentInspectorBox,SVerticalBox)]
        +SVerticalBox::Slot().FillHeight(1)
        [DetailsView.ToSharedRef()]];
    RefreshAgentInspector();
    RefreshSimulationViews();
    return Tab;
}

TSharedRef<SDockTab> FTacticalLabEditorToolkit::SpawnTimelineTab(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SNew(SScrollBox).Orientation(Orient_Vertical)
        +SScrollBox::Slot()[SAssignNew(TimelineBox,SVerticalBox)]];
}

TSharedRef<SDockTab> FTacticalLabEditorToolkit::SpawnLifetimesTab(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(LifetimesBox,SVerticalBox)]];
}

void FTacticalLabEditorToolkit::HandleEntitySelected(const int32 EntityIndex)
{
    if(const FTacticalLabPIEFrame* Frame=GetDisplayedPIEFrame())
    {
        SelectedEntityIndex=INDEX_NONE;
        SelectedPIEAgentId=Frame->Agents.IsValidIndex(EntityIndex)
            ?Frame->Agents[EntityIndex].Entity.Id:NAME_None;
        RefreshAgentInspector();
        RefreshSimulationViews();
        if(!SelectedPIEAgentId.IsNone()&&GetTabManager().IsValid())
            GetTabManager()->TryInvokeTab(InspectorTabId);
        return;
    }
    SelectedPIEAgentId=NAME_None;
    SelectedEntityIndex=EntityIndex;
    if(TacticalSurface&&EntityIndex!=INDEX_NONE)TacticalSurface->SelectEntity(EntityIndex);
    if(Asset&&Asset->Scenario.Entities.IsValidIndex(EntityIndex)&&
        Asset->Scenario.Entities[EntityIndex].Kind==EHellRunTacticalLabEntityKind::Enemy)
    {
        PendingEQSEntityIndex=EntityIndex;
        PendingEQSPosition=Asset->Scenario.Entities[EntityIndex].Position;
        QueueLiveEQS(EntityIndex,PendingEQSPosition.GetValue());
    }
    RefreshAgentInspector();
    RefreshSimulationViews();
    if(EntityIndex!=INDEX_NONE&&GetTabManager().IsValid())
        GetTabManager()->TryInvokeTab(InspectorTabId);
}

void FTacticalLabEditorToolkit::SetSelectedGoal(const EHellRunTacticalLabGoal Goal)
{
    if(!Asset||!Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex))return;
    Asset->Modify();
    FHellRunTacticalLabEntity& Entity=Asset->Scenario.Entities[SelectedEntityIndex];
    Entity.Goal=Goal;
    if(Entity.GoalTargetId.IsNone()&&Goal!=EHellRunTacticalLabGoal::HoldPosition)
        for(const FHellRunTacticalLabEntity& Candidate:Asset->Scenario.Entities)
            if(Candidate.bAlive&&Candidate.Kind==EHellRunTacticalLabEntityKind::Player)
            {Entity.GoalTargetId=Candidate.Id;Entity.TargetId=Candidate.Id;break;}
    HandleScenarioEdited();RefreshAgentInspector();
}

void FTacticalLabEditorToolkit::SetSelectedTarget(const FName TargetId)
{
    if(!Asset||!Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex))return;
    Asset->Modify();
    FHellRunTacticalLabEntity& Entity=Asset->Scenario.Entities[SelectedEntityIndex];
    Entity.GoalTargetId=TargetId;Entity.TargetId=TargetId;
    HandleScenarioEdited();RefreshAgentInspector();
}

void FTacticalLabEditorToolkit::ApplySelectedGoalToSquad()
{
    if(!Asset||!Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex))return;
    const FHellRunTacticalLabEntity Source=Asset->Scenario.Entities[SelectedEntityIndex];
    if(Source.SquadId.IsNone())
    {Status=TEXT("Assign a Squad ID before applying a squad goal.");RefreshSimulationViews();return;}
    Asset->Modify();int32 Applied=0;
    for(FHellRunTacticalLabEntity& Entity:Asset->Scenario.Entities)
        if(Entity.SquadId==Source.SquadId&&Entity.Kind==EHellRunTacticalLabEntityKind::Enemy)
        {Entity.Goal=Source.Goal;Entity.GoalTargetId=Source.GoalTargetId;
            Entity.TargetId=Source.TargetId;++Applied;}
    HandleScenarioEdited();
    Status=FString::Printf(TEXT("Applied %s to %d members of squad %s"),
        *StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(
            static_cast<int64>(Source.Goal)),Applied,*Source.SquadId.ToString());
    RefreshAgentInspector();RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::RunFindCoverForSelected()
{
    if(const FTacticalLabPIEFrame* Frame=GetDisplayedPIEFrame())
    {
        const FTacticalLabPIEAgentSnapshot* Agent=Frame->Agents.FindByPredicate(
            [this](const FTacticalLabPIEAgentSnapshot& Candidate)
            {return Candidate.Entity.Id==SelectedPIEAgentId;});
        if(!Agent||Agent->Entity.Kind!=EHellRunTacticalLabEntityKind::Enemy)
        {
            Status=TEXT("Attach PIE and select a live AI pawn before running Find Cover.");
            RefreshSimulationViews();
            return;
        }
        Asset->bShowCandidates=true;
        PendingEQSEntityIndex=INDEX_NONE;
        PendingEQSPosition=Agent->Entity.Position;
        PendingEQSGoalOverride=EHellRunTacticalLabGoal::FindCover;
        bPendingManualEQS=true;
        LaunchLiveEQS();
        return;
    }
    if(!Asset||!Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex))return;
    const FHellRunTacticalLabEntity& Agent=Asset->Scenario.Entities[SelectedEntityIndex];
    const bool bHasProfile=Asset->Scenario.Profiles.ContainsByPredicate(
        [&Agent](const FHellRunEnemySimulationProfile& P){return P.ArchetypeId==Agent.ArchetypeId;});
    if(!bHasProfile)
    {
        Status=FString::Printf(TEXT("Find Cover requires a profiled AI agent; %s has no tactical/perception profile."),
            *Agent.Id.ToString());
        RefreshSimulationViews();
        return;
    }
    Asset->bShowCandidates=true;
    PendingEQSEntityIndex=SelectedEntityIndex;
    PendingEQSPosition=Agent.Position;
    PendingEQSGoalOverride=EHellRunTacticalLabGoal::FindCover;
    bPendingManualEQS=true;
    LaunchLiveEQS();
}

void FTacticalLabEditorToolkit::RefreshAgentInspector()
{
    if(!AgentInspectorBox)return;
    AgentInspectorBox->ClearChildren();
    if(const FTacticalLabPIEFrame* Frame=GetDisplayedPIEFrame())
    {
        const FTacticalLabPIEAgentSnapshot* Agent=Frame->Agents.FindByPredicate(
            [this](const FTacticalLabPIEAgentSnapshot& Candidate)
            {return Candidate.Entity.Id==SelectedPIEAgentId;});
        if(!Agent)
        {
            Agent=Frame->Agents.FindByPredicate([](const FTacticalLabPIEAgentSnapshot& Candidate)
                {return Candidate.bHasGOAP;});
            if(!Agent&&!Frame->Agents.IsEmpty())Agent=&Frame->Agents[0];
            if(Agent)SelectedPIEAgentId=Agent->Entity.Id;
        }
        if(!Agent)
        {
            AgentInspectorBox->AddSlot().AutoHeight().Padding(8)
            [SNew(STextBlock).Text(LOCTEXT("NoPIEAgents","PIE is attached, but no controlled pawns are currently available."))];
            return;
        }
        const FString FeedMode=PIEFrameCursor==INDEX_NONE?TEXT("LIVE"):TEXT("REPLAY");
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,4)
        [SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("PIE %s  |  %s  |  t=%.2fs"),
            *FeedMode,*Agent->Entity.Id.ToString(),Frame->WorldTime)))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11))
            .ColorAndOpacity(FLinearColor(.25f,.85f,.95f))];
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
        [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(
            TEXT("Position: %.0f, %.0f\nFacing: %.2f, %.2f\nVelocity: %.0f cm/s\nRuntime class: %s"),
            Agent->Entity.Position.X,Agent->Entity.Position.Y,Agent->Entity.Facing.X,
            Agent->Entity.Facing.Y,Agent->Entity.Velocity.Size(),*Agent->Entity.ArchetypeId.ToString())))];
        if(Agent->Entity.Kind==EHellRunTacticalLabEntityKind::Enemy)
            AgentInspectorBox->AddSlot().AutoHeight().Padding(6,5)
            [SNew(SButton).Text(LOCTEXT("FindCoverPIE","Find Cover Now (PIE, No Movement)"))
                .ToolTipText(LOCTEXT("FindCoverPIETip","Runs the tactical cover EQS in the active PIE world using this pawn as the query context."))
                .OnClicked_Lambda([this]{RunFindCoverForSelected();return FReply::Handled();})];
        FString Blockers;
        for(const FTacticalLabPIEVisionRay& Ray:Agent->VisionRays)
            if(Ray.bBlocked)
            {
                Blockers+=FString::Printf(TEXT("\n  %s / %s"),
                    *Ray.BlockingActor.ToString(),*Ray.BlockingComponent.ToString());
                if(Blockers.Len()>700){Blockers+=TEXT("\n  ...");break;}
            }
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,8,6,2)
        [SNew(STextBlock).Text(LOCTEXT("PIEPerceptionHeader","RUNTIME PERCEPTION / NAVIGATION"))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))
            .ColorAndOpacity(FLinearColor(.85f,.68f,.2f))];
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
        [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(
            TEXT("Sight: %.0f cm, half-angle %.1f deg\nResolved rays: %d / %d\nPath points: %d\nBlocking hits:%s"),
            Agent->VisionRange,Agent->VisionHalfAngle,Agent->VisionRays.Num(),
            Agent->VisionRayCount,Agent->MovementPath.Num(),Blockers.IsEmpty()?TEXT(" none"):*Blockers)))];
        if(Agent->bHasGOAP)
        {
            const FGOAPBrainDebugSnapshot& Debug=Agent->GOAP;
            const FString Plan=FString::JoinBy(Debug.RemainingPlan,TEXT(" -> "),
                [](const FName Name){return Name.ToString();});
            FString Scores;
            for(const FGOAPGoalScore& Score:Debug.GoalScores)
                Scores+=FString::Printf(TEXT("\n  %s: %.2f%s  %s"),*Score.GoalName.ToString(),
                    Score.Score,Score.bEligible?TEXT(""):TEXT(" (ineligible)"),*Score.Reason);
            FString Facts;
            for(int32 I=0;I<Debug.Facts.Num()&&I<24;++I)
            {
                const FGOAPFactDebugEntry& Fact=Debug.Facts[I];
                Facts+=FString::Printf(TEXT("\n  %s = %s  [%s, %.0f%%]"),
                    *Fact.Name.ToString(),*Fact.Value.ToString(),
                    Fact.bUsingDefault?TEXT("default"):*Fact.Source.ToString(),Fact.Confidence*100.0f);
            }
            AgentInspectorBox->AddSlot().AutoHeight().Padding(6,8,6,2)
            [SNew(STextBlock).Text(LOCTEXT("PIEGOAPHeader","LIVE GOAP BRAIN"))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))
                .ColorAndOpacity(FLinearColor(.35f,.9f,.65f))];
            AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
            [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(
                TEXT("Domain: %s\nGoal: %s\nAction: %s (%s)\nPlan: %s\nReplan: %s\nWorld revision: %d\n\nGOAL SCORES%s\n\nFACTS%s"),
                *Debug.DomainName.ToString(),*Debug.ActiveGoal.ToString(),*Debug.ActiveAction.ToString(),
                *UEnum::GetValueAsString(Debug.ActionStatus),Plan.IsEmpty()?TEXT("none"):*Plan,
                *Debug.LastReplanReason,Debug.WorldStateRevision,*Scores,*Facts)))];
        }
        else
            AgentInspectorBox->AddSlot().AutoHeight().Padding(6,8)
            [SNew(STextBlock).Text(LOCTEXT("NoLiveGOAPBrain","This pawn has no running GOAP brain component."))
                .ColorAndOpacity(FLinearColor(.9f,.55f,.2f))];
        return;
    }
    if(!Asset||!Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex))
    {
        AgentInspectorBox->AddSlot().AutoHeight().Padding(8)
        [SNew(STextBlock).Text(LOCTEXT("SelectAgentIntent","Select an enemy to configure its GOAP goal, target, squad, and cover query."))
            .AutoWrapText(true).ColorAndOpacity(FLinearColor(.55f,.68f,.75f))];
        return;
    }
    const int32 Index=SelectedEntityIndex;
    auto GoalText=[this,Index]
    {
        return FText::FromString(Asset&&Asset->Scenario.Entities.IsValidIndex(Index)
            ?StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(
                static_cast<int64>(Asset->Scenario.Entities[Index].Goal)):TEXT("None"));
    };
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,4)
    [SNew(STextBlock).Text_Lambda([this,Index]
        {return FText::FromString(Asset&&Asset->Scenario.Entities.IsValidIndex(Index)
            ?FString::Printf(TEXT("AGENT INTENT  |  %s"),*Asset->Scenario.Entities[Index].Id.ToString()):TEXT("AGENT INTENT"));})
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11)).ColorAndOpacity(FLinearColor(.25f,.78f,.9f))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,3)
    [SNew(STextBlock).Text(LOCTEXT("GoalLabel","Goal"))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
    [SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda(GoalText)]
        .OnGetMenuContent_Lambda([this]
        {
            FMenuBuilder Menu(true,nullptr);
            for(int32 Value=0;Value<=static_cast<int32>(EHellRunTacticalLabGoal::Regroup);++Value)
            {
                const auto Goal=static_cast<EHellRunTacticalLabGoal>(Value);
                const FString Name=StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(Value);
                Menu.AddMenuEntry(FText::FromString(Name),FText::GetEmpty(),FSlateIcon(),
                    FUIAction(FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::SetSelectedGoal,Goal)));
            }
            return Menu.MakeWidget();
        })];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,3)
    [SNew(STextBlock).Text(LOCTEXT("GoalTargetLabel","Goal Target"))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
    [SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this,Index]
        {return FText::FromName(Asset&&Asset->Scenario.Entities.IsValidIndex(Index)
            ?Asset->Scenario.Entities[Index].GoalTargetId:NAME_None);})]
        .OnGetMenuContent_Lambda([this]
        {
            FMenuBuilder Menu(true,nullptr);
            Menu.AddMenuEntry(LOCTEXT("NoGoalTarget","None"),FText::GetEmpty(),FSlateIcon(),
                FUIAction(FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::SetSelectedTarget,FName())));
            if(Asset)for(const FHellRunTacticalLabEntity& Entity:Asset->Scenario.Entities)
                if(Entity.Kind!=EHellRunTacticalLabEntityKind::Candidate&&
                    (!Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex)||Entity.Id!=Asset->Scenario.Entities[SelectedEntityIndex].Id))
                    Menu.AddMenuEntry(FText::FromName(Entity.Id),FText::FromString(UEnum::GetValueAsString(Entity.Kind)),FSlateIcon(),
                        FUIAction(FExecuteAction::CreateSP(this,&FTacticalLabEditorToolkit::SetSelectedTarget,Entity.Id)));
            return Menu.MakeWidget();
        })];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,3)
    [SNew(STextBlock).Text(LOCTEXT("SquadIdLabel","Squad ID"))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
    [SNew(SEditableTextBox).Text_Lambda([this,Index]
        {return FText::FromName(Asset&&Asset->Scenario.Entities.IsValidIndex(Index)
            ?Asset->Scenario.Entities[Index].SquadId:NAME_None);})
        .OnTextCommitted_Lambda([this,Index](const FText& Text,ETextCommit::Type)
        {if(Asset&&Asset->Scenario.Entities.IsValidIndex(Index)){Asset->Modify();Asset->Scenario.Entities[Index].SquadId=FName(*Text.ToString());HandleScenarioEdited();RefreshAgentInspector();}})];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,5)
    [SNew(SCheckBox).IsChecked_Lambda([this,Index]
        {return Asset&&Asset->Scenario.Entities.IsValidIndex(Index)&&Asset->Scenario.Entities[Index].bMovementGranted
            ?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([this,Index](ECheckBoxState State)
        {if(Asset&&Asset->Scenario.Entities.IsValidIndex(Index)){Asset->Modify();Asset->Scenario.Entities[Index].bMovementGranted=State==ECheckBoxState::Checked;HandleScenarioEdited();}})
        [SNew(STextBlock).Text(LOCTEXT("MovementGranted","Movement Granted"))]];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,4)
    [SNew(SButton).Text(LOCTEXT("ApplySquadGoal","Apply Goal + Target to Squad"))
        .OnClicked_Lambda([this]{ApplySelectedGoalToSquad();return FReply::Handled();})];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,4)
    [SNew(STextBlock).Text_Lambda([this,Index]
        {
            if(!Asset)return FText::GetEmpty();
            if(!Asset->LiveEQSQuery.IsNull())return FText::FromString(
                FString::Printf(TEXT("EQS SOURCE  |  Override: %s"),*Asset->LiveEQSQuery.GetAssetName()));
            const auto Goal=Asset->Scenario.Entities.IsValidIndex(Index)
                ?Asset->Scenario.Entities[Index].Goal:EHellRunTacticalLabGoal::Auto;
            return FText::FromString(FString::Printf(TEXT("EQS SOURCE  |  Native Tactical Lab: %s"),
                *StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(static_cast<int64>(Goal))));
        }).ColorAndOpacity(FLinearColor(.2f,.9f,.72f))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,4)
    [SNew(SButton).Text(LOCTEXT("FindCoverEQS","Find Cover (No Movement)"))
        .ToolTipText(LOCTEXT("FindCoverEQSTip","Evaluate and draw cover candidates and voxel spline paths without stepping the simulation."))
        .OnClicked_Lambda([this]{RunFindCoverForSelected();return FReply::Handled();})];

    const FHellRunTacticalLabLifetime* Lifetime=Simulation?&Simulation->GetLifetime():
        (Lifetimes.IsEmpty()?nullptr:Lifetimes.Last().Get());
    const FName AgentId=Asset->Scenario.Entities[Index].Id;
    const FHellRunTacticalLabEntity* RuntimeAgent=&Asset->Scenario.Entities[Index];
    if(Simulation)
        if(const FHellRunTacticalLabEntity* Found=Simulation->GetState().Entities.FindByPredicate(
            [AgentId](const FHellRunTacticalLabEntity& E){return E.Id==AgentId;}))
            RuntimeAgent=Found;
    const FHellRunTacticalLabDecision* Decision=nullptr;
    if(Lifetime)
        for(int32 DecisionIndex=Lifetime->Decisions.Num()-1;DecisionIndex>=0;--DecisionIndex)
            if(Lifetime->Decisions[DecisionIndex].AgentId==AgentId)
            {Decision=&Lifetime->Decisions[DecisionIndex];break;}
    const FHellRunTacticalLabEntity* Target=nullptr;
    const FName TargetId=!RuntimeAgent->GoalTargetId.IsNone()
        ?RuntimeAgent->GoalTargetId:RuntimeAgent->TargetId;
    if(Simulation)Target=Simulation->GetState().Entities.FindByPredicate(
        [TargetId](const FHellRunTacticalLabEntity& E){return E.Id==TargetId;});
    if(!Target)Target=Asset->Scenario.Entities.FindByPredicate(
        [TargetId](const FHellRunTacticalLabEntity& E){return E.Id==TargetId;});
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,10,6,2)
    [SNew(STextBlock).Text(LOCTEXT("AgentStateHeader","AGENT  |  SIMULATED STATE"))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))
        .ColorAndOpacity(FLinearColor(.25f,.78f,.9f))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
    [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(
        TEXT("Position: %.0f, %.0f\nFacing: %.2f, %.2f\nVelocity: %.0f cm/s\nTeam: %s  Squad: %s  Role: %s\nTarget: %s"),
        RuntimeAgent->Position.X,RuntimeAgent->Position.Y,RuntimeAgent->Facing.X,
        RuntimeAgent->Facing.Y,RuntimeAgent->Velocity.Size(),*RuntimeAgent->Team.ToString(),
        *RuntimeAgent->SquadId.ToString(),*RuntimeAgent->SquadRole.ToString(),
        Target?*Target->Id.ToString():TEXT("None"))))];
    const float TargetDistance=Target?FVector2D::Distance(RuntimeAgent->Position,Target->Position):0.0f;
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,8,6,2)
    [SNew(STextBlock).Text(LOCTEXT("StateComparisonHeader","GLOBAL STATE  <->  AGENT PLANNING INPUT"))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))
        .ColorAndOpacity(FLinearColor(.35f,.9f,.65f))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
    [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(
        TEXT("GLOBAL\nTarget alive: %s\nTarget position: %s\nDistance: %.0f cm\n\nPLANNING INPUT / BELIEF\nHasTarget: %s\nUnderFire: %s\nMovementGranted: %s\nSource: deterministic simulation snapshot"),
        Target&&Target->bAlive?TEXT("true"):TEXT("false"),
        Target?*FString::Printf(TEXT("%.0f, %.0f"),Target->Position.X,Target->Position.Y):TEXT("unknown"),
        TargetDistance,Target?TEXT("true"):TEXT("false"),
        RuntimeAgent->bUnderFire?TEXT("true"):TEXT("false"),
        RuntimeAgent->bMovementGranted?TEXT("true"):TEXT("false"))))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,10,6,3)
    [SNew(STextBlock).Text(LOCTEXT("ResolvedDecisionHeader","RESOLVED DECISION"))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10)).ColorAndOpacity(FLinearColor(.95f,.55f,.12f))];
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,2)
    [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Decision
        ?FString::Printf(TEXT("Intent: %s\nCandidate: %s\nRoute: %s\nPlan: %s\nReason: %s"),
            *Decision->Intent.ToString(),*Decision->SelectedCandidateId.ToString(),
            *Decision->SelectedRouteId.ToString(),*FString::JoinBy(Decision->GOAPPlan,
                TEXT(" -> "),[](FName N){return N.ToString();}),*Decision->Reason)
        :TEXT("No decision has been evaluated for this agent.")))];
    if(Decision)
    {
        TArray<TPair<FName,float>> SortedScores;
        for(const TPair<FName,float>& Pair:Decision->GOAPGoalScores)SortedScores.Add(Pair);
        SortedScores.Sort([](const TPair<FName,float>& A,const TPair<FName,float>& B)
            {return A.Value>B.Value;});
        FString GoalScoreText=TEXT("GOAL SCORES");
        for(const TPair<FName,float>& Pair:SortedScores)
            GoalScoreText+=FString::Printf(TEXT("\n%s  %.3f  %s"),*Pair.Key.ToString(),Pair.Value,
                *Decision->GOAPGoalReasons.FindRef(Pair.Key));
        if(!Decision->GOAPFailureReason.IsEmpty())
            GoalScoreText+=FString::Printf(TEXT("\nPLAN FAILURE  %s"),*Decision->GOAPFailureReason);
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,4)
        [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(GoalScoreText))
            .ColorAndOpacity(FLinearColor(.72f,.58f,.95f))];

        FString PlanText=TEXT("ACTIVE PLAN");
        if(Decision->GOAPPlan.IsEmpty())
            PlanText+=Decision->GOAPFailureReason.IsEmpty()
                ?TEXT("\nNo gameplay plan was returned.")
                :FString::Printf(TEXT("\nFAILED: %s"),*Decision->GOAPFailureReason);
        else
        {
            for(int32 ActionIndex=0;ActionIndex<Decision->GOAPPlan.Num();++ActionIndex)
                PlanText+=FString::Printf(TEXT("\n%s  %s"),ActionIndex==0?TEXT(">") : TEXT(" "),
                    *Decision->GOAPPlan[ActionIndex].ToString());
            PlanText+=TEXT("\nAction execution state is not emitted by the gameplay bridge yet.");
        }
        PlanText+=TEXT("\nReplan reason: not emitted by the gameplay bridge.");
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,5)
        [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(PlanText))
            .ColorAndOpacity(FLinearColor(.92f,.72f,.3f))];

        TArray<const FHellRunTacticalLabCandidateRecord*> AgentCandidates;
        if(Lifetime)for(const FHellRunTacticalLabCandidateRecord& Candidate:Lifetime->Candidates)
            if(Candidate.DecisionId==Decision->DecisionId)AgentCandidates.Add(&Candidate);
        AgentCandidates.Sort([](const FHellRunTacticalLabCandidateRecord& A,
            const FHellRunTacticalLabCandidateRecord& B)
            {return A.Score.FinalScore>B.Score.FinalScore;});
        FString TacticalText=FString::Printf(TEXT("TACTICAL CANDIDATES  |  %d considered"),
            AgentCandidates.Num());
        for(int32 CandidateIndex=0;CandidateIndex<FMath::Min(8,AgentCandidates.Num());++CandidateIndex)
        {
            const FHellRunTacticalLabCandidateRecord& C=*AgentCandidates[CandidateIndex];
            TacticalText+=FString::Printf(TEXT("\n%s %s  %.3f  %s"),
                C.Score.bAccepted?TEXT("PASS"):TEXT("FAIL"),*C.CandidateId.ToString(),
                C.Score.FinalScore,C.Score.bAccepted?TEXT("accepted"):*C.Score.RejectionReason);
            if(!C.BlockingObstacleId.IsNone())
                TacticalText+=FString::Printf(TEXT("\n    BLOCKED segment %d by %s"),
                    C.FailedSegmentIndex,*C.BlockingObstacleId.ToString());
        }
        AgentInspectorBox->AddSlot().AutoHeight().Padding(6,5)
        [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TacticalText))
            .ColorAndOpacity(FLinearColor(.9f,.48f,.34f))];
    }
    const FHellRunEnemySimulationProfile* Profile=Asset->Scenario.Profiles.FindByPredicate(
        [this,Index](const FHellRunEnemySimulationProfile& P)
        {return P.ArchetypeId==Asset->Scenario.Entities[Index].ArchetypeId;});
    AgentInspectorBox->AddSlot().AutoHeight().Padding(6,6,6,2)
    [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Profile
        ?FString::Printf(TEXT("PERCEPTION CONFIG\nRange %.0f cm | Half-angle %.1f deg | %d rays\nConfigured = profile; Resolved = baked 2D LOS approximation"),
            Profile->VisionRange,Profile->VisionHalfAngleDegrees,Profile->VisionRayCount)
        :TEXT("PERCEPTION CONFIG\nNone. This fixture has no AI perception profile, so no FOV is rendered.")))
        .ColorAndOpacity(FLinearColor(.35f,.75f,.95f))];
}

void FTacticalLabEditorToolkit::BakeCurrentMap()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) { Status = TEXT("No editor world is loaded."); RefreshSimulationViews(); return; }
    const double BakeStartedAt=FPlatformTime::Seconds();
    UE_LOG(LogHellRunTacticalLabEditor,Display,
        TEXT("[TacticalLabPanel] Bake requested | map=%s actors=%d"),
        *World->GetMapName(),World->GetActorCount());
    FScopedSlowTask SlowTask(100.0f,LOCTEXT("BakeMapProgress","Baking Tactical AI map..."),true);
    SlowTask.MakeDialog(true);
    float ReportedProgress=0.0f;
    const IHellRunTacticalLabEditorModule::FBakeProgress Progress=
        [&SlowTask,&ReportedProgress](const float Fraction,const FText& Message)
        {
            const float Target=FMath::Clamp(Fraction,0.0f,1.0f)*75.0f;
            SlowTask.EnterProgressFrame(FMath::Max(0.0f,Target-ReportedProgress),Message);
            ReportedProgress=FMath::Max(ReportedProgress,Target);
            return !SlowTask.ShouldCancel();
        };
    FHellRunTacticalLabScenario Baked; FString Summary, Error;
    if (!IHellRunTacticalLabEditorModule::Get().BakeWorld(*World, Baked, Summary, Error,Progress))
    {
        UE_LOG(LogHellRunTacticalLabEditor,Error,
            TEXT("[TacticalLabPanel] Bake failed after %.2fs: %s"),
            FPlatformTime::Seconds()-BakeStartedAt,*Error);
        Status = Error; RefreshSimulationViews(); return;
    }
    UE_LOG(LogHellRunTacticalLabEditor,Display,
        TEXT("[TacticalLabPanel] Data bake completed in %.2fs; building safe minimap"),
        FPlatformTime::Seconds()-BakeStartedAt);
    Asset->Modify(); Asset->Scenario = MoveTemp(Baked); Asset->SourceMap = FSoftObjectPath(World);
    SlowTask.EnterProgressFrame(15.0f,LOCTEXT("BakePreview","Loading or building CPU minimap..."));
    FString PreviewError;
    const bool bPreviewRendered=RenderMapPreview(*World,PreviewError);
    if(bPreviewRendered)
    {
        // Present the useful map first. Debug data remains one click away in the layer controls.
        Asset->bShowGrid=false;
        Asset->bShowGeometry=false;
        Asset->bShowCandidates=false;
    }
    else
    {
        Summary+=FString::Printf(TEXT(" | minimap failed: %s"),*PreviewError);
        UE_LOG(LogHellRunTacticalLabEditor,Error,
            TEXT("[TacticalLabPanel] Minimap generation failed: %s"),*PreviewError);
    }
    SlowTask.EnterProgressFrame(5.0f,LOCTEXT("BakeApply","Applying baked tactical data..."));
    Asset->MarkPackageDirty(); Status = Summary;
    const FString CachePath=FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("TacticalAI/Scenarios"),Asset->Scenario.ScenarioId.ToString()+TEXT(".json"));
    FString SaveError;
    SlowTask.EnterProgressFrame(5.0f,LOCTEXT("BakeSave","Saving scenario cache..."));
    if(!FHellRunTacticalLab::SaveScenario(CachePath,Asset->Scenario,SaveError))
        Status+=FString::Printf(TEXT(" | cache failed: %s"),*SaveError);
    if (TacticalSurface) TacticalSurface->SetAsset(Asset);
    const FString BakeStatus=Status;
    ResetSimulation();
    Status=BakeStatus;
    RefreshSimulationViews();
    FrameAllNextTick();
    UE_LOG(LogHellRunTacticalLabEditor,Display,
        TEXT("[TacticalLabPanel] Bake pipeline finished in %.2fs | preview=%s | cache=%s"),
        FPlatformTime::Seconds()-BakeStartedAt,bPreviewRendered?TEXT("yes"):TEXT("no"),
        *GetMapPreviewCachePath());
}

FString FTacticalLabEditorToolkit::GetMapPreviewCachePath() const
{
    if(!Asset)return FString();
    FString CacheName=Asset->Scenario.ScenarioId.IsNone()
        ?TEXT("Untitled"):Asset->Scenario.ScenarioId.ToString();
    CacheName=FPaths::MakeValidFileName(CacheName);
    return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("TacticalAI/Minimaps"),
        CacheName+TEXT(".png"));
}

void FTacticalLabEditorToolkit::LoadCachedMapPreview()
{
    if(!Asset||IsValid(Asset->GeneratedMapPreview))return;
    const FString CachePath=GetMapPreviewCachePath();
    if(CachePath.IsEmpty()||!IFileManager::Get().FileExists(*CachePath))return;
    if(UTexture2D* Preview=FImageUtils::ImportFileAsTexture2D(CachePath))
    {
        Preview->AddressX=TA_Clamp;
        Preview->AddressY=TA_Clamp;
        Preview->Filter=TF_Bilinear;
        // ImportFileAsTexture2D creates transient platform data. Recreate the
        // render resource after changing sampler settings before Slate uses it.
        Preview->UpdateResource();
        Asset->GeneratedMapPreview=Preview;
        Status=FString::Printf(TEXT("Loaded cached minimap: %s"),*CachePath);
    }
}

bool FTacticalLabEditorToolkit::RenderMapPreview(UWorld& World,FString& OutError)
{
    (void)World;
    OutError.Reset();
    if(!Asset)
    {
        OutError=TEXT("No tactical scenario asset is open.");
        return false;
    }

    const FHellRunTacticalLabBakeMetadata& Metadata=Asset->Scenario.BakeMetadata;
    const FVector2D BoundsSize=Metadata.BoundsMax-Metadata.BoundsMin;
    if(!Metadata.bBakedFromProductionMap||BoundsSize.X<100.0||BoundsSize.Y<100.0)
    {
        OutError=TEXT("The baked navigation bounds are invalid.");
        return false;
    }

    // An explicitly authored texture is always preferable to generated data.
    // Do not hide it behind an old transient preview after rebaking.
    if(!Asset->TacticalMapTexture.IsNull())
    {
        Asset->GeneratedMapPreview=nullptr;
        return true;
    }

    // Reuse the static PNG when one already exists. The previous implementation
    // spawned a SceneCapture2D and read a render target during every bake. On
    // D3D12 that transient capture could outlive its target until the next GC,
    // producing a delayed GPU page fault. A normal Bake Map must never submit a
    // second scene render merely to provide editor decoration.
    const FString CachePath=GetMapPreviewCachePath();
    if(!CachePath.IsEmpty()&&IFileManager::Get().FileExists(*CachePath))
    {
        if(UTexture2D* CachedPreview=FImageUtils::ImportFileAsTexture2D(CachePath))
        {
            CachedPreview->AddressX=TA_Clamp;
            CachedPreview->AddressY=TA_Clamp;
            CachedPreview->Filter=TF_Bilinear;
            CachedPreview->UpdateResource();
            Asset->GeneratedMapPreview=CachedPreview;
            UE_LOG(LogHellRunTacticalLabEditor,Display,
                TEXT("[TacticalLabPanel] Reused static minimap cache; no scene capture: %s"),
                *CachePath);
            return true;
        }
    }

    constexpr int32 LongEdgePixels=768;
    constexpr int32 MinimumShortEdgePixels=256;
    int32 Width=LongEdgePixels;
    int32 Height=LongEdgePixels;
    if(BoundsSize.X>=BoundsSize.Y)
        Height=FMath::Max(MinimumShortEdgePixels,
            FMath::RoundToInt(LongEdgePixels*BoundsSize.Y/BoundsSize.X));
    else
        Width=FMath::Max(MinimumShortEdgePixels,
            FMath::RoundToInt(LongEdgePixels*BoundsSize.X/BoundsSize.Y));

    // With no authored or cached art, generate a bounded CPU-only planning map.
    // It is intentionally schematic but remains useful and cannot invoke the
    // renderer, Lumen, ray tracing, or an RHI readback during a data bake.
    FImage CapturedImage(Width,Height,ERawImageFormat::BGRA8,EGammaSpace::sRGB);
    FColor* CapturedPixels=reinterpret_cast<FColor*>(CapturedImage.RawData.GetData());
    const int64 CapturedPixelCount=CapturedImage.RawData.Num()/sizeof(FColor);
    for(int64 PixelIndex=0;PixelIndex<CapturedPixelCount;++PixelIndex)
        CapturedPixels[PixelIndex]=FColor(10,18,24,255);

    const auto ToPixel=[&](const FVector2D& Point)
    {
        constexpr float Padding=.025f;
        const float U=FMath::Clamp((Point.X-Metadata.BoundsMin.X)/BoundsSize.X,0.0,1.0);
        const float V=FMath::Clamp((Metadata.BoundsMax.Y-Point.Y)/BoundsSize.Y,0.0,1.0);
        return FIntPoint(
            FMath::RoundToInt(FMath::Lerp(Padding*Width,(1.0f-Padding)*Width,U)),
            FMath::RoundToInt(FMath::Lerp(Padding*Height,(1.0f-Padding)*Height,V)));
    };
    const auto DrawPixel=[&](const int32 X,const int32 Y,const FColor Color)
    {
        if(X>=0&&X<Width&&Y>=0&&Y<Height)CapturedPixels[Y*Width+X]=Color;
    };
    const auto DrawLine=[&](FIntPoint A,const FIntPoint B,const FColor Color)
    {
        int32 X=A.X,Y=A.Y;
        const int32 DeltaX=FMath::Abs(B.X-A.X),StepX=A.X<B.X?1:-1;
        const int32 DeltaY=-FMath::Abs(B.Y-A.Y),StepY=A.Y<B.Y?1:-1;
        int32 Error=DeltaX+DeltaY;
        for(;;)
        {
            DrawPixel(X,Y,Color);
            DrawPixel(X+1,Y,Color);
            DrawPixel(X,Y+1,Color);
            if(X==B.X&&Y==B.Y)break;
            const int32 Error2=2*Error;
            if(Error2>=DeltaY){Error+=DeltaY;X+=StepX;}
            if(Error2<=DeltaX){Error+=DeltaX;Y+=StepY;}
        }
    };
    for(const FHellRunTacticalLabObstacle& Obstacle:Asset->Scenario.Obstacles)
    {
        const FColor Color=Obstacle.bBlocksMovement
            ?FColor(122,137,147,255):FColor(181,124,52,255);
        DrawLine(ToPixel(Obstacle.Start),ToPixel(Obstacle.End),Color);
    }

    UTexture2D* Preview=FImageUtils::CreateTexture2DFromImage(CapturedImage);
    if(!Preview)
    {
        OutError=TEXT("Could not create the static minimap preview.");
        return false;
    }
    Preview->AddressX=TA_Clamp;
    Preview->AddressY=TA_Clamp;
    Preview->Filter=TF_Bilinear;
    // CreateTexture2DFromImage does not guarantee that a render resource is
    // ready for a brush in the same editor frame on every RHI.
    Preview->UpdateResource();
    // This is platform-data only: no TextureSource, compression, or background DDC.
    Asset->GeneratedMapPreview=Preview;
    TArray64<uint8> PngData;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath),true);
    if(!FImageUtils::CompressImage(PngData,TEXT(".png"),CapturedImage)||
        !FFileHelper::SaveArrayToFile(PngData,*CachePath))
    {
        UE_LOG(LogTemp,Warning,TEXT("Tactical Lab could not cache minimap at %s"),
            *CachePath);
    }
    return true;
}

void FTacticalLabEditorToolkit::FrameAllNextTick()
{
    if(!TacticalSurface)return;
    TWeakPtr<STacticalLabSurface> WeakSurface=TacticalSurface;
    TacticalSurface->RegisterActiveTimer(0.0f,FWidgetActiveTimerDelegate::CreateLambda(
        [WeakSurface](double,float)
        {
            if(const TSharedPtr<STacticalLabSurface> Surface=WeakSurface.Pin())Surface->FrameAll();
            return EActiveTimerReturnType::Stop;
        }));
}

void FTacticalLabEditorToolkit::FrameAll() { if (TacticalSurface) TacticalSurface->FrameAll(); }

const FTacticalLabPIEFrame* FTacticalLabEditorToolkit::GetDisplayedPIEFrame() const
{
    if(!bPIEAttached||PIEFrames.IsEmpty())return nullptr;
    const int32 Index=PIEFrameCursor==INDEX_NONE?PIEFrames.Num()-1:
        FMath::Clamp(PIEFrameCursor,0,PIEFrames.Num()-1);
    return &PIEFrames[Index];
}

void FTacticalLabEditorToolkit::TogglePIEAttachment()
{
    bPIEAttached=!bPIEAttached;
    PIEFrameCursor=INDEX_NONE;
    SelectedPIEAgentId=NAME_None;
    if(!bPIEAttached)
    {
        if(TacticalSurface)TacticalSurface->SetPIEFrame(nullptr);
        Status=TEXT("Detached from PIE; showing the authored tactical fixture.");
        RefreshAgentInspector();
        return;
    }
    Status=GEditor&&GEditor->PlayWorld
        ?TEXT("Attached to PIE live feed; recording gameplay snapshots.")
        :TEXT("PIE attachment armed. Start PIE to begin the live feed.");
    if(TacticalSurface&&!bPIETickerActive)
    {
        bPIETickerActive=true;
        TWeakPtr<FTacticalLabEditorToolkit> WeakThis=SharedThis(this);
        TacticalSurface->RegisterActiveTimer(0.0f,
            FWidgetActiveTimerDelegate::CreateLambda([WeakThis](double Now,float Delta)
            {
                const TSharedPtr<FTacticalLabEditorToolkit> Self=WeakThis.Pin();
                return Self?Self->TickPIEFeed(Now,Delta):EActiveTimerReturnType::Stop;
            }));
    }
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::TogglePIEFollow()
{
    bFollowPIEPlayers=!bFollowPIEPlayers;
    Status=bFollowPIEPlayers
        ?TEXT("PIE player-group follow enabled. Disable Follow Players to pan freely.")
        :TEXT("PIE player-group follow disabled; manual pan/zoom remains active.");
    if(bFollowPIEPlayers)
        if(const FTacticalLabPIEFrame* Frame=GetDisplayedPIEFrame())
            if(Frame->bHasPlayers&&TacticalSurface)
                TacticalSurface->CenterOnWorld(Frame->PlayerGroupCenter);
}

void FTacticalLabEditorToolkit::ReturnToLivePIE()
{
    if(!bPIEAttached)return;
    PIEFrameCursor=INDEX_NONE;
    if(TacticalSurface)TacticalSurface->SetPIEFrame(GetDisplayedPIEFrame());
    Status=TEXT("PIE feed: LIVE");
    RefreshAgentInspector();
}

void FTacticalLabEditorToolkit::StepPIERecording(const int32 DeltaFrames)
{
    if(!bPIEAttached||PIEFrames.IsEmpty())return;
    const int32 Current=PIEFrameCursor==INDEX_NONE?PIEFrames.Num()-1:PIEFrameCursor;
    PIEFrameCursor=FMath::Clamp(Current+DeltaFrames,0,PIEFrames.Num()-1);
    if(TacticalSurface)TacticalSurface->SetPIEFrame(GetDisplayedPIEFrame());
    Status=FString::Printf(TEXT("PIE replay %.2fs | frame %d/%d | Live returns to gameplay"),
        PIEFrames[PIEFrameCursor].WorldTime,PIEFrameCursor+1,PIEFrames.Num());
    RefreshAgentInspector();
}

bool FTacticalLabEditorToolkit::CapturePIEFrame(UWorld& World,
    FTacticalLabPIEFrame& OutFrame) const
{
    OutFrame={};
    OutFrame.WorldTime=World.GetTimeSeconds();
    FVector2D PlayerSum=FVector2D::ZeroVector;
    int32 PlayerCount=0;
    for(TActorIterator<APawn> It(&World);It;++It)
        if(It->GetController()&&It->GetController()->IsPlayerController())
        {PlayerSum+=FVector2D(It->GetActorLocation());++PlayerCount;}
    if(PlayerCount>0)
    {
        OutFrame.PlayerGroupCenter=PlayerSum/PlayerCount;
        OutFrame.bHasPlayers=true;
    }

    constexpr int32 MaximumTrackedAgents=64;
    for(TActorIterator<APawn> It(&World);It&&OutFrame.Agents.Num()<MaximumTrackedAgents;++It)
    {
        APawn* Pawn=*It;
        AController* Controller=Pawn->GetController();
        if(!Controller)continue;
        AAIController* AI=Cast<AAIController>(Controller);
        const bool bPlayer=Controller->IsPlayerController();
        if(!AI&&!bPlayer)continue;

        FTacticalLabPIEAgentSnapshot& Agent=OutFrame.Agents.AddDefaulted_GetRef();
        Agent.Entity.Id=Pawn->GetFName();
        Agent.Entity.Kind=bPlayer?EHellRunTacticalLabEntityKind::Player:
            EHellRunTacticalLabEntityKind::Enemy;
        Agent.Entity.Team=bPlayer?TEXT("Players"):TEXT("AI");
        Agent.Entity.ArchetypeId=Pawn->GetClass()->GetFName();
        Agent.Entity.Position=FVector2D(Pawn->GetActorLocation());
        Agent.Entity.Velocity=FVector2D(Pawn->GetVelocity());
        const FVector Forward=Pawn->GetActorForwardVector();
        Agent.Entity.Facing=FVector2D(Forward).GetSafeNormal();
        Agent.Entity.bAlive=!Pawn->IsActorBeingDestroyed();

        UGOAPBrainComponent* Brain=Controller->FindComponentByClass<UGOAPBrainComponent>();
        if(!Brain)Brain=Pawn->FindComponentByClass<UGOAPBrainComponent>();
        if(Brain&&Brain->IsRunning())
        {
            Agent.bHasGOAP=true;
            Agent.GOAP=Brain->GetDebugSnapshot();
        }

        if(AI)
        {
            if(const UPathFollowingComponent* Following=AI->GetPathFollowingComponent())
                if(const FNavPathSharedPtr Path=Following->GetPath();Path.IsValid())
                    for(const FNavPathPoint& Point:Path->GetPathPoints())
                        Agent.MovementPath.Add(FVector2D(Point.Location));

            UAIPerceptionComponent* Perception=AI->GetPerceptionComponent();
            const UAISenseConfig_Sight* Sight=Perception
                ?Perception->GetSenseConfig<UAISenseConfig_Sight>():nullptr;
            if(Sight)
            {
                FVector Eye;FRotator EyeRotation;
                Pawn->GetActorEyesViewPoint(Eye,EyeRotation);
                Agent.VisionOrigin=FVector2D(Eye);
                Agent.Entity.Facing=FVector2D(EyeRotation.Vector()).GetSafeNormal();
                Agent.VisionRange=Sight->SightRadius;
                Agent.VisionHalfAngle=Sight->PeripheralVisionAngleDegrees;
                Agent.VisionRayCount=25;
                if(SelectedPIEAgentId==Agent.Entity.Id)
                {
                    FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalLabPIEVision),true,Pawn);
                    Params.AddIgnoredActor(Pawn);Params.AddIgnoredActor(Controller);
                    for(int32 RayIndex=0;RayIndex<Agent.VisionRayCount;++RayIndex)
                    {
                        const float Alpha=RayIndex/static_cast<float>(Agent.VisionRayCount-1);
                        const FRotator RayRotation(0.0f,EyeRotation.Yaw+FMath::Lerp(
                            -Agent.VisionHalfAngle,Agent.VisionHalfAngle,Alpha),0.0f);
                        const FVector RayEnd=Eye+RayRotation.Vector()*Agent.VisionRange;
                        FHitResult Hit;
                        const bool bHit=World.LineTraceSingleByChannel(Hit,Eye,RayEnd,
                            ECC_Visibility,Params);
                        FTacticalLabPIEVisionRay& Ray=Agent.VisionRays.AddDefaulted_GetRef();
                        Ray.End=FVector2D(bHit?Hit.ImpactPoint:RayEnd);
                        Ray.bBlocked=bHit;
                        if(bHit)
                        {
                            Ray.BlockingActor=Hit.GetActor()?Hit.GetActor()->GetFName():NAME_None;
                            Ray.BlockingComponent=Hit.GetComponent()?Hit.GetComponent()->GetFName():NAME_None;
                        }
                    }
                }
            }
        }
    }
    return !OutFrame.Agents.IsEmpty();
}

EActiveTimerReturnType FTacticalLabEditorToolkit::TickPIEFeed(double CurrentTime,float)
{
    if(!bPIEAttached)
    {
        bPIETickerActive=false;
        return EActiveTimerReturnType::Stop;
    }
    UWorld* PIEWorld=GEditor?GEditor->PlayWorld:nullptr;
    if(PIEWorld&&CurrentTime-LastPIECaptureSeconds>=.2)
    {
        LastPIECaptureSeconds=CurrentTime;
        FTacticalLabPIEFrame Frame;
        if(CapturePIEFrame(*PIEWorld,Frame))
        {
            if(!PIEFrames.IsEmpty()&&Frame.WorldTime+0.01f<PIEFrames.Last().WorldTime)
            {
                PIEFrames.Reset();
                PIEFrameCursor=INDEX_NONE;
            }
            PIEFrames.Add(MoveTemp(Frame));
            constexpr int32 MaximumFrames=900;
            if(PIEFrames.Num()>MaximumFrames)
            {
                const int32 Removed=PIEFrames.Num()-MaximumFrames;
                PIEFrames.RemoveAt(0,Removed,EAllowShrinking::No);
                if(PIEFrameCursor!=INDEX_NONE)PIEFrameCursor=FMath::Max(0,PIEFrameCursor-Removed);
            }
            if(PIEFrameCursor==INDEX_NONE&&TacticalSurface)
            {
                TacticalSurface->SetPIEFrame(&PIEFrames.Last());
                if(bFollowPIEPlayers&&PIEFrames.Last().bHasPlayers)
                    TacticalSurface->CenterOnWorld(PIEFrames.Last().PlayerGroupCenter);
            }
            RefreshAgentInspector();
        }
    }
    return EActiveTimerReturnType::Continue;
}

void FTacticalLabEditorToolkit::ResetSimulation()
{
    bPlaying=false;DecisionAccumulator=0.0f;PlaybackUIAccumulator=0.0f;
    Simulation = MakeUnique<FHellRunTacticalLab>();
    FString Error;
    const FHellRunTacticalLabScenario Scenario=BuildSimulationScenario();
    if (!Simulation->Initialize(Scenario, Seed, Lifetimes.Num(), Error)) Status = Error;
    else Status = FString::Printf(TEXT("Ready — seed %d"), Seed);
    bSimulationDirty=false;
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::StartPlayback()
{
    if(bPlaying)return;
    if(!Simulation||bSimulationDirty||Simulation->IsComplete())ResetSimulation();
    bPlaying=true;
    Status=FString::Printf(TEXT("Playing %s at %.2gx"),
        bMoveAgentsDuringPlayback?TEXT("with movement"):TEXT("analysis-frozen"),PlaybackSpeed);
    if(TacticalSurface)
    {
        TWeakPtr<FTacticalLabEditorToolkit> WeakThis=SharedThis(this);
        TacticalSurface->RegisterActiveTimer(0.0f,FWidgetActiveTimerDelegate::CreateLambda(
            [WeakThis](double Now,float Delta)
            {
                const TSharedPtr<FTacticalLabEditorToolkit> Self=WeakThis.Pin();
                return Self?Self->TickPlayback(Now,Delta):EActiveTimerReturnType::Stop;
            }));
    }
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::PausePlayback()
{
    bPlaying=false;
    Status=TEXT("Simulation paused. State and query results are preserved; Play resumes and Step advances one decision.");
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::StopPlayback()
{
    bPlaying=false;
    DecisionAccumulator=0.0f;
    PlaybackUIAccumulator=0.0f;
    Status=TEXT("Simulation stopped. Current state is preserved; Reset restores the authored scenario.");
    RefreshSimulationViews();
}

EActiveTimerReturnType FTacticalLabEditorToolkit::TickPlayback(double,float DeltaTime)
{
    if(!bPlaying||!Simulation)return EActiveTimerReturnType::Stop;
    const float SimDelta=FMath::Clamp(DeltaTime*PlaybackSpeed,0.0f,.1f);
    DecisionAccumulator+=SimDelta;PlaybackUIAccumulator+=DeltaTime;
    bool bDecisionAdvanced=false;
    if(Simulation->HasActiveMovement())
        Simulation->StepTick(SimDelta,bMoveAgentsDuringPlayback,false);
    // Do not rebuild the same movement plan every half-second while an agent
    // is already traversing it. Frozen analysis still re-evaluates on cadence.
    if(!Simulation->HasActiveMovement()||
        (!bMoveAgentsDuringPlayback&&DecisionAccumulator>=.5f))
    {
        Simulation->StepDecision();DecisionAccumulator=0.0f;bDecisionAdvanced=true;
    }
    if(Simulation->IsComplete())
    {
        bPlaying=false;
        Lifetimes.Add(MakeShared<FHellRunTacticalLabLifetime>(Simulation->Finish()));
        Status=TEXT("Interactive lifetime complete");
    }
    // Rebuilding the timeline and candidate overlays every Slate frame was a
    // major source of editor stalls on baked production maps.
    if(bDecisionAdvanced||PlaybackUIAccumulator>=.1f||!bPlaying)
    {RefreshSimulationViews();PlaybackUIAccumulator=0.0f;}
    return bPlaying?EActiveTimerReturnType::Continue:EActiveTimerReturnType::Stop;
}

void FTacticalLabEditorToolkit::StepSimulation()
{
    if (!Simulation||bSimulationDirty) ResetSimulation();
    if (Simulation && !Simulation->IsComplete())
    {
        if(Simulation->HasActiveMovement())
        {Simulation->StepTick(.25f);Status=TEXT("Advanced movement by 0.25 seconds");}
        else
        {Simulation->StepDecision();Status=TEXT("Advanced one tactical decision");}
    }
    if (Simulation && Simulation->IsComplete())
    {Lifetimes.Add(MakeShared<FHellRunTacticalLabLifetime>(Simulation->Finish()));
        Status+=TEXT(" - lifetime complete");}
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::RunSimulation()
{
    FHellRunTacticalLabLifetime Result = FHellRunTacticalLab::RunLifetime(BuildSimulationScenario(), Seed++, Lifetimes.Num());
    Lifetimes.Add(MakeShared<FHellRunTacticalLabLifetime>(MoveTemp(Result)));
    ResetSimulation();Status = TEXT("Lifetime complete — displaying selected paths and candidates");
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::RunBatch(int32 Count)
{
    const FHellRunTacticalLabScenario Scenario=BuildSimulationScenario();
    for (int32 I=0; I<Count; ++I)
        Lifetimes.Add(MakeShared<FHellRunTacticalLabLifetime>(
            FHellRunTacticalLab::RunLifetime(Scenario, Seed+I, Lifetimes.Num())));
    Seed += Count;ResetSimulation();
    Status = FString::Printf(TEXT("Completed %d lifetimes — displaying latest run"), Count);
    RefreshSimulationViews();
}

FHellRunTacticalLabScenario FTacticalLabEditorToolkit::BuildSimulationScenario() const
{
    FHellRunTacticalLabScenario Scenario=Asset?Asset->Scenario:FHellRunTacticalLabScenario();
    for(const FHellRunTacticalLabEntity& Candidate:TransientEQSCandidates)
        Scenario.Entities.Add(Candidate);
    return Scenario;
}

void FTacticalLabEditorToolkit::ExportReport()
{
    if (Lifetimes.IsEmpty()) { Status=TEXT("Run a lifetime before exporting."); RefreshSimulationViews(); return; }
    const FString Filename = FPaths::ProjectSavedDir()/TEXT("TacticalAI/Reports")/
        FString::Printf(TEXT("%s_%d.json"), *Asset->Scenario.ScenarioId.ToString(), Lifetimes.Last()->Seed);
    FString Error;
    Status = FHellRunTacticalLab::SaveLifetime(Filename,*Lifetimes.Last(),Error)
        ? FString::Printf(TEXT("Exported %s"),*Filename) : Error;
    RefreshSimulationViews();
}

FText FTacticalLabEditorToolkit::GetStatusText() const { return FText::FromString(Status); }

void FTacticalLabEditorToolkit::HandleScenarioEdited()
{
    bSimulationDirty=true;
    Status=TEXT("Scenario edited - simulation will rebuild on Run, Step, or Reset");
    if(GoalGraph)GoalGraph->SetLifetime(nullptr,Status,
        Asset&&Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex)
            ?Asset->Scenario.Entities[SelectedEntityIndex].Id:NAME_None);
}

void FTacticalLabEditorToolkit::QueueLiveEQS(const int32 EntityIndex,
    const FVector2D Position)
{
    if(!Asset||!Asset->bEnableLiveEQS)return;
    if(Asset->LiveEQSQuery.IsNull())
    {
        const EHellRunTacticalLabGoal Goal=Asset->Scenario.Entities.IsValidIndex(EntityIndex)
            ?Asset->Scenario.Entities[EntityIndex].Goal:EHellRunTacticalLabGoal::Auto;
        const FString Message=FString::Printf(TEXT("LIVE EQS: native %s query queued"),
            *StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(static_cast<int64>(Goal)));
        if(TacticalSurface)TacticalSurface->SetEQSResults({},Message,Position);
    }
    PendingEQSEntityIndex=EntityIndex;PendingEQSPosition=Position;
    if(bEQSDebounceScheduled||!TacticalSurface)return;
    bEQSDebounceScheduled=true;
    TWeakPtr<FTacticalLabEditorToolkit> WeakThis=SharedThis(this);
    TacticalSurface->RegisterActiveTimer(FMath::Max(.05f,Asset->LiveEQSDebounceSeconds),
        FWidgetActiveTimerDelegate::CreateLambda([WeakThis](double,float)
        {
            if(const TSharedPtr<FTacticalLabEditorToolkit> Self=WeakThis.Pin())
            {Self->bEQSDebounceScheduled=false;Self->LaunchLiveEQS();}
            return EActiveTimerReturnType::Stop;
        }));
}

void FTacticalLabEditorToolkit::RunEQSAtSelectedEnemy()
{
    if(Asset)Asset->bShowCandidates=true;
    if(Asset&&Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex)&&
        Asset->Scenario.Entities[SelectedEntityIndex].Kind==EHellRunTacticalLabEntityKind::Enemy)
    {PendingEQSEntityIndex=SelectedEntityIndex;
        PendingEQSPosition=Asset->Scenario.Entities[SelectedEntityIndex].Position;}
    if(!PendingEQSPosition.IsSet()&&Asset)
        for(int32 I=0;I<Asset->Scenario.Entities.Num();++I)
            if(Asset->Scenario.Entities[I].Kind==EHellRunTacticalLabEntityKind::Enemy)
            {PendingEQSEntityIndex=I;PendingEQSPosition=Asset->Scenario.Entities[I].Position;break;}
    bPendingManualEQS=true;
    LaunchLiveEQS();
}

void FTacticalLabEditorToolkit::AbortLiveEQS()
{
    if(ActiveEQSQueryId!=INDEX_NONE&&IsValid(EQSQueryProxy))
        if(UEnvQueryManager* Manager=UEnvQueryManager::GetCurrent(EQSQueryProxy))
            Manager->AbortQuery(ActiveEQSQueryId);
    ActiveEQSQueryId=INDEX_NONE;
}

EActiveTimerReturnType FTacticalLabEditorToolkit::TickPendingEQS(double,float DeltaTime)
{
    if(ActiveEQSQueryId==INDEX_NONE||!IsValid(EQSQueryProxy))
    {
        bEQSTickScheduled=false;
        return EActiveTimerReturnType::Stop;
    }
    const UWorld* QueryWorld=EQSQueryProxy->GetWorld();
    const bool bPIEWorldIsAdvancing=bPIEAttached&&GEditor&&GEditor->PlayWorld&&
        QueryWorld==GEditor->PlayWorld.Get()&&!QueryWorld->IsPaused();
    if(!bPlaying&&!bPIEWorldIsAdvancing)
        if(UEnvQueryManager* Manager=UEnvQueryManager::GetCurrent(EQSQueryProxy))
            Manager->Tick(FMath::Clamp(DeltaTime,0.001f,0.033f));
    return ActiveEQSQueryId==INDEX_NONE
        ?(bEQSTickScheduled=false,EActiveTimerReturnType::Stop)
        :EActiveTimerReturnType::Continue;
}

UEnvQuery* FTacticalLabEditorToolkit::GetOrCreateNativeTacticalQuery(
    const EHellRunTacticalLabGoal Goal)
{
    if(const TObjectPtr<UEnvQuery>* Existing=NativeTacticalQueries.Find(Goal))return *Existing;
    UEnvQuery* Query=NewObject<UEnvQuery>(GetTransientPackage(),
        *FString::Printf(TEXT("TacticalLab_%s"),
            *StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(static_cast<int64>(Goal))),RF_Transient);
    UEnvQueryOption* Option=NewObject<UEnvQueryOption>(Query);
    UTacticalLabEQSGenerator_VoxelNodes* VoxelNodes=
        NewObject<UTacticalLabEQSGenerator_VoxelNodes>(Option);
    VoxelNodes->SearchRadius.DefaultValue=
        Goal==EHellRunTacticalLabGoal::HoldPosition?150.0f:3000.0f;
    VoxelNodes->bCoverOnly=Goal==EHellRunTacticalLabGoal::FindCover;
    VoxelNodes->MaximumItems.DefaultValue=Asset
        ?FMath::Clamp(Asset->MaximumDisplayedEQSItems,32,1000):500;
    VoxelNodes->GenerateAround=(Goal==EHellRunTacticalLabGoal::ReachTarget||
        Goal==EHellRunTacticalLabGoal::Regroup)
        ?UTacticalLabEQSContext_Threat::StaticClass():UEnvQueryContext_Querier::StaticClass();
    Option->Generator=VoxelNodes;

    UTacticalLabEQSTest_VoxelPath* VoxelPath=
        NewObject<UTacticalLabEQSTest_VoxelPath>(Option);
    VoxelPath->Context=UEnvQueryContext_Querier::StaticClass();
    VoxelPath->PathFromContext.DefaultValue=true;
    VoxelPath->TestPurpose=EEnvTestPurpose::Filter;
    VoxelPath->FilterType=EEnvTestFilterType::Match;
    VoxelPath->BoolValue.DefaultValue=true;
    Option->Tests.Add(VoxelPath);

    if(Goal==EHellRunTacticalLabGoal::FindCover)
    {
        UTacticalLabEQSTest_CoverLOS* CoverLOS=NewObject<UTacticalLabEQSTest_CoverLOS>(Option);
        UAIDataProvider_QueryParams* TraceHeight=NewObject<UAIDataProvider_QueryParams>(CoverLOS);
        TraceHeight->ParamName=TEXT("CoverTraceHeight");
        CoverLOS->TraceHeight.DataBinding=TraceHeight;
        CoverLOS->TraceHeight.DataField=TEXT("FloatValue");
        CoverLOS->TestPurpose=EEnvTestPurpose::FilterAndScore;
        CoverLOS->FilterType=EEnvTestFilterType::Match;
        CoverLOS->BoolValue.DefaultValue=true;
        CoverLOS->ScoringFactor.DefaultValue=2.0f;
        Option->Tests.Add(CoverLOS);
    }
    else if(Goal==EHellRunTacticalLabGoal::EliminateTarget||
        Goal==EHellRunTacticalLabGoal::Auto)
    {
        UEnvQueryTest_Trace* Trace=NewObject<UEnvQueryTest_Trace>(Option);
        Trace->Context=UTacticalLabEQSContext_Threat::StaticClass();
        Trace->TraceData.SetGeometryOnly();
        Trace->TraceData.TraceMode=EEnvQueryTrace::GeometryByChannel;
        Trace->TraceData.TraceShape=EEnvTraceShape::Line;
        Trace->TraceData.SerializedChannel=ECC_Visibility;
        Trace->TraceData.TraceChannel=UEngineTypes::ConvertToTraceType(ECC_Visibility);
        Trace->ItemHeightOffset.DefaultValue=80.0f;
        Trace->ContextHeightOffset.DefaultValue=Trace->ItemHeightOffset.DefaultValue;
        Trace->TestPurpose=EEnvTestPurpose::FilterAndScore;
        Trace->FilterType=EEnvTestFilterType::Match;
        // Cover wants an occluder; attack positions want a clear trace.
        Trace->BoolValue.DefaultValue=false;
        Trace->ScoringFactor.DefaultValue=2.0f;
        Option->Tests.Add(Trace);
    }

    UEnvQueryTest_Distance* Distance=NewObject<UEnvQueryTest_Distance>(Option);
    Distance->TestMode=EEnvTestDistance::Distance2D;
    Distance->DistanceTo=(Goal==EHellRunTacticalLabGoal::ReachTarget||
        Goal==EHellRunTacticalLabGoal::Regroup||Goal==EHellRunTacticalLabGoal::EliminateTarget)
        ?UTacticalLabEQSContext_Threat::StaticClass():UEnvQueryContext_Querier::StaticClass();
    Distance->TestPurpose=EEnvTestPurpose::Score;
    Distance->ScoringEquation=EEnvTestScoreEquation::InverseLinear;
    Distance->ScoringFactor.DefaultValue=1.0f;
    Option->Tests.Add(Distance);
    Query->GetOptionsMutable().Add(Option);
    NativeTacticalQueries.Add(Goal,Query);
    return Query;
}

void FTacticalLabEditorToolkit::LaunchLiveEQS()
{
    if(!Asset||(!Asset->bEnableLiveEQS&&!bPendingManualEQS)||!PendingEQSPosition.IsSet())return;
    EHellRunTacticalLabGoal Goal=PendingEQSGoalOverride.Get(
        EHellRunTacticalLabGoal::Auto);
    if(!PendingEQSGoalOverride.IsSet()&&
        Asset->Scenario.Entities.IsValidIndex(PendingEQSEntityIndex))
        Goal=Asset->Scenario.Entities[PendingEQSEntityIndex].Goal;
    const bool bUsingNativeQuery=Asset->LiveEQSQuery.IsNull();
    UEnvQuery* Query=bUsingNativeQuery
        ?GetOrCreateNativeTacticalQuery(Goal):Asset->LiveEQSQuery.Get();
    if(!Query)
    {
        if(!EQSQueryLoadHandle||!EQSQueryLoadHandle->IsActive())
            EQSQueryLoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(
                Asset->LiveEQSQuery.ToSoftObjectPath(),
                FStreamableDelegate::CreateSP(this,&FTacticalLabEditorToolkit::LaunchLiveEQS));
        if(TacticalSurface)TacticalSurface->SetEQSResults({},TEXT("Loading EQS query asset..."),
            PendingEQSPosition);
        return;
    }
    UWorld* World=bPIEAttached&&GEditor&&GEditor->PlayWorld
        ?GEditor->PlayWorld.Get():(GEditor?GEditor->GetEditorWorldContext().World():nullptr);
    if(!World)return;
    APawn* LivePawn=nullptr;
    if(bPIEAttached&&!SelectedPIEAgentId.IsNone())
        for(TActorIterator<APawn> It(World);It;++It)
            if(It->GetFName()==SelectedPIEAgentId){LivePawn=*It;break;}
    const FVector2D Position=LivePawn?FVector2D(LivePawn->GetActorLocation()):
        PendingEQSPosition.GetValue();
    PendingEQSPosition=Position;
    const float Z=(LivePawn?LivePawn->GetActorLocation().Z:
        Asset->Scenario.BakeMetadata.ProjectionHeight)+Asset->LiveEQSQueryHeightOffset;
    if(IsValid(EQSQueryProxy)&&EQSQueryProxy->GetWorld()!=World)
    {
        AbortLiveEQS();
        EQSQueryProxy->Destroy();
        EQSQueryProxy=nullptr;
    }
    if(!IsValid(EQSQueryProxy))
    {
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.Name=MakeUniqueObjectName(World,AActor::StaticClass(),TEXT("TacticalLabEQSQuerier"));
        EQSQueryProxy=World->SpawnActor<ATacticalLabEQSQuerier>(
            ATacticalLabEQSQuerier::StaticClass(),FVector(Position,Z),
            FRotator::ZeroRotator,Params);
        if(IsValid(EQSQueryProxy))EQSQueryProxy->SetIsTemporarilyHiddenInEditor(true);
    }
    if(!IsValid(EQSQueryProxy))return;
    EQSQueryProxy->SetActorLocation(FVector(Position,Z),false,nullptr,ETeleportType::TeleportPhysics);
    FVector2D TargetPosition=Position;
    if(LivePawn)
    {
        if(ATacticalLabEQSQuerier* Querier=Cast<ATacticalLabEQSQuerier>(EQSQueryProxy))
        {
            Querier->bCanWalk=true;Querier->bCanClimb=false;
            Querier->bCanMantle=false;Querier->bCanDrop=false;
            Querier->bCanJump=false;Querier->bCanVault=false;Querier->bCanFly=false;
        }
        float BestThreatDistanceSq=BIG_NUMBER;
        for(TActorIterator<APawn> It(World);It;++It)
            if(It->GetController()&&It->GetController()->IsPlayerController())
            {
                const FVector2D Candidate(It->GetActorLocation());
                const float DistanceSq=FVector2D::DistSquared(Position,Candidate);
                if(DistanceSq<BestThreatDistanceSq)
                {BestThreatDistanceSq=DistanceSq;TargetPosition=Candidate;}
            }
    }
    else if(Asset->Scenario.Entities.IsValidIndex(PendingEQSEntityIndex))
    {
        const FHellRunTacticalLabEntity& Agent=Asset->Scenario.Entities[PendingEQSEntityIndex];
        if(ATacticalLabEQSQuerier* Querier=Cast<ATacticalLabEQSQuerier>(EQSQueryProxy))
        {
            Querier->bCanWalk=true;Querier->bCanClimb=false;
            Querier->bCanMantle=false;Querier->bCanDrop=false;
            Querier->bCanJump=false;Querier->bCanVault=false;Querier->bCanFly=false;
            for(const FHellRunEnemySimulationProfile& Profile:Asset->Scenario.Profiles)
                if(Profile.ArchetypeId==Agent.ArchetypeId)
                {
                    Querier->bCanClimb=Profile.bCanClimb;
                    Querier->bCanMantle=Profile.bCanMantle;
                    Querier->bCanDrop=Profile.bCanDrop;
                    Querier->bCanJump=Profile.bCanJump;
                    Querier->bCanVault=Profile.bCanVault;
                    Querier->bCanFly=Profile.bCanFly;
                    break;
                }
        }
        if(Goal==EHellRunTacticalLabGoal::Regroup&&!Agent.SquadId.IsNone())
        {
            FVector2D Sum=FVector2D::ZeroVector;int32 Count=0;
            for(const FHellRunTacticalLabEntity& Member:Asset->Scenario.Entities)
                if(Member.SquadId==Agent.SquadId&&Member.Id!=Agent.Id)
                {Sum+=Member.Position;++Count;}
            if(Count>0)TargetPosition=Sum/Count;
        }
        else
        {
            const FName TargetId=!Agent.GoalTargetId.IsNone()?Agent.GoalTargetId:Agent.TargetId;
            for(const FHellRunTacticalLabEntity& Target:Asset->Scenario.Entities)
                if(Target.Id==TargetId){TargetPosition=Target.Position;break;}
        }
    }
    if(ATacticalLabEQSQuerier* Querier=Cast<ATacticalLabEQSQuerier>(EQSQueryProxy))
        Querier->ThreatLocation=FVector(TargetPosition,Z);
    if(bUsingNativeQuery)
    {
        const ATacticalLabEQSQuerier* Querier=Cast<ATacticalLabEQSQuerier>(EQSQueryProxy);
        FVector SnappedLocation;
        bool bResolved=false;
        for(TActorIterator<AHellRunVoxelNavVolume> Volume(World);Volume;++Volume)
            if(Volume->ResolveQueryLocation(EQSQueryProxy->GetActorLocation(),
                !Querier||Querier->bCanWalk,Querier&&Querier->bCanClimb,
                Querier&&Querier->bCanFly,SnappedLocation))
            {bResolved=true;break;}
        if(!bResolved)
        {
            int32 VolumeCount=0;int32 CurrentVolumeCount=0;
            for(TActorIterator<AHellRunVoxelNavVolume> Volume(World);Volume;++Volume)
            {++VolumeCount;if(Volume->HasCurrentBakedNavigationData())++CurrentVolumeCount;}
            const FString Failure=FString::Printf(
                TEXT("Native EQS failed: %s cannot snap to runtime voxel navigation (volumes=%d, current=%d)."),
                LivePawn?*LivePawn->GetName():TEXT("authored agent"),VolumeCount,CurrentVolumeCount);
            Status=Failure;
            UE_LOG(LogHellRunTacticalLabEditor,Warning,TEXT("[TacticalLabEQS] %s agent=%s position=(%.0f,%.0f,%.0f)"),
                *Failure,LivePawn?*LivePawn->GetName():
                    (Asset->Scenario.Entities.IsValidIndex(PendingEQSEntityIndex)
                    ?*Asset->Scenario.Entities[PendingEQSEntityIndex].Id.ToString():TEXT("None")),
                Position.X,Position.Y,Z);
            if(TacticalSurface)TacticalSurface->SetEQSResults({},Failure,Position);
            RefreshSimulationViews();
            return;
        }
        EQSQueryProxy->SetActorLocation(SnappedLocation,false,nullptr,ETeleportType::TeleportPhysics);
        UE_LOG(LogHellRunTacticalLabEditor,Verbose,
            TEXT("[TacticalLabEQS] snapped query context authored=(%.0f,%.0f,%.0f) nav=(%.0f,%.0f,%.0f)"),
            Position.X,Position.Y,Z,SnappedLocation.X,SnappedLocation.Y,SnappedLocation.Z);
    }
    AbortLiveEQS();
    FEnvQueryRequest Request(Query,EQSQueryProxy);
    Request.SetWorldOverride(World);
    float CoverTraceHeight=60.0f;
    if(const ACharacter* Character=Cast<ACharacter>(LivePawn))
    {
        if(const UCharacterMovementComponent* Movement=Character->GetCharacterMovement())
            CoverTraceHeight=Movement->GetCrouchedHalfHeight();
    }
    else if(Asset->Scenario.Entities.IsValidIndex(PendingEQSEntityIndex))
    {
        const FName Archetype=Asset->Scenario.Entities[PendingEQSEntityIndex].ArchetypeId;
        if(const FHellRunEnemySimulationProfile* Profile=Asset->Scenario.Profiles.FindByPredicate(
            [Archetype](const FHellRunEnemySimulationProfile& P){return P.ArchetypeId==Archetype;}))
            CoverTraceHeight=Profile->CrouchedCapsuleHalfHeight;
    }
    Request.SetFloatParam(TEXT("CoverTraceHeight"),CoverTraceHeight);
    ActiveCoverTraceHeight=CoverTraceHeight;
    for(const TPair<FName,float>& Parameter:Asset->LiveEQSNamedParameters)
        Request.SetFloatParam(Parameter.Key,Parameter.Value);
    EEnvQueryRunMode::Type Mode=EEnvQueryRunMode::AllMatching;
    switch(Asset->LiveEQSRunMode)
    {
    case ETacticalLabEQSRunMode::SingleResult:Mode=EEnvQueryRunMode::SingleResult;break;
    case ETacticalLabEQSRunMode::RandomBestFivePercent:Mode=EEnvQueryRunMode::RandomBest5Pct;break;
    case ETacticalLabEQSRunMode::RandomBestTwentyFivePercent:Mode=EEnvQueryRunMode::RandomBest25Pct;break;
    default:break;
    }
    EQSQueryStartedSeconds=FPlatformTime::Seconds();
    ActiveEQSQueryId=Request.Execute(Mode,
        FQueryFinishedSignature::CreateSP(this,&FTacticalLabEditorToolkit::HandleLiveEQSFinished));
    ActiveEQSGoal=Goal;
    PendingEQSGoalOverride.Reset();
    bPendingManualEQS=false;
    if(ActiveEQSQueryId!=INDEX_NONE&&TacticalSurface&&!bEQSTickScheduled)
    {
        bEQSTickScheduled=true;
        TWeakPtr<FTacticalLabEditorToolkit> WeakThis=SharedThis(this);
        TacticalSurface->RegisterActiveTimer(0.0f,
            FWidgetActiveTimerDelegate::CreateLambda([WeakThis](double Now,float Delta)
            {
                const TSharedPtr<FTacticalLabEditorToolkit> Self=WeakThis.Pin();
                return Self?Self->TickPendingEQS(Now,Delta):EActiveTimerReturnType::Stop;
            }));
    }
    if(TacticalSurface)TacticalSurface->SetEQSResults({},FString::Printf(
        TEXT("EQS running: %s"),*Query->GetName()),Position);
}

void FTacticalLabEditorToolkit::HandleLiveEQSFinished(TSharedPtr<FEnvQueryResult> Result)
{
    if(!Result.IsValid()||Result->QueryID!=ActiveEQSQueryId)return;
    ActiveEQSQueryId=INDEX_NONE;
    TArray<FTacticalLabEQSItem> DisplayItems;
    const int32 Limit=Asset?FMath::Max(1,Asset->MaximumDisplayedEQSItems):500;
    DisplayItems.Reserve(FMath::Min(Limit,Result->Items.Num()));
    for(int32 I=0;I<Result->Items.Num()&&DisplayItems.Num()<Limit;++I)
    {
        if(Result->Items[I].DataOffset<0)continue;
        FTacticalLabEQSItem& Item=DisplayItems.AddDefaulted_GetRef();
        Item.Position=FVector2D(Result->GetItemAsLocation(I));
        Item.Label=FString::Printf(TEXT("C%02d"),DisplayItems.Num());
        Item.Score=Result->GetItemScore(I);Item.bValid=Result->Items[I].IsValid();
    }
    const double Milliseconds=(FPlatformTime::Seconds()-EQSQueryStartedSeconds)*1000.0;
    const FString QueryName=Asset&&!Asset->LiveEQSQuery.IsNull()
        ?Asset->LiveEQSQuery.GetAssetName():FString::Printf(TEXT("Native %s"),
            *StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(
                static_cast<int64>(ActiveEQSGoal)));
    FString QueryStatus=FString::Printf(TEXT("%s | %s | %d/%d items | %.1f ms"),
        *QueryName,Result->IsSuccessful()?TEXT("SUCCESS"):TEXT("FAILED"),
        DisplayItems.Num(),Result->Items.Num(),Milliseconds);
    if(ActiveEQSGoal==EHellRunTacticalLabGoal::FindCover)
        QueryStatus+=FString::Printf(TEXT(" | protected-side dot <= -0.10 | crouch LOS %.0f cm"),
            ActiveCoverTraceHeight);
    UE_LOG(LogHellRunTacticalLabEditor,Display,TEXT("[TacticalLabEQS] %s"),*QueryStatus);
    TransientEQSCandidates.RemoveAll([this](const FHellRunTacticalLabEntity& Item)
        {return PendingEQSEntityIndex!=INDEX_NONE&&Asset&&Asset->Scenario.Entities.IsValidIndex(PendingEQSEntityIndex)
            &&Item.CandidateOwnerId==Asset->Scenario.Entities[PendingEQSEntityIndex].Id;});
    if(Asset&&Asset->Scenario.Entities.IsValidIndex(PendingEQSEntityIndex))
    {
        const FName Owner=Asset->Scenario.Entities[PendingEQSEntityIndex].Id;
        const int32 CandidateLimit=FMath::Min(128,DisplayItems.Num());
        for(int32 I=0;I<CandidateLimit;++I)if(DisplayItems[I].bValid)
        {
            FHellRunTacticalLabEntity& Candidate=TransientEQSCandidates.AddDefaulted_GetRef();
            Candidate.Id=FName(*FString::Printf(TEXT("EQS_%s_%03d"),*Owner.ToString(),I));
            Candidate.Kind=EHellRunTacticalLabEntityKind::Candidate;
            Candidate.Team=TEXT("Neutral");Candidate.Position=DisplayItems[I].Position;
            Candidate.CandidateOwnerId=Owner;
        }
    }
    bSimulationDirty=true;Status=QueryStatus;
    TArray<TArray<FVector2D>> QueryPaths;
    int32 BestItemIndex=INDEX_NONE;float BestItemScore=-BIG_NUMBER;
    for(int32 ItemIndex=0;ItemIndex<Result->Items.Num();++ItemIndex)
        if(Result->Items[ItemIndex].DataOffset>=0&&Result->Items[ItemIndex].IsValid()&&
            Result->GetItemScore(ItemIndex)>BestItemScore)
        {BestItemScore=Result->GetItemScore(ItemIndex);BestItemIndex=ItemIndex;}
    if(BestItemIndex!=INDEX_NONE&&IsValid(EQSQueryProxy))
    {
        const ATacticalLabEQSQuerier* Querier=Cast<ATacticalLabEQSQuerier>(EQSQueryProxy);
        const FVector Start=EQSQueryProxy->GetActorLocation();
        const FVector GoalLocation=Result->GetItemAsLocation(BestItemIndex);
        if(UWorld* World=EQSQueryProxy->GetWorld())
            for(TActorIterator<AHellRunVoxelNavVolume> Volume(World);Volume;++Volume)
            {
                TArray<FVector> VoxelPath;
                if(Volume->BuildQueryPath(Start,GoalLocation,!Querier||Querier->bCanWalk,
                    Querier&&Querier->bCanClimb,Querier&&Querier->bCanMantle,
                    Querier&&Querier->bCanDrop,Querier&&Querier->bCanJump,
                    Querier&&Querier->bCanVault,Querier&&Querier->bCanFly,VoxelPath))
                {
                    TArray<FVector2D>& Path2D=QueryPaths.AddDefaulted_GetRef();
                    Path2D.Reserve(VoxelPath.Num());
                    for(const FVector& Point:VoxelPath)Path2D.Add(FVector2D(Point));
                    break;
                }
            }
    }
    if(TacticalSurface)TacticalSurface->SetEQSPaths(MoveTemp(QueryPaths));
    if(TacticalSurface)TacticalSurface->SetEQSResults(MoveTemp(DisplayItems),QueryStatus,
        PendingEQSPosition);
    RefreshSimulationViews();
}

void FTacticalLabEditorToolkit::ActivateSection(FName Section)
{
    ActiveSection=Section;
    if(Section==TEXT("Scenario"))GetTabManager()->TryInvokeTab(InspectorTabId);
    else if(Section==TEXT("Simulation")||Section==TEXT("Replay"))
        GetTabManager()->TryInvokeTab(TimelineTabId);
    else if(Section==TEXT("GOAP"))GetTabManager()->TryInvokeTab(GoalGraphTabId);
    else if(Section==TEXT("Reports"))GetTabManager()->TryInvokeTab(LifetimesTabId);
    else if(TacticalSurface)
    {
        TacticalSurface->SetViewMode(Section);
        if(Section==TEXT("Agents"))Status=TEXT("Agent overlay: candidates hidden, labels enabled");
        else if(Section==TEXT("Squads"))Status=TEXT("Squad overlay: membership links enabled");
        else if(Section==TEXT("Routes"))Status=TEXT("Route overlay: authored, accepted, and traversal paths");
        else Status=TEXT("Tactical overlay");
    }
    if(TacticalSurface)TacticalSurface->Invalidate(EInvalidateWidgetReason::Paint);
    if(GoalGraph)GoalGraph->Invalidate(EInvalidateWidgetReason::Paint);
}

void FTacticalLabEditorToolkit::RefreshSimulationViews()
{
    const FHellRunTacticalLabLifetime* Current=Simulation?&Simulation->GetLifetime():nullptr;
    // Initialize emits LifetimeStarted immediately. Treating that bookkeeping
    // event as simulation output hid the completed lifetime after Run/Run 100,
    // including every selected route and candidate overlay.
    const bool bCurrentHasData=Current&&(!Current->Decisions.IsEmpty()||
        !Current->Candidates.IsEmpty()||Current->DurationSeconds>0.0f);
    const FHellRunTacticalLabLifetime* L=bCurrentHasData?Current:
        (Lifetimes.IsEmpty()?Current:Lifetimes.Last().Get());
    if(TacticalSurface)TacticalSurface->SetRuntimeRoutes(L);
    if(TacticalSurface)
    {
        const TArray<FHellRunTacticalLabEntity>* DisplayEntities=bCurrentHasData&&Simulation
            ?&Simulation->GetState().Entities:(L?&L->FinalEntities:nullptr);
        TacticalSurface->SetRuntimeEntities(DisplayEntities);
    }
    if (GoalGraph) GoalGraph->SetLifetime(L,Status,
        Asset&&Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex)
            ?Asset->Scenario.Entities[SelectedEntityIndex].Id:NAME_None);
    if(AgentInspectorBox)RefreshAgentInspector();
    if (TimelineBox)
    {
        TimelineBox->ClearChildren();
        const FName SelectedAgent=Asset&&Asset->Scenario.Entities.IsValidIndex(SelectedEntityIndex)
            ?Asset->Scenario.Entities[SelectedEntityIndex].Id:NAME_None;
        TimelineBox->AddSlot().AutoHeight().Padding(10,6)
        [SNew(STextBlock).Text(FText::FromString(SelectedAgent.IsNone()
            ?TEXT("EVENT TIMELINE  |  Select an agent to focus its reasoning")
            :FString::Printf(TEXT("EVENT TIMELINE  |  %s + world events"),*SelectedAgent.ToString())))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10))
            .ColorAndOpacity(FLinearColor(.35f,.72f,.85f))];
        if (!L || L->Timeline.IsEmpty()) TimelineBox->AddSlot().AutoHeight().Padding(20)[SNew(STextBlock).Text(LOCTEXT("Empty","Run or step the simulation to populate the timeline."))];
        else
        {
            TArray<int32> VisibleEvents;
            VisibleEvents.Reserve(100);
            const auto IsMeaningful=[this](const FHellRunTacticalLabEvent& E)
            {
                if(!bPlaying)return true;
                return E.Type==TEXT("LifetimeStarted")||E.Type==TEXT("LifetimeCompleted")||
                    E.Type==TEXT("SimulationError")||E.Type==TEXT("GOAPPlanBuilt")||
                    E.Type==TEXT("TacticalEvaluation")||
                    E.Type==TEXT("CandidateSelected")||E.Type==TEXT("HoldSelected")||
                    E.Type==TEXT("MovementStarted")||E.Type==TEXT("MovementCompleted")||
                    E.Type==TEXT("MovementBlocked")||E.Type==TEXT("GoalSelected")||
                    E.Type==TEXT("PlanInvalidated")||E.Type==TEXT("ReplanStarted")||
                    E.Type==TEXT("ReplanCompleted")||E.Type==TEXT("ActionStarted")||
                    E.Type==TEXT("ActionCompleted")||E.Type==TEXT("ActionFailed")||
                    E.Type==TEXT("PerceptionChanged")||E.Type==TEXT("StateChanged");
            };
            const int32 RowLimit=bPlaying?80:160;
            for(int32 I=L->Timeline.Num()-1;I>=0&&VisibleEvents.Num()<RowLimit;--I)
            {
                const FHellRunTacticalLabEvent& E=L->Timeline[I];
                if(!IsMeaningful(E))continue;
                if(!SelectedAgent.IsNone()&&!E.AgentId.IsNone()&&E.AgentId!=SelectedAgent)continue;
                VisibleEvents.Add(I);
            }
            Algo::Reverse(VisibleEvents);
            for(const int32 EventIndex:VisibleEvents)
            {
                const FHellRunTacticalLabEvent& E=L->Timeline[EventIndex];
                const FString Detail=E.Detail.Len()>180?E.Detail.Left(177)+TEXT("..."):E.Detail;
                const FString Row=FString::Printf(TEXT("%06.2f  %-13s  %-19s  %s"),
                    E.Time,E.AgentId.IsNone()?TEXT("WORLD"):*E.AgentId.ToString(),
                    *E.Type.ToString(),*Detail);
                TimelineBox->AddSlot().AutoHeight().Padding(6,1)
                [SNew(SBox).HeightOverride(25)
                [SNew(SButton).ContentPadding(FMargin(6,3))
                    .ToolTipText(FText::FromString(E.Detail))
                    .OnClicked_Lambda([this,EventAgent=E.AgentId]
                    {
                        if(Asset&&!EventAgent.IsNone())
                            for(int32 EntityIndex=0;EntityIndex<Asset->Scenario.Entities.Num();++EntityIndex)
                                if(Asset->Scenario.Entities[EntityIndex].Id==EventAgent)
                                {HandleEntitySelected(EntityIndex);break;}
                        return FReply::Handled();
                    })
                [SNew(STextBlock).Text(FText::FromString(Row))
                    .ToolTipText(FText::FromString(E.Detail))
                    .Clipping(EWidgetClipping::ClipToBoundsAlways)]]];
            }
        }
    }
    if (LifetimesBox)
    {
        LifetimesBox->ClearChildren();
        LifetimesBox->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(LOCTEXT("LifetimeColumns","LIFETIME       SEED       DURATION       RESULT"))
            .ColorAndOpacity(FLinearColor(.55f,.68f,.75f))];
        for (int32 I=Lifetimes.Num()-1; I>=0 && I>=Lifetimes.Num()-250; --I)
        {
            const FHellRunTacticalLabLifetime& Item=*Lifetimes[I];
            LifetimesBox->AddSlot().AutoHeight().Padding(5,2)[SNew(SBorder).Padding(5)
                [SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("L-%06d       %-8d   %07.2fs       %s"),Item.LifetimeIndex,Item.Seed,Item.DurationSeconds,*UEnum::GetValueAsString(Item.Result))))
                    .ColorAndOpacity(Item.Result==EHellRunTacticalLabResult::Pass?FLinearColor(.2f,.9f,.35f):FLinearColor(1,.25f,.12f))]];
        }
    }
}

#undef LOCTEXT_NAMESPACE
