#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"

namespace DarkFactoryAgent {
struct FCommand {
    FString CallId, Tool, Argument, Reason;
    double Seconds=0;
};
// Syntax/capability validation only. No recipe, inspection or temperature tests.
bool ParseCommand(const TSharedPtr<FJsonObject>& Reply,FCommand& Out,FString& Error);
// Model-visible serialization only; the diagnostic journal keeps full precision.
TSharedRef<FJsonObject> CompactToolResult(const TSharedRef<FJsonObject>& Result);
bool SameScope(uint64 ExpectedEpoch,const FString& ExpectedBody,const FString& ExpectedRequest,
    uint64 Epoch,const FString& Body,const FString& Request,bool Paused);
struct FEvidence { FString Code; bool Valid=false; double At=0; };
struct FAudit {
    TMap<FString,FEvidence> Evidence;
    FEvidence InitialPresence;
    TMap<FString,int32> Completed;
    void ObserveInspection(const FString& Stage,const FEvidence& Observation);
    // Returns observations, never a permit. The executor must ignore this return
    // value when dispatching. Ground truth stays solely in this evaluator.
    TArray<FString> ObserveAction(const FString& Action,bool SourcePreassembled,double SealantC,double WeldC) const;
    void Complete(const FString& Action);
};
struct FRuntime {
    FString Endpoint, SystemPrompt, RequestId, Job, Error;
    TArray<TSharedPtr<FJsonValue>> Tools, Journal, Messages, Trace, Audits, Outcomes;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> Request;
    FCommand Command;
    // A validated reply can arrive while R4 is still withdrawing. Cancellation
    // clears it with the request epoch; no new inference runs while it is queued.
    TOptional<FCommand> PendingCommand;
    FAudit Audit;
    uint64 Epoch=0, Serial=0;
    int32 Turns=0, Requests=0, Rejected=0, Failures=0;
    double RequestedAt=0, NextDecision=0, WaitUntil=0, Started=0;
    bool Held=false, Smoke=false, Busy=false, HolderEngaged=false, BodyPlaced=false;
};
}
