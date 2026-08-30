#pragma once

#include "CoreMinimal.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"
#include "GOAPTypes.h"
#include "TacticalLabIntegrations.h"

struct FTacticalLabPIEVisionRay
{
    FVector2D End = FVector2D::ZeroVector;
    FName BlockingActor;
    FName BlockingComponent;
    bool bBlocked = false;
};

struct FTacticalLabPIEAgentSnapshot
{
    FGuid AgentId;
    FHellRunTacticalLabEntity Entity;
    FVector2D VisionOrigin = FVector2D::ZeroVector;
    float VisionRange = 0.0f;
    float VisionHalfAngle = 0.0f;
    int32 VisionRayCount = 0;
    TArray<FTacticalLabPIEVisionRay> VisionRays;
    TArray<FVector2D> MovementPath;
    FString RouteProvider;
    FString RouteAdmission;
    float RouteExposure = -1.0f;
    bool bHasGOAP = false;
    FGOAPBrainDebugSnapshot GOAP;
};

struct FTacticalLabPIEFrame
{
    float WorldTime = 0.0f;
    TArray<FTacticalLabPIEAgentSnapshot> Agents;
    FVector2D PlayerGroupCenter = FVector2D::ZeroVector;
    bool bHasPlayers = false;
    bool bHasDirectorDebug = false;
    FTacticalLabDirectorDebugSnapshot Director;
};

struct FTacticalLabPIEAgentIdentity
{
    FGuid AgentId;
    FName DisplayName;
    FName ClassName;
    bool bHasGOAP = false;
};

/** Editor-module-owned recording. It outlives every Tactical Lab window. */
struct FTacticalLabPIESession
{
    FGuid SessionId;
    double StartWorldTime = 0.0;
    double EndWorldTime = 0.0;
    bool bActive = false;
    TArray<FTacticalLabPIEFrame> Frames;
    TArray<FGOAPRuntimeEvent> Events;
    TMap<FGuid,FTacticalLabPIEAgentIdentity> Agents;
    uint64 Revision = 0;
};
