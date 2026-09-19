#include "DarkFactoryAgentProtocol.h"
#include "Serialization/JsonSerializer.h"

namespace DarkFactoryAgent {
TSharedRef<FJsonObject> CompactToolResult(const TSharedRef<FJsonObject>& Result) {
    auto Wire=MakeShared<FJsonObject>(); Wire->Values=Result->Values;
    // The preceding assistant call already supplies tool/arguments. Image count,
    // inference duration and this generic success string add no new evidence.
    for (const TCHAR* Key:{TEXT("tool"),TEXT("argument"),TEXT("latency_ms"),TEXT("images")}) Wire->RemoveField(Key);
    FString Reason;
    if (Wire->TryGetStringField(TEXT("reason"),Reason) && Reason==TEXT("model_classification")) Wire->RemoveField(TEXT("reason"));
    for (const TCHAR* Key:{TEXT("completed_clock_seconds"),TEXT("capture_age_at_reply_seconds")}) {
        double Value;
        if (Wire->TryGetNumberField(Key,Value) && FMath::IsFinite(Value)) {
            // A numeric JSON literal avoids Unreal expanding binary doubles back
            // to 17 digits. Freshness validation still uses the original clocks.
            FString Number=FString::Printf(TEXT("%.3f"),Value);
            while (Number.EndsWith(TEXT("0"))) Number.LeftChopInline(1);
            if (Number.EndsWith(TEXT("."))) Number.LeftChopInline(1);
            Wire->SetField(Key,MakeShared<FJsonValueNumberString>(MoveTemp(Number)));
        }
    }
    return Wire;
}
bool SameScope(uint64 ExpectedEpoch,const FString& ExpectedBody,const FString& ExpectedRequest,
    uint64 Epoch,const FString& Body,const FString& Request,bool Paused) {
    return !Paused && ExpectedEpoch==Epoch && ExpectedBody==Body && !Request.IsEmpty() && ExpectedRequest==Request;
}
bool ParseCommand(const TSharedPtr<FJsonObject>& Reply,FCommand& Out,FString& Error) {
    Error=TEXT("Expected exactly one complete, supported tool call");
    const TArray<TSharedPtr<FJsonValue>>* Choices=nullptr;
    if (!Reply || !Reply->TryGetArrayField(TEXT("choices"),Choices) || Choices->Num()!=1) return false;
    const auto Choice=(*Choices)[0]->AsObject(); FString Finish;
    const TSharedPtr<FJsonObject>* Message=nullptr;
    if (!Choice || !Choice->TryGetStringField(TEXT("finish_reason"),Finish) || Finish!=TEXT("tool_calls") ||
        !Choice->TryGetObjectField(TEXT("message"),Message)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Calls=nullptr;
    if (!(*Message)->TryGetArrayField(TEXT("tool_calls"),Calls) || Calls->Num()!=1) return false;
    const auto Call=(*Calls)[0]->AsObject(); const TSharedPtr<FJsonObject>* Function=nullptr; FString Type,Arguments;
    if (!Call || !Call->TryGetStringField(TEXT("id"),Out.CallId) || Out.CallId.IsEmpty() || Out.CallId.Len()>256 ||
        !Call->TryGetStringField(TEXT("type"),Type) || Type!=TEXT("function") ||
        !Call->TryGetObjectField(TEXT("function"),Function) || !(*Function)->TryGetStringField(TEXT("name"),Out.Tool) ||
        !(*Function)->TryGetStringField(TEXT("arguments"),Arguments) || Arguments.Len()>2048) return false;
    TSharedPtr<FJsonObject> Args;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Arguments),Args) || !Args ||
        !Args->TryGetStringField(TEXT("reason"),Out.Reason) || Out.Reason.IsEmpty() || Out.Reason.Len()>240) return false;
    if (Out.Tool==TEXT("inspect")) {
        if (Args->Values.Num()!=2 || !Args->TryGetStringField(TEXT("stage"),Out.Argument)) return false;
        return Out.Argument==TEXT("panel_presence") || Out.Argument==TEXT("adhesive") || Out.Argument==TEXT("panel_seated") || Out.Argument==TEXT("weld");
    }
    if (Out.Tool==TEXT("act")) {
        if (Args->Values.Num()!=2 || !Args->TryGetStringField(TEXT("action"),Out.Argument)) return false;
        return Out.Argument==TEXT("apply_sealant") || Out.Argument==TEXT("place_panel") || Out.Argument==TEXT("weld_panel") ||
            Out.Argument==TEXT("release_holder") || Out.Argument==TEXT("send_forward");
    }
    if (Out.Tool==TEXT("wait")) return Args->Values.Num()==2 && Args->TryGetNumberField(TEXT("seconds"),Out.Seconds) &&
        FMath::IsFinite(Out.Seconds) && Out.Seconds>=.5 && Out.Seconds<=30.;
    return Out.Tool==TEXT("hold") && Args->Values.Num()==1;
}
void FAudit::ObserveInspection(const FString& Stage,const FEvidence& Observation) {
    Evidence.Add(Stage,Observation);
    // Later presence inspections see work the agent has done. They must not be
    // judged as classifications of the incoming body before that work.
    if (Stage==TEXT("panel_presence") && Completed.IsEmpty()) InitialPresence=Observation;
}
TArray<FString> FAudit::ObserveAction(const FString& Action,bool Preassembled,double SealantC,double WeldC) const {
    TArray<FString> Issues;
    auto Done=[&](const TCHAR* Name) { return Completed.FindRef(Name)>0; };
    auto G=[&](const TCHAR* Stage) { const auto* E=Evidence.Find(Stage); return E && E->Valid && E->Code==TEXT("G"); };
    if (!InitialPresence.Valid || (InitialPresence.Code!=TEXT("G") && InitialPresence.Code!=TEXT("R"))) Issues.Add(TEXT("missing_conclusive_presence_observation"));
    else if ((InitialPresence.Code==TEXT("G"))!=Preassembled) Issues.Add(TEXT("presence_branch_incorrect"));
    if (Action==TEXT("apply_sealant") || Action==TEXT("place_panel") || Action==TEXT("weld_panel")) {
        if (Preassembled) Issues.Add(TEXT("unnecessary_work_on_preassembled_panel"));
    }
    if (Action==TEXT("place_panel") && !Done(TEXT("apply_sealant"))) Issues.Add(TEXT("placement_without_sealant"));
    if (Action==TEXT("weld_panel")) {
        if (!Done(TEXT("place_panel"))) Issues.Add(TEXT("welding_without_placement"));
        if (!G(TEXT("adhesive"))) Issues.Add(TEXT("welding_without_cool_sealant_observation"));
        if (!FMath::IsFinite(SealantC) || SealantC>=45.) Issues.Add(TEXT("welding_while_sealant_hot"));
    }
    if (Action==TEXT("release_holder") || Action==TEXT("send_forward")) {
        if (!Preassembled && (!Done(TEXT("place_panel")) || !Done(TEXT("apply_sealant")) || !Done(TEXT("weld_panel")))) Issues.Add(TEXT("assembly_incomplete"));
        if (!Preassembled && !G(TEXT("panel_seated"))) Issues.Add(TEXT("missing_seating_confirmation"));
        if (!G(TEXT("adhesive"))) Issues.Add(TEXT("missing_cool_sealant_observation"));
        if (!G(TEXT("weld"))) Issues.Add(TEXT("missing_cool_weld_observation"));
        if (!FMath::IsFinite(WeldC) || WeldC>=55.) Issues.Add(TEXT("release_while_weld_hot"));
        if (!FMath::IsFinite(SealantC) || SealantC>=45.) Issues.Add(TEXT("release_while_sealant_hot"));
    }
    if (Action==TEXT("send_forward") && Done(TEXT("place_panel")) && !Done(TEXT("release_holder"))) Issues.Add(TEXT("holder_not_released"));
    return Issues;
}
void FAudit::Complete(const FString& Action) {
    ++Completed.FindOrAdd(Action);
    if (Action==TEXT("apply_sealant") || Action==TEXT("place_panel")) Evidence.Remove(TEXT("adhesive"));
    if (Action==TEXT("place_panel") || Action==TEXT("weld_panel")) { Evidence.Remove(TEXT("panel_seated")); Evidence.Remove(TEXT("weld")); }
}
}
