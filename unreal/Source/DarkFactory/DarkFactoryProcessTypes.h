#pragma once
#include "CoreMinimal.h"

/** Model-gated demo status only; never supplied to the vision model as evidence. */
enum class EDarkFactoryProcessPhase : uint8 {
    Arriving, Doors, Adhesive, Welding, WeldCooling, Departing, Returning
};
enum class EDarkFactoryGateState : uint8 { Pending, Inspecting, Waiting, Passed, Rejected, Error };
struct FDarkFactoryProcessStatus {
    bool bEnabled=false, bPaused=false, bRequestPending=false;
    EDarkFactoryProcessPhase Phase=EDarkFactoryProcessPhase::Arriving;
    EDarkFactoryGateState Gates[3]={EDarkFactoryGateState::Pending,EDarkFactoryGateState::Pending,EDarkFactoryGateState::Pending};
    int32 ActiveGate=-1, BodySequence=0, PassedBodies=0, ReturnedBodies=0, ConsecutivePasses=0;
    float LastLatencyMs=0, PhaseProgress=0;
    double ClockSeconds=0, StageSeconds=0;
    FString BodyId, Decision=TEXT("A"), Backend=TEXT("CONNECTING"), Message=TEXT("Waiting for a new body");
};
