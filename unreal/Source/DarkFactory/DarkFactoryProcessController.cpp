#include "DarkFactoryProcessController.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactoryCurtainPortal.h"
#include "Components/StaticMeshComponent.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryThermalRadiometry.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "ImageUtils.h"
#include "Async/Async.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Camera/CameraTypes.h"
#include "UnrealClient.h"
#include "SceneView.h"

namespace {
constexpr int32 Width=448,Height=256;
const TCHAR* CameraIds[]={TEXT("left"),TEXT("right"),TEXT("spot1"),TEXT("spot2"),TEXT("spot3")};
FString Json(const TSharedRef<FJsonObject>& Value) {
    FString Text; FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
}
TArray<uint8> Encode(TArray<FColor> Pixels) {
    for (auto& Pixel:Pixels) {
        const uint8 Gray=FMath::Clamp(FMath::RoundToInt(.2126*Pixel.R+.7152*Pixel.G+.0722*Pixel.B),0,255);
        Pixel=FColor(Gray,Gray,Gray,255);
    }
    TArray64<uint8> Bytes; FImageUtils::PNGCompressImageArray(Width,Height,Pixels,Bytes);
    TArray<uint8> Result; Result.Append(Bytes.GetData(),Bytes.Num()); return Result;
}
const TCHAR* PhaseName(EDarkFactoryProcessPhase Phase) {
    switch (Phase) {
    case EDarkFactoryProcessPhase::Arriving:return TEXT("arriving");
    case EDarkFactoryProcessPhase::Doors:return TEXT("doors");
    case EDarkFactoryProcessPhase::Adhesive:return TEXT("adhesive");
    case EDarkFactoryProcessPhase::Welding:return TEXT("welding");
    case EDarkFactoryProcessPhase::WeldCooling:return TEXT("weld");
    case EDarkFactoryProcessPhase::Departing:return TEXT("departing");
    default:return TEXT("returning");
    }
}
}
ADarkFactoryProcessController::ADarkFactoryProcessController() {
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PrePhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ProcessSensors")); SetRootComponent(SceneRoot);
}
bool ADarkFactoryProcessController::Initialize(ADarkFactoryWorld* World,ADarkFactoryWeldingCell* Cell,
    ADarkFactoryProcessScene* Scene,ADarkFactoryThermalRig* Thermal) {
    if (!World || !Cell || !Scene || !Thermal || !Thermal->IsReady()) return false;
    WorldOwner=World; WeldingCell=Cell; ProcessScene=Scene; ThermalRig=Thermal;
    SessionId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString RequestedSession;
    if (FParse::Value(FCommandLine::Get(),TEXT("DFProcessRunId="),RequestedSession)) {
        FGuid Parsed; if (!FGuid::Parse(RequestedSession,Parsed)) return false; SessionId=RequestedSession;
    }
    Endpoint=TEXT("http://127.0.0.1:18191/v1/inspect");
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessEndpoint="),Endpoint);
    // Dedicated local experimentation endpoint; never silently fall back to the parcel bridge.
    if (!Endpoint.StartsWith(TEXT("http://127.0.0.1:")) && !Endpoint.StartsWith(TEXT("http://localhost:"))) return false;
    bCaptureQA=FParse::Param(FCommandLine::Get(),TEXT("DFProcessCapture"));
    bModelQA=FParse::Param(FCommandLine::Get(),TEXT("DFProcessSmoke"));
    bPolicyQA=FParse::Param(FCommandLine::Get(),TEXT("DFProcessPolicySmoke"));
    bModelRequestsEnabled=!FParse::Param(FCommandLine::Get(),TEXT("DFNoInference"));
    if (bModelQA && !bModelRequestsEnabled) { UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_SMOKE_REQUIRES_INFERENCE")); return false; }
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessFault="),ForcedFault);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessCaseStart="),QAStartCase);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessCaseEnd="),QAEndCase);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessEpisodes="),QAEpisodes);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessDatasetSeed="),QASeed);
    QAEpisodes=FMath::Clamp(QAEpisodes,0,1024);
    if (QAEpisodes>0) { QAStartCase=0; QAEndCase=QAEpisodes*7; }
    else { QAStartCase=FMath::Clamp(QAStartCase,0,39); QAEndCase=FMath::Clamp(QAEndCase,QAStartCase+1,40); }
    OutputRoot=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),TEXT("../artifacts/process-live")));
    FParse::Value(FCommandLine::Get(),TEXT("DFOutput="),OutputRoot);
    IFileManager::Get().MakeDirectory(*OutputRoot,true);
    if (bModelQA) IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputRoot,TEXT("screenshots"),SessionId),true);
    if (bCaptureQA && IFileManager::Get().FileExists(*FPaths::Combine(OutputRoot,TEXT("manifest.jsonl")))) {
        UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_REFUSES_EXISTING_MANIFEST choose new output")); return false;
    }
    FString Text; TSharedPtr<FJsonObject> Contract;
    if (!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectDir(),TEXT("../configs/process_inspection.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Contract) || !Contract) return false;
    if (!Contract->TryGetStringField(TEXT("version"),ContractVersion) || ContractVersion!=TEXT("automotive-process-v1")) return false;
    // This renderer implements a frozen sensor contract. Reject unsupported edits rather than
    // silently collecting labels under different camera, window or gate-policy assumptions.
    auto Object=[](const TSharedPtr<FJsonObject>& Parent,const TCHAR* Key)->TSharedPtr<FJsonObject> {
        const TSharedPtr<FJsonObject>* Value=nullptr;
        return Parent && Parent->TryGetObjectField(Key,Value) && Value ? *Value:nullptr;
    };
    auto Number=[](const TSharedPtr<FJsonObject>& Parent,const TCHAR* Key,double Expected) {
        double Value=0; return Parent && Parent->TryGetNumberField(Key,Value) && FMath::IsFinite(Value) && FMath::IsNearlyEqual(Value,Expected,1.e-7);
    };
    auto String=[](const TSharedPtr<FJsonObject>& Parent,const TCHAR* Key,const TCHAR* Expected) {
        FString Value; return Parent && Parent->TryGetStringField(Key,Value) && Value==Expected;
    };
    const auto ImageContract=Object(Contract,TEXT("image"));
    if (!String(ImageContract,TEXT("format"),TEXT("PNG")) || !Number(ImageContract,TEXT("width"),Width) ||
        !Number(ImageContract,TEXT("height"),Height) || !Number(ImageContract,TEXT("bit_depth"),8) ||
        !String(ImageContract,TEXT("channels"),TEXT("grayscale")) || !Number(ImageContract,TEXT("visual_tokens_per_image"),112)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Window=nullptr;
    double Low=0,High=0;
    if (!Contract->TryGetArrayField(TEXT("thermal_window_c"),Window) || !Window || Window->Num()!=2 ||
        !(*Window)[0]->TryGetNumber(Low) || !(*Window)[1]->TryGetNumber(High) || Low!=20. || High!=120.) return false;
    const auto Stages=Object(Contract,TEXT("stages")); if (!Stages || Stages->Values.Num()!=3) return false;
    for (const TCHAR* Stage:{TEXT("doors"),TEXT("adhesive"),TEXT("weld")}) {
        const auto Entry=Object(Stages,Stage); FString Hash; const bool NIR=FString(Stage)==TEXT("doors");
        const TArray<TSharedPtr<FJsonValue>>* Ids=nullptr;
        if (!Entry || !Entry->TryGetStringField(TEXT("prompt_sha256"),Hash) || Hash.Len()!=64 ||
            !String(Entry,TEXT("modality"),NIR ? TEXT("nir"):TEXT("thermal")) ||
            !Entry->TryGetArrayField(TEXT("camera_ids"),Ids) || !Ids || Ids->Num()!=(NIR ? 2:3)) return false;
        for (const TCHAR C:Hash) if (!FChar::IsHexDigit(C)) return false;
        for (int32 I=0;I<Ids->Num();++I) { FString Id; if (!(*Ids)[I]->TryGetString(Id) || Id!=CameraIds[NIR ? I:I+2]) return false; }
        if (!NIR && (!Number(Entry,TEXT("ready_below_c"),FString(Stage)==TEXT("adhesive") ? 45.:55.) || !Number(Entry,TEXT("ambiguity_band_c"),3.))) return false;
        PromptHashes.Add(Stage,Hash);
    }
    const auto Generation=Object(Contract,TEXT("generation")); bool Thinking=true;
    if (!String(Contract,TEXT("model_family"),TEXT("nvidia/Cosmos-Reason2-2B")) ||
        !String(Generation,TEXT("method"),TEXT("single_next_token_logits")) || !Number(Generation,TEXT("temperature"),0) ||
        !Generation->TryGetBoolField(TEXT("enable_thinking"),Thinking) || Thinking || !Number(Generation,TEXT("image_cache_entries"),0)) return false;
    const auto Policy=Object(Contract,TEXT("decision_policy"));
    if (!Number(Policy,TEXT("consecutive_green"),2) || !Policy->TryGetNumberField(TEXT("max_reply_age_seconds"),MaxCaptureAgeSeconds) ||
        !FMath::IsFinite(MaxCaptureAgeSeconds) || MaxCaptureAgeSeconds<1. || MaxCaptureAgeSeconds>60. ||
        !Policy->TryGetNumberField(TEXT("minimum_label_mass"),MinimumLabelMass) || !FMath::IsFinite(MinimumLabelMass) || MinimumLabelMass<.5 || MinimumLabelMass>1. ||
        !Policy->TryGetNumberField(TEXT("minimum_conditional_probability"),MinimumConditionalProbability) || !FMath::IsFinite(MinimumConditionalProbability) || MinimumConditionalProbability<.5 || MinimumConditionalProbability>1.) return false;
    auto* NIRParent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingNIRGrayscale.M_WeldingNIRGrayscale"));
    auto* ThermalParent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PP_Thermal.M_PP_Thermal"));
    if (!NIRParent || !ThermalParent) return false;
    PreviewStamps.SetNum(5);
    for (int32 I=0;I<5;++I) {
        auto* Target=NewObject<UTextureRenderTarget2D>(this);
        Target->InitCustomFormat(Width,Height,PF_B8G8R8A8,I>=2);
        if (I>=2) Target->TargetGamma=1.f;
        Target->ClearColor=FLinearColor::Black; Target->UpdateResourceImmediate(true); Targets.Add(Target);
    }
    for (int32 I=0;I<3;++I) {
        auto* Camera=NewObject<USceneCaptureComponent2D>(this,*FString::Printf(TEXT("ProcessCamera%d"),I));
        Camera->SetupAttachment(SceneRoot); Camera->bCaptureEveryFrame=false; Camera->bCaptureOnMovement=false;
        Camera->bAlwaysPersistRenderingState=true; Camera->CaptureSource=SCS_FinalColorLDR;
        Camera->ViewLightingChannels.bViewChannel0=I==2;
        Camera->ViewLightingChannels.bViewChannel1=I<2;
        Camera->ViewLightingChannels.bViewChannel2=false;
        Camera->ShowFlags.SetTemporalAA(false); Camera->ShowFlags.SetAntiAliasing(false);
        Camera->ShowFlags.SetMotionBlur(false); Camera->ShowFlags.SetFog(false); Camera->ShowFlags.SetAtmosphere(false);
        auto& PP=Camera->PostProcessSettings;
        PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
        PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP.AutoExposureApplyPhysicalCameraExposure=false;
        PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=I<2 ? -4.f:0.f;
        PP.bOverride_VignetteIntensity=true; PP.VignetteIntensity=0;
        PP.bOverride_MotionBlurAmount=true; PP.MotionBlurAmount=0;
        PP.bOverride_FilmGrainIntensity=true; PP.FilmGrainIntensity=0;
        if (I<2) { PP.bOverride_ColorSaturation=true; PP.ColorSaturation=FVector4(0,0,0,1); }
        else {
            Camera->ShowFlags.SetLighting(false); Camera->ShowFlags.SetDynamicShadows(false);
            Camera->ShowFlags.SetBloom(false); Camera->ShowFlags.SetAmbientOcclusion(false);
        }
        auto* Material=UMaterialInstanceDynamic::Create(I<2 ? NIRParent:ThermalParent,this);
        Camera->AddOrUpdateBlendable(Material,1); Camera->TextureTarget=Targets[I]; Camera->RegisterComponent();
        CameraMaterials.Add(Material); Cameras.Add(Camera);
        if (I<2) {
            FVector Location; FRotator Rotation; float FOV=58;
            Scene->GetDoorSensorView(I,Location,Rotation,FOV); Camera->SetWorldLocationAndRotation(Location,Rotation); Camera->FOVAngle=FOV;
            auto* Light=NewObject<USpotLightComponent>(this,*FString::Printf(TEXT("ProcessNIR%d"),I));
            Light->SetupAttachment(SceneRoot); Light->SetMobility(EComponentMobility::Movable);
            Light->SetWorldLocationAndRotation(Location,Rotation);
            Light->ViewLightingChannels.bViewChannel0=false; Light->ViewLightingChannels.bViewChannel1=true; Light->ViewLightingChannels.bViewChannel2=false;
            Light->SetIntensityUnits(ELightUnits::Lumens); Light->SetIntensity(14000); Light->SetAttenuationRadius(1600);
            Light->SetInnerConeAngle(40); Light->SetOuterConeAngle(62); Light->SetLightColor(FLinearColor::White);
            Light->SetCastShadows(true); Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
            Light->bAffectTranslucentLighting=false; Light->bAffectGlobalIllumination=false; Light->bAffectReflection=false;
            Light->RegisterComponent(); NIRLights.Add(Light);
        }
    }
    State.bEnabled=true; State.Backend=bCaptureQA ? TEXT("OFFLINE CAPTURE"):bModelRequestsEnabled ? TEXT("Reason2 host GPU"):TEXT("INFERENCE DISABLED");
    bReady=true; SessionStarted=FPlatformTime::Seconds(); CaptureCase=QAStartCase-1;
    if (!bCaptureQA) BeginBody();
    if (bPolicyQA) { const bool Passed=RunPolicySmoke(); bReady=false; FPlatformMisc::RequestExitWithStatus(false,Passed ? 0:1); }
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_READY cameras=2_NIR_plus_arm_thermal contract=%s endpoint=%s capture_only=%d"),*ContractVersion,*Endpoint,bCaptureQA);
    return true;
}
UTextureRenderTarget2D* ADarkFactoryProcessController::GetPreviewTexture(int32 Slot) const {
    if (!Targets.IsValidIndex(Slot) || !PreviewStamps.IsValidIndex(Slot) || PreviewStamps[Slot].Body!=State.BodyId) return nullptr;
    const FString Expected=Slot<2 ? TEXT("doors"):(State.Phase==EDarkFactoryProcessPhase::Adhesive || State.Phase==EDarkFactoryProcessPhase::Welding) ? TEXT("adhesive"):TEXT("weld");
    return PreviewStamps[Slot].Stage==Expected ? Targets[Slot].Get():nullptr;
}
FString ADarkFactoryProcessController::StageName() const {
    return State.ActiveGate==0 ? TEXT("doors"):State.ActiveGate==1 ? TEXT("adhesive"):State.ActiveGate==2 ? TEXT("weld"):FString();
}
void ADarkFactoryProcessController::BeginBody() {
    CancelInspection();
    ++State.BodySequence; State.BodyId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    for (auto& Gate:State.Gates) Gate=EDarkFactoryGateState::Pending;
    State.ConsecutivePasses=0; State.Decision=TEXT("A");
    const int32 Fault=bModelQA ? (State.BodySequence==2 ? 1:0):ForcedFault;
    ProcessScene->BeginBody(State.BodySequence,State.ClockSeconds,Fault);
    EnterPhase(EDarkFactoryProcessPhase::Arriving);
}
void ADarkFactoryProcessController::EnterPhase(EDarkFactoryProcessPhase Phase) {
    CancelInspection(); bPhaseScreenshotTaken=false; State.Phase=Phase; PhaseStarted=State.ClockSeconds; State.StageSeconds=0; State.PhaseProgress=0;
    State.ActiveGate=Phase==EDarkFactoryProcessPhase::Doors ? 0:Phase==EDarkFactoryProcessPhase::Adhesive ? 1:Phase==EDarkFactoryProcessPhase::WeldCooling ? 2:-1;
    State.ConsecutivePasses=0; NextInspection=FPlatformTime::Seconds()+.4;
    if (State.ActiveGate>=0) { State.Gates[State.ActiveGate]=bModelRequestsEnabled ? EDarkFactoryGateState::Inspecting:EDarkFactoryGateState::Waiting; State.Message=bModelRequestsEnabled ? TEXT("Inspecting camera evidence"):TEXT("Inference disabled; holding at camera gate"); }
    else if (Phase==EDarkFactoryProcessPhase::Arriving) State.Message=TEXT("New body entering inspection cell");
    else if (Phase==EDarkFactoryProcessPhase::Welding) State.Message=TEXT("Door and adhesive gates passed: welding");
    else if (Phase==EDarkFactoryProcessPhase::Departing) State.Message=TEXT("All three gates passed: sending body forward");
    else State.Message=TEXT("Door anomaly detected: returning body to entry");
    RecordEvent(TEXT("phase"));
}
void ADarkFactoryProcessController::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!bReady || !ProcessScene.IsValid() || !WeldingCell.IsValid()) return;
    if (bCaptureQA) { CaptureQATick(); return; }
    if (bModelQA && FPlatformTime::Seconds()-SessionStarted>240.) {
        State.Message=TEXT("Model process smoke timed out"); RecordEvent(TEXT("timeout"));
        CancelInspection(); bReady=false; FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if (State.bPaused) return;
    const double ProcessDelta=FMath::Clamp(double(DeltaSeconds),0.,.1);
    State.ClockSeconds+=ProcessDelta;
    State.StageSeconds=State.ClockSeconds-PhaseStarted;
    double Offset=0; bool Visible=true,Permit=false;
    switch (State.Phase) {
    case EDarkFactoryProcessPhase::Arriving:
        State.PhaseProgress=FMath::Clamp(State.StageSeconds/ADarkFactoryProcessScene::CarriageTravelSeconds,0.,1.);
        Offset=FMath::Lerp(-ADarkFactoryProcessScene::CarriageTravelCm,0.,double(FMath::SmoothStep(0.f,1.f,State.PhaseProgress)));
        MotionPose=3.4;
        if (State.StageSeconds>=ADarkFactoryProcessScene::CarriageTravelSeconds+.2) EnterPhase(EDarkFactoryProcessPhase::Doors);
        break;
    case EDarkFactoryProcessPhase::Doors:
    case EDarkFactoryProcessPhase::Adhesive: MotionPose=3.4; break;
    case EDarkFactoryProcessPhase::Welding:
        MotionPose=FMath::Min(16.3,3.4+State.StageSeconds); Permit=true;
        State.PhaseProgress=FMath::Clamp(State.StageSeconds/12.9,0.,1.);
        if (State.StageSeconds>=12.9) { EnterPhase(EDarkFactoryProcessPhase::WeldCooling); Permit=false; }
        break;
    case EDarkFactoryProcessPhase::WeldCooling: MotionPose=16.3; break;
    case EDarkFactoryProcessPhase::Departing:
    case EDarkFactoryProcessPhase::Returning:
        State.PhaseProgress=FMath::Clamp(State.StageSeconds/ADarkFactoryProcessScene::CarriageTravelSeconds,0.,1.);
        Offset=FMath::Lerp(0.,State.Phase==EDarkFactoryProcessPhase::Returning ? -ADarkFactoryProcessScene::CarriageTravelCm:ADarkFactoryProcessScene::CarriageTravelCm,double(FMath::SmoothStep(0.f,1.f,State.PhaseProgress)));
        if (State.StageSeconds>=ADarkFactoryProcessScene::CarriageTravelSeconds+.4) {
            if (State.Phase==EDarkFactoryProcessPhase::Departing) ++State.PassedBodies; else ++State.ReturnedBodies;
            if (bModelQA) RecordModelSmokeOutcome();
            ProcessScene->EndBody(); ++CompletedBodies; RecordEvent(TEXT("body_completed"));
            if (bModelQA && CompletedBodies>=3) { const bool Saved=WriteReport(); FPlatformMisc::RequestExitWithStatus(false,Saved && HasPassedModelSmoke() ? 0:1); return; }
            BeginBody(); Offset=-ADarkFactoryProcessScene::CarriageTravelCm; MotionPose=3.4;
        }
        break;
    }
    const bool ThermalInspection=State.Phase==EDarkFactoryProcessPhase::Adhesive || State.Phase==EDarkFactoryProcessPhase::WeldCooling;
    InspectionExtension=FMath::Clamp(InspectionExtension+(ThermalInspection ? 1.:-1.)*ProcessDelta/.8,0.,1.);
    WeldingCell->SetAnimationEnabled(true);
    ProcessScene->ApplyFrame(MotionPose,State.ClockSeconds,Offset,Visible,Permit,InspectionExtension);
}
void ADarkFactoryProcessController::TogglePaused() {
    if (!bReady || bCaptureQA) return;
    State.bPaused=!State.bPaused; CancelInspection();
    WeldingCell->SetAnimationEnabled(!State.bPaused);
    State.ConsecutivePasses=0;
    if (State.ActiveGate>=0) State.Gates[State.ActiveGate]=EDarkFactoryGateState::Waiting;
    State.Message=State.bPaused ? TEXT("Process paused; outstanding inspection discarded"):bModelRequestsEnabled ? TEXT("Resuming with fresh camera evidence"):TEXT("Inference disabled; holding at camera gate");
    NextInspection=FPlatformTime::Seconds()+.4; RecordEvent(TEXT("pause"));
}
void ADarkFactoryProcessController::CancelInspection() {
    if (Request.IsValid()) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    Readback.Cancel(); bCaptureActive=false; State.bRequestPending=false; ActiveRequestId.Empty();
    BatchImages.Reset(); CaptureSlot=-1;
    // Detached encoder owns only pixels. Poll it before starting any new work.
}
void ADarkFactoryProcessController::StartCapture() {
    if (bCaptureActive || Request.IsValid() || Readback.IsBusy() || EncodeFuture.IsValid() || StageName().IsEmpty()) return;
    // Never aim a fixed capture from a lens that is still travelling to its
    // calibrated thermal pose. Offline snapshots explicitly seek this endpoint.
    if (State.ActiveGate>0 && WeldingCell->GetInspectionExtension()<1.) return;
    CaptureBodyId=State.BodyId; CaptureStage=StageName();
    ExpectedImages=State.ActiveGate==0 ? 2:3; BatchImages.Reset(); CaptureSlot=0;
    bCaptureActive=true; State.bRequestPending=true;
    BatchStarted=FPlatformTime::Seconds(); CapturedAtMs=BatchStarted*1000.;
    if (State.ActiveGate>=0) State.Gates[State.ActiveGate]=EDarkFactoryGateState::Inspecting;
    CaptureNext();
}
void ADarkFactoryProcessController::CaptureNext() {
    const bool NIR=CaptureStage==TEXT("doors");
    const int32 TargetIndex=NIR ? CaptureSlot:CaptureSlot+2;
    auto* Camera=Cameras[NIR ? CaptureSlot:2].Get();
    Camera->TextureTarget=Targets[TargetIndex];
    PreviewStamps[TargetIndex]=FPreviewStamp();
    if (auto* World=WorldOwner.Get()) {
        if (auto* Cabinet=World->GetControlCabinet()) if (auto* Glass=Cabinet->GetDisplaySurface()) Camera->HideComponent(Glass);
        if (auto* Mechanisms=World->GetCellMechanisms()) Mechanisms->HideProcessIndicatorsFrom(Camera);
    }
    if (NIR) {
        FVector Location; FRotator Rotation; float FOV=58;
        ProcessScene->GetDoorSensorView(CaptureSlot,Location,Rotation,FOV);
        Camera->SetWorldLocationAndRotation(Location,Rotation); Camera->FOVAngle=FOV;
        NIRLights[CaptureSlot]->SetWorldLocationAndRotation(Location,Rotation);
    }
    if (!NIR) {
        FVector Location; FRotator Rotation; float FOV=40;
        ProcessScene->GetInspectionCameraPose(CaptureSlot,Location,Rotation,FOV);
        Camera->SetWorldLocationAndRotation(Location,Rotation); Camera->FOVAngle=FOV;
        ThermalRig->PrepareInspectionMaterial(CameraMaterials[2]);
    }
    SettleFrames=3;
}
void ADarkFactoryProcessController::CaptureTick() {
    if (!bReady) return;
    const double Now=FPlatformTime::Seconds();
    if (bModelQA && !bPhaseScreenshotTaken && State.StageSeconds>(State.Phase==EDarkFactoryProcessPhase::Welding ? 7.:.6)) {
        bool CapturesReady=State.ActiveGate<0 || !State.bRequestPending;
        if (State.ActiveGate>=0) for (int32 I=0;I<(State.ActiveGate==0 ? 2:3);++I) CapturesReady &= GetPreviewTexture(State.ActiveGate==0 ? I:I+2)!=nullptr;
        if (CapturesReady) {
            const FString Name=FString::Printf(TEXT("body-%02d-%s.png"),State.BodySequence,PhaseName(State.Phase));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,TEXT("screenshots"),SessionId,Name),true,false); bPhaseScreenshotTaken=true;
        }
    }
    if (EncodeFuture.IsValid() && EncodeFuture.IsReady()) {
        auto Bytes=EncodeFuture.Consume();
        if (bCaptureActive && Bytes.IsEmpty()) {
            CancelInspection();
            if (bCaptureQA) { bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_PNG_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            ApplyDecision(TEXT("A"),false,TEXT("PNG encoding failed; holding"),0,State.Backend);
        }
        if (bCaptureActive && CaptureBodyId==State.BodyId && CaptureStage==StageName() && !Bytes.IsEmpty()) {
            BatchImages.Add(MoveTemp(Bytes)); ++CaptureSlot;
            if (CaptureSlot>=ExpectedImages) SubmitBatch(); else CaptureNext();
        }
    }
    TArray<FColor> Pixels; uint64 Id=0;
    if (Readback.Poll(Pixels,Id)) {
        if (bCaptureActive && Id==CaptureSerial && Pixels.Num()==Width*Height && CaptureBodyId==State.BodyId && CaptureStage==StageName()) {
            const int32 Slot=CaptureStage==TEXT("doors") ? CaptureSlot:CaptureSlot+2;
            PreviewStamps[Slot].Body=CaptureBodyId; PreviewStamps[Slot].Stage=CaptureStage; PreviewStamps[Slot].At=Now;
            EncodeFuture=Async(EAsyncExecution::ThreadPool,[Pixels=MoveTemp(Pixels)]() mutable { return Encode(MoveTemp(Pixels)); });
        }
        else if (bCaptureActive && Pixels.IsEmpty()) {
            CancelInspection();
            if (bCaptureQA) { bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_READBACK_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            ApplyDecision(TEXT("A"),false,TEXT("GPU capture failed"),0,State.Backend);
        }
    }
    if (!bCaptureQA && bModelRequestsEnabled && !State.bPaused && State.ActiveGate>=0 && !bCaptureActive && !Request.IsValid() && Now>=NextInspection) StartCapture();
    if (bCaptureActive && !Readback.IsBusy() && !EncodeFuture.IsValid()) {
        auto* Camera=Cameras[CaptureStage==TEXT("doors") ? CaptureSlot:2].Get();
        if (CaptureStage!=TEXT("doors")) ThermalRig->PrepareInspectionMaterial(CameraMaterials[2]);
        Camera->CaptureScene();
        if (SettleFrames>0) --SettleFrames;
        else if (!Readback.Enqueue(Camera->TextureTarget,++CaptureSerial)) {
            CancelInspection();
            if (bCaptureQA) { bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_ENQUEUE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            ApplyDecision(TEXT("A"),false,TEXT("GPU readback busy"),0,State.Backend);
        }
    }
    if (bCaptureQA && State.bRequestPending && Now-BatchStarted>30.) {
        CancelInspection(); bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_TIMEOUT")); FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if (!bCaptureQA && State.bRequestPending && Now-BatchStarted>MaxCaptureAgeSeconds) {
        CancelInspection(); ApplyDecision(TEXT("A"),false,TEXT("Inspection timed out; holding for fresh evidence"),0,State.Backend);
    }
}
void ADarkFactoryProcessController::SubmitBatch() {
    bCaptureActive=false;
    if (bCaptureQA) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("sample_id"),CaptureSampleId);
        Row->SetStringField(TEXT("body_id"),State.BodyId); Row->SetStringField(TEXT("stage"),CaptureStage);
        Row->SetStringField(TEXT("contract_version"),ContractVersion); Row->SetStringField(TEXT("prompt_sha256"),PromptHashes[CaptureStage]);
        TArray<TSharedPtr<FJsonValue>> Images;
        for (int32 I=0;I<BatchImages.Num();++I) {
            const FString Camera=CameraIds[CaptureStage==TEXT("doors") ? I:I+2];
            const FString Name=CaptureSampleId+TEXT("_")+Camera+TEXT(".png");
            if (!FFileHelper::SaveArrayToFile(BatchImages[I],*FPaths::Combine(OutputRoot,Name))) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
            auto Image=MakeShared<FJsonObject>(); Image->SetStringField(TEXT("camera_id"),Camera); Image->SetStringField(TEXT("path"),Name);
            Images.Add(MakeShared<FJsonValueObject>(Image));
        }
        Row->SetArrayField(TEXT("images"),Images);
        auto Truth=ProcessScene->BuildMetadata(); Row->SetObjectField(TEXT("ground_truth"),Truth);
        FString Expected;
        if (CaptureStage==TEXT("doors")) Expected=QAExpectedFault==0 ? TEXT("G"):TEXT("R");
        else {
            const double Maximum=CaptureStage==TEXT("adhesive") ? ProcessScene->GetMaximumAdhesiveC():DarkFactoryThermal::ApparentTemperatureC(ProcessScene->GetMaximumWeldC(),.74f,20.f);
            const double Limit=CaptureStage==TEXT("adhesive") ? 45.:55.;
            Truth->SetNumberField(TEXT("maximum_apparent_temperature_c"),Maximum); Truth->SetNumberField(TEXT("readiness_limit_c"),Limit);
            Expected=Maximum<Limit-3 ? TEXT("G"):Maximum>Limit+3 ? TEXT("R"):TEXT("A");
        }
        Row->SetStringField(TEXT("expected_decision"),Expected);
        if (QAEpisodes>0) {
            const int32 Episode=CaptureCase/7;
            Row->SetStringField(TEXT("episode_id"),State.BodyId);
            Row->SetStringField(TEXT("split"),Episode<QAEpisodes*5/8 ? TEXT("train"):Episode<QAEpisodes*13/16 ? TEXT("validation"):TEXT("test"));
            Row->SetNumberField(TEXT("dataset_seed"),QASeed);
            Row->SetStringField(TEXT("capture_recipe"),TEXT("process-episodes-v1"));
        }
        const FString Line=Json(Row)+LINE_TERMINATOR;
        if (!FFileHelper::SaveStringToFile(Line,*FPaths::Combine(OutputRoot,TEXT("manifest.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append)) {
            bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_MANIFEST_WRITE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_CAPTURE case=%d stage=%s expected=%s images=%d"),CaptureCase,*CaptureStage,*Expected,BatchImages.Num());
        State.bRequestPending=false; BatchImages.Reset(); bCasePrepared=false; return;
    }
    auto Payload=MakeShared<FJsonObject>(); ActiveRequestId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Payload->SetStringField(TEXT("contract_version"),ContractVersion); Payload->SetStringField(TEXT("request_id"),ActiveRequestId);
    Payload->SetStringField(TEXT("body_id"),CaptureBodyId); Payload->SetStringField(TEXT("stage"),CaptureStage);
    Payload->SetNumberField(TEXT("captured_at_ms"),CapturedAtMs);
    TArray<TSharedPtr<FJsonValue>> Images;
    for (int32 I=0;I<BatchImages.Num();++I) {
        auto Image=MakeShared<FJsonObject>(); Image->SetStringField(TEXT("camera_id"),CameraIds[CaptureStage==TEXT("doors") ? I:I+2]);
        Image->SetStringField(TEXT("png_base64"),FBase64::Encode(BatchImages[I])); Images.Add(MakeShared<FJsonValueObject>(Image));
    }
    Payload->SetArrayField(TEXT("images"),Images); BatchImages.Reset();
    Request=FHttpModule::Get().CreateRequest(); Request->SetURL(Endpoint); Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"),TEXT("application/json")); Request->SetContentAsString(Json(Payload)); Request->SetTimeout(ResponseDeadlineSeconds);
    RequestStarted=FPlatformTime::Seconds(); const FString Id=ActiveRequestId,Body=CaptureBodyId,Stage=CaptureStage; const double Stamp=CapturedAtMs;
    TWeakObjectPtr<ADarkFactoryProcessController> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda([WeakThis,Id,Body,Stage,Stamp](FHttpRequestPtr,FHttpResponsePtr Response,bool Success) {
        auto* Self=WeakThis.Get(); if (!Self || Self->ActiveRequestId!=Id || Self->State.BodyId!=Body || Self->StageName()!=Stage || Self->State.bPaused) return;
        Self->Request.Reset(); Self->State.bRequestPending=false;
        FString Decision=TEXT("A"),Reason=TEXT("Model connection unavailable"),Backend=TEXT("Reason2 host GPU"); bool Valid=false;
        const double Age=FPlatformTime::Seconds()-Self->BatchStarted;
        float Latency=(FPlatformTime::Seconds()-Self->RequestStarted)*1000.f;
        if (Success && Response.IsValid() && Response->GetResponseCode()==200 && Response->GetContentLength()<65536 && Age<=Self->MaxCaptureAgeSeconds) {
            TSharedPtr<FJsonObject> Result;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Result) && Result) {
                FString ReplyId,ReplyBody,ReplyStage,ReplyContract,Hash,Source; double Captured=-1; bool ClaimedValid=false;
                Result->TryGetStringField(TEXT("request_id"),ReplyId); Result->TryGetStringField(TEXT("body_id"),ReplyBody);
                Result->TryGetStringField(TEXT("stage"),ReplyStage); Result->TryGetStringField(TEXT("contract_version"),ReplyContract);
                Result->TryGetStringField(TEXT("prompt_sha256"),Hash); Result->TryGetNumberField(TEXT("captured_at_ms"),Captured);
                Result->TryGetStringField(TEXT("decision"),Decision); Result->TryGetBoolField(TEXT("valid"),ClaimedValid);
                Result->TryGetStringField(TEXT("reason"),Reason); Result->TryGetStringField(TEXT("source"),Source);
                FString Modality,MessageLayout,ScoringMethod; Result->TryGetStringField(TEXT("modality"),Modality);
                Result->TryGetStringField(TEXT("scoring_method"),ScoringMethod);
                const bool ScoringKnown=ScoringMethod==TEXT("gar-full-vocabulary-logits-v1") || ScoringMethod==TEXT("gar-unmasked-topk-intervals-v1");
                Result->TryGetStringField(TEXT("message_layout"),MessageLayout);
                const TArray<TSharedPtr<FJsonValue>>* ReplyCameras=nullptr;
                bool CamerasMatch=Result->TryGetArrayField(TEXT("camera_ids"),ReplyCameras) && ReplyCameras && ReplyCameras->Num()==(Stage==TEXT("doors") ? 2:3);
                if (CamerasMatch) for (int32 I=0;I<ReplyCameras->Num();++I) {
                    FString CameraId; CamerasMatch &= (*ReplyCameras)[I]->TryGetString(CameraId) && CameraId==CameraIds[Stage==TEXT("doors") ? I:I+2];
                }
                const bool Code=Decision==TEXT("G") || Decision==TEXT("A") || Decision==TEXT("R");
                Valid=ClaimedValid && Code && ScoringKnown && CamerasMatch && MessageLayout==TEXT("independent-camera-images-newline-v1") && Modality==(Stage==TEXT("doors") ? TEXT("nir"):TEXT("thermal")) && ReplyId==Id && ReplyBody==Body && ReplyStage==Stage && ReplyContract==Self->ContractVersion &&
                    Hash==Self->PromptHashes[Stage] && FMath::Abs(Captured-Stamp)<.01 && Source==TEXT("host_gpu");
                const TSharedPtr<FJsonObject>* Policy=nullptr;
                bool PolicyMatches=Result->TryGetObjectField(TEXT("decision_policy"),Policy) && Policy && Policy->IsValid();
                if (PolicyMatches) {
                    const TCHAR* Keys[]={TEXT("minimum_label_mass"),TEXT("minimum_conditional_probability"),TEXT("consecutive_green"),TEXT("max_reply_age_seconds")};
                    const double Expected[]={Self->MinimumLabelMass,Self->MinimumConditionalProbability,2.,Self->MaxCaptureAgeSeconds};
                    for (int32 I=0;I<4;++I) { double Value=0; PolicyMatches &= (*Policy)->TryGetNumberField(Keys[I],Value) && FMath::IsFinite(Value) && FMath::IsNearlyEqual(Value,Expected[I],1.e-7); }
                }
                Valid &= PolicyMatches;
                if (Valid) {
                    Self->LastModel.Empty(); Self->LastAdapter.Empty(); Self->LastBackendAlias.Empty(); Self->LastScoringMethod=ScoringMethod;
                    Result->TryGetStringField(TEXT("backend_alias"),Self->LastBackendAlias);
                    Result->TryGetStringField(TEXT("model"),Self->LastModel);
                    Result->TryGetStringField(TEXT("adapter"),Self->LastAdapter);
                    Backend=Self->LastAdapter.IsEmpty() ? TEXT("Reason2 host GPU / base experiment"):TEXT("Reason2 host GPU / LoRA experiment");
                }
                if (!Valid) { Decision=TEXT("A"); Reason=TEXT("Invalid or mismatched model reply; holding"); }
            }
        } else if (Age>Self->MaxCaptureAgeSeconds) Reason=TEXT("Stale model result discarded");
        Self->ActiveRequestId.Empty(); Self->ApplyDecision(Decision,Valid,Reason,Latency,Backend);
    });
    if (!Request->ProcessRequest()) { Request.Reset(); State.bRequestPending=false; ApplyDecision(TEXT("A"),false,TEXT("Cannot start model request"),0,State.Backend); }
}
void ADarkFactoryProcessController::ApplyDecision(const FString& Code,bool Valid,const FString& Reason,float Latency,const FString& Backend) {
    if (State.ActiveGate<0 || State.bPaused) return;
    State.Decision=Valid ? Code:TEXT("A"); State.LastLatencyMs=Latency; State.Backend=Backend;
    State.bRequestPending=false; NextInspection=FPlatformTime::Seconds()+.75;
    if (!Valid) { State.ConsecutivePasses=0; State.Gates[State.ActiveGate]=EDarkFactoryGateState::Error; State.Message=Reason; }
    else if (Code==TEXT("G")) {
        ++State.ConsecutivePasses; State.Gates[State.ActiveGate]=EDarkFactoryGateState::Inspecting;
        State.Message=FString::Printf(TEXT("Readiness confirmed %d / 2; acquiring fresh evidence"),State.ConsecutivePasses);
        if (State.ConsecutivePasses>=2) {
            if (bModelQA && State.ActiveGate>0) RecordModelSmokeThermalGate();
            State.Gates[State.ActiveGate]=EDarkFactoryGateState::Passed; RecordEvent(TEXT("gate_passed"));
            EnterPhase(State.Phase==EDarkFactoryProcessPhase::Doors ? EDarkFactoryProcessPhase::Adhesive:State.Phase==EDarkFactoryProcessPhase::Adhesive ? EDarkFactoryProcessPhase::Welding:EDarkFactoryProcessPhase::Departing);
            return;
        }
    } else {
        State.ConsecutivePasses=0;
        if (Code==TEXT("R") && State.Phase==EDarkFactoryProcessPhase::Doors) {
            State.Gates[0]=EDarkFactoryGateState::Rejected; State.Message=TEXT("Reason2 detected a door anomaly"); RecordEvent(TEXT("gate_rejected"));
            EnterPhase(EDarkFactoryProcessPhase::Returning); return;
        }
        State.Gates[State.ActiveGate]=EDarkFactoryGateState::Waiting;
        State.Message=Code==TEXT("R") ? TEXT("Thermal area remains hot; waiting and reinspecting"):TEXT("Insufficient visual evidence; holding and reinspecting");
    }
    RecordEvent(TEXT("decision"));
}
bool ADarkFactoryProcessController::IsModelSmokeOutcomeCorrect(int32 BodySequence,bool bDeparted) {
    return BodySequence>=1 && BodySequence<=3 && bDeparted==(BodySequence!=2);
}
bool ADarkFactoryProcessController::HasPassedModelSmoke() const {
    return bModelQA && CompletedBodies==3 && ModelSmokeOutcomes.Num()==3 && bModelSmokeOutcomesMatch && bModelSmokeThermalLimitsMatch && State.PassedBodies==2 && State.ReturnedBodies==1;
}
void ADarkFactoryProcessController::RecordModelSmokeThermalGate() {
    // QA observes the accepted action; it never overrides it or enters the model request.
    const bool Adhesive=State.ActiveGate==1;
    const double Maximum=Adhesive ? ProcessScene->GetMaximumAdhesiveC():DarkFactoryThermal::ApparentTemperatureC(ProcessScene->GetMaximumWeldC(),.74f,20.f);
    const double Limit=Adhesive ? 45.:55.;
    const bool Ready=FMath::IsFinite(Maximum) && Maximum<Limit;
    bModelSmokeThermalLimitsMatch &= Ready;
    auto Check=MakeShared<FJsonObject>(); Check->SetStringField(TEXT("body_id"),State.BodyId);
    Check->SetNumberField(TEXT("body_sequence"),State.BodySequence); Check->SetStringField(TEXT("stage"),StageName());
    Check->SetNumberField(TEXT("maximum_apparent_c_at_acceptance"),Maximum); Check->SetNumberField(TEXT("limit_c"),Limit);
    Check->SetBoolField(TEXT("ready_at_acceptance"),Ready); ModelSmokeThermalChecks.Add(MakeShared<FJsonValueObject>(Check));
}
void ADarkFactoryProcessController::RecordModelSmokeOutcome() {
    // Test-fixture expectations affect only the report/exit code, never model input or motion decisions.
    const bool Departed=State.Phase==EDarkFactoryProcessPhase::Departing;
    const bool Correct=IsModelSmokeOutcomeCorrect(State.BodySequence,Departed);
    bModelSmokeOutcomesMatch &= Correct;
    auto Outcome=MakeShared<FJsonObject>(); Outcome->SetStringField(TEXT("body_id"),State.BodyId);
    Outcome->SetNumberField(TEXT("body_sequence"),State.BodySequence);
    Outcome->SetStringField(TEXT("expected_destination"),State.BodySequence==2 ? TEXT("entry"):TEXT("exit"));
    Outcome->SetStringField(TEXT("actual_destination"),Departed ? TEXT("exit"):TEXT("entry"));
    Outcome->SetBoolField(TEXT("correct"),Correct); ModelSmokeOutcomes.Add(MakeShared<FJsonValueObject>(Outcome));
}
void ADarkFactoryProcessController::RecordEvent(const FString& Name) {
    auto Event=MakeShared<FJsonObject>(); Event->SetStringField(TEXT("event"),Name); Event->SetStringField(TEXT("body_id"),State.BodyId);
    Event->SetNumberField(TEXT("body_sequence"),State.BodySequence); Event->SetStringField(TEXT("phase"),PhaseName(State.Phase));
    Event->SetStringField(TEXT("decision"),State.Decision); Event->SetStringField(TEXT("message"),State.Message);
    Event->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds); Event->SetNumberField(TEXT("latency_ms"),State.LastLatencyMs);
    Event->SetStringField(TEXT("model"),LastModel); Event->SetStringField(TEXT("adapter"),LastAdapter);
    Event->SetStringField(TEXT("scoring_method"),LastScoringMethod); Event->SetStringField(TEXT("backend_alias"),LastBackendAlias);
    Events.Add(MakeShared<FJsonValueObject>(Event)); if (Events.Num()>256) Events.RemoveAt(0);
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_EVENT %s"),*Json(Event)); WriteReport();
}
TSharedRef<FJsonObject> ADarkFactoryProcessController::BuildMetadata() const {
    auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("contract_version"),ContractVersion);
    Report->SetStringField(TEXT("session_id"),SessionId);
    if (bModelQA) Report->SetStringField(TEXT("screenshot_directory"),FPaths::Combine(OutputRoot,TEXT("screenshots"),SessionId));
    Report->SetStringField(TEXT("source"),bPolicyQA ? TEXT("policy_test"):bCaptureQA ? TEXT("offline_capture"):bModelRequestsEnabled ? TEXT("host_gpu"):TEXT("disabled"));
    Report->SetStringField(TEXT("body_id"),State.BodyId); Report->SetStringField(TEXT("phase"),PhaseName(State.Phase));
    Report->SetStringField(TEXT("endpoint"),Endpoint); Report->SetStringField(TEXT("model"),LastModel); Report->SetStringField(TEXT("adapter"),LastAdapter);
    Report->SetStringField(TEXT("scoring_method"),LastScoringMethod); Report->SetStringField(TEXT("backend_alias"),LastBackendAlias);
    Report->SetStringField(TEXT("message_layout"),TEXT("independent-camera-images-newline-v1"));
    auto Hashes=MakeShared<FJsonObject>(); for (const auto& Pair:PromptHashes) Hashes->SetStringField(Pair.Key,Pair.Value); Report->SetObjectField(TEXT("prompt_hashes"),Hashes);
    auto Policy=MakeShared<FJsonObject>(); Policy->SetNumberField(TEXT("minimum_label_mass"),MinimumLabelMass); Policy->SetNumberField(TEXT("minimum_conditional_probability"),MinimumConditionalProbability);
    Policy->SetNumberField(TEXT("consecutive_green"),2); Policy->SetNumberField(TEXT("max_reply_age_seconds"),MaxCaptureAgeSeconds); Report->SetObjectField(TEXT("decision_policy"),Policy);
    Report->SetStringField(TEXT("decision"),State.Decision); Report->SetStringField(TEXT("message"),State.Message);
    Report->SetBoolField(TEXT("paused"),State.bPaused); Report->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds);
    Report->SetNumberField(TEXT("passed_bodies"),State.PassedBodies); Report->SetNumberField(TEXT("returned_bodies"),State.ReturnedBodies);
    Report->SetNumberField(TEXT("body_sequence"),State.BodySequence); Report->SetArrayField(TEXT("events"),Events);
    Report->SetBoolField(TEXT("model_smoke"),bModelQA); Report->SetNumberField(TEXT("completed_bodies"),CompletedBodies);
    Report->SetBoolField(TEXT("passed"),HasPassedModelSmoke());
    if (bModelQA) {
        Report->SetArrayField(TEXT("body_outcomes"),ModelSmokeOutcomes); Report->SetBoolField(TEXT("body_outcomes_correct"),bModelSmokeOutcomesMatch);
        Report->SetArrayField(TEXT("thermal_gate_checks"),ModelSmokeThermalChecks); Report->SetBoolField(TEXT("thermal_limits_respected"),bModelSmokeThermalLimitsMatch);
        // Private QA telemetry only; never part of an inspection request or verdict.
        TArray<TSharedPtr<FJsonValue>> Portals; Portals.Reserve(2);
        if (UWorld* World=GetWorld()) for (TActorIterator<ADarkFactoryCurtainPortal> It(World); It && Portals.Num()<2; ++It)
            Portals.Add(MakeShared<FJsonValueObject>(It->BuildPhysicsMetadata()));
        Report->SetArrayField(TEXT("curtain_portals"),Portals);
    }
    Report->SetNumberField(TEXT("ground_truth_fields_sent_to_model"),0); Report->SetNumberField(TEXT("legacy_bridge_requests"),0);
    return Report;
}
bool ADarkFactoryProcessController::WriteReport() const {
    const bool Saved=!OutputRoot.IsEmpty() && FFileHelper::SaveStringToFile(Json(BuildMetadata()),*FPaths::Combine(OutputRoot,TEXT("process_runtime.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!Saved) UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_REPORT_SAVE_FAILED %s"),*OutputRoot);
    return Saved;
}
void ADarkFactoryProcessController::EndPlay(const EEndPlayReason::Type Reason) {
    CancelInspection();
    // Completed model QA already saved the live scene before shutdown. Portal
    // EndPlay may tear down constraints first; preserve that completed snapshot.
    if (!bModelQA || CompletedBodies<3) WriteReport();
    Super::EndPlay(Reason);
}
void ADarkFactoryProcessController::CaptureQATick() {
    if (bCasePrepared || bCaptureActive || Readback.IsBusy() || EncodeFuture.IsValid()) return;
    ++CaptureCase;
    if (CaptureCase>=QAEndCase) { UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_CAPTURE_COMPLETE samples=%d"),QAEndCase-QAStartCase); FPlatformMisc::RequestExit(false); bReady=false; return; }
    ProcessScene->EndBody(); State.ClockSeconds=0; State.BodySequence=CaptureCase+1;
    State.BodyId=FString::Printf(TEXT("capture-body-%03d"),CaptureCase); CaptureSampleId=FString::Printf(TEXT("sample-%03d"),CaptureCase);
    if (QAEpisodes>0) {
        const int32 Episode=CaptureCase/7,Kind=CaptureCase%7;
        State.BodySequence=QASeed*1000+Episode+1;
        State.BodyId=FString::Printf(TEXT("dataset-%d-episode-%04d"),QASeed,Episode);
        CaptureSampleId=FString::Printf(TEXT("episode-%04d-case-%d"),Episode,Kind);
        QAExpectedFault=Kind==0 && Episode%2 ? 1+(Episode/2)%3:0;
        ProcessScene->SetCaptureVariation(QASeed*4099+Episode);
        ProcessScene->BeginBody(State.BodySequence,0,QAExpectedFault,(Episode/6)%4);
        FRandomStream SampleRandom(QASeed*6151+Episode*71+Kind);
        if (Kind==0) {
            State.Phase=EDarkFactoryProcessPhase::Doors; State.ActiveGate=0;
            for (int32 Step=1;Step<=120;++Step) ProcessScene->ApplyFrame(3.4,Step/30.,0,true,false,0.);
            State.ClockSeconds=4;
        } else if (Kind<=3) {
            State.Phase=EDarkFactoryProcessPhase::Adhesive; State.ActiveGate=1;
            const double Target=Kind==1 ? SampleRandom.FRandRange(58.f,75.f):Kind==2 ? SampleRandom.FRandRange(43.5f,46.5f):SampleRandom.FRandRange(23.f,38.f);
            double Age=0; ProcessScene->ApplyFrame(3.4,Age,0,true,false);
            while (ProcessScene->GetMaximumAdhesiveC()>Target && Age<120.) {
                Age+=.2; ProcessScene->ApplyFrame(3.4,Age,0,true,false);
            }
            State.ClockSeconds=Age;
        } else {
            State.Phase=EDarkFactoryProcessPhase::WeldCooling; State.ActiveGate=2;
            // Pre-weld adhesive cooling and the actual authored weld are simulated before each sample.
            ProcessScene->ApplyFrame(3.4,60.,0,true,false);
            for (int32 Step=0;Step<=258;++Step) ProcessScene->ApplyFrame(3.4+Step*.05,60.+Step*.05,0,true,true);
            const double Target=Kind==4 ? SampleRandom.FRandRange(70.f,105.f):Kind==5 ? SampleRandom.FRandRange(53.5f,56.5f):SampleRandom.FRandRange(25.f,43.f);
            double Age=0;
            while (DarkFactoryThermal::ApparentTemperatureC(ProcessScene->GetMaximumWeldC(),.74f,20.f)>Target && Age<30.) {
                Age+=.05; ProcessScene->ApplyFrame(16.3,72.9+Age,0,true,false);
            }
            State.ClockSeconds=72.9+Age;
        }
        WeldingCell->SetAnimationEnabled(false); bCasePrepared=true; StartCapture(); return;
    }
    QAExpectedFault=CaptureCase<16 ? CaptureCase/4:0;
    const int32 Fault=CaptureCase<16 ? CaptureCase/4:0;
    ProcessScene->BeginBody(State.BodySequence,0,Fault,CaptureCase%4);
    if (CaptureCase<16) {
        State.Phase=EDarkFactoryProcessPhase::Doors; State.ActiveGate=0;
        for (int32 Step=1;Step<=120;++Step) ProcessScene->ApplyFrame(3.4,Step/30.,0,true,false,0.);
        State.ClockSeconds=4;
    } else if (CaptureCase<28) {
        const double Ages[]={0.,5.,16.,60.}; const double Age=Ages[(CaptureCase-16)%4];
        State.Phase=EDarkFactoryProcessPhase::Adhesive; State.ActiveGate=1;
        ProcessScene->ApplyFrame(3.4,Age,0,true,false); State.ClockSeconds=Age;
    } else {
        const double Ages[]={0.,2.,8.,18.}; const double Age=Ages[(CaptureCase-28)%4];
        State.Phase=EDarkFactoryProcessPhase::WeldCooling; State.ActiveGate=2;
        // Run an entire authored weld before sampling post-weld cooldown.
        for (int32 Step=0;Step<=326;++Step) ProcessScene->ApplyFrame(Step*.05,Step*.05,0,true,true);
        ProcessScene->ApplyFrame(16.3,16.3+Age,0,true,false); State.ClockSeconds=16.3+Age;
    }
    WeldingCell->SetAnimationEnabled(false); bCasePrepared=true;
    StartCapture();
}
