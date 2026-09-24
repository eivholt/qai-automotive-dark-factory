#include "DarkFactoryProcessController.h"
#include "DarkFactoryAgentProtocol.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool ADarkFactoryProcessController::InitializeModelBackends() {
    bEvkAgentNpu=FParse::Param(FCommandLine::Get(),TEXT("DFEvkAgentNpu"));
    bBackendSwitchEnabled=FParse::Param(FCommandLine::Get(),TEXT("DFProcessBackendSwitch"));
    bBackendSwitchSmoke=FParse::Param(FCommandLine::Get(),TEXT("DFProcessBackendSmoke"));
    if (bBackendSwitchSmoke && (!bBackendSwitchEnabled || !bModelQA)) return false;
    if (!bBackendSwitchEnabled) return true;
    if (!bModelRequestsEnabled || bCaptureQA || bPolicyQA || bPanelMotionQA) return false;
    ModelBackends={
        {TEXT("evk_hexagon"),TEXT("http://127.0.0.1:18198/v1/inspect"),bEvkAgentNpu ? TEXT("http://127.0.0.1:18197/v1/chat/completions"):TEXT("http://127.0.0.1:18196/v1/chat/completions")},
        {TEXT("host_gpu"),TEXT("http://127.0.0.1:18191/v1/inspect"),TEXT("http://127.0.0.1:18194/v1/chat/completions")}
    };
    // Preserve explicit endpoint overrides for the initially selected profile.
    for (auto& Backend:ModelBackends) if (Backend.Source==ExpectedModelSource) {
        Backend.VisionEndpoint=Endpoint;
        if (Agent) Backend.AgentEndpoint=Agent->Endpoint;
    }
    return true;
}

FString ADarkFactoryProcessController::GetModelBackendLabel() const {
    return ExpectedModelSource==TEXT("evk_hexagon")
        ? (Agent ? (bEvkAgentNpu ? TEXT("IQ9 EVK: Reason2 HTP0 + agent HTP1"):TEXT("IQ9 EVK: Reason2 NPU + agent CPU")):TEXT("IQ9 EVK: Reason2 NPU"))
        : (Agent ? TEXT("Host GPU: Reason2 + agent"):TEXT("Host GPU: Reason2"));
}

void ADarkFactoryProcessController::ToggleModelBackend() {
    SelectModelBackend(ExpectedModelSource==TEXT("evk_hexagon") ? TEXT("host_gpu"):TEXT("evk_hexagon"));
}

bool ADarkFactoryProcessController::SelectModelBackend(const FString& Source) {
    if (!bReady || !bBackendSwitchEnabled || !bModelRequestsEnabled) return false;
    const auto* Selected=ModelBackends.FindByPredicate([&](const FModelBackend& B) { return B.Source==Source; });
    if (!Selected) return false;
    if (Source==ExpectedModelSource) return true;
    const FString Previous=ExpectedModelSource;
    // Invalidate old HTTP callbacks, GPU readbacks, partial image batches and
    // queued agent commands before changing either endpoint. Detached encoders
    // are still drained by CaptureTick before a fresh batch may start.
    CancelInspection();
    if (Agent) {
        CancelAgentDecision();
        if (Agent->Job==TEXT("inspect")) {
            auto Cancelled=MakeShared<FJsonObject>();
            Cancelled->SetStringField(TEXT("status"),TEXT("cancelled_by_backend_switch"));
            CompleteAgentTool(Cancelled);
        }
        // A transport failure can be retried on the newly selected endpoint.
        // An explicit model hold and the per-body decision budget remain intact.
        if (Agent->Held && !Agent->Error.IsEmpty() && Agent->Turns<=40) {
            Agent->Held=false; Agent->Error.Empty(); State.bAgentHeld=false;
        }
        Agent->Endpoint=Selected->AgentEndpoint;
        Agent->NextDecision=State.ClockSeconds;
        State.AgentLatencyMs=0;
    }
    Endpoint=Selected->VisionEndpoint; ExpectedModelSource=Selected->Source; ++BackendSwitches;
    RefreshStreamingCapability();
    CoolingRecovery.Reset(); State.ConsecutivePasses=0; State.ConsensusCode.Empty();
    State.Decision=TEXT("A"); State.LastLatencyMs=0;
    LastModel.Empty(); LastAdapter.Empty(); LastBackendAlias.Empty(); LastScoringMethod.Empty();
    for (auto& Stamp:PreviewStamps) Stamp=FPreviewStamp();
    if (State.ActiveGate>=0) State.Gates[State.ActiveGate]=EDarkFactoryGateState::Waiting;
    State.Backend=Source==TEXT("evk_hexagon") ? TEXT("Reason2 IQ9 EVK"):TEXT("Reason2 host GPU");
    State.Message=TEXT("Switched to ")+GetModelBackendLabel();
    NextInspection=FPlatformTime::Seconds()+.4;
    // Only actual completed tool messages survive. No model result, process job,
    // body, paused state or physical trajectory is fabricated/restarted here.
    if (Agent) {
        auto Detail=MakeShared<FJsonObject>(); Detail->SetStringField(TEXT("previous_source"),Previous);
        Detail->SetStringField(TEXT("source"),Source);
        RecordAgentTrace(TEXT("backend_switch"),Detail);
    }
    RecordEvent(TEXT("backend_switch"));
    return true;
}
