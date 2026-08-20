#include "Core/AI/TacticalLab/HellRunTacticalLab.h"

#include "Core/AI/TacticalLab/HellRunTacticalEvaluator.h"
#include "TacticalLabIntegrations.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"

namespace
{
const TCHAR* ResultString(const EHellRunTacticalLabResult Result)
{
    switch (Result)
    {
    case EHellRunTacticalLabResult::Pass: return TEXT("PASS");
    case EHellRunTacticalLabResult::Fail: return TEXT("FAIL");
    case EHellRunTacticalLabResult::Timeout: return TEXT("TIMEOUT");
    case EHellRunTacticalLabResult::Deadlock: return TEXT("DEADLOCK");
    case EHellRunTacticalLabResult::InvalidScenario: return TEXT("INVALID_SCENARIO");
    default: return TEXT("SIMULATION_ERROR");
    }
}

TArray<FVector2D> BuildSmoothRouteSamples(const TArray<FVector2D>& Points)
{
    if(Points.Num()<3)return Points;
    TArray<FVector2D> Result;
    Result.Reserve(Points.Num()*8);
    for(int32 I=1;I<Points.Num();++I)
    {
        const FVector2D P0=Points[FMath::Max(0,I-2)];
        const FVector2D P1=Points[I-1];
        const FVector2D P2=Points[I];
        const FVector2D P3=Points[FMath::Min(Points.Num()-1,I+1)];
        const int32 Samples=FMath::Clamp(FMath::CeilToInt(
            FVector2D::Distance(P1,P2)/100.0),2,32);
        for(int32 Step=I==1?0:1;Step<=Samples;++Step)
        {
            const double T=Step/static_cast<double>(Samples),T2=T*T,T3=T2*T;
            Result.Add(.5*((2.0*P1)+(-P0+P2)*T+
                (2.0*P0-5.0*P1+4.0*P2-P3)*T2+
                (-P0+3.0*P1-3.0*P2+P3)*T3));
        }
    }
    return Result;
}

void ExpandShapeObstacles(FHellRunTacticalLabScenario& Scenario)
{
    for(const FHellRunTacticalLabShape& Shape:Scenario.Shapes)
    {
        TArray<FVector2D> Vertices;
        if(Shape.Kind==EHellRunTacticalLabShapeKind::Circle)
        {
            Vertices.Reserve(24);
            for(int32 I=0;I<24;++I)
            {const double A=2.0*PI*I/24.0;Vertices.Add(Shape.Position+FVector2D(FMath::Cos(A),FMath::Sin(A))*Shape.Extents.X);}
        }
        else
        {
            const double A=FMath::DegreesToRadians(Shape.RotationDegrees);
            const FVector2D X(FMath::Cos(A),FMath::Sin(A)),Y(-X.Y,X.X);
            Vertices={Shape.Position-X*Shape.Extents.X-Y*Shape.Extents.Y,
                Shape.Position+X*Shape.Extents.X-Y*Shape.Extents.Y,
                Shape.Position+X*Shape.Extents.X+Y*Shape.Extents.Y,
                Shape.Position-X*Shape.Extents.X+Y*Shape.Extents.Y};
        }
        for(int32 I=0;I<Vertices.Num();++I)
        {
            FHellRunTacticalLabObstacle& Edge=Scenario.Obstacles.AddDefaulted_GetRef();
            Edge.Id=FName(*FString::Printf(TEXT("%s_Edge_%02d"),*Shape.Id.ToString(),I));
            Edge.Start=Vertices[I];Edge.End=Vertices[(I+1)%Vertices.Num()];
            Edge.Height=Shape.Height;Edge.bBlocksMovement=Shape.bBlocksMovement;
            Edge.bBlocksLOS=Shape.bBlocksLOS;
        }
    }
}

template<typename StructType>
bool SaveStruct(const FString& Filename, const StructType& Value,
    FString& OutError)
{
    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(Value, Json))
    {
        OutError = TEXT("Could not serialize JSON");
        return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!FFileHelper::SaveStringToFile(Json, *Filename,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = FString::Printf(TEXT("Could not write %s"), *Filename);
        return false;
    }
    return true;
}

bool LabIsHostile(const FHellRunTacticalLabEntity& A,
    const FHellRunTacticalLabEntity& B)
{
    return A.Team != B.Team && A.Team != TEXT("Neutral")
        && B.Team != TEXT("Neutral");
}
}

FHellRunTacticalLab::FHellRunTacticalLab() = default;

TArray<FHellRunEnemySimulationProfile> FHellRunTacticalLab::GetBuiltInProfiles()
{
    TArray<FHellRunEnemySimulationProfile> Profiles;
    auto Add = [&Profiles](const TCHAR* Id, const TCHAR* Pawn,
        const TCHAR* Controller, const TCHAR* Domain)
    {
        FHellRunEnemySimulationProfile& Profile = Profiles.AddDefaulted_GetRef();
        Profile.ArchetypeId = Id;
        Profile.ProductionPawnClass = FSoftClassPath(Pawn);
        Profile.ProductionControllerClass = FSoftClassPath(Controller);
        Profile.GOAPDomain = FSoftObjectPath(Domain);
        return &Profile;
    };

    FHellRunEnemySimulationProfile* Zombie = Add(TEXT("MeleeZombie"),
        TEXT("/Script/Hell_Run.MeleeEnemyCharacter"),
        TEXT("/Script/Hell_Run.EnemyAIController"),
        TEXT("/Game/AI/GOAP/DA_GOAP_Zombie.DA_GOAP_Zombie"));
    Zombie->bUsesMeleeCombat = true;
    Zombie->PreferredCombatRange = 120.0f;
    Zombie->MaximumCombatRange = 300.0f;

    FHellRunEnemySimulationProfile* Cultist = Add(TEXT("RangedCultist"),
        TEXT("/Script/Hell_Run.CultistEnemyCharacter"),
        TEXT("/Script/Hell_Run.RangedEnemyAIController"),
        TEXT("/Game/AI/GOAP/DA_GOAP_Cultist.DA_GOAP_Cultist"));
    Cultist->bUsesSquadCoordinator = true;
    Cultist->bUsesCover = true;
    Cultist->bUsesRangedCombat = true;
    Cultist->bCanVault = true;
    Cultist->bCanMantle = true;

    FHellRunEnemySimulationProfile* Flyer = Add(TEXT("FlyingEnemy"),
        TEXT("/Script/Hell_Run.FlyingEnemyCharacter"),
        TEXT("/Script/Hell_Run.FlyingEnemyAIController"),
        TEXT("/Game/AI/GOAP/DA_GOAP_Flyer.DA_GOAP_Flyer"));
    Flyer->bUsesRangedCombat = true;
    Flyer->bCanFly = true;
    Flyer->PreferredCombatRange = 1800.0f;

    FHellRunEnemySimulationProfile* Heavy = Add(TEXT("Heavy"),
        TEXT("/Script/Hell_Run.RunningBoomerEnemyCharacter"),
        TEXT("/Script/Hell_Run.EnemyAIController"),
        TEXT("/Game/AI/GOAP/DA_GOAP_Zombie.DA_GOAP_Zombie"));
    Heavy->bUsesMeleeCombat = true;
    Heavy->PreferredCombatRange = 180.0f;
    Heavy->MovementSpeed = 300.0f;
    Heavy->Policy.TraversalRiskWeight = 8.0f;
    return Profiles;
}

bool FHellRunTacticalLab::LoadScenario(const FString& Filename,
    FHellRunTacticalLabScenario& OutScenario, FString& OutError)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Filename))
    {
        OutError = FString::Printf(TEXT("Could not read %s"), *Filename);
        return false;
    }
    if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutScenario))
    {
        OutError = FString::Printf(TEXT("Invalid Tactical Lab JSON: %s"),
            *Filename);
        return false;
    }
    return true;
}

bool FHellRunTacticalLab::SaveScenario(const FString& Filename,
    const FHellRunTacticalLabScenario& Scenario, FString& OutError)
{
    return SaveStruct(Filename, Scenario, OutError);
}

bool FHellRunTacticalLab::SaveLifetime(const FString& Filename,
    const FHellRunTacticalLabLifetime& InLifetime, FString& OutError)
{
    return SaveStruct(Filename, InLifetime, OutError);
}

bool FHellRunTacticalLab::SaveSummary(const FString& Filename,
    const FHellRunTacticalLabBatchSummary& Summary, FString& OutError)
{
    return SaveStruct(Filename, Summary, OutError);
}

bool FHellRunTacticalLab::SaveFailureReport(const FString& Filename,
    const FHellRunTacticalLabLifetime& InLifetime, FString& OutError)
{
    FString Report = FString::Printf(
        TEXT("# Tactical AI Lifetime Failure\n\nScenario: `%s`\n\nSeed: `%d`\n\nLifetime: `%d`\n\nResult: **%s**\n\n## Failure tags\n\n"),
        *InLifetime.ScenarioId.ToString(), InLifetime.Seed,
        InLifetime.LifetimeIndex, ResultString(InLifetime.Result));
    for (const FName Tag : InLifetime.FailureTags)
        Report += FString::Printf(TEXT("- `%s`\n"), *Tag.ToString());
    Report += TEXT("\n## Decisions\n\n");
    for (const FHellRunTacticalLabDecision& Decision : InLifetime.Decisions)
    {
        Report += FString::Printf(TEXT("- %.3f `%s` %s; candidate `%s`, route `%s`: %s\n"),
            Decision.Time, *Decision.AgentId.ToString(), *Decision.Intent.ToString(),
            *Decision.SelectedCandidateId.ToString(),
            *Decision.SelectedRouteId.ToString(), *Decision.Reason);
    }
    Report += TEXT("\n## Reproduce\n\n```text\nHellRunTacticalLab -Scenario=<scenario.json> -Seed=");
    Report += FString::FromInt(InLifetime.Seed);
    Report += TEXT(" -Lifetime=");
    Report += FString::FromInt(InLifetime.LifetimeIndex);
    Report += TEXT("\n```\n");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!FFileHelper::SaveStringToFile(Report, *Filename,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = FString::Printf(TEXT("Could not write %s"), *Filename);
        return false;
    }
    return true;
}

bool FHellRunTacticalLab::Initialize(
    const FHellRunTacticalLabScenario& InScenario, const int32 Seed,
    const int32 LifetimeIndex, FString& OutError)
{
    InitialScenario = InScenario;
    State = InScenario;
    ExpandShapeObstacles(State);
    if (State.Profiles.IsEmpty()) State.Profiles = GetBuiltInProfiles();
    const bool bHasEnemy = State.Entities.ContainsByPredicate(
        [](const FHellRunTacticalLabEntity& Entity)
        { return Entity.Kind == EHellRunTacticalLabEntityKind::Enemy; });
    const bool bHasThreat = State.Entities.ContainsByPredicate(
        [](const FHellRunTacticalLabEntity& Entity)
        { return Entity.Kind == EHellRunTacticalLabEntityKind::Player
            || Entity.Kind == EHellRunTacticalLabEntityKind::Friendly; });
    if (!bHasEnemy || !bHasThreat || State.ScenarioId.IsNone())
    {
        OutError = TEXT("Scenario requires an id, at least one enemy, and at least one player/friendly threat");
        return false;
    }
    for (const FHellRunTacticalLabEntity& Entity : State.Entities)
    {
        if (Entity.Kind == EHellRunTacticalLabEntityKind::Enemy
            && !FindProfile(Entity.ArchetypeId))
        {
            OutError = FString::Printf(TEXT("No simulation profile for %s (%s)"),
                *Entity.Id.ToString(), *Entity.ArchetypeId.ToString());
            return false;
        }
    }

    Random.Initialize(Seed);
    Lifetime = {};
    Lifetime.ScenarioId = State.ScenarioId;
    Lifetime.ScenarioVersion = State.ScenarioVersion;
    Lifetime.Seed = Seed;
    Lifetime.LifetimeIndex = LifetimeIndex;
    Lifetime.InitialEntities = State.Entities;
    Lifetime.Result = EHellRunTacticalLabResult::Pass;
    Lifetime.TuningSnapshotId = FString::Printf(TEXT("%s-v%d"),
        *State.ScenarioId.ToString(), State.ScenarioVersion);
    Lifetime.BuildIdentifier = FEngineVersion::Current().ToString();
    Time = 0.0f;
    DecisionEpoch = 0;
    IdleDecisionCount = 0;
    bComplete = false;
    ActiveDestinations.Reset();
    ActiveRouteIds.Reset();
    ActiveRoutes.Reset();
    ActiveRoutePointIndices.Reset();
    RecordEvent(TEXT("LifetimeStarted"), NAME_None, NAME_None,
        FString::Printf(TEXT("seed=%d"), Seed));
    return true;
}

const FHellRunEnemySimulationProfile* FHellRunTacticalLab::FindProfile(
    const FName ArchetypeId) const
{
    return State.Profiles.FindByPredicate(
        [ArchetypeId](const FHellRunEnemySimulationProfile& Profile)
        { return Profile.ArchetypeId == ArchetypeId; });
}

const FHellRunTacticalLabEntity* FHellRunTacticalLab::FindThreat(
    const FHellRunTacticalLabEntity& Agent) const
{
    const FName ExplicitTargetId=!Agent.GoalTargetId.IsNone()
        ?Agent.GoalTargetId:Agent.TargetId;
    if (!ExplicitTargetId.IsNone())
    {
        if (const FHellRunTacticalLabEntity* Explicit = State.Entities.FindByPredicate(
            [ExplicitTargetId](const FHellRunTacticalLabEntity& Entity)
            { return Entity.Id == ExplicitTargetId && Entity.bAlive; }))
            return Explicit;
    }
    const FHellRunTacticalLabEntity* Best = nullptr;
    float BestDistance = BIG_NUMBER;
    for (const FHellRunTacticalLabEntity& Candidate : State.Entities)
    {
        if (!Candidate.bAlive || !LabIsHostile(Agent, Candidate)) continue;
        const float Distance = FVector2D::DistSquared(
            Agent.Position, Candidate.Position);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = &Candidate;
        }
    }
    return Best;
}

void FHellRunTacticalLab::RecordEvent(const FName Type, const FName AgentId,
    const FName DecisionId, const FString& Detail)
{
    FHellRunTacticalLabEvent& Event = Lifetime.Timeline.AddDefaulted_GetRef();
    Event.Time = Time;
    Event.Type = Type;
    Event.AgentId = AgentId;
    Event.DecisionId = DecisionId;
    Event.Detail = Detail;
}

bool FHellRunTacticalLab::StepDecision()
{
    if (bComplete) return false;
    ++DecisionEpoch;
    bool bSelectedMovement = false;
    for (FHellRunTacticalLabEntity& Agent : State.Entities)
    {
        if (!Agent.bAlive || Agent.Kind != EHellRunTacticalLabEntityKind::Enemy)
            continue;
        const FHellRunEnemySimulationProfile* Profile = FindProfile(Agent.ArchetypeId);
        const FHellRunTacticalLabEntity* Threat = FindThreat(Agent);
        if (!Profile || !Threat) continue;
        const FName DecisionId(*FString::Printf(TEXT("TD_%d_%s"),
            DecisionEpoch, *Agent.Id.ToString()));
        RecordEvent(TEXT("CandidateQueryStarted"), Agent.Id, DecisionId,
            FString::Printf(TEXT("goal=%s target=%s"),
                *StaticEnum<EHellRunTacticalLabGoal>()->GetNameStringByValue(
                    static_cast<int64>(Agent.Goal)),*Threat->Id.ToString()));

        FHellRunTacticalLabDecision& Decision =
            Lifetime.Decisions.AddDefaulted_GetRef();
        Decision.Time = Time;
        Decision.DecisionId = DecisionId;
        Decision.AgentId = Agent.Id;
        Decision.ArchetypeId = Agent.ArchetypeId;
        Decision.Intent = FName(*StaticEnum<EHellRunTacticalLabGoal>()->
            GetNameStringByValue(static_cast<int64>(Agent.Goal)));
        Decision.FactProvenance.Add(TEXT("ConfiguredGoal"),
            FString::Printf(TEXT("Scenario:%s"),*Decision.Intent.ToString()));
        Decision.FactProvenance.Add(TEXT("HasTarget"),
            FString::Printf(TEXT("SimulationWorld:%s"), *Threat->Id.ToString()));
        Decision.FactProvenance.Add(TEXT("MovementGranted"),
            FString::Printf(TEXT("SquadFixture:%s"),
                Agent.bMovementGranted ? TEXT("true") : TEXT("false")));

        if(Agent.Goal==EHellRunTacticalLabGoal::HoldPosition)
        {
            Decision.Reason=TEXT("Configured goal requires holding the current position");
            RecordEvent(TEXT("HoldSelected"),Agent.Id,DecisionId,Decision.Reason);
            continue;
        }
        TArray<FHellRunTacticalLabRoute> Routes;
        FHellRunTacticalEvaluator::GenerateCandidateRoutes(State, Agent,
            *Threat, Random, Routes);
        FHellRunTacticalLabCandidateRecord* Best = nullptr;
        int32 AcceptedCount=0;
        int32 RejectedCount=0;
        for (const FHellRunTacticalLabRoute& Route : Routes)
        {
            FHellRunEnemySimulationProfile GoalProfile=*Profile;
            if(Agent.Goal==EHellRunTacticalLabGoal::FindCover)
            {
                GoalProfile.Policy.CoverWeight=FMath::Max(8.0f,
                    GoalProfile.Policy.CoverWeight);
                GoalProfile.Policy.AttackUtilityWeight=0.0f;
                GoalProfile.Policy.DestinationWeight=FMath::Max(3.0f,
                    GoalProfile.Policy.DestinationWeight);
            }
            FHellRunTacticalLabCandidateRecord Record =
                FHellRunTacticalEvaluator::EvaluateRoute(State, Agent,
                    *Threat, Route, GoalProfile, DecisionId);
            const int32 Index = Lifetime.Candidates.Add(MoveTemp(Record));
            FHellRunTacticalLabCandidateRecord& Stored = Lifetime.Candidates[Index];
            AcceptedCount+=Stored.Score.bAccepted?1:0;
            RejectedCount+=Stored.Score.bAccepted?0:1;
            if (Stored.Score.bAccepted && (!Best
                || Stored.Score.FinalScore > Best->Score.FinalScore))
                Best = &Stored;
        }
        RecordEvent(TEXT("TacticalEvaluation"),Agent.Id,DecisionId,
            FString::Printf(TEXT("candidates=%d accepted=%d rejected=%d best=%s score=%.3f"),
                Routes.Num(),AcceptedCount,RejectedCount,
                Best?*Best->CandidateId.ToString():TEXT("None"),
                Best?Best->Score.FinalScore:-BIG_NUMBER));

        if (Profile->bUsesGOAP && !Profile->GOAPDomain.IsNull())
        {
            FTacticalLabPlanRequest Request;
            Request.Domain = Profile->GOAPDomain;
            Request.CaseName = DecisionId;
            auto SetBoolIfDeclared = [&Request](const FName Name,
                const bool bValue) { Request.BoolFacts.Add(Name, bValue); };
                SetBoolIfDeclared(TEXT("HasTarget"), true);
                SetBoolIfDeclared(TEXT("HasFiringLane"),
                    Best && Best->Score.AttackUtility > 0.0f);
                SetBoolIfDeclared(TEXT("MovementGranted"),
                    Agent.bMovementGranted);
                SetBoolIfDeclared(TEXT("HasApprovedCoverManeuver"),
                    Best && Best->Score.CoverProtection > 0.0f);
                SetBoolIfDeclared(TEXT("HasApprovedFlankManeuver"),
                    Best && Best->Score.AngularUtility >= 0.25f);
                SetBoolIfDeclared(TEXT("HasApprovedLateralManeuver"),
                    Best && Best->Score.AngularUtility < 0.45f);
                SetBoolIfDeclared(TEXT("HasCoverCandidate"), Best != nullptr);
                SetBoolIfDeclared(TEXT("HasFlankCandidate"), Best != nullptr);
                SetBoolIfDeclared(TEXT("RouteValid"), Best != nullptr);
                SetBoolIfDeclared(TEXT("UnderFire"), Agent.bUnderFire);
                SetBoolIfDeclared(TEXT("InCover"), false);
                const bool bCanFlank = Agent.SquadRole == TEXT("Mover")
                    || Agent.SquadRole == TEXT("Flanker")
                    || Agent.SquadRole.IsNone();
                const bool bCanSuppress = Agent.SquadRole == TEXT("Anchor")
                    || Agent.SquadRole == TEXT("Suppressor")
                    || Agent.SquadRole == TEXT("Reserve")
                    || Agent.SquadRole.IsNone();
                SetBoolIfDeclared(TEXT("RoleCanFlank"), bCanFlank);
                SetBoolIfDeclared(TEXT("RoleCanSuppress"), bCanSuppress);
                SetBoolIfDeclared(TEXT("RoleShouldFlank"),
                    Agent.SquadRole == TEXT("Mover")
                        || Agent.SquadRole == TEXT("Flanker"));
                SetBoolIfDeclared(TEXT("RoleShouldTakeCover"),
                    Agent.SquadRole == TEXT("Mover") && Best
                        && Best->Score.CoverProtection > 0.0f);
                const bool bSuppression = State.Entities.ContainsByPredicate(
                    [&Agent](const FHellRunTacticalLabEntity& Entity)
                    { return Entity.Team == Agent.Team && Entity.Id != Agent.Id
                        && Entity.bAlive && Entity.bFiring; });
                SetBoolIfDeclared(TEXT("TargetSuppressed"), bSuppression);
                Request.FloatFacts.Add(TEXT("TargetDistance"),
                    FVector2D::Distance(Agent.Position, Threat->Position));
                FTacticalLabPlanResult Plan;
                if (FTacticalLabIntegrations::BuildPlan(Request, Plan))
                {
                Decision.GOAPPlan = Plan.Plan;
                Decision.GOAPGoalScores=Plan.GoalScores;
                Decision.GOAPGoalReasons=Plan.GoalReasons;
                Decision.GOAPFailureReason=Plan.FailureReason;
                if (!Plan.SelectedGoal.IsNone()&&Agent.Goal==EHellRunTacticalLabGoal::Auto)
                    Decision.Intent = Plan.SelectedGoal;
                RecordEvent(TEXT("GOAPPlanBuilt"), Agent.Id, DecisionId,
                    FString::Printf(TEXT("goal=%s cost=%.2f expanded=%d plan=%s"),
                        *Plan.SelectedGoal.ToString(), Plan.Cost,
                        Plan.ExpandedNodes, *FString::JoinBy(Plan.Plan,
                            TEXT(" -> "), [](FName Name){ return Name.ToString(); })));
                }
            }
        if (Decision.Intent.IsNone()||Decision.Intent==TEXT("Auto")) Decision.Intent = Best
            ? TEXT("MoveToApprovedTacticalPosition") : TEXT("HoldNoSafeManeuver");

        if (Best && Agent.bMovementGranted)
        {
            Decision.SelectedCandidateId = Best->CandidateId;
            Decision.SelectedRouteId = Best->RouteId;
            Decision.Reason = FString::Printf(TEXT("Highest accepted shared-policy score %.3f"),
                Best->Score.FinalScore);
            ActiveDestinations.Add(Agent.Id, Best->Position);
            ActiveRouteIds.Add(Agent.Id, Best->RouteId);
            TArray<FVector2D> SmoothRoute=BuildSmoothRouteSamples(Best->RoutePoints);
            if(SmoothRoute.IsEmpty())SmoothRoute={Agent.Position,Best->Position};
            SmoothRoute[0]=Agent.Position;
            ActiveRoutes.Add(Agent.Id,MoveTemp(SmoothRoute));
            ActiveRoutePointIndices.Add(Agent.Id,1);
            RecordEvent(TEXT("CandidateSelected"), Agent.Id, DecisionId,
                Decision.Reason);
            RecordEvent(TEXT("MovementStarted"), Agent.Id, DecisionId,
                FString::Printf(TEXT("route=%s"), *Best->RouteId.ToString()));
            bSelectedMovement = true;
        }
        else
        {
            Decision.Reason = Best
                ? TEXT("Movement held: squad movement authorization is false")
                : Agent.Goal==EHellRunTacticalLabGoal::FindCover
                    ? TEXT("Find Cover held: no reachable authored or EQS/voxel-validated cover candidate")
                    : TEXT("Movement held: every derived route failed shared tactical policy");
            RecordEvent(TEXT("HoldSelected"), Agent.Id, DecisionId, Decision.Reason);
        }
    }
    IdleDecisionCount = bSelectedMovement ? 0 : IdleDecisionCount + 1;
    if (IdleDecisionCount >= 4 && !HasMovement())
    {
        bComplete = true;
        const bool bExpectedHold = State.Assertions.ContainsByPredicate(
            [](const FHellRunTacticalLabAssertion& Assertion)
            { return Assertion.Type == TEXT("NoMovementWhenAllCandidatesUnsafe"); });
        if (bExpectedHold)
        {
            Lifetime.Result = EHellRunTacticalLabResult::Pass;
            RecordEvent(TEXT("LifetimeCompleted"), NAME_None, NAME_None,
                TEXT("expected terminal hold: routes existed but all were tactically unsafe"));
        }
        else
        {
            Lifetime.Result = EHellRunTacticalLabResult::Deadlock;
            AddFailure(TEXT("NoSafeManeuver"));
            RecordEvent(TEXT("LifetimeCompleted"), NAME_None, NAME_None,
                TEXT("deadlock: four decision epochs without movement or state change"));
        }
    }
    return bSelectedMovement;
}

bool FHellRunTacticalLab::StepTick(const float DeltaSeconds,
    const bool bMoveAgents,const bool bCompleteWhenMovementFinishes)
{
    if (bComplete) return false;
    Time += FMath::Max(0.0f, DeltaSeconds);
    bool bMoved = false;
    for (FHellRunTacticalLabEntity& Agent : State.Entities)
    {
        FVector2D* Destination = ActiveDestinations.Find(Agent.Id);
        const FHellRunEnemySimulationProfile* Profile = FindProfile(Agent.ArchetypeId);
        if (!Destination || !Profile) continue;
        if(!bMoveAgents)
        {
            Agent.Velocity=FVector2D::ZeroVector;
            continue;
        }
        TArray<FVector2D>* Route=ActiveRoutes.Find(Agent.Id);
        int32* RoutePointIndex=ActiveRoutePointIndices.Find(Agent.Id);
        float RemainingStep=Profile->MovementSpeed*DeltaSeconds;
        bool bFinished=false;
        while(RemainingStep>0.0f&&!bFinished)
        {
            const FVector2D Target=Route&&RoutePointIndex&&Route->IsValidIndex(*RoutePointIndex)
                ?(*Route)[*RoutePointIndex]:*Destination;
            const FVector2D Delta=Target-Agent.Position;
            const float Distance=Delta.Size();
            if(Distance<=FMath::Max(1.0f,RemainingStep))
            {
                Agent.Position=Target;RemainingStep=FMath::Max(0.0f,RemainingStep-Distance);
                if(Route&&RoutePointIndex&&Route->IsValidIndex(*RoutePointIndex+1))
                    ++(*RoutePointIndex);
                else bFinished=true;
            }
            else
            {
                const FVector2D Direction=Delta.GetSafeNormal();
                Agent.Facing=Direction;Agent.Position+=Direction*RemainingStep;
                RemainingStep=0.0f;
            }
        }
        if(bFinished)
        {
            Agent.Position=*Destination;Agent.Velocity=FVector2D::ZeroVector;
            RecordEvent(TEXT("MovementCompleted"),Agent.Id,NAME_None,
                FString::Printf(TEXT("route=%s"),*ActiveRouteIds.FindRef(Agent.Id).ToString()));
            ActiveDestinations.Remove(Agent.Id);ActiveRouteIds.Remove(Agent.Id);
            ActiveRoutes.Remove(Agent.Id);ActiveRoutePointIndices.Remove(Agent.Id);
        }
        else Agent.Velocity=Agent.Facing*Profile->MovementSpeed;
        bMoved = true;
    }
    if (bCompleteWhenMovementFinishes&&bMoved&&ActiveDestinations.IsEmpty())
    {
        bComplete = true;
        RecordEvent(TEXT("LifetimeCompleted"), NAME_None, NAME_None,
            TEXT("all selected tactical movement completed"));
    }
    if (Time >= State.MaximumDurationSeconds)
    {
        bComplete = true;
        Lifetime.Result = EHellRunTacticalLabResult::Timeout;
        AddFailure(TEXT("MovementTimeout"));
        RecordEvent(TEXT("LifetimeCompleted"), NAME_None, NAME_None,
            TEXT("scenario time limit reached"));
    }
    return bMoved;
}

bool FHellRunTacticalLab::HasMovement() const
{
    return !ActiveDestinations.IsEmpty();
}

void FHellRunTacticalLab::AddFailure(const FName Tag)
{
    Lifetime.FailureTags.AddUnique(Tag);
    if (Lifetime.Result == EHellRunTacticalLabResult::Pass)
        Lifetime.Result = EHellRunTacticalLabResult::Fail;
}

void FHellRunTacticalLab::EvaluateAssertions()
{
    for (const FHellRunTacticalLabAssertion& Assertion : State.Assertions)
    {
        if (Assertion.Type == TEXT("NoFriendlyFiringLaneCrossing"))
        {
            const bool bViolated = Lifetime.Candidates.ContainsByPredicate(
                [](const FHellRunTacticalLabCandidateRecord& Candidate)
                { return Candidate.Score.bAccepted
                    && Candidate.Score.FriendlyLaneConflict > 0.0f; });
            if (bViolated) AddFailure(TEXT("FriendlyFireLaneCrossing"));
        }
        else if (Assertion.Type == TEXT("NoFormationPenetration"))
        {
            const bool bViolated = Lifetime.Candidates.ContainsByPredicate(
                [](const FHellRunTacticalLabCandidateRecord& Candidate)
                { return Candidate.Score.bAccepted
                    && Candidate.Score.FormationPenetration > 0.32f; });
            if (bViolated) AddFailure(TEXT("FormationPenetration"));
        }
        else if (Assertion.Type == TEXT("NoUnsafeTraversal"))
        {
            const bool bViolated = Lifetime.Candidates.ContainsByPredicate(
                [](const FHellRunTacticalLabCandidateRecord& Candidate)
                { return Candidate.Score.bAccepted
                    && FMath::Max(Candidate.Score.TraversalRisk,
                        Candidate.Score.LandingExposure) > 0.72f; });
            if (bViolated) AddFailure(TEXT("UnsafeTraversal"));
        }
        else if (Assertion.Type == TEXT("MaximumRouteExposure"))
        {
            const bool bViolated = Lifetime.Decisions.ContainsByPredicate(
                [this, &Assertion](const FHellRunTacticalLabDecision& Decision)
                {
                    const FHellRunTacticalLabCandidateRecord* Candidate =
                        Lifetime.Candidates.FindByPredicate(
                            [&Decision](const FHellRunTacticalLabCandidateRecord& Item)
                            { return Item.DecisionId == Decision.DecisionId
                                && Item.CandidateId == Decision.SelectedCandidateId; });
                    return Candidate && Candidate->Score.RouteExposure > Assertion.Value;
                });
            if (bViolated) AddFailure(TEXT("ExcessiveRouteExposure"));
        }
        else if (Assertion.Type == TEXT("NoMovementWhenAllCandidatesUnsafe"))
        {
            const bool bAnySelected = Lifetime.Decisions.ContainsByPredicate(
                [](const FHellRunTacticalLabDecision& Decision)
                { return !Decision.SelectedCandidateId.IsNone(); });
            if (bAnySelected) AddFailure(TEXT("UnsafeMovementAccepted"));
        }
        else if (Assertion.Type == TEXT("RequiredTraversalType"))
        {
            const UEnum* Enum = StaticEnum<EHellRunTacticalLabTraversal>();
            const int64 Value = Enum->GetValueByNameString(Assertion.Argument.ToString());
            bool bFound = false;
            for (const FHellRunTacticalLabDecision& Decision : Lifetime.Decisions)
            {
                const FHellRunTacticalLabCandidateRecord* Candidate =
                    Lifetime.Candidates.FindByPredicate(
                        [&Decision](const FHellRunTacticalLabCandidateRecord& Item)
                        { return Item.DecisionId == Decision.DecisionId
                            && Item.CandidateId == Decision.SelectedCandidateId; });
                bFound |= Candidate && Candidate->SegmentTypes.Contains(
                    static_cast<EHellRunTacticalLabTraversal>(Value));
            }
            if (!bFound) AddFailure(TEXT("TraversalOpportunityMissed"));
        }
        else if (Assertion.Type == TEXT("UniqueTacticalReservations"))
        {
            TSet<FName> Selected;
            bool bDuplicate = false;
            for (const FHellRunTacticalLabDecision& Decision : Lifetime.Decisions)
            {
                if (Decision.SelectedCandidateId.IsNone()) continue;
                bDuplicate |= Selected.Contains(Decision.SelectedCandidateId);
                Selected.Add(Decision.SelectedCandidateId);
            }
            if (bDuplicate) AddFailure(TEXT("ReservationConflict"));
        }
        else if (Assertion.Type == TEXT("AtLeastOnePressureElement"))
        {
            const bool bHasPressure = State.Entities.ContainsByPredicate(
                [](const FHellRunTacticalLabEntity& Entity)
                { return Entity.bAlive && Entity.Kind
                        == EHellRunTacticalLabEntityKind::Enemy
                    && (Entity.bFiring || Entity.SquadRole == TEXT("Suppressor")); });
            if (!bHasPressure)
                AddFailure(TEXT("BoundingAdvanceCoordinationFailure"));
        }
        else if (Assertion.Type == TEXT("MovementWaitsForSuppression"))
        {
            for (const FHellRunTacticalLabDecision& Decision : Lifetime.Decisions)
            {
                const FHellRunTacticalLabEntity* Agent =
                    InitialScenario.Entities.FindByPredicate(
                        [&Decision](const FHellRunTacticalLabEntity& Entity)
                        { return Entity.Id == Decision.AgentId; });
                if (!Agent || Decision.SelectedCandidateId.IsNone()) continue;
                const bool bSuppressed = InitialScenario.Entities.ContainsByPredicate(
                    [Agent](const FHellRunTacticalLabEntity& Entity)
                    { return Entity.bFiring && Entity.Team == Agent->Team
                        && Entity.Id != Agent->Id; });
                if (!bSuppressed)
                    AddFailure(TEXT("MovementWithoutAuthorization"));
            }
        }
        else if (Assertion.Type == TEXT("FlankRequiresLateralDisplacement"))
        {
            bool bValidFlank = false;
            for (const FHellRunTacticalLabDecision& Decision : Lifetime.Decisions)
            {
                const FHellRunTacticalLabCandidateRecord* Candidate =
                    Lifetime.Candidates.FindByPredicate(
                        [&Decision](const FHellRunTacticalLabCandidateRecord& Item)
                        { return Item.DecisionId == Decision.DecisionId
                            && Item.CandidateId == Decision.SelectedCandidateId; });
                bValidFlank |= Candidate && Candidate->Score.AngularUtility >= 0.25f
                    && Candidate->RoutePoints.Num() >= 3;
            }
            if (!bValidFlank) AddFailure(TEXT("FlankTopologyFailure"));
        }
        else if (Assertion.Type == TEXT("SelectedCandidateWithinRange"))
        {
            for (const FHellRunTacticalLabDecision& Decision : Lifetime.Decisions)
            {
                const FHellRunTacticalLabEntity* Agent = State.Entities.FindByPredicate(
                    [&Decision](const FHellRunTacticalLabEntity& Entity)
                    { return Entity.Id == Decision.AgentId; });
                const FHellRunTacticalLabEntity* Threat = Agent
                    ? FindThreat(*Agent) : nullptr;
                const FHellRunTacticalLabCandidateRecord* Candidate =
                    Lifetime.Candidates.FindByPredicate(
                        [&Decision](const FHellRunTacticalLabCandidateRecord& Item)
                        { return Item.DecisionId == Decision.DecisionId
                            && Item.CandidateId == Decision.SelectedCandidateId; });
                if (Threat && Candidate && FVector2D::Distance(
                    Candidate->Position, Threat->Position) > Assertion.Value)
                    AddFailure(TEXT("TargetSelectionFailure"));
            }
        }
        else if (Assertion.Type == TEXT("FactMustNotBeProducedByAction"))
        {
            FString ActionName;
            FString FactName;
            if (!Assertion.Argument.ToString().Split(TEXT(":"),
                &ActionName, &FactName))
            {
                AddFailure(TEXT("InvalidScenario"));
                continue;
            }
            for (const FHellRunEnemySimulationProfile& Profile : State.Profiles)
            {
                if (FTacticalLabIntegrations::ActionProducesFact(
                    Profile.GOAPDomain, FName(*ActionName), FName(*FactName)))
                    AddFailure(TEXT("GOAPSemanticShortcut"));
            }
        }
    }
}

FHellRunTacticalLabLifetime FHellRunTacticalLab::Finish()
{
    EvaluateAssertions();
    if (!bComplete)
    {
        bComplete = true;
        RecordEvent(TEXT("LifetimeCompleted"), NAME_None, NAME_None,
            Lifetime.FailureTags.IsEmpty() ? TEXT("pass") : TEXT("assertion failure"));
    }
    Lifetime.DurationSeconds = Time;
    Lifetime.FinalEntities = State.Entities;
    Lifetime.Metrics.Add(TEXT("DecisionCount"), Lifetime.Decisions.Num());
    Lifetime.Metrics.Add(TEXT("CandidateCount"), Lifetime.Candidates.Num());
    Lifetime.Metrics.Add(TEXT("AcceptedCandidateCount"),
        Lifetime.Candidates.FilterByPredicate(
            [](const FHellRunTacticalLabCandidateRecord& Candidate)
            { return Candidate.Score.bAccepted; }).Num());
    Lifetime.Metrics.Add(TEXT("FailureCount"), Lifetime.FailureTags.Num());
    return Lifetime;
}

FHellRunTacticalLabLifetime FHellRunTacticalLab::RunLifetime(
    const FHellRunTacticalLabScenario& Scenario, const int32 Seed,
    const int32 LifetimeIndex)
{
    FHellRunTacticalLab Lab;
    FString Error;
    if (!Lab.Initialize(Scenario, Seed, LifetimeIndex, Error))
    {
        FHellRunTacticalLabLifetime Invalid;
        Invalid.ScenarioId = Scenario.ScenarioId;
        Invalid.ScenarioVersion = Scenario.ScenarioVersion;
        Invalid.Seed = Seed;
        Invalid.LifetimeIndex = LifetimeIndex;
        Invalid.Result = EHellRunTacticalLabResult::InvalidScenario;
        Invalid.FailureTags.Add(TEXT("InvalidScenario"));
        FHellRunTacticalLabEvent& Event = Invalid.Timeline.AddDefaulted_GetRef();
        Event.Type = TEXT("SimulationError");
        Event.Detail = Error;
        return Invalid;
    }
    while (!Lab.IsComplete() && Lab.Time < Scenario.MaximumDurationSeconds)
    {
        Lab.StepDecision();
        for (int32 Tick = 0; Tick < 10 && Lab.HasMovement()
            && !Lab.IsComplete(); ++Tick)
            Lab.StepTick(0.1f);
        if (!Lab.HasMovement() && Lab.DecisionEpoch >= 4) break;
    }
    return Lab.Finish();
}

FHellRunTacticalLabBatchSummary FHellRunTacticalLab::RunBatch(
    const FHellRunTacticalLabScenario& Scenario, const int32 BaseSeed,
    const int32 Count, const FString& OutputDirectory)
{
    FHellRunTacticalLabBatchSummary Summary;
    Summary.ScenarioId = Scenario.ScenarioId;
    Summary.LifetimeCount = FMath::Max(0, Count);
    for (int32 Index = 0; Index < Summary.LifetimeCount; ++Index)
    {
        const int32 Seed = BaseSeed + Index;
        const FHellRunTacticalLabLifetime Lifetime = RunLifetime(
            Scenario, Seed, Index);
        switch (Lifetime.Result)
        {
        case EHellRunTacticalLabResult::Pass: ++Summary.Passed; break;
        case EHellRunTacticalLabResult::Timeout: ++Summary.TimedOut; break;
        case EHellRunTacticalLabResult::Deadlock: ++Summary.Deadlocked; break;
        default: ++Summary.Failed; break;
        }
        for (const FName Tag : Lifetime.FailureTags)
            ++Summary.FailureTagCounts.FindOrAdd(Tag);
        if (Lifetime.Result != EHellRunTacticalLabResult::Pass)
            Summary.InterestingLifetimeIndices.Add(Index);
        if (!OutputDirectory.IsEmpty())
        {
            FString Error;
            SaveLifetime(FPaths::Combine(OutputDirectory,
                FString::Printf(TEXT("lifetime_%03d.json"), Index)),
                Lifetime, Error);
            if (Lifetime.Result != EHellRunTacticalLabResult::Pass)
                SaveFailureReport(FPaths::Combine(OutputDirectory,
                    FString::Printf(TEXT("lifetime_%03d.md"), Index)),
                    Lifetime, Error);
        }
    }
    if (!OutputDirectory.IsEmpty())
    {
        FString Error;
        SaveSummary(FPaths::Combine(OutputDirectory, TEXT("summary.json")),
            Summary, Error);
    }
    return Summary;
}
