#pragma once
#include "CoreMinimal.h"

/** Controller status only; never supplied to the vision model as evidence. */
enum class EDarkFactoryProcessPhase : uint8 {
    Arriving, PanelPresence, AdhesiveApplication, PanelPlacement, Adhesive,
    Welding, PanelSeated, WeldCooling, HolderRetract, Departing
};
enum class EDarkFactoryGateState : uint8 {
    Pending, Inspecting, Waiting, Passed, Rejected, Error, BranchChosen, Skipped
};
enum class EDarkFactoryStepState : uint8 { Pending, Active, Complete, Skipped, Hold, Exception };
struct FDarkFactoryProcessStatus {
    static constexpr int32 GateCount=4, StepCount=10;
    bool bEnabled=false, bPaused=false, bRequestPending=false;
    // These branch/action flags describe controller decisions, not scene truth.
    bool bBranchKnown=false, bPreassembled=false, bAssemblyPerformed=false, bWeldAuthorized=false;
    EDarkFactoryProcessPhase Phase=EDarkFactoryProcessPhase::Arriving;
    EDarkFactoryGateState Gates[GateCount]={};
    EDarkFactoryStepState Steps[StepCount]={};
    int32 ActiveGate=-1, BodySequence=0, PassedBodies=0, ConsecutivePasses=0;
    float LastLatencyMs=0, PhaseProgress=0;
    double ClockSeconds=0, StageSeconds=0;
    FString BodyId, Decision=TEXT("A"), ConsensusCode, Backend=TEXT("CONNECTING"), Message=TEXT("Waiting for a new body");
};
