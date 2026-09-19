#include "DarkFactoryProcessController.h"
#include "DarkFactoryAgentProtocol.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRadiometry.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace {
FString AgentJson(const TSharedRef<FJsonObject>& O) {
    FString S; FJsonSerializer::Serialize(O,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S;
}
TSharedRef<FJsonObject> Result(const TCHAR* Status) { auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("status"),Status); return O; }
EDarkFactoryProcessPhase JobPhase(const FString& Job) {
    using P=EDarkFactoryProcessPhase;
    if (Job==TEXT("apply_sealant")) return P::AdhesiveApplication;
    if (Job==TEXT("place_panel")) return P::PanelPlacement;
    if (Job==TEXT("weld_panel")) return P::Welding;
    if (Job==TEXT("release_holder")) return P::HolderRetract;
    return P::Departing;
}
}
bool ADarkFactoryProcessController::InitializeAgent() {
    FString Mode=TEXT("flow"); FParse::Value(FCommandLine::Get(),TEXT("DFProcessControl="),Mode);
    if (Mode==TEXT("flow")) return true;
    if (Mode!=TEXT("agent") || bCaptureQA || bPolicyQA || bPanelMotionQA || !bModelRequestsEnabled) return false;
    Agent=MakeShared<DarkFactoryAgent::FRuntime>(); Agent->Smoke=bModelQA;
    Agent->Endpoint=TEXT("http://127.0.0.1:18193/v1/chat/completions");
    FParse::Value(FCommandLine::Get(),TEXT("DFAgentEndpoint="),Agent->Endpoint);
    if (!Agent->Endpoint.StartsWith(TEXT("http://127.0.0.1:")) && !Agent->Endpoint.StartsWith(TEXT("http://localhost:"))) return false;
    FString Text; TSharedPtr<FJsonObject> Config; FString Version;
    const TArray<TSharedPtr<FJsonValue>>* Tools=nullptr;
    if (!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectDir(),TEXT("../configs/live_agent.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Config) || !Config ||
        !Config->TryGetStringField(TEXT("version"),Version) || Version!=TEXT("darkfactory-live-agent-v1") ||
        !Config->TryGetStringField(TEXT("system_prompt"),Agent->SystemPrompt) ||
        !Config->TryGetArrayField(TEXT("tools"),Tools) || Tools->Num()!=4) return false;
    Agent->Tools=*Tools; Agent->Started=FPlatformTime::Seconds();
    State.bAgentControl=true; ProcessScene->SetAgentControl(true);
    return true;
}
void ADarkFactoryProcessController::CancelAgentDecision() {
    if (!Agent) return;
    ++Agent->Epoch; Agent->RequestId.Empty(); State.bAgentThinking=false;
    Agent->PendingCommand.Reset();
    if (Agent->Request) { Agent->Request->OnProcessRequestComplete().Unbind(); Agent->Request->CancelRequest(); Agent->Request.Reset(); }
}
void ADarkFactoryProcessController::BeginAgentBody() {
    if (!Agent) return;
    CancelAgentDecision(); Agent->Journal.Reset(); Agent->Messages.Reset(); Agent->Audit=DarkFactoryAgent::FAudit();
    Agent->Turns=0; Agent->Held=false; Agent->Busy=false; Agent->HolderEngaged=false; Agent->BodyPlaced=false;
    Agent->Job.Empty(); Agent->Error.Empty(); State.bAgentHeld=false; State.AgentAction=TEXT("arriving"); State.AgentReason.Empty();
    State.LastInspectionStage.Empty(); for (auto& Code:State.AgentGateCodes) Code.Empty();
}
void ADarkFactoryProcessController::RecordAgentTrace(const FString& Event,const TSharedRef<FJsonObject>& Detail) {
    if (!Agent) return;
    auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"),Event); Row->SetStringField(TEXT("body_id"),State.BodyId);
    Row->SetNumberField(TEXT("body_sequence"),State.BodySequence); Row->SetNumberField(TEXT("epoch"),double(Agent->Epoch));
    Row->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds); Row->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-Agent->Started);
    Row->SetObjectField(TEXT("detail"),Detail); Agent->Trace.Add(MakeShared<FJsonValueObject>(Row));
    if (Agent->Trace.Num()>512) Agent->Trace.RemoveAt(0);
    UE_LOG(LogTemp,Display,TEXT("DF_AGENT_EVENT %s"),*AgentJson(Row)); WriteReport();
}
void ADarkFactoryProcessController::AgentTransportHold(const FString& Error) {
    CancelAgentDecision(); Agent->Held=true; Agent->Error=Error; State.bAgentHeld=true;
    State.Message=Error+TEXT("; pause/resume to retry agent connection");
    auto O=Result(TEXT("unavailable")); O->SetStringField(TEXT("error"),Error); RecordAgentTrace(TEXT("transport_hold"),O);
}
void ADarkFactoryProcessController::CompleteAgentTool(const TSharedRef<FJsonObject>& O) {
    // Allowlist tool observations only. Never copy BuildMetadata, evaluator
    // messages, branch/permit flags or QA source labels into this journal.
    O->SetStringField(TEXT("tool"),Agent->Command.Tool); O->SetStringField(TEXT("argument"),Agent->Command.Argument);
    O->SetNumberField(TEXT("completed_clock_seconds"),State.ClockSeconds);
    O->SetBoolField(TEXT("holder_engaged"),Agent->HolderEngaged);
    const auto Wire=DarkFactoryAgent::CompactToolResult(O);
    auto Message=MakeShared<FJsonObject>();
    if (Agent->Command.Tool==TEXT("arrival")) {
        Message->SetStringField(TEXT("role"),TEXT("user"));
        auto Arrival=MakeShared<FJsonObject>(); Arrival->SetStringField(TEXT("body_id"),State.BodyId);
        Arrival->SetStringField(TEXT("event"),TEXT("new_body_arrived_choose_tools_to_complete_the_task")); Arrival->SetObjectField(TEXT("arrival"),Wire);
        Message->SetStringField(TEXT("content"),AgentJson(Arrival));
    } else {
        Message->SetStringField(TEXT("role"),TEXT("tool")); Message->SetStringField(TEXT("tool_call_id"),Agent->Command.CallId);
        Message->SetStringField(TEXT("content"),AgentJson(Wire));
    }
    Agent->Messages.Add(MakeShared<FJsonValueObject>(Message));
    Agent->Journal.Add(MakeShared<FJsonValueObject>(O)); if (Agent->Journal.Num()>40) Agent->Journal.RemoveAt(0);
    Agent->Busy=false; Agent->Job.Empty();
    if (State.ActiveGate<0 && static_cast<int32>(State.Phase)<State.StepCount)
        State.Steps[static_cast<int32>(State.Phase)]=EDarkFactoryStepState::Complete;
    State.Phase=EDarkFactoryProcessPhase::AgentIdle; State.ActiveGate=-1; PhaseStarted=State.ClockSeconds;
    State.bRequestPending=false; State.StageSeconds=0; State.Message=TEXT("Job complete; waiting for agent decision");
    // Think while the camera withdraws. Dispatch waits for its actual parked
    // state; Reason2, readback and image encoding remain serial with inference.
    Agent->NextDecision=State.ClockSeconds; RecordAgentTrace(TEXT("tool_result"),O);
}
void ADarkFactoryProcessController::AgentInspectionResult(const FString& Code,bool Valid,const FString& Reason,float Latency) {
    if (!Agent || Agent->Job!=TEXT("inspect")) return;
    const FString Stage=StageName(); const int32 Gate=State.ActiveGate;
    State.LastInspectionStage=Stage; State.Decision=Valid ? Code:TEXT("A"); State.LastLatencyMs=Latency;
    State.AgentGateCodes[Gate]=Valid ? Code:TEXT("invalid");
    State.ConsecutivePasses=Valid && Code!=TEXT("A") ? 1:0; State.ConsensusCode=Valid ? Code:TEXT("A");
    State.Gates[Gate]=!Valid ? EDarkFactoryGateState::Error:Code==TEXT("G") ? EDarkFactoryGateState::Passed:
        Code==TEXT("R") && Gate==2 ? EDarkFactoryGateState::Rejected:EDarkFactoryGateState::Waiting;
    State.Steps[static_cast<int32>(State.Phase)]=Valid ? EDarkFactoryStepState::Complete:EDarkFactoryStepState::Exception;
    Agent->Audit.ObserveInspection(Stage,{Valid ? Code:TEXT("A"),Valid,State.ClockSeconds});
    auto O=Result(Valid ? TEXT("observed"):TEXT("invalid")); O->SetStringField(TEXT("stage"),Stage);
    O->SetStringField(TEXT("decision"),Valid ? Code:TEXT("A")); O->SetBoolField(TEXT("valid"),Valid);
    O->SetStringField(TEXT("reason"),Reason); O->SetNumberField(TEXT("latency_ms"),Latency);
    O->SetNumberField(TEXT("capture_age_at_reply_seconds"),FPlatformTime::Seconds()-BatchStarted);
    O->SetNumberField(TEXT("images"),Valid ? 3:0); CompleteAgentTool(O);
}
void ADarkFactoryProcessController::AuditAgentAction(const FString& Action) {
    // Independent measurement, never consulted to dispatch/reject a job.
    const auto Scene=ProcessScene->BuildMetadata();
    const bool Preassembled=Scene->GetBoolField(TEXT("preassembled"));
    const double Sealant=ProcessScene->GetMaximumSealantContactApparentC();
    const double Weld=DarkFactoryThermal::ApparentTemperatureC(ProcessScene->GetMaximumWeldC(),.74f,20.f);
    const auto Issues=Agent->Audit.ObserveAction(Action,Preassembled,Sealant,Weld);
    auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("body_id"),State.BodyId); Row->SetStringField(TEXT("action"),Action);
    Row->SetNumberField(TEXT("body_sequence"),State.BodySequence); Row->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds);
    Row->SetBoolField(TEXT("source_preassembled"),Preassembled); Row->SetNumberField(TEXT("sealant_apparent_c"),Sealant); Row->SetNumberField(TEXT("weld_apparent_c"),Weld);
    TArray<TSharedPtr<FJsonValue>> Failures; for (const auto& Issue:Issues) Failures.Add(MakeShared<FJsonValueString>(Issue));
    Row->SetArrayField(TEXT("failures"),Failures); Row->SetBoolField(TEXT("executed_despite_failures"),!Issues.IsEmpty());
    auto Ages=MakeShared<FJsonObject>(); for (const auto& Pair:Agent->Audit.Evidence) Ages->SetNumberField(Pair.Key,State.ClockSeconds-Pair.Value.At);
    Row->SetObjectField(TEXT("observation_age_at_action_seconds"),Ages);
    Agent->Failures+=Issues.Num(); State.AgentAuditFailures=Agent->Failures;
    Agent->Audits.Add(MakeShared<FJsonValueObject>(Row)); if (Agent->Audits.Num()>512) Agent->Audits.RemoveAt(0);
    RecordAgentTrace(TEXT("independent_audit"),Row);
}
void ADarkFactoryProcessController::DispatchAgentCommand(const DarkFactoryAgent::FCommand& Command) {
    if (!Agent || Agent->Busy || State.bPaused || State.Phase!=EDarkFactoryProcessPhase::AgentIdle) return;
    Agent->Command=Command; State.AgentAction=Command.Tool+TEXT(" ")+Command.Argument; State.AgentReason=Command.Reason;
    auto C=MakeShared<FJsonObject>(); C->SetStringField(TEXT("tool"),Command.Tool); C->SetStringField(TEXT("argument"),Command.Argument);
    C->SetStringField(TEXT("reason"),Command.Reason); C->SetNumberField(TEXT("seconds"),Command.Seconds);
    C->SetBoolField(TEXT("inspection_arm_parked"),ProcessScene->IsInspectionArmParked());
    C->SetNumberField(TEXT("latency_ms"),State.AgentLatencyMs); RecordAgentTrace(TEXT("agent_choice"),C);
    Agent->Busy=true; Agent->Job=Command.Tool;
    // Keep the real tool conversation. Flattening all results into a new user
    // event caused Qwen to lose the distinction between past work and new work.
    auto Args=MakeShared<FJsonObject>(); Args->SetStringField(TEXT("reason"),Command.Reason);
    if (Command.Tool==TEXT("inspect")) Args->SetStringField(TEXT("stage"),Command.Argument);
    else if (Command.Tool==TEXT("act")) Args->SetStringField(TEXT("action"),Command.Argument);
    else if (Command.Tool==TEXT("wait")) Args->SetNumberField(TEXT("seconds"),Command.Seconds);
    auto Fn=MakeShared<FJsonObject>(); Fn->SetStringField(TEXT("name"),Command.Tool); Fn->SetStringField(TEXT("arguments"),AgentJson(Args));
    auto Call=MakeShared<FJsonObject>(); Call->SetStringField(TEXT("id"),Command.CallId); Call->SetStringField(TEXT("type"),TEXT("function")); Call->SetObjectField(TEXT("function"),Fn);
    auto Message=MakeShared<FJsonObject>(); Message->SetStringField(TEXT("role"),TEXT("assistant")); Message->SetStringField(TEXT("content"),TEXT(""));
    Message->SetArrayField(TEXT("tool_calls"),{MakeShared<FJsonValueObject>(Call)}); Agent->Messages.Add(MakeShared<FJsonValueObject>(Message));
    if (Command.Tool==TEXT("hold")) {
        CompleteAgentTool(Result(TEXT("held"))); Agent->Held=true; State.bAgentHeld=true; State.Message=TEXT("Agent holds for attention: ")+Command.Reason;
        RecordAgentTrace(TEXT("agent_hold"),C); return;
    }
    if (Command.Tool==TEXT("wait")) { Agent->WaitUntil=State.ClockSeconds+Command.Seconds; State.Message=TEXT("Agent chose to wait: ")+Command.Reason; return; }
    if (Command.Tool==TEXT("inspect")) {
        using P=EDarkFactoryProcessPhase;
        const P Phase=Command.Argument==TEXT("panel_presence") ? P::PanelPresence:Command.Argument==TEXT("adhesive") ? P::Adhesive:
            Command.Argument==TEXT("panel_seated") ? P::PanelSeated:P::WeldCooling;
        EnterPhase(Phase); return;
    }
    // These limits describe unavailable mechanical capabilities, not process
    // prerequisites. No temperature/vision/recipe verdict can veto a job.
    FString MechanicalError;
    if (Command.Argument==TEXT("place_panel") && Agent->BodyPlaced) MechanicalError=TEXT("The single-panel holder has already performed this body's pickup; a second-panel placement path is not implemented");
    if (Command.Argument==TEXT("release_holder") && !Agent->HolderEngaged) MechanicalError=TEXT("Holder is already parked; nothing is mechanically engaged");
    if (Command.Argument==TEXT("send_forward") && Agent->HolderEngaged) MechanicalError=TEXT("Carriage motion conflicts with the engaged holder; retract the holder before moving the carriage");
    if (!MechanicalError.IsEmpty()) {
        ++Agent->Rejected; auto O=Result(TEXT("unsupported_mechanical_state")); O->SetStringField(TEXT("error"),MechanicalError); CompleteAgentTool(O); return;
    }
    AuditAgentAction(Command.Argument); // Deliberately no conditional around this audit.
    Agent->Job=Command.Argument;
    ProcessScene->BeginAgentJob(JobPhase(Command.Argument)); EnterPhase(JobPhase(Command.Argument));
    State.Message=TEXT("Agent: ")+Command.Argument+TEXT(" - ")+Command.Reason;
}
void ADarkFactoryProcessController::RequestAgentDecision() {
    if (!Agent || bPolicyQA || !bModelRequestsEnabled || Agent->Request || Agent->PendingCommand.IsSet() || Agent->Busy || Agent->Held || State.bPaused || State.Phase!=EDarkFactoryProcessPhase::AgentIdle ||
        Request.IsValid() || bCaptureActive || Readback.IsBusy() || EncodeFuture.IsValid()) return;
    if (++Agent->Turns>40) { AgentTransportHold(TEXT("Agent decision budget exhausted for this body")); return; }
    auto Observation=MakeShared<FJsonObject>(); Observation->SetStringField(TEXT("body_id"),State.BodyId);
    Observation->SetStringField(TEXT("event"),TEXT("choose_next_tool"));
    Observation->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds); Observation->SetArrayField(TEXT("tool_results"),Agent->Journal);
    Observation->SetBoolField(TEXT("holder_engaged"),Agent->HolderEngaged);
    auto System=MakeShared<FJsonObject>(); System->SetStringField(TEXT("role"),TEXT("system")); System->SetStringField(TEXT("content"),Agent->SystemPrompt);
    auto Payload=MakeShared<FJsonObject>(); Payload->SetStringField(TEXT("model"),TEXT("dark-factory-agent"));
    TArray<TSharedPtr<FJsonValue>> Messages={MakeShared<FJsonValueObject>(System)}; Messages.Append(Agent->Messages);
    Payload->SetArrayField(TEXT("messages"),Messages);
    Payload->SetArrayField(TEXT("tools"),Agent->Tools); Payload->SetStringField(TEXT("tool_choice"),TEXT("required"));
    Payload->SetBoolField(TEXT("parallel_tool_calls"),false); Payload->SetNumberField(TEXT("temperature"),0); Payload->SetNumberField(TEXT("seed"),42);
    Payload->SetNumberField(TEXT("max_tokens"),256); auto Template=MakeShared<FJsonObject>(); Template->SetBoolField(TEXT("enable_thinking"),false);
    Payload->SetObjectField(TEXT("chat_template_kwargs"),Template);
    Agent->RequestId=FGuid::NewGuid().ToString(EGuidFormats::Digits); Agent->RequestedAt=FPlatformTime::Seconds();
    State.bAgentThinking=true; State.Message=TEXT("Agent choosing the next job from tool observations");
    RecordAgentTrace(TEXT("agent_observation"),Observation);
    const uint64 Epoch=Agent->Epoch; const FString Body=State.BodyId,Id=Agent->RequestId;
    Agent->Request=FHttpModule::Get().CreateRequest(); Agent->Request->SetURL(Agent->Endpoint); Agent->Request->SetVerb(TEXT("POST"));
    Agent->Request->SetHeader(TEXT("Content-Type"),TEXT("application/json")); Agent->Request->SetContentAsString(AgentJson(Payload)); Agent->Request->SetTimeout(120.f);
    // CPU EVK prefill can be silent for >30 s. UE's activity timeout is
    // independent of the total request timeout. Neither changes vision TTL.
    Agent->Request->SetActivityTimeout(120.f);
    TWeakObjectPtr<ADarkFactoryProcessController> Weak(this);
    Agent->Request->OnProcessRequestComplete().BindLambda([Weak,Epoch,Body,Id](FHttpRequestPtr Req,FHttpResponsePtr Response,bool Success) {
        auto* Self=Weak.Get(); if (!Self || !Self->Agent || !Self->bReady) return;
        auto& A=*Self->Agent;
        if (!DarkFactoryAgent::SameScope(Epoch,Body,Id,A.Epoch,Self->State.BodyId,A.RequestId,Self->State.bPaused)) return;
        A.Request.Reset(); A.RequestId.Empty(); Self->State.bAgentThinking=false;
        Self->State.AgentLatencyMs=(FPlatformTime::Seconds()-A.RequestedAt)*1000.;
        if (!Success || !Response || Response->GetResponseCode()!=200 || Response->GetContentLength()>65536) {
            Self->AgentTransportHold(TEXT("Agent endpoint unavailable or request failed")); return;
        }
        TSharedPtr<FJsonObject> Reply; DarkFactoryAgent::FCommand Command; FString Error;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Reply) ||
            !DarkFactoryAgent::ParseCommand(Reply,Command,Error)) {
            ++A.Rejected; Self->AgentTransportHold(TEXT("Malformed/incomplete agent tool call: ")+Error); return;
        }
        // Consume the scope once, then wait for physical clearance if necessary.
        // Pause/body turnover clears the queued command as well as HTTP replies.
        A.PendingCommand=Command;
    });
    if (Agent->Request->ProcessRequest()) ++Agent->Requests;
    else AgentTransportHold(TEXT("Cannot submit agent request"));
}
void ADarkFactoryProcessController::AgentTick() {
    if (!Agent || State.bPaused) return;
    using P=EDarkFactoryProcessPhase;
    if (Agent->Smoke && FPlatformTime::Seconds()-Agent->Started>1800.) {
        AgentTransportHold(TEXT("Live agent fixture timed out")); bReady=false; WriteReport(); FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if (Agent->Held) {
        if (Agent->Smoke) { bReady=false; WriteReport(); FPlatformMisc::RequestExitWithStatus(false,1); }
        return;
    }
    if (State.Phase==P::Arriving && State.StageSeconds>=ADarkFactoryProcessScene::CarriageTravelSeconds) {
        Agent->Command=DarkFactoryAgent::FCommand(); Agent->Command.Tool=TEXT("arrival"); CompleteAgentTool(Result(TEXT("completed"))); return;
    }
    if (Agent->Job==TEXT("wait") && State.ClockSeconds>=Agent->WaitUntil) { CompleteAgentTool(Result(TEXT("completed"))); return; }
    const double Duration=ADarkFactoryProcessScene::GetPhaseDuration(State.Phase);
    if (Agent->Busy && Agent->Job!=TEXT("inspect") && Duration>0 && State.StageSeconds>=Duration) {
        const FString Job=Agent->Job;
        ProcessScene->CompleteAgentJob(State.Phase); Agent->Audit.Complete(Job);
        if (Job==TEXT("place_panel")) { Agent->HolderEngaged=true; Agent->BodyPlaced=true; State.bAssemblyPerformed=true; }
        if (Job==TEXT("release_holder")) Agent->HolderEngaged=false;
        if (Job==TEXT("weld_panel")) State.bWeldAuthorized=true;
        if (Job==TEXT("send_forward")) {
            auto Outcome=Result(TEXT("departed")); Outcome->SetStringField(TEXT("body_id"),State.BodyId);
            Outcome->SetNumberField(TEXT("body_sequence"),State.BodySequence); Outcome->SetNumberField(TEXT("agent_turns"),Agent->Turns);
            Agent->Outcomes.Add(MakeShared<FJsonValueObject>(Outcome));
            if (Agent->Outcomes.Num()>128) Agent->Outcomes.RemoveAt(0);
            ++State.PassedBodies; ++CompletedBodies;
            CompleteAgentTool(Outcome); ProcessScene->EndBody();
            if (Agent->Smoke && CompletedBodies>=3) {
                bReady=false; const bool Saved=WriteReport(); FPlatformMisc::RequestExitWithStatus(false,Saved && Agent->Failures==0 && Agent->Rejected==0 ? 0:1); return;
            }
            BeginBody(); return;
        }
        CompleteAgentTool(Result(TEXT("completed"))); return;
    }
    if (State.Phase==P::AgentIdle && !Agent->Busy && State.ClockSeconds>=Agent->NextDecision) {
        if (Agent->PendingCommand.IsSet()) {
            if (ProcessScene->IsInspectionArmParked()) {
                const auto Command=Agent->PendingCommand.GetValue(); Agent->PendingCommand.Reset();
                DispatchAgentCommand(Command);
            }
        } else RequestAgentDecision();
    }
}
void ADarkFactoryProcessController::AppendAgentMetadata(const TSharedRef<FJsonObject>& Report) const {
    Report->SetStringField(TEXT("control_mode"),Agent ? TEXT("agent"):TEXT("flow"));
    if (!Agent) return;
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("schema"),TEXT("darkfactory-live-agent-v1"));
    O->SetStringField(TEXT("endpoint"),Agent->Endpoint); O->SetStringField(TEXT("system_prompt"),Agent->SystemPrompt); O->SetArrayField(TEXT("tools"),Agent->Tools);
    O->SetNumberField(TEXT("requests"),Agent->Requests); O->SetNumberField(TEXT("rejected_calls"),Agent->Rejected); O->SetNumberField(TEXT("audit_failures"),Agent->Failures);
    O->SetBoolField(TEXT("held"),Agent->Held); O->SetBoolField(TEXT("recipe_vetoes_enabled"),false);
    O->SetArrayField(TEXT("current_body_messages"),Agent->Messages);
    O->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-Agent->Started);
    O->SetArrayField(TEXT("trace"),Agent->Trace); O->SetArrayField(TEXT("audits"),Agent->Audits); O->SetArrayField(TEXT("outcomes"),Agent->Outcomes);
    O->SetBoolField(TEXT("new_held_out_accuracy"),false); Report->SetObjectField(TEXT("agent"),O);
    Report->SetBoolField(TEXT("passed"),Agent->Smoke && CompletedBodies==3 && Agent->Failures==0 && Agent->Rejected==0 && !Agent->Held);
}
