#include "DarkFactoryProcessController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace {
const FString StreamProtocol=TEXT("ordered-three-view-prefill-v1");
FString StreamJSON(const TSharedRef<FJsonObject>& Object) {
    FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text)); return Text;
}
bool StreamText(const TSharedPtr<FJsonObject>& Object,const TCHAR* Field,const FString& Expected) {
    FString Actual; return Object && Object->TryGetStringField(Field,Actual) && Actual==Expected;
}
bool StreamNumber(const TSharedPtr<FJsonObject>& Object,const TCHAR* Field,double Expected) {
    double Actual=0; return Object && Object->TryGetNumberField(Field,Actual) && FMath::IsFinite(Actual) && FMath::Abs(Actual-Expected)<.001;
}
}

void ADarkFactoryProcessController::RefreshStreamingCapability() {
    if (StreamHealthRequest) { StreamHealthRequest->OnProcessRequestComplete().Unbind(); StreamHealthRequest->CancelRequest(); StreamHealthRequest.Reset(); }
    bStreamingAvailable=false;
    if (!bModelRequestsEnabled || bCaptureQA || bPolicyQA || ExpectedModelSource!=TEXT("evk_hexagon") ||
        FParse::Param(FCommandLine::Get(),TEXT("DFNoProcessStreaming")) || !Endpoint.EndsWith(TEXT("/v1/inspect"))) return;
    const FString SelectedEndpoint=Endpoint;
    StreamHealthRequest=FHttpModule::Get().CreateRequest(); StreamHealthRequest->SetVerb(TEXT("GET"));
    StreamHealthRequest->SetURL(Endpoint.LeftChop(FString(TEXT("/inspect")).Len())+TEXT("/stream/capabilities"));
    StreamHealthRequest->SetTimeout(3.f);
    TWeakObjectPtr<ADarkFactoryProcessController> Weak(this);
    StreamHealthRequest->OnProcessRequestComplete().BindLambda([Weak,SelectedEndpoint](FHttpRequestPtr,FHttpResponsePtr Response,bool Success) {
        auto* Self=Weak.Get(); if (!Self || Self->Endpoint!=SelectedEndpoint) return;
        Self->StreamHealthRequest.Reset();
        TSharedPtr<FJsonObject> Result;
        Self->bStreamingAvailable=Success && Response && Response->GetResponseCode()==200 && Response->GetContentLength()<65536 &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Result) && Result &&
            StreamText(Result,TEXT("status"),TEXT("ready")) &&
            StreamText(Result,TEXT("source"),Self->ExpectedModelSource) &&
            StreamText(Result,TEXT("contract_version"),Self->ContractVersion) &&
            StreamText(Result,TEXT("streaming_protocol"),StreamProtocol);
    });
    if (!StreamHealthRequest->ProcessRequest()) StreamHealthRequest.Reset();
}

TSharedRef<FJsonObject> ADarkFactoryProcessController::StreamEnvelope() const {
    auto Object=MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("contract_version"),ContractVersion); Object->SetStringField(TEXT("request_id"),ActiveRequestId);
    Object->SetStringField(TEXT("body_id"),CaptureBodyId); Object->SetStringField(TEXT("stage"),CaptureStage);
    Object->SetNumberField(TEXT("captured_at_ms"),CapturedAtMs); Object->SetStringField(TEXT("session_id"),SessionId);
    Object->SetNumberField(TEXT("epoch"),BatchSerial); Object->SetStringField(TEXT("prompt_sha256"),PromptHashes.FindRef(CaptureStage));
    return Object;
}

void ADarkFactoryProcessController::StreamEvent(const FString& Event,const TSharedPtr<FJsonObject>& Detail) {
    auto Object=StreamEnvelope(); Object->SetStringField(TEXT("event"),Event);
    Object->SetNumberField(TEXT("batch_age_ms"),(FPlatformTime::Seconds()-BatchStarted)*1000.);
    if (Detail) Object->SetObjectField(TEXT("detail"),Detail);
    StreamingEvents.Add(MakeShared<FJsonValueObject>(Object));
    if (StreamingEvents.Num()>256) StreamingEvents.RemoveAt(0);
}

void ADarkFactoryProcessController::PumpStreamPreparation() {
    if (!bBatchStreaming || StreamRequest || State.bPaused || StreamPreparedImages>=BatchImages.Num()) return;
    const int32 Index=StreamPreparedImages;
    const FString Id=ActiveRequestId,Body=CaptureBodyId,Stage=CaptureStage;
    const uint64 Epoch=BatchSerial;
    const double Started=FPlatformTime::Seconds();
    auto Payload=StreamEnvelope(); auto Image=MakeShared<FJsonObject>();
    Image->SetStringField(TEXT("camera_id"),FString::Printf(TEXT("spot%d"),Index+1));
    Image->SetStringField(TEXT("png_base64"),FBase64::Encode(BatchImages[Index])); Payload->SetObjectField(TEXT("image"),Image);
    StreamRequest=FHttpModule::Get().CreateRequest(); StreamRequest->SetVerb(TEXT("POST"));
    StreamRequest->SetURL(Endpoint.LeftChop(FString(TEXT("/inspect")).Len())+TEXT("/stream/prepare"));
    StreamRequest->SetHeader(TEXT("Content-Type"),TEXT("application/json")); StreamRequest->SetContentAsString(StreamJSON(Payload));
    StreamRequest->SetTimeout(8.f);
    auto Sent=MakeShared<FJsonObject>(); Sent->SetNumberField(TEXT("index"),Index); StreamEvent(TEXT("prepare_sent"),Sent);
    TWeakObjectPtr<ADarkFactoryProcessController> Weak(this);
    StreamRequest->OnProcessRequestComplete().BindLambda([Weak,Id,Body,Stage,Epoch,Index,Started](FHttpRequestPtr,FHttpResponsePtr Response,bool Success) {
        auto* Self=Weak.Get();
        if (!Self || !Self->bBatchStreaming || Self->ActiveRequestId!=Id || Self->BatchSerial!=Epoch ||
            Self->State.BodyId!=Body || Self->StageName()!=Stage || Self->State.bPaused) return;
        Self->StreamRequest.Reset();
        TSharedPtr<FJsonObject> Reply; bool Prepared=false; FString Hash;
        bool Valid=Success && Response && Response->GetResponseCode()==200 && Response->GetContentLength()<65536 &&
            FPlatformTime::Seconds()-Self->BatchStarted<Self->MaxCaptureAgeSeconds &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Reply) && Reply &&
            Reply->TryGetBoolField(TEXT("prepared"),Prepared) && Prepared &&
            StreamText(Reply,TEXT("protocol"),StreamProtocol) && StreamText(Reply,TEXT("session_id"),Self->SessionId) &&
            StreamText(Reply,TEXT("request_id"),Id) && StreamText(Reply,TEXT("body_id"),Body) && StreamText(Reply,TEXT("stage"),Stage) &&
            StreamText(Reply,TEXT("contract_version"),Self->ContractVersion) && StreamText(Reply,TEXT("prompt_sha256"),Self->PromptHashes.FindRef(Stage)) &&
            StreamText(Reply,TEXT("camera_id"),FString::Printf(TEXT("spot%d"),Index+1)) && StreamNumber(Reply,TEXT("index"),Index) &&
            StreamNumber(Reply,TEXT("epoch"),Epoch) && StreamNumber(Reply,TEXT("captured_at_ms"),Self->CapturedAtMs) &&
            Reply->TryGetStringField(TEXT("png_sha256"),Hash) && Hash.Len()==64;
        for (const TCHAR C:Hash) Valid &= (C>=TEXT('0') && C<=TEXT('9')) || (C>=TEXT('a') && C<=TEXT('f'));
        if (!Valid) {
            Self->CancelInspection(); Self->ApplyDecision(TEXT("A"),false,TEXT("Image preparation failed; holding for a fresh scan"),0,Self->State.Backend); return;
        }
        Self->StreamImageHashes.Add(Hash); ++Self->StreamPreparedImages;
        auto Detail=MakeShared<FJsonObject>(); Detail->SetNumberField(TEXT("index"),Index);
        Detail->SetNumberField(TEXT("http_ms"),(FPlatformTime::Seconds()-Started)*1000.); Self->StreamEvent(TEXT("prepared"),Detail);
        Self->PumpStreamPreparation();
        if (Self->bStreamCommitPending && Self->StreamPreparedImages==3) Self->SubmitBatch();
    });
    if (!StreamRequest->ProcessRequest()) {
        CancelInspection(); ApplyDecision(TEXT("A"),false,TEXT("Cannot prepare inspection image; holding"),0,State.Backend);
    }
}

bool ADarkFactoryProcessController::ValidateStreamReply(const TSharedPtr<FJsonObject>& Reply) const {
    const TSharedPtr<FJsonObject>* Value=nullptr;
    if (!Reply->TryGetObjectField(TEXT("streaming"),Value) || !Value || !Value->IsValid()) return false;
    const auto& Stream=*Value;
    const TArray<TSharedPtr<FJsonValue>>* Hashes=nullptr;
    if (!StreamText(Stream,TEXT("protocol"),StreamProtocol) || !StreamText(Stream,TEXT("session_id"),SessionId) ||
        !StreamNumber(Stream,TEXT("epoch"),BatchSerial) || !StreamNumber(Stream,TEXT("prepared_images"),3) ||
        !StreamNumber(Stream,TEXT("early_prefill_tokens"),128) || !Stream->TryGetArrayField(TEXT("image_sha256"),Hashes) ||
        !Hashes || Hashes->Num()!=3 || StreamImageHashes.Num()!=3) return false;
    for (int32 I=0;I<3;++I) { FString Hash; if (!(*Hashes)[I]->TryGetString(Hash) || Hash!=StreamImageHashes[I]) return false; }
    return true;
}

void ADarkFactoryProcessController::CancelStream() {
    if (StreamRequest) { StreamRequest->OnProcessRequestComplete().Unbind(); StreamRequest->CancelRequest(); StreamRequest.Reset(); }
    if (bBatchStreaming && !ActiveRequestId.IsEmpty()) {
        ++StreamingCancelledBatches; StreamEvent(TEXT("cancel"));
        if (StreamCancelRequest) { StreamCancelRequest->CancelRequest(); StreamCancelRequest.Reset(); }
        StreamCancelRequest=FHttpModule::Get().CreateRequest(); StreamCancelRequest->SetVerb(TEXT("POST"));
        StreamCancelRequest->SetURL(Endpoint.LeftChop(FString(TEXT("/inspect")).Len())+TEXT("/stream/cancel"));
        StreamCancelRequest->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
        StreamCancelRequest->SetContentAsString(StreamJSON(StreamEnvelope())); StreamCancelRequest->SetTimeout(3.f);
        // Cancellation never produces evidence. The server also expires abandoned
        // batches, and a newer epoch supersedes a delayed cancellation safely.
        StreamCancelRequest->ProcessRequest();
    }
    bBatchStreaming=false; bStreamCommitPending=false; StreamPreparedImages=0; StreamImageHashes.Reset();
}

void ADarkFactoryProcessController::StreamingQATick() {
    if (!bModelQA || Agent || !bBackendSwitchEnabled || !FParse::Param(FCommandLine::Get(),TEXT("DFProcessStreamingSmoke"))) return;
    // Exercise the real interruption entry points. Never provide an inspection
    // answer or advance a job: subsequent branches still require actual models.
    if (StreamingQAStep==0 && bBatchStreaming && StreamPreparedImages==1) {
        StreamEvent(TEXT("qa_pause_after_first_image")); TogglePaused();
        StreamingQAResumeAt=FPlatformTime::Seconds()+.3; StreamingQAStep=1;
    } else if (StreamingQAStep==1 && FPlatformTime::Seconds()>=StreamingQAResumeAt) {
        TogglePaused(); StreamingQAStep=2;
    } else if (StreamingQAStep==2 && bBatchStreaming && StreamPreparedImages==2) {
        StreamEvent(TEXT("qa_switch_after_early_prefill"));
        if (SelectModelBackend(TEXT("host_gpu")) && SelectModelBackend(TEXT("evk_hexagon"))) StreamingQAStep=3;
    } else if (StreamingQAStep==3 && bStreamingAvailable && StreamingCompletedBatches>0) StreamingQAStep=4;
}
