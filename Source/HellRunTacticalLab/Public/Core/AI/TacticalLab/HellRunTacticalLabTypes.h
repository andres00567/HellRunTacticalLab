// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HellRunTacticalLabTypes.generated.h"

UENUM(BlueprintType)
enum class EHellRunTacticalLabEntityKind : uint8
{
    Player,
    Friendly,
    Enemy,
    Neutral,
    Candidate,
    Objective,
};

UENUM(BlueprintType)
enum class EHellRunTacticalLabTraversal : uint8
{
    Walk,
    Sprint,
    Crouch,
    Vault,
    Mantle,
    Climb,
    Jump,
    Drop,
    Fly,
    Leap,
    ArchetypeSpecific,
};

UENUM(BlueprintType)
enum class EHellRunTacticalLabResult : uint8
{
    Pass,
    Fail,
    Timeout,
    Deadlock,
    InvalidScenario,
    SimulationError,
};

/** Explicit behavior intent for an agent or every member of a squad. */
UENUM(BlueprintType)
enum class EHellRunTacticalLabGoal : uint8
{
    Auto,
    EliminateTarget,
    FindCover,
    ReachTarget,
    HoldPosition,
    Regroup,
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalPolicy
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float DestinationWeight = 3.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float CoverWeight = 1.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float RouteSafetyWeight = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float AttackUtilityWeight = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float AngularUtilityWeight = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float TraversalUtilityWeight = 0.75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float ExposureWeight = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float WeaponConeWeight = 3.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float ThreatProximityWeight = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float HazardWeight = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float FriendlyLaneWeight = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float FormationWeight = 9.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float CongestionWeight = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float TraversalRiskWeight = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float TravelTimeWeight = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumRouteExposure = 0.78f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumFormationPenetration = 0.32f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumTraversalExposure = 0.72f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumHazardDanger = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MinimumAcceptableScore = -1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumTravelSeconds = 20.0f;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunEnemySimulationProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName ArchetypeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FSoftClassPath ProductionPawnClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FSoftClassPath ProductionControllerClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FSoftObjectPath GOAPDomain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bUsesGOAP = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bUsesSquadCoordinator = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bUsesCover = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bUsesRangedCombat = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bUsesMeleeCombat = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bCanVault = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bCanMantle = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bCanClimb = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bCanJump = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bCanDrop = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bCanFly = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float PreferredCombatRange = 1500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MinimumCombatRange = 300.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumCombatRange = 3000.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MovementSpeed = 450.0f;
    /** Authoritative lab-side perception configuration for this archetype. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab|Perception", meta=(ClampMin="100.0", Units="cm"))
    float VisionRange = 2500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab|Perception", meta=(ClampMin="1.0", ClampMax="89.0", Units="Degrees"))
    float VisionHalfAngleDegrees = 52.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab|Perception", meta=(ClampMin="3", ClampMax="128"))
    int32 VisionRayCount = 24;
    /** Z offset used by cover LOS tests, measured from the ground location. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab|Perception", meta=(ClampMin="20.0", ClampMax="150.0", Units="cm"))
    float CrouchedCapsuleHalfHeight = 60.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName TacticalPolicyId = TEXT("Default");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FHellRunTacticalPolicy Policy;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabEntity
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") EHellRunTacticalLabEntityKind Kind = EHellRunTacticalLabEntityKind::Neutral;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Team = TEXT("Neutral");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName ArchetypeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName SquadId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName SquadRole;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Facing = FVector2D(1.0f, 0.0f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Velocity = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float Health = 100.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bAlive = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bFiring = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bUnderFire = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bMovementGranted = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName TargetId;
    /** Per-agent intent. Auto falls back to the normal tactical/GOAP selection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation Intent")
    EHellRunTacticalLabGoal Goal = EHellRunTacticalLabGoal::Auto;
    /** Optional explicit entity/objective used by the selected goal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation Intent") FName GoalTargetId;
    /** Optional owner for transient/query candidates; empty candidates are shared. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation Intent") FName CandidateOwnerId;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabObstacle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Start = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D End = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float Height = 180.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bBlocksMovement = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bBlocksLOS = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bVaultable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bMantleable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bClimbable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bJumpable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bDroppable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName SurfaceType;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabHazard
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float Radius = 300.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float Danger = 1.0f;
};

UENUM(BlueprintType)
enum class EHellRunTacticalLabShapeKind : uint8
{
    Rectangle,
    Circle,
};

/** Editable 2D obstacle primitive. It is expanded into deterministic obstacle
 * edges when a simulation starts. */
USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabShape
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") EHellRunTacticalLabShapeKind Kind = EHellRunTacticalLabShapeKind::Rectangle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab", meta=(ClampMin="25.0")) FVector2D Extents = FVector2D(300.0, 200.0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float RotationDegrees = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float Height = 180.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bBlocksMovement = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") bool bBlocksLOS = true;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabRoute
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName RouteId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") int32 Version = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName AgentId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName CandidateId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FVector2D> Points;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<EHellRunTacticalLabTraversal> SegmentTypes;
    /** Populated when the route provider could not produce a navigable route.
     * Points may still contain the attempted segment for diagnostics only. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab|Diagnostics") FString GenerationFailure;
};

/** A provider-authored traversal connection baked from the production map. */
USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabTraversalEdge
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D Start = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FVector2D End = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float StartHeight = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float EndHeight = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") EHellRunTacticalLabTraversal Type = EHellRunTacticalLabTraversal::Walk;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float BaseCost = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Source;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabBakeMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") bool bBakedFromProductionMap = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString SourceMapPackage;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString SourceMapName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString BakedAtUtc;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString BakeVersion = TEXT("1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString SourceFingerprint;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FVector2D BoundsMin = FVector2D::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FVector2D BoundsMax = FVector2D::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float ProjectionHeight = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 SourceActorCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 BakedEntityCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 BakedObstacleSegmentCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 BakedCandidateCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 BakedTraversalEdgeCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabAssertion
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Type;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float Value = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName Argument;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabScenario
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") int32 SchemaVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FName ScenarioId = TEXT("Untitled");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") int32 ScenarioVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") int32 DefaultSeed = 1337;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") float MaximumDurationSeconds = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunEnemySimulationProfile> Profiles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabEntity> Entities;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabObstacle> Obstacles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabHazard> Hazards;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabShape> Shapes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabRoute> Routes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabTraversalEdge> TraversalEdges;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FHellRunTacticalLabAssertion> Assertions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactical Lab") TArray<FName> ExpectedBehaviorTags;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FHellRunTacticalLabBakeMetadata BakeMetadata;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalScore
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float EndpointScore = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float CoverProtection = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float AttackUtility = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float AngularUtility = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float RouteSafety = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float RouteExposure = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float WeaponConeExposure = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float ThreatProximity = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float HazardDanger = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float FriendlyLaneConflict = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float FormationPenetration = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float Congestion = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float TraversalRisk = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float TraversalUtility = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float LandingExposure = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float TravelSeconds = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float FinalScore = -BIG_NUMBER;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") bool bAccepted = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString RejectionReason;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabCandidateRecord
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName CandidateId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName AgentId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName DecisionId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName RouteId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FHellRunTacticalScore Score;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FVector2D> RoutePoints;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<EHellRunTacticalLabTraversal> SegmentTypes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FVector2D> ExposureSamples;
    /** First segment rejected by movement validation, or INDEX_NONE. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab|Diagnostics") int32 FailedSegmentIndex = INDEX_NONE;
    /** Stable baked obstacle id responsible for the rejection, when known. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab|Diagnostics") FName BlockingObstacleId;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabEvent
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float Time = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName Type;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName AgentId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName DecisionId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString Detail;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabDecision
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float Time = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName DecisionId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName AgentId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName ArchetypeId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName Intent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName SelectedCandidateId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName SelectedRouteId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString Reason;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FName> GOAPPlan;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TMap<FName,float> GOAPGoalScores;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TMap<FName,FString> GOAPGoalReasons;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString GOAPFailureReason;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TMap<FName, FString> FactProvenance;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabLifetime
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 SchemaVersion = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName ScenarioId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 ScenarioVersion = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 Seed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 LifetimeIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") EHellRunTacticalLabResult Result = EHellRunTacticalLabResult::InvalidScenario;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FName> FailureTags;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") float DurationSeconds = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FHellRunTacticalLabEntity> InitialEntities;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FHellRunTacticalLabEntity> FinalEntities;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FHellRunTacticalLabEvent> Timeline;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FHellRunTacticalLabDecision> Decisions;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<FHellRunTacticalLabCandidateRecord> Candidates;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TMap<FName, float> Metrics;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString TuningSnapshotId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FString BuildIdentifier;
};

USTRUCT(BlueprintType)
struct HELLRUNTACTICALLAB_API FHellRunTacticalLabBatchSummary
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") FName ScenarioId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 LifetimeCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 Passed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 Failed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 TimedOut = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") int32 Deadlocked = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TMap<FName, int32> FailureTagCounts;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tactical Lab") TArray<int32> InterestingLifetimeIndices;
};
