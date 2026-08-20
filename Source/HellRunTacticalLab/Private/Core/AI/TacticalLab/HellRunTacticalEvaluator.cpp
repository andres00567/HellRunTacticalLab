#include "Core/AI/TacticalLab/HellRunTacticalEvaluator.h"
#include "Algo/Reverse.h"
#include "Containers/Queue.h"

namespace
{
constexpr float SampleSpacing = 150.0f;

float Cross(const FVector2D& A, const FVector2D& B)
{
    return A.X * B.Y - A.Y * B.X;
}

bool SegmentsIntersect(const FVector2D& A, const FVector2D& B,
    const FVector2D& C, const FVector2D& D)
{
    const FVector2D R = B - A;
    const FVector2D S = D - C;
    const float Denominator = Cross(R, S);
    if (FMath::IsNearlyZero(Denominator)) return false;
    const float T = Cross(C - A, S) / Denominator;
    const float U = Cross(C - A, R) / Denominator;
    return T >= 0.0f && T <= 1.0f && U >= 0.0f && U <= 1.0f;
}

float DistanceToSegment(const FVector2D& Point, const FVector2D& A,
    const FVector2D& B)
{
    const FVector2D Segment = B - A;
    const float Denominator = Segment.SizeSquared();
    const float T = Denominator > UE_SMALL_NUMBER
        ? FMath::Clamp(FVector2D::DotProduct(Point - A, Segment)
            / Denominator, 0.0f, 1.0f) : 0.0f;
    return FVector2D::Distance(Point, A + Segment * T);
}

bool HasLineOfSight(const FHellRunTacticalLabScenario& Scenario,
    const FVector2D& A, const FVector2D& B)
{
    for (const FHellRunTacticalLabObstacle& Obstacle : Scenario.Obstacles)
    {
        if (Obstacle.bBlocksLOS
            && SegmentsIntersect(A, B, Obstacle.Start, Obstacle.End))
        {
            return false;
        }
    }
    return true;
}

const FHellRunTacticalLabEntity* FindEntity(
    const FHellRunTacticalLabScenario& Scenario, FName Id)
{
    return Scenario.Entities.FindByPredicate(
        [Id](const FHellRunTacticalLabEntity& Entity)
        { return Entity.Id == Id; });
}

bool IsHostile(const FHellRunTacticalLabEntity& A,
    const FHellRunTacticalLabEntity& B)
{
    return A.Team != B.Team && A.Team != TEXT("Neutral")
        && B.Team != TEXT("Neutral");
}

/** Builds a deterministic coarse 2D path around the collision edges baked from
 * the production map. This keeps generated candidates from using the old
 * straight-line shortcut through walls. */
bool BuildObstacleAwarePath(const FHellRunTacticalLabScenario& Scenario,
    const FVector2D& Start,const FVector2D& End,TArray<FVector2D>& OutPoints)
{
    auto IsBlockedSegment=[&Scenario](const FVector2D& A,const FVector2D& B)
    {
        for(const FHellRunTacticalLabObstacle& Obstacle:Scenario.Obstacles)
            if(Obstacle.bBlocksMovement&&SegmentsIntersect(A,B,Obstacle.Start,Obstacle.End))
                return true;
        return false;
    };
    OutPoints={Start,End};
    if(!IsBlockedSegment(Start,End))return true;

    constexpr float Cell=180.0f;
    constexpr float Margin=1080.0f;
    const FVector2D Min(FMath::Min(Start.X,End.X)-Margin,
        FMath::Min(Start.Y,End.Y)-Margin);
    const FVector2D Max(FMath::Max(Start.X,End.X)+Margin,
        FMath::Max(Start.Y,End.Y)+Margin);
    const int32 Width=FMath::Clamp(FMath::CeilToInt((Max.X-Min.X)/Cell)+1,3,128);
    const int32 Height=FMath::Clamp(FMath::CeilToInt((Max.Y-Min.Y)/Cell)+1,3,128);
    const int32 Count=Width*Height;
    auto Index=[Width](int32 X,int32 Y){return Y*Width+X;};
    auto Position=[&](int32 X,int32 Y){return Min+FVector2D(X*Cell,Y*Cell);};
    auto Nearest=[&](const FVector2D& P)
    {return FIntPoint(FMath::Clamp(FMath::RoundToInt((P.X-Min.X)/Cell),0,Width-1),
        FMath::Clamp(FMath::RoundToInt((P.Y-Min.Y)/Cell),0,Height-1));};
    const FIntPoint StartCell=Nearest(Start),EndCell=Nearest(End);
    TArray<int32> Parent;Parent.Init(INDEX_NONE,Count);
    TArray<uint8> Visited;Visited.Init(0,Count);
    TArray<int8> Blocked;Blocked.Init(-1,Count);
    auto IsBlockedCell=[&](int32 X,int32 Y)
    {
        const int32 I=Index(X,Y);
        if(Blocked[I]>=0)return Blocked[I]!=0;
        const FVector2D P=Position(X,Y);
        bool bBlocked=false;
        for(const FHellRunTacticalLabObstacle& Obstacle:Scenario.Obstacles)
            if(Obstacle.bBlocksMovement&&DistanceToSegment(P,Obstacle.Start,Obstacle.End)<85.0f)
            {bBlocked=true;break;}
        Blocked[I]=bBlocked?1:0;return bBlocked;
    };
    TQueue<FIntPoint> Open;
    Open.Enqueue(StartCell);Visited[Index(StartCell.X,StartCell.Y)]=1;
    const FIntPoint Directions[]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    while(!Open.IsEmpty()&&!Visited[Index(EndCell.X,EndCell.Y)])
    {
        FIntPoint Current;Open.Dequeue(Current);
        for(const FIntPoint D:Directions)
        {
            const FIntPoint Next=Current+D;
            if(Next.X<0||Next.Y<0||Next.X>=Width||Next.Y>=Height)continue;
            const int32 NextIndex=Index(Next.X,Next.Y);
            if(Visited[NextIndex]||(Next!=EndCell&&IsBlockedCell(Next.X,Next.Y)))continue;
            if(IsBlockedSegment(Position(Current.X,Current.Y),Position(Next.X,Next.Y)))continue;
            Visited[NextIndex]=1;Parent[NextIndex]=Index(Current.X,Current.Y);Open.Enqueue(Next);
        }
    }
    if(!Visited[Index(EndCell.X,EndCell.Y)])return false;
    TArray<FVector2D> Reverse;
    for(int32 At=Index(EndCell.X,EndCell.Y);At!=INDEX_NONE;At=Parent[At])
        Reverse.Add(Position(At%Width,At/Width));
    Algo::Reverse(Reverse);
    Reverse[0]=Start;Reverse.Last()=End;
    // Remove grid stair-steps while retaining collision-safe corners.
    OutPoints.Reset();OutPoints.Add(Start);
    for(int32 From=0;From<Reverse.Num()-1;)
    {
        int32 To=Reverse.Num()-1;
        while(To>From+1&&IsBlockedSegment(Reverse[From],Reverse[To]))--To;
        OutPoints.Add(Reverse[To]);From=To;
    }
    return OutPoints.Num()>1;
}
}

bool FHellRunTacticalEvaluator::ApplyPolicy(FHellRunTacticalScore& Score,
    const FHellRunTacticalPolicy& Policy, const FString& FriendlyLaneReason)
{
    Score.RejectionReason.Reset();
    const float TravelPenalty = FMath::Clamp(Score.TravelSeconds / 8.0f,
        0.0f, 2.0f);
    Score.FinalScore = Score.EndpointScore * Policy.DestinationWeight
        + Score.CoverProtection * Policy.CoverWeight
        + Score.RouteSafety * Policy.RouteSafetyWeight
        + Score.AttackUtility * Policy.AttackUtilityWeight
        + Score.AngularUtility * Policy.AngularUtilityWeight
        + Score.TraversalUtility * Policy.TraversalUtilityWeight
        - Score.RouteExposure * Policy.ExposureWeight
        - Score.WeaponConeExposure * Policy.WeaponConeWeight
        - Score.ThreatProximity * Policy.ThreatProximityWeight
        - Score.HazardDanger * Policy.HazardWeight
        - Score.FriendlyLaneConflict * Policy.FriendlyLaneWeight
        - Score.FormationPenetration * Policy.FormationWeight
        - Score.Congestion * Policy.CongestionWeight
        - FMath::Max(Score.TraversalRisk, Score.LandingExposure)
            * Policy.TraversalRiskWeight
        - TravelPenalty * Policy.TravelTimeWeight;

    if (Score.FriendlyLaneConflict > 0.0f)
        Score.RejectionReason = FriendlyLaneReason.IsEmpty()
            ? TEXT("Route crosses active friendly firing lane")
            : FriendlyLaneReason;
    else if (Score.RouteExposure > Policy.MaximumRouteExposure)
        Score.RejectionReason = FString::Printf(
            TEXT("Route exposure %.2f exceeds %.2f"),
            Score.RouteExposure, Policy.MaximumRouteExposure);
    else if (Score.HazardDanger > Policy.MaximumHazardDanger)
        Score.RejectionReason = FString::Printf(
            TEXT("Hazard danger %.2f exceeds %.2f"),
            Score.HazardDanger, Policy.MaximumHazardDanger);
    else if (Score.FormationPenetration > Policy.MaximumFormationPenetration)
        Score.RejectionReason = FString::Printf(
            TEXT("Formation penetration %.2f exceeds %.2f"),
            Score.FormationPenetration, Policy.MaximumFormationPenetration);
    else if (FMath::Max(Score.TraversalRisk, Score.LandingExposure)
        > Policy.MaximumTraversalExposure)
        Score.RejectionReason = FString::Printf(
            TEXT("Traversal exposure %.2f exceeds %.2f"),
            FMath::Max(Score.TraversalRisk, Score.LandingExposure),
            Policy.MaximumTraversalExposure);
    else if (Score.TravelSeconds > Policy.MaximumTravelSeconds)
        Score.RejectionReason = FString::Printf(
            TEXT("Travel time %.2fs exceeds %.2fs action commitment"),
            Score.TravelSeconds, Policy.MaximumTravelSeconds);
    else if (Score.FinalScore < Policy.MinimumAcceptableScore)
        Score.RejectionReason = FString::Printf(
            TEXT("Final score %.2f below %.2f"), Score.FinalScore,
            Policy.MinimumAcceptableScore);

    Score.bAccepted = Score.RejectionReason.IsEmpty();
    return Score.bAccepted;
}

FHellRunTacticalLabCandidateRecord FHellRunTacticalEvaluator::EvaluateRoute(
    const FHellRunTacticalLabScenario& Scenario,
    const FHellRunTacticalLabEntity& Agent,
    const FHellRunTacticalLabEntity& Threat,
    const FHellRunTacticalLabRoute& Route,
    const FHellRunEnemySimulationProfile& Profile, const FName DecisionId)
{
    FHellRunTacticalLabCandidateRecord Result;
    Result.CandidateId = Route.CandidateId;
    Result.AgentId = Agent.Id;
    Result.DecisionId = DecisionId;
    Result.RouteId = Route.RouteId;
    Result.RoutePoints = Route.Points;
    Result.SegmentTypes = Route.SegmentTypes;
    if (Route.Points.Num() < 2)
    {
        Result.Score.RejectionReason = TEXT("No complete authoritative route");
        return Result;
    }
    Result.Position = Route.Points.Last();

    TArray<const FHellRunTacticalLabEntity*> Threats;
    TArray<const FHellRunTacticalLabEntity*> Allies;
    for (const FHellRunTacticalLabEntity& Entity : Scenario.Entities)
    {
        if (!Entity.bAlive || Entity.Kind == EHellRunTacticalLabEntityKind::Candidate)
            continue;
        if (IsHostile(Agent, Entity)) Threats.Add(&Entity);
        else if (Entity.Team == Agent.Team && Entity.Id != Agent.Id)
            Allies.Add(&Entity);
    }
    if (Threats.IsEmpty()) Threats.Add(&Threat);

    FVector2D ThreatCentroid = FVector2D::ZeroVector;
    for (const FHellRunTacticalLabEntity* Item : Threats)
        ThreatCentroid += Item->Position;
    ThreatCentroid /= static_cast<float>(Threats.Num());
    float ThreatSpread = 0.0f;
    for (const FHellRunTacticalLabEntity* Item : Threats)
        ThreatSpread = FMath::Max(ThreatSpread,
            FVector2D::Distance(Item->Position, ThreatCentroid));
    const float FormationRadius = FMath::Max(325.0f, ThreatSpread + 180.0f);

    float Exposure = 0.0f;
    float Cone = 0.0f;
    float Proximity = 0.0f;
    float Formation = 0.0f;
    float Congestion = 0.0f;
    float Hazard = 0.0f;
    float TraversalExposure = 0.0f;
    float LandingExposure = 0.0f;
    int32 Samples = 0;
    int32 TraversalSamples = 0;
    int32 Traversals = 0;
    int32 UsefulTraversals = 0;
    float TotalDistance = 0.0f;
    FString LaneReason;
    FString RouteGeometryReason=Route.GenerationFailure;

    for (int32 SegmentIndex = 1; SegmentIndex < Route.Points.Num(); ++SegmentIndex)
    {
        const FVector2D Start = Route.Points[SegmentIndex - 1];
        const FVector2D End = Route.Points[SegmentIndex];
        const float Length = FVector2D::Distance(Start, End);
        TotalDistance += Length;
        const int32 SegmentSamples = FMath::Max(1,
            FMath::CeilToInt(Length / SampleSpacing));
        const EHellRunTacticalLabTraversal Mode = Route.SegmentTypes.IsValidIndex(
            SegmentIndex - 1) ? Route.SegmentTypes[SegmentIndex - 1]
            : EHellRunTacticalLabTraversal::Walk;
        const bool bTraversal = Mode != EHellRunTacticalLabTraversal::Walk
            && Mode != EHellRunTacticalLabTraversal::Sprint
            && Mode != EHellRunTacticalLabTraversal::Crouch;
        for (const FHellRunTacticalLabObstacle& Obstacle : Scenario.Obstacles)
        {
            if (!Obstacle.bBlocksMovement
                || !SegmentsIntersect(Start, End, Obstacle.Start, Obstacle.End))
                continue;
            const bool bAllowed = (Mode == EHellRunTacticalLabTraversal::Vault
                    && Obstacle.bVaultable)
                || (Mode == EHellRunTacticalLabTraversal::Mantle
                    && Obstacle.bMantleable)
                || (Mode == EHellRunTacticalLabTraversal::Climb
                    && Obstacle.bClimbable)
                || (Mode == EHellRunTacticalLabTraversal::Jump
                    && Obstacle.bJumpable)
                || Mode == EHellRunTacticalLabTraversal::Fly
                || Mode == EHellRunTacticalLabTraversal::Leap;
            if (!bAllowed && RouteGeometryReason.IsEmpty())
            {
                Result.FailedSegmentIndex=SegmentIndex-1;
                Result.BlockingObstacleId=Obstacle.Id;
                RouteGeometryReason = FString::Printf(
                    TEXT("%s segment intersects movement blocker %s"),
                    *StaticEnum<EHellRunTacticalLabTraversal>()
                        ->GetNameStringByValue(static_cast<int64>(Mode)),
                    *Obstacle.Id.ToString());
            }
        }
        float SegmentExposure = 0.0f;
        for (int32 SampleIndex = 1; SampleIndex <= SegmentSamples; ++SampleIndex)
        {
            const FVector2D Position = FMath::Lerp(Start, End,
                static_cast<float>(SampleIndex) / SegmentSamples);
            Result.ExposureSamples.Add(Position);
            float Visible = 0.0f;
            float InCone = 0.0f;
            float NearestThreat = BIG_NUMBER;
            for (const FHellRunTacticalLabEntity* Item : Threats)
            {
                const bool bVisible = HasLineOfSight(Scenario,
                    Position, Item->Position);
                Visible += bVisible ? 1.0f : 0.0f;
                const FVector2D Direction = (Position - Item->Position)
                    .GetSafeNormal();
                InCone += bVisible && FVector2D::DotProduct(
                    Item->Facing.GetSafeNormal(), Direction) >= 0.35f
                    ? 1.0f : 0.0f;
                NearestThreat = FMath::Min(NearestThreat,
                    FVector2D::Distance(Position, Item->Position));
            }
            const float SampleExposure = Visible / Threats.Num();
            Exposure += SampleExposure;
            SegmentExposure += SampleExposure;
            Cone += InCone / Threats.Num();
            Proximity += 1.0f - FMath::Clamp(NearestThreat / 900.0f, 0.0f, 1.0f);
            Formation += FVector2D::Distance(Position, ThreatCentroid)
                < FormationRadius ? 1.0f : 0.0f;
            float NearbyAllies = 0.0f;
            for (const FHellRunTacticalLabEntity* Ally : Allies)
                NearbyAllies += FVector2D::Distance(Position, Ally->Position)
                    < 190.0f ? 1.0f : 0.0f;
            Congestion += FMath::Min(1.0f, NearbyAllies / 2.0f);
            for (const FHellRunTacticalLabHazard& Item : Scenario.Hazards)
            {
                const float Alpha = 1.0f - FMath::Clamp(
                    FVector2D::Distance(Position, Item.Position)
                    / FMath::Max(1.0f, Item.Radius), 0.0f, 1.0f);
                Hazard += Alpha * Item.Danger;
            }
            for (const FHellRunTacticalLabEntity* Ally : Allies)
            {
                const FHellRunTacticalLabEntity* LaneTarget = Ally->bFiring
                    ? FindEntity(Scenario, Ally->TargetId) : nullptr;
                if (LaneTarget && DistanceToSegment(Position, Ally->Position,
                    LaneTarget->Position) < 135.0f)
                {
                    Result.Score.FriendlyLaneConflict = 1.0f;
                    LaneReason = FString::Printf(
                        TEXT("Route crosses %s firing lane to %s"),
                        *Ally->Id.ToString(), *LaneTarget->Id.ToString());
                }
            }
            if (bTraversal)
            {
                TraversalExposure += SampleExposure;
                ++TraversalSamples;
            }
            ++Samples;
        }
        if (bTraversal)
        {
            ++Traversals;
            const float Average = SegmentExposure / SegmentSamples;
            LandingExposure += Average;
            UsefulTraversals += Length >= 100.0f && Average <= 0.45f ? 1 : 0;
        }
    }

    const float Denominator = FMath::Max(1, Samples);
    FHellRunTacticalScore& Score = Result.Score;
    Score.RouteExposure = Exposure / Denominator;
    Score.WeaponConeExposure = Cone / Denominator;
    Score.ThreatProximity = Proximity / Denominator;
    Score.FormationPenetration = Formation / Denominator;
    Score.Congestion = Congestion / Denominator;
    Score.HazardDanger = FMath::Clamp(Hazard / Denominator, 0.0f, 1.0f);
    Score.TraversalRisk = TraversalSamples > 0
        ? TraversalExposure / TraversalSamples : 0.0f;
    Score.LandingExposure = Traversals > 0
        ? LandingExposure / Traversals : 0.0f;
    Score.TraversalUtility = Traversals > 0
        ? static_cast<float>(UsefulTraversals) / Traversals : 0.0f;
    Score.RouteSafety = 1.0f - FMath::Clamp(
        Score.RouteExposure * 0.65f + Score.WeaponConeExposure * 0.20f
        + Score.ThreatProximity * 0.15f, 0.0f, 1.0f);
    Score.TravelSeconds = TotalDistance / FMath::Max(1.0f, Profile.MovementSpeed);
    Score.AttackUtility = HasLineOfSight(Scenario, Result.Position,
        Threat.Position) ? 1.0f : 0.0f;
    const FVector2D CurrentLane = (Agent.Position - Threat.Position).GetSafeNormal();
    const FVector2D DestinationLane = (Result.Position - Threat.Position).GetSafeNormal();
    Score.AngularUtility = FMath::Clamp((1.0f - FVector2D::DotProduct(
        CurrentLane, DestinationLane)) * 0.5f, 0.0f, 1.0f);
    const float Distance = FVector2D::Distance(Result.Position, Threat.Position);
    const float RangeError = FMath::Abs(Distance - Profile.PreferredCombatRange)
        / FMath::Max(1.0f, Profile.PreferredCombatRange);
    if(Agent.Goal==EHellRunTacticalLabGoal::FindCover)
    {
        // Find Cover should prefer protected progress toward the configured
        // threat, not the generic combat-range retreat destination.
        const FVector2D TowardThreat=(Threat.Position-Agent.Position).GetSafeNormal();
        const float ForwardProgress=FVector2D::DotProduct(
            Result.Position-Agent.Position,TowardThreat);
        Score.EndpointScore=FMath::Clamp(.5f+ForwardProgress/3000.0f,.0f,1.0f);
    }
    else Score.EndpointScore = 1.0f - FMath::Clamp(RangeError, 0.0f, 1.0f);
    for (const FHellRunTacticalLabObstacle& Obstacle : Scenario.Obstacles)
    {
        if (Obstacle.bBlocksLOS && DistanceToSegment(Result.Position,
            Obstacle.Start, Obstacle.End) <= 200.0f
            && !HasLineOfSight(Scenario, Result.Position, Threat.Position))
        {
            Score.CoverProtection = FMath::Clamp(Obstacle.Height / 180.0f,
                0.0f, 1.0f);
        }
    }
    if (!RouteGeometryReason.IsEmpty())
    {
        Score.RejectionReason = RouteGeometryReason;
        Score.bAccepted = false;
    }
    else
    {
        ApplyPolicy(Score, Profile.Policy, LaneReason);
    }
    return Result;
}

void FHellRunTacticalEvaluator::GenerateCandidateRoutes(
    const FHellRunTacticalLabScenario& Scenario,
    const FHellRunTacticalLabEntity& Agent,
    const FHellRunTacticalLabEntity& Threat, FRandomStream& Random,
    TArray<FHellRunTacticalLabRoute>& OutRoutes)
{
    OutRoutes.Reset();
    for (const FHellRunTacticalLabRoute& Route : Scenario.Routes)
    {
        if (Route.AgentId == Agent.Id)
        {
            FHellRunTacticalLabRoute Copy = Route;
            if (!Copy.Points.IsEmpty()) Copy.Points[0] = Agent.Position;
            OutRoutes.Add(MoveTemp(Copy));
        }
    }
    if (!OutRoutes.IsEmpty()) return;
    for (const FHellRunTacticalLabEntity& Entity : Scenario.Entities)
    {
        if (Entity.Kind != EHellRunTacticalLabEntityKind::Candidate
            ||(!Entity.CandidateOwnerId.IsNone()&&Entity.CandidateOwnerId!=Agent.Id)) continue;
        FHellRunTacticalLabRoute& Route = OutRoutes.AddDefaulted_GetRef();
        Route.AgentId = Agent.Id;
        Route.CandidateId = Entity.Id;
        Route.RouteId = FName(*FString::Printf(TEXT("%s_%s_v1"),
            *Agent.Id.ToString(), *Entity.Id.ToString()));
        if(!BuildObstacleAwarePath(Scenario,Agent.Position,Entity.Position,Route.Points))
        {
            Route.Points={Agent.Position,Entity.Position};
            Route.GenerationFailure=TEXT("No navigable route from the baked navigation fixture");
        }
        Route.SegmentTypes.Init(EHellRunTacticalLabTraversal::Walk,
            FMath::Max(0,Route.Points.Num()-1));

        int32 AddedTraversalRoutes = 0;
        for (const FHellRunTacticalLabTraversalEdge& Edge
            : Scenario.TraversalEdges)
        {
            if (FVector2D::Distance(Agent.Position, Edge.Start) > 1200.0f
                || FVector2D::Distance(Entity.Position, Edge.End) > 1600.0f)
                continue;
            FHellRunTacticalLabRoute& TraversalRoute =
                OutRoutes.AddDefaulted_GetRef();
            TraversalRoute.AgentId = Agent.Id;
            TraversalRoute.CandidateId = Entity.Id;
            TraversalRoute.RouteId = FName(*FString::Printf(
                TEXT("%s_%s_%s_v1"), *Agent.Id.ToString(),
                *Entity.Id.ToString(), *Edge.Id.ToString()));
            TraversalRoute.Points = {Agent.Position, Edge.Start,
                Edge.End, Entity.Position};
            TraversalRoute.SegmentTypes = {
                EHellRunTacticalLabTraversal::Walk, Edge.Type,
                EHellRunTacticalLabTraversal::Walk};
            if (++AddedTraversalRoutes >= 3) break;
        }
    }
    if (!OutRoutes.IsEmpty()) return;

    // Never invent a retreat point for Find Cover. That goal is only allowed
    // to consume authored or EQS/voxel-validated cover candidates.
    if(Agent.Goal==EHellRunTacticalLabGoal::FindCover)return;

    const FVector2D Away = (Agent.Position - Threat.Position).GetSafeNormal();
    const FVector2D Side(-Away.Y, Away.X);
    const float Sign = Random.RandRange(0, 1) == 0 ? -1.0f : 1.0f;
    const FVector2D Destinations[] =
    {
        Threat.Position + Away * 1500.0f,
        Agent.Position + Side * Sign * 650.0f,
        Agent.Position - Side * Sign * 650.0f,
        Agent.Position + Away * 500.0f,
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Destinations); ++Index)
    {
        FHellRunTacticalLabRoute& Route = OutRoutes.AddDefaulted_GetRef();
        Route.AgentId = Agent.Id;
        Route.CandidateId = FName(*FString::Printf(TEXT("Generated_%d"), Index));
        Route.RouteId = FName(*FString::Printf(TEXT("%s_Generated_%d_v1"),
            *Agent.Id.ToString(), Index));
        if(!BuildObstacleAwarePath(Scenario,Agent.Position,Destinations[Index],Route.Points))
        {
            Route.Points={Agent.Position,Destinations[Index]};
            Route.GenerationFailure=TEXT("No navigable route from the baked navigation fixture");
        }
        Route.SegmentTypes.Init(EHellRunTacticalLabTraversal::Walk,
            FMath::Max(0,Route.Points.Num()-1));
    }
}
