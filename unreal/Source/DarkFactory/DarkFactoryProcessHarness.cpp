#include "DarkFactoryProcessController.h"
#include "DarkFactoryAgentProtocol.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"

namespace {
FString WireJson(const TSharedRef<FJsonObject>& Value) {
    FString Text; FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
}
}

void ADarkFactoryProcessController::RecordAgentObservation() {
    // Local audit mirror only in remote mode. Never sent to the EVK API.
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("body_id"),State.BodyId);
    O->SetStringField(TEXT("event"),TEXT("choose_next_tool"));
    O->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds); O->SetArrayField(TEXT("tool_results"),Agent->Journal);
    O->SetBoolField(TEXT("holder_engaged"),Agent->HolderEngaged); RecordAgentTrace(TEXT("agent_observation"),O);
}

void ADarkFactoryProcessController::QueueAgentHarnessEvent(const FString& Kind,const TSharedRef<FJsonObject>& Result) {
    if (!Agent || !Agent->Remote) return;
    if (Agent->HarnessEvents.Num()>=16) { AgentTransportHold(TEXT("EVK equipment event queue is full")); return; }
    auto Event=MakeShared<FJsonObject>(); Event->SetNumberField(TEXT("seq"),double(++Agent->HarnessSerial));
    Event->SetStringField(TEXT("kind"),Kind); Event->SetStringField(TEXT("body_id"),State.BodyId);
    Event->SetNumberField(TEXT("epoch"),double(Agent->Epoch));
    Event->SetStringField(TEXT("call_id"),Kind==TEXT("arrival") ? FString():Agent->Command.CallId);
    Event->SetObjectField(TEXT("result"),Result);
    Agent->HarnessEvents.Add(MakeShared<FJsonValueObject>(Event)); Agent->NextHarnessPoll=0;
}

TSharedRef<FJsonObject> ADarkFactoryProcessController::AgentHarnessPacket(bool Closing) const {
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("schema"),TEXT("darkfactory-edge-agent-runtime-v1"));
    O->SetStringField(TEXT("session_id"),SessionId); O->SetStringField(TEXT("body_id"),State.BodyId);
    O->SetNumberField(TEXT("epoch"),double(Agent->Epoch));
    O->SetBoolField(TEXT("paused"),State.bPaused || Agent->Held);
    O->SetBoolField(TEXT("busy"),Agent->Busy || State.Phase!=EDarkFactoryProcessPhase::AgentIdle);
    O->SetBoolField(TEXT("closed"),Closing || Agent->Finishing);
    O->SetStringField(TEXT("model_source"),ExpectedModelSource);
    O->SetStringField(TEXT("agent_device"),bEvkAgentNpu ? TEXT("npu"):TEXT("cpu"));
    O->SetArrayField(TEXT("events"),Agent->HarnessEvents);
    // Deliberately no system prompt, tool schema, conversation, source truth,
    // expected action or recipe state. The EVK owns the prompt/tools/history;
    // only actual equipment observations enter that history.
    return O;
}

void ADarkFactoryProcessController::PumpAgentHarness() {
    if (!Agent || !Agent->Remote || bPolicyQA || Agent->HarnessRequest || State.BodyId.IsEmpty()) return;
    const double Now=FPlatformTime::Seconds();
    if (Now<Agent->NextHarnessPoll) return;
    Agent->NextHarnessPoll=Now+.15;
    const uint64 Epoch=Agent->Epoch; const FString Body=State.BodyId;
    auto HarnessHttp=FHttpModule::Get().CreateRequest(); Agent->HarnessRequest=HarnessHttp;
    HarnessHttp->SetURL(Agent->HarnessEndpoint); HarnessHttp->SetVerb(TEXT("POST")); HarnessHttp->SetTimeout(3.f); HarnessHttp->SetActivityTimeout(3.f);
    HarnessHttp->SetHeader(TEXT("Content-Type"),TEXT("application/json")); HarnessHttp->SetContentAsString(WireJson(AgentHarnessPacket()));
    ++Agent->HarnessExchanges;
    TWeakObjectPtr<ADarkFactoryProcessController> Weak(this);
    HarnessHttp->OnProcessRequestComplete().BindLambda([Weak,Epoch,Body](FHttpRequestPtr,FHttpResponsePtr Response,bool Success) {
        auto* Self=Weak.Get(); if (!Self || !Self->Agent || !Self->bReady) return;
        auto& A=*Self->Agent;
        if (Epoch!=A.Epoch || Body!=Self->State.BodyId) return;
        A.HarnessRequest.Reset();
        const double Now=FPlatformTime::Seconds();
        if (!Success || !Response || Response->GetResponseCode()!=200 || Response->GetContentLength()>65536) {
            // An already-started actuator job is atomic. Finish it, retain its
            // unacknowledged result, then hold; never start a replacement job.
            if (!A.Held && !A.Busy && (Now-A.LastHarnessReply>5. || (Response && Response->GetResponseCode()==409)))
                Self->AgentTransportHold(TEXT("EVK agent runtime unavailable or equipment API rejected the event"));
            return;
        }
        TSharedPtr<FJsonObject> Reply; FString Schema,Session,ReplyBody,StateName,Error;
        double ReplyEpoch=-1,Ack=-1,Turns=0;
        bool Valid=FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Reply) && Reply;
        Valid=Valid && Reply->TryGetStringField(TEXT("schema"),Schema) && Schema==TEXT("darkfactory-edge-agent-runtime-v1") &&
            Reply->TryGetStringField(TEXT("session_id"),Session) && Session==Self->SessionId &&
            Reply->TryGetStringField(TEXT("body_id"),ReplyBody) && ReplyBody==Body &&
            Reply->TryGetNumberField(TEXT("epoch"),ReplyEpoch) && ReplyEpoch==double(Epoch) &&
            Reply->TryGetNumberField(TEXT("ack"),Ack) && FMath::IsFinite(Ack) && Ack>=double(A.HarnessAck) && Ack<=double(A.HarnessSerial) && Ack==FMath::FloorToDouble(Ack) &&
            Reply->TryGetNumberField(TEXT("turns"),Turns) && FMath::IsFinite(Turns) && Turns>=0 && Turns<=40 && Turns==FMath::FloorToDouble(Turns) &&
            Reply->TryGetStringField(TEXT("state"),StateName) && Reply->TryGetStringField(TEXT("error"),Error);
        if (!Valid) { Self->AgentTransportHold(TEXT("Invalid EVK equipment API reply")); return; }
        A.LastHarnessReply=Now; A.HarnessAck=uint64(Ack); A.Turns=int32(Turns);
        A.HarnessEvents.RemoveAll([&](const TSharedPtr<FJsonValue>& Event) { return Event->AsObject()->GetNumberField(TEXT("seq"))<=Ack; });
        Self->State.bAgentThinking=StateName==TEXT("thinking");
        if (!Error.IsEmpty()) { if (!A.Held && !A.Busy) Self->AgentTransportHold(TEXT("EVK agent: ")+Error); return; }
        const TSharedPtr<FJsonObject>* Command=nullptr;
        if (Reply->TryGetObjectField(TEXT("command"),Command) && !A.Busy && !A.Held && !Self->State.bPaused && !A.PendingCommand.IsSet()) {
            DarkFactoryAgent::FCommand Parsed; FString ParseError;
            if (!Command || !DarkFactoryAgent::ParseCommand(*Command,Parsed,ParseError,true)) {
                ++A.Rejected; Self->AgentTransportHold(TEXT("Malformed EVK equipment command")); return;
            }
            if (!A.HarnessConsumed.Contains(Parsed.CallId)) {
                double Latency=0; Reply->TryGetNumberField(TEXT("latency_ms"),Latency);
                Self->State.AgentLatencyMs=FMath::IsFinite(Latency) ? float(Latency):0.f;
                A.PendingCommand=Parsed;
            }
        }
        // Persist acknowledgement of the final departure before smoke exits.
        if (A.Finishing && A.HarnessEvents.IsEmpty()) Self->WriteReport();
    });
    if (!HarnessHttp->ProcessRequest()) {
        Agent->HarnessRequest.Reset();
        if (!Agent->Busy && !Agent->Held && Now-Agent->LastHarnessReply>5.) AgentTransportHold(TEXT("Cannot contact EVK agent runtime"));
    }
}

void ADarkFactoryProcessController::CloseAgentHarness() {
    if (!Agent || !Agent->Remote || State.BodyId.IsEmpty()) return;
    auto HarnessHttp=FHttpModule::Get().CreateRequest();
    HarnessHttp->SetURL(Agent->HarnessEndpoint); HarnessHttp->SetVerb(TEXT("POST")); HarnessHttp->SetTimeout(2.f);
    HarnessHttp->SetHeader(TEXT("Content-Type"),TEXT("application/json")); HarnessHttp->SetContentAsString(WireJson(AgentHarnessPacket(true)));
    HarnessHttp->ProcessRequest(); // Best effort; the EVK heartbeat lease also expires.
}
