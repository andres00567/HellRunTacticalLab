#include "TacticalLabPIESessionRecorder.h"

#include "AIController.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GOAPBrainComponent.h"
#include "GOAPWorldStateSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TacticalLabIntegrations.h"

FTacticalLabPIESessionRecorder* FTacticalLabPIESessionRecorder::Instance=nullptr;

FTacticalLabPIESessionRecorder::FTacticalLabPIESessionRecorder() = default;

FTacticalLabPIESessionRecorder::~FTacticalLabPIESessionRecorder()
{
    Shutdown();
}

void FTacticalLabPIESessionRecorder::Initialize()
{
    if(bInitialized) return;
    check(!Instance);
    Instance=this;
    bInitialized=true;
    BeginPIEHandle=FEditorDelegates::BeginPIE.AddRaw(this,
        &FTacticalLabPIESessionRecorder::HandleBeginPIE);
    EndPIEHandle=FEditorDelegates::EndPIE.AddRaw(this,
        &FTacticalLabPIESessionRecorder::HandleEndPIE);
    RuntimeEventHandle=UGOAPBrainComponent::OnAnyRuntimeEvent().AddRaw(this,
        &FTacticalLabPIESessionRecorder::HandleRuntimeEvent);
    TickerHandle=FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this,&FTacticalLabPIESessionRecorder::Tick),
        0.05f);
}

void FTacticalLabPIESessionRecorder::Shutdown()
{
    if(!bInitialized) return;
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
    FEditorDelegates::EndPIE.Remove(EndPIEHandle);
    UGOAPBrainComponent::OnAnyRuntimeEvent().Remove(RuntimeEventHandle);
    EndSession();
    bInitialized=false;
    if(Instance==this) Instance=nullptr;
}

FTacticalLabPIESessionRecorder* FTacticalLabPIESessionRecorder::Get()
{
    return Instance;
}

void FTacticalLabPIESessionRecorder::HandleBeginPIE(bool)
{
    LastCapturePlatformSeconds=-BIG_NUMBER;
    if(GEditor&&GEditor->PlayWorld)
        StartSession(*GEditor->PlayWorld.Get());
}

void FTacticalLabPIESessionRecorder::HandleEndPIE(bool)
{
    // Actor/component EndPlay follows the editor notification. Keep the session
    // open until PlayWorld disappears so LogicStopped/ActionAborted events are
    // retained instead of being dropped during teardown.
    LastCapturePlatformSeconds=-BIG_NUMBER;
}

bool FTacticalLabPIESessionRecorder::Tick(float)
{
    UWorld* PIEWorld=GEditor?GEditor->PlayWorld.Get():nullptr;
    if(!PIEWorld)
    {
        if(Session.bActive) EndSession();
        return true;
    }
    if(!Session.bActive||ActiveWorld.Get()!=PIEWorld)
        StartSession(*PIEWorld);

    DiscoverBrains(*PIEWorld);
    const double PlatformNow=FPlatformTime::Seconds();
    constexpr double SpatialCaptureIntervalSeconds=0.1;
    if(PlatformNow-LastCapturePlatformSeconds>=SpatialCaptureIntervalSeconds)
    {
        LastCapturePlatformSeconds=PlatformNow;
        FTacticalLabPIEFrame Frame;
        if(CaptureFrame(*PIEWorld,Frame))
        {
            Session.Frames.Add(MoveTemp(Frame));
            TrimRecording();
            ++Session.Revision;
        }
    }
    return true;
}

void FTacticalLabPIESessionRecorder::StartSession(UWorld& World)
{
    if(Session.bActive) EndSession();
    Session={};
    Session.SessionId=FGuid::NewGuid();
    Session.StartWorldTime=World.GetTimeSeconds();
    Session.EndWorldTime=Session.StartWorldTime;
    Session.bActive=true;
    Session.Revision=1;
    ActiveWorld=&World;
    SpatialAgentIds.Reset();
    LastCapturePlatformSeconds=-BIG_NUMBER;
    DiscoverBrains(World);
}

void FTacticalLabPIESessionRecorder::EndSession()
{
    if(!Session.bActive) return;
    Session.EndWorldTime=ActiveWorld.IsValid()
        ?ActiveWorld->GetTimeSeconds()
        :(Session.Frames.IsEmpty()?Session.StartWorldTime
            :Session.Frames.Last().WorldTime);
    Session.bActive=false;
    ++Session.Revision;
    ActiveWorld.Reset();
    SpatialAgentIds.Reset();
}

void FTacticalLabPIESessionRecorder::DiscoverBrains(UWorld& World)
{
    UGOAPWorldStateSubsystem* State=World.GetSubsystem<UGOAPWorldStateSubsystem>();
    if(!State) return;
    TArray<UGOAPBrainComponent*> Brains;
    State->GetBrains(Brains);
    for(UGOAPBrainComponent* Brain:Brains)
    {
        if(!IsValid(Brain)) continue;
        const FGuid AgentId=Brain->GetRuntimeAgentId();
        if(!AgentId.IsValid()) continue;
        AActor* Agent=Brain->GetOwner();
        if(const AController* Controller=Cast<AController>(Agent))
            if(Controller->GetPawn()) Agent=Controller->GetPawn();
        FTacticalLabPIEAgentIdentity& Identity=Session.Agents.FindOrAdd(AgentId);
        Identity.AgentId=AgentId;
        Identity.DisplayName=Agent?Agent->GetFName():NAME_None;
        Identity.ClassName=Agent?Agent->GetClass()->GetFName():NAME_None;
        Identity.bHasGOAP=true;
    }
}

void FTacticalLabPIESessionRecorder::HandleRuntimeEvent(
    const FGOAPRuntimeEvent& Event)
{
    const UGOAPBrainComponent* Brain=Cast<UGOAPBrainComponent>(Event.Source.Get());
    if(!Brain||!Brain->GetWorld()
        ||Brain->GetWorld()->WorldType!=EWorldType::PIE) return;
    if(!Session.bActive) StartSession(*Brain->GetWorld());
    if(Brain->GetWorld()!=ActiveWorld.Get()) return;
    Session.Events.Add(Event);
    FTacticalLabPIEAgentIdentity& Identity=Session.Agents.FindOrAdd(Event.AgentId);
    Identity.AgentId=Event.AgentId;
    Identity.DisplayName=Event.AgentName;
    Identity.ClassName=Event.AgentClass;
    Identity.bHasGOAP=true;
    TrimRecording();
    ++Session.Revision;
}

FGuid FTacticalLabPIESessionRecorder::FindOrAddSpatialId(AActor& Actor)
{
    const TWeakObjectPtr<AActor> Key(&Actor);
    if(const FGuid* Existing=SpatialAgentIds.Find(Key)) return *Existing;
    const FGuid NewId=FGuid::NewGuid();
    SpatialAgentIds.Add(Key,NewId);
    return NewId;
}

bool FTacticalLabPIESessionRecorder::CaptureFrame(UWorld& World,
    FTacticalLabPIEFrame& OutFrame)
{
    OutFrame={};
    OutFrame.WorldTime=World.GetTimeSeconds();
    OutFrame.bHasDirectorDebug=FTacticalLabIntegrations::CaptureDirectorDebug(
        World,OutFrame.Director);
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
    for(TActorIterator<APawn> It(&World);
        It&&OutFrame.Agents.Num()<MaximumTrackedAgents;++It)
    {
        APawn* Pawn=*It;
        AController* Controller=Pawn->GetController();
        if(!Controller) continue;
        AAIController* AI=Cast<AAIController>(Controller);
        const bool bPlayer=Controller->IsPlayerController();
        if(!AI&&!bPlayer) continue;

        UGOAPBrainComponent* Brain=
            Controller->FindComponentByClass<UGOAPBrainComponent>();
        if(!Brain) Brain=Pawn->FindComponentByClass<UGOAPBrainComponent>();

        FTacticalLabPIEAgentSnapshot& Agent=
            OutFrame.Agents.AddDefaulted_GetRef();
        Agent.AgentId=Brain&&Brain->GetRuntimeAgentId().IsValid()
            ?Brain->GetRuntimeAgentId():FindOrAddSpatialId(*Pawn);
        Agent.Entity.Id=Pawn->GetFName();
        ETeamAttitude::Type PlayerAttitude=ETeamAttitude::Neutral;
        if(AI)
            for(TActorIterator<APawn> PlayerIt(&World);PlayerIt;++PlayerIt)
                if(PlayerIt->GetController()&&PlayerIt->GetController()->IsPlayerController())
                {
                    const ETeamAttitude::Type Attitude=AI->GetTeamAttitudeTowards(**PlayerIt);
                    if(Attitude==ETeamAttitude::Hostile)
                    {PlayerAttitude=Attitude;break;}
                    if(Attitude==ETeamAttitude::Friendly)
                        PlayerAttitude=Attitude;
                }
        Agent.Entity.Kind=bPlayer?EHellRunTacticalLabEntityKind::Player:
            PlayerAttitude==ETeamAttitude::Friendly?EHellRunTacticalLabEntityKind::Friendly:
            PlayerAttitude==ETeamAttitude::Hostile?EHellRunTacticalLabEntityKind::Enemy:
            EHellRunTacticalLabEntityKind::Neutral;
        Agent.Entity.Team=bPlayer?TEXT("Players"):
            PlayerAttitude==ETeamAttitude::Friendly?TEXT("Friendly"):
            PlayerAttitude==ETeamAttitude::Hostile?TEXT("Hostile"):TEXT("Neutral");
        Agent.Entity.ArchetypeId=Pawn->GetClass()->GetFName();
        Agent.Entity.Position=FVector2D(Pawn->GetActorLocation());
        Agent.Entity.Velocity=FVector2D(Pawn->GetVelocity());
        FVector ViewLocation;
        FRotator ViewRotation;
        if(const APlayerController* PlayerController=Cast<APlayerController>(Controller))
            PlayerController->GetPlayerViewPoint(ViewLocation,ViewRotation);
        else
            Pawn->GetActorEyesViewPoint(ViewLocation,ViewRotation);
        Agent.VisionOrigin=FVector2D(ViewLocation);
        Agent.Entity.Facing=FVector2D(ViewRotation.Vector()).GetSafeNormal();
        if(Agent.Entity.Facing.IsNearlyZero())
            Agent.Entity.Facing=FVector2D(Pawn->GetActorForwardVector()).GetSafeNormal();
        Agent.Entity.bAlive=!Pawn->IsActorBeingDestroyed();

        FTacticalLabPIEAgentIdentity& Identity=
            Session.Agents.FindOrAdd(Agent.AgentId);
        Identity.AgentId=Agent.AgentId;
        Identity.DisplayName=Agent.Entity.Id;
        Identity.ClassName=Agent.Entity.ArchetypeId;
        Identity.bHasGOAP=Brain!=nullptr;

        if(Brain)
        {
            Agent.bHasGOAP=true;
            Agent.GOAP=Brain->GetDebugSnapshot();
        }

        if(AI)
        {
            if(const UPathFollowingComponent* Following=
                AI->GetPathFollowingComponent())
            {
                if(const FNavPathSharedPtr Path=Following->GetPath();Path.IsValid())
                    for(const FNavPathPoint& Point:Path->GetPathPoints())
                        Agent.MovementPath.Add(FVector2D(Point.Location));
                if(Agent.MovementPath.Num()<2&&
                    Following->GetStatus()!=EPathFollowingStatus::Idle)
                {
                    const FVector Target=Following->GetCurrentTargetLocation();
                    if(!Target.ContainsNaN()&&
                        !FVector::PointsAreNear(Pawn->GetActorLocation(),Target,1.0f))
                    {
                        Agent.MovementPath={FVector2D(Pawn->GetActorLocation()),
                            FVector2D(Target)};
                    }
                }
            }
            FTacticalLabAgentRuntimeDebugSnapshot HostDebug;
            if(FTacticalLabIntegrations::CaptureAgentRuntimeDebug(*Pawn,HostDebug)
                &&HostDebug.RetainedRoute.Num()>1)
            {
                Agent.MovementPath.Reset(HostDebug.RetainedRoute.Num());
                for(const FVector& Point:HostDebug.RetainedRoute)
                    Agent.MovementPath.Add(FVector2D(Point));
                Agent.RouteProvider=MoveTemp(HostDebug.RouteProvider);
                Agent.RouteAdmission=MoveTemp(HostDebug.RouteAdmission);
                Agent.RouteExposure=HostDebug.RouteExposure;
            }

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
                // The configured cone is authoritative even when resolved trace
                // samples are not available from the gameplay perception system.
                Agent.VisionRayCount=25;
            }
        }
    }
    return !OutFrame.Agents.IsEmpty();
}

void FTacticalLabPIESessionRecorder::TrimRecording()
{
    constexpr int32 MaximumFrames=900;
    constexpr int32 MaximumEvents=10000;
    if(Session.Frames.Num()>MaximumFrames)
        Session.Frames.RemoveAt(0,Session.Frames.Num()-MaximumFrames,
            EAllowShrinking::No);
    if(Session.Events.Num()>MaximumEvents)
        Session.Events.RemoveAt(0,Session.Events.Num()-MaximumEvents,
            EAllowShrinking::No);
}
