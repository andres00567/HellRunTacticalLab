#pragma once

#include "CoreMinimal.h"
#include "Core/AI/TacticalLab/HellRunTacticalLabTypes.h"
#include "GOAPTypes.h"

struct FTacticalLabPIEVisionRay
{
    FVector2D End = FVector2D::ZeroVector;
    FName BlockingActor;
    FName BlockingComponent;
    bool bBlocked = false;
};

struct FTacticalLabPIEAgentSnapshot
{
    FHellRunTacticalLabEntity Entity;
    FVector2D VisionOrigin = FVector2D::ZeroVector;
    float VisionRange = 0.0f;
    float VisionHalfAngle = 0.0f;
    int32 VisionRayCount = 0;
    TArray<FTacticalLabPIEVisionRay> VisionRays;
    TArray<FVector2D> MovementPath;
    bool bHasGOAP = false;
    FGOAPBrainDebugSnapshot GOAP;
};

struct FTacticalLabPIEFrame
{
    float WorldTime = 0.0f;
    TArray<FTacticalLabPIEAgentSnapshot> Agents;
    FVector2D PlayerGroupCenter = FVector2D::ZeroVector;
    bool bHasPlayers = false;
};
