#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "TacticalLabPIEDebugTypes.h"

class UGOAPBrainComponent;
class UWorld;
class AActor;

/** Persistent editor service that records PIE independently of Slate windows. */
class FTacticalLabPIESessionRecorder final
{
public:
    FTacticalLabPIESessionRecorder();
    ~FTacticalLabPIESessionRecorder();

    void Initialize();
    void Shutdown();

    static FTacticalLabPIESessionRecorder* Get();
    const FTacticalLabPIESession& GetSession() const { return Session; }

private:
    void HandleBeginPIE(bool bIsSimulating);
    void HandleEndPIE(bool bIsSimulating);
    bool Tick(float DeltaSeconds);
    void StartSession(UWorld& World);
    void EndSession();
    void DiscoverBrains(UWorld& World);
    void HandleRuntimeEvent(const FGOAPRuntimeEvent& Event);
    bool CaptureFrame(UWorld& World,FTacticalLabPIEFrame& OutFrame);
    FGuid FindOrAddSpatialId(AActor& Actor);
    void TrimRecording();

    static FTacticalLabPIESessionRecorder* Instance;
    FTacticalLabPIESession Session;
    TWeakObjectPtr<UWorld> ActiveWorld;
    TMap<TWeakObjectPtr<AActor>,FGuid> SpatialAgentIds;
    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle BeginPIEHandle;
    FDelegateHandle EndPIEHandle;
    FDelegateHandle RuntimeEventHandle;
    double LastCapturePlatformSeconds = -BIG_NUMBER;
    bool bInitialized = false;
};
