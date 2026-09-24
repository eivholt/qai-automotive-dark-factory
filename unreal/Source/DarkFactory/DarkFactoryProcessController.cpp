#include "DarkFactoryProcessController.h"
#include "DarkFactoryAgentProtocol.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactoryCurtainPortal.h"
#include "Components/StaticMeshComponent.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryThermalRadiometry.h"
#include "DarkFactoryGraphics.h"
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
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace {
constexpr int32 Width=448,Height=256;
const TCHAR* CameraIds[]={TEXT("spot1"),TEXT("spot2"),TEXT("spot3")};
bool IsNIRStage(const FString& Stage) { return Stage==TEXT("panel_presence") || Stage==TEXT("panel_seated") || Stage==TEXT("sealant_coverage"); }
int32 GateForPhase(EDarkFactoryProcessPhase Phase) {
    return Phase==EDarkFactoryProcessPhase::PanelPresence ? 0:Phase==EDarkFactoryProcessPhase::Adhesive ? 1:
        Phase==EDarkFactoryProcessPhase::PanelSeated ? 2:Phase==EDarkFactoryProcessPhase::WeldCooling ? 3:-1;
}
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
    case EDarkFactoryProcessPhase::PanelPresence:return TEXT("panel_presence");
    case EDarkFactoryProcessPhase::AdhesiveApplication:return TEXT("adhesive_application");
    case EDarkFactoryProcessPhase::PanelPlacement:return TEXT("panel_placement");
    case EDarkFactoryProcessPhase::Adhesive:return TEXT("adhesive");
    case EDarkFactoryProcessPhase::Welding:return TEXT("welding");
    case EDarkFactoryProcessPhase::PanelSeated:return TEXT("panel_seated");
    case EDarkFactoryProcessPhase::WeldCooling:return TEXT("weld");
    case EDarkFactoryProcessPhase::HolderRetract:return TEXT("holder_retract");
    case EDarkFactoryProcessPhase::Departing:return TEXT("departing");
    case EDarkFactoryProcessPhase::AgentIdle:return TEXT("agent_idle");
    }
    return TEXT("unknown");
}

}
bool ADarkFactoryProcessController::HasGrayscaleSensorPixels(const TArray<FColor>& Pixels) {
    if (Pixels.Num()!=Width*Height) return false;
    for (const FColor& Pixel:Pixels) {
        const int32 Spread=FMath::Max3(int32(Pixel.R),int32(Pixel.G),int32(Pixel.B))-FMath::Min3(int32(Pixel.R),int32(Pixel.G),int32(Pixel.B));
        if (Spread>1) return false;
    }
    return true;
}
bool ADarkFactoryProcessController::AdvanceSensorIdleWindow(double Now,bool bCompiling,double& IdleSince) {
    if (bCompiling) { IdleSince=0.; return false; }
    if (IdleSince==0.) IdleSince=Now;
    return Now-IdleSince>=5.;
}
bool ADarkFactoryProcessController::SensorResourcesReady() {
    if (bPolicyQA || bSensorResourcesReady) return true;
    const double Now=FPlatformTime::Seconds();
    bool Compiling=false;
#if WITH_EDITOR
    Compiling=FAssetCompilingManager::Get().GetNumRemainingAssets()>0 || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling());
#endif
    // Compilation readiness precedes camera priming and the request-age clock.
    // Polling keeps the game/render threads available to finish async resources;
    // the five-second idle window matches the independently verified graphics QA.
    if (!AdvanceSensorIdleWindow(Now,Compiling,SensorAssetsIdleSince)) {
        State.Message=Compiling ? TEXT("Waiting for sensor assets and shaders"):TEXT("Preparing sensor rendering resources");
        if (bCaptureQA && Now-SessionStarted>720.) {
            bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_SENSOR_STARTUP_TIMEOUT"));
            FPlatformMisc::RequestExitWithStatus(false,1);
        }
        return false;
    }
    bSensorResourcesReady=true; SensorResourcesReadyAt=Now;
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_SENSOR_RESOURCES_READY startup_seconds=%.3f stable_idle_seconds=%.3f"),Now-SessionStarted,Now-SensorAssetsIdleSince);
    return true;
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
    FString Control=TEXT("flow"),AgentRuntime=TEXT("evk");
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessControl="),Control);
    FParse::Value(FCommandLine::Get(),TEXT("DFAgentRuntime="),AgentRuntime);
    const bool EdgeAgent=Control==TEXT("agent") && AgentRuntime==TEXT("evk");
    Endpoint=EdgeAgent ? TEXT("http://127.0.0.1:18198/v1/inspect"):TEXT("http://127.0.0.1:18191/v1/inspect");
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessEndpoint="),Endpoint);
    ExpectedModelSource=EdgeAgent ? TEXT("evk_hexagon"):TEXT("host_gpu");
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessModelSource="),ExpectedModelSource);
    if (ExpectedModelSource!=TEXT("host_gpu") && ExpectedModelSource!=TEXT("evk_hexagon")) return false;
    // Dedicated local experimentation endpoint; never silently fall back to the parcel bridge.
    if (!Endpoint.StartsWith(TEXT("http://127.0.0.1:")) && !Endpoint.StartsWith(TEXT("http://localhost:"))) return false;
    bCaptureQA=FParse::Param(FCommandLine::Get(),TEXT("DFProcessCapture"));
    bInspectionReplay=FParse::Param(FCommandLine::Get(),TEXT("DFInspectionReplay"));
    bSealantCoverageCapture=FParse::Param(FCommandLine::Get(),TEXT("DFSealantCoverageCapture"));
    if (bSealantCoverageCapture && !bCaptureQA) return false;
    bModelQA=FParse::Param(FCommandLine::Get(),TEXT("DFProcessSmoke"));
    bPolicyQA=FParse::Param(FCommandLine::Get(),TEXT("DFProcessPolicySmoke"));
    bPanelMotionQA=FParse::Param(FCommandLine::Get(),TEXT("DFPanelMotionSmoke"));
    if (bPanelMotionQA && (bCaptureQA || bModelQA || bPolicyQA)) return false;
    bModelRequestsEnabled=!bPanelMotionQA && !FParse::Param(FCommandLine::Get(),TEXT("DFNoInference"));
    if (bModelQA && !bModelRequestsEnabled) { UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_SMOKE_REQUIRES_INFERENCE")); return false; }
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessPreassembled="),ForcedPreassembled);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessCaseStart="),QAStartCase);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessCaseEnd="),QAEndCase);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessEpisodes="),QAEpisodes);
    FParse::Value(FCommandLine::Get(),TEXT("DFProcessDatasetSeed="),QASeed);
    QAEpisodes=FMath::Clamp(QAEpisodes,0,1024);
    if (QAEpisodes>0) { QAStartCase=0; QAEndCase=QAEpisodes*8; }
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
    if (!Contract->TryGetStringField(TEXT("version"),ContractVersion) || ContractVersion!=TEXT("automotive-panel-process-v3")) return false;
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
    const auto Stages=Object(Contract,TEXT("stages")); if (!Stages || Stages->Values.Num()!=4) return false;
    for (const TCHAR* Stage:{TEXT("panel_presence"),TEXT("adhesive"),TEXT("panel_seated"),TEXT("weld")}) {
        const auto Entry=Object(Stages,Stage); FString Hash; const bool NIR=IsNIRStage(Stage);
        const TArray<TSharedPtr<FJsonValue>>* Ids=nullptr;
        if (!Entry || !Entry->TryGetStringField(TEXT("prompt_sha256"),Hash) || Hash.Len()!=64 ||
            !String(Entry,TEXT("modality"),NIR ? TEXT("nir"):TEXT("thermal")) ||
            !Entry->TryGetArrayField(TEXT("camera_ids"),Ids) || !Ids || Ids->Num()!=3) return false;
        for (const TCHAR C:Hash) if (!FChar::IsHexDigit(C)) return false;
        for (int32 I=0;I<Ids->Num();++I) { FString Id; if (!(*Ids)[I]->TryGetString(Id) || Id!=CameraIds[I]) return false; }
        if (!NIR && (!Number(Entry,TEXT("ready_below_c"),FString(Stage)==TEXT("adhesive") ? 45.:55.) || !Number(Entry,TEXT("ambiguity_band_c"),3.))) return false;
        PromptHashes.Add(Stage,Hash);
    }
    const auto Generation=Object(Contract,TEXT("generation")); bool Thinking=true;
    if (!String(Contract,TEXT("model_family"),TEXT("nvidia/Cosmos-Reason2-2B")) ||
        !String(Generation,TEXT("method"),TEXT("single_next_token_logits")) || !Number(Generation,TEXT("temperature"),0) ||
        !Generation->TryGetBoolField(TEXT("enable_thinking"),Thinking) || Thinking || !Number(Generation,TEXT("image_cache_entries"),0)) return false;
    const auto Policy=Object(Contract,TEXT("decision_policy"));
    if (!Number(Policy,TEXT("consecutive_green"),State.RequiredConsensusBatches) || !Policy->TryGetNumberField(TEXT("max_reply_age_seconds"),MaxCaptureAgeSeconds) ||
        !FMath::IsFinite(MaxCaptureAgeSeconds) || MaxCaptureAgeSeconds<1. || MaxCaptureAgeSeconds>60. ||
        !Policy->TryGetNumberField(TEXT("minimum_label_mass"),MinimumLabelMass) || !FMath::IsFinite(MinimumLabelMass) || MinimumLabelMass<.5 || MinimumLabelMass>1. ||
        !Policy->TryGetNumberField(TEXT("minimum_conditional_probability"),MinimumConditionalProbability) || !FMath::IsFinite(MinimumConditionalProbability) || MinimumConditionalProbability<.5 || MinimumConditionalProbability>1.) return false;
    auto* NIRParent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingNIRGrayscale.M_WeldingNIRGrayscale"));
    if (bSealantCoverageCapture) {
        TSharedPtr<FJsonObject> Study;
        if (!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectDir(),TEXT("../configs/sealant_coverage_experiment.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Study) || !Study ||
            !String(Study,TEXT("version"),TEXT("automotive-sealant-coverage-study-v1"))) return false;
        const auto Entry=Object(Object(Study,TEXT("stages")),TEXT("sealant_coverage")); FString Hash;
        if (!Entry || !Entry->TryGetStringField(TEXT("prompt_sha256"),Hash) || Hash.Len()!=64) return false;
        PromptHashes.Add(TEXT("sealant_coverage"),Hash); bModelRequestsEnabled=false;
    }
    auto* ThermalParent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PP_Thermal.M_PP_Thermal"));
    if (!NIRParent || !ThermalParent) return false;
    PreviewStamps.SetNum(6);
    for (int32 I=0;I<6;++I) {
        auto* Target=NewObject<UTextureRenderTarget2D>(this);
        Target->InitCustomFormat(Width,Height,PF_B8G8R8A8,I>=3);
        if (I>=3) Target->TargetGamma=1.f;
        Target->ClearColor=FLinearColor::Black; Target->UpdateResourceImmediate(true); Targets.Add(Target);
    }
    // Two modality settings share the actual R4 optical pose. Render targets
    // preserve the three ordered views without additional physical cameras.
    for (int32 I=0;I<2;++I) {
        const bool NIR=I==0;
        auto* Camera=NewObject<USceneCaptureComponent2D>(this,*FString::Printf(TEXT("R4%sCapture"),NIR ? TEXT("NIR"):TEXT("Thermal")));
        Camera->SetupAttachment(SceneRoot); Camera->bCaptureEveryFrame=false; Camera->bCaptureOnMovement=false;
        Camera->bAlwaysPersistRenderingState=true; Camera->CaptureSource=SCS_FinalColorLDR;
        // UE registration restores archetype ShowFlags; configure isolation afterward.
        Camera->RegisterComponent();
        Camera->ViewLightingChannels.bViewChannel0=!NIR;
        Camera->ViewLightingChannels.bViewChannel1=NIR;
        Camera->ViewLightingChannels.bViewChannel2=false;
        Camera->ShowFlags.SetTemporalAA(false); Camera->ShowFlags.SetAntiAliasing(false);
        Camera->ShowFlags.SetMotionBlur(false); Camera->ShowFlags.SetFog(false); Camera->ShowFlags.SetAtmosphere(false);
        auto& PP=Camera->PostProcessSettings;
        PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
        PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP.AutoExposureApplyPhysicalCameraExposure=false;
        PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=NIR ? -4.f:0.f;
        PP.bOverride_VignetteIntensity=true; PP.VignetteIntensity=0;
        PP.bOverride_MotionBlurAmount=true; PP.MotionBlurAmount=0;
        PP.bOverride_FilmGrainIntensity=true; PP.FilmGrainIntensity=0;
        if (NIR) { PP.bOverride_ColorSaturation=true; PP.ColorSaturation=FVector4(0,0,0,1); }
        else {
            Camera->ShowFlags.SetLighting(false); Camera->ShowFlags.SetDynamicShadows(false);
            Camera->ShowFlags.SetBloom(false); Camera->ShowFlags.SetAmbientOcclusion(false);
        }
        DarkFactoryGraphics::ConfigureSensorCapture(Camera);
        auto* Material=UMaterialInstanceDynamic::Create(NIR ? NIRParent:ThermalParent,this);
        Camera->AddOrUpdateBlendable(Material,1); Camera->TextureTarget=Targets[NIR ? 0:3];
        CameraMaterials.Add(Material); Cameras.Add(Camera);
    }
    auto* Light=NewObject<USpotLightComponent>(this,TEXT("R4NIRIlluminator"));
    Light->SetupAttachment(SceneRoot); Light->SetMobility(EComponentMobility::Movable);
    Light->ViewLightingChannels.bViewChannel0=false; Light->ViewLightingChannels.bViewChannel1=true; Light->ViewLightingChannels.bViewChannel2=false;
    Light->SetIntensityUnits(ELightUnits::Lumens); Light->SetIntensity(14000); Light->SetAttenuationRadius(1600);
    Light->SetInnerConeAngle(40); Light->SetOuterConeAngle(62); Light->SetLightColor(FLinearColor::White);
    Light->SetCastShadows(true); Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
    Light->bAffectTranslucentLighting=false; Light->bAffectGlobalIllumination=false; Light->bAffectReflection=false;
    Light->SetVisibility(false); Light->RegisterComponent(); NIRLights.Add(Light);
    State.bEnabled=true; State.Backend=bCaptureQA ? TEXT("OFFLINE CAPTURE"):bModelRequestsEnabled ? (ExpectedModelSource==TEXT("evk_hexagon") ? TEXT("Reason2 IQ9 EVK"):TEXT("Reason2 host GPU")):TEXT("INFERENCE DISABLED");
    if (!InitializeAgent()) return false;
    if (!InitializeModelBackends()) return false;
    bReady=true; SessionStarted=FPlatformTime::Seconds(); CaptureCase=QAStartCase-1;
    RefreshStreamingCapability();
    if (!bCaptureQA && !bPanelMotionQA) BeginBody();
    if (bPanelMotionQA) { State.Backend=TEXT("PHYSICAL QA / NO MODEL"); Cell->SetAnimationEnabled(false); }
    if (bPolicyQA) { const bool Passed=RunPolicySmoke(); bReady=false; FPlatformMisc::RequestExitWithStatus(false,Passed ? 0:1); }
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_READY cameras=physical_R4_NIR_and_thermal_ordered_three_views contract=%s endpoint=%s capture_only=%d"),*ContractVersion,*Endpoint,bCaptureQA);
    return true;
}
UTextureRenderTarget2D* ADarkFactoryProcessController::GetPreviewTexture(int32 Slot) const {
    if (!Targets.IsValidIndex(Slot) || !PreviewStamps.IsValidIndex(Slot) || PreviewStamps[Slot].Body!=State.BodyId) return nullptr;
    FString Expected;
    if (Agent) return Targets[Slot].Get(); // Latest actual modality capture for this body.
    if (Slot<3) Expected=State.Phase>=EDarkFactoryProcessPhase::PanelSeated ? TEXT("panel_seated"):TEXT("panel_presence");
    else Expected=State.Phase>=EDarkFactoryProcessPhase::WeldCooling ? TEXT("weld"):TEXT("adhesive");
    return PreviewStamps[Slot].Stage==Expected ? Targets[Slot].Get():nullptr;
}
FString ADarkFactoryProcessController::StageName() const {
    if (bSealantCoverageCapture) return TEXT("sealant_coverage");
    static const TCHAR* Stages[]={TEXT("panel_presence"),TEXT("adhesive"),TEXT("panel_seated"),TEXT("weld")};
    return State.ActiveGate>=0 && State.ActiveGate<4 ? Stages[State.ActiveGate]:FString();
}
void ADarkFactoryProcessController::BeginBody() {
    CancelInspection();
    CoolingRecovery.Reset();
    ++State.BodySequence; State.BodyId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    BeginAgentBody();
    for (auto& Gate:State.Gates) Gate=EDarkFactoryGateState::Pending;
    for (auto& Step:State.Steps) Step=EDarkFactoryStepState::Pending;
    State.bBranchKnown=false; State.bPreassembled=false; State.bAssemblyPerformed=false; State.bWeldAuthorized=false;
    State.ConsecutivePasses=0; State.ConsensusCode.Empty(); State.Decision=TEXT("A");
    const int32 Present=bModelQA ? (State.BodySequence==2 ? 1:0):ForcedPreassembled;
    ProcessScene->BeginBody(State.BodySequence,State.ClockSeconds,Present);
    EnterPhase(EDarkFactoryProcessPhase::Arriving);
    // Explicit QA only: exercise both real backends without changing choices,
    // prompts or branch evidence. Interactive selection is exclusively manual.
    if (bBackendSwitchSmoke) SelectModelBackend(State.BodySequence==2 ? TEXT("host_gpu"):TEXT("evk_hexagon"));
}
void ADarkFactoryProcessController::EnterPhase(EDarkFactoryProcessPhase Phase) {
    CancelInspection();
    CoolingRecovery.Reset();
    if (Phase!=EDarkFactoryProcessPhase::Adhesive) ProcessScene->StopRecoveryCooling();
    const int32 Previous=static_cast<int32>(State.Phase);
    if (Previous<State.StepCount && State.Steps[Previous]!=EDarkFactoryStepState::Pending && State.Steps[Previous]!=EDarkFactoryStepState::Skipped)
        State.Steps[Previous]=EDarkFactoryStepState::Complete;
    bPhaseScreenshotTaken=false; State.Phase=Phase; PhaseStarted=State.ClockSeconds; State.StageSeconds=0; State.PhaseProgress=0;
    if (static_cast<int32>(Phase)<State.StepCount) State.Steps[static_cast<int32>(Phase)]=EDarkFactoryStepState::Active;
    State.ActiveGate=GateForPhase(Phase); State.ConsecutivePasses=0; State.ConsensusCode.Empty(); NextInspection=FPlatformTime::Seconds()+.4;
    if (State.ActiveGate>=0) {
        ProcessScene->RequestInspectionView(StageName(),0,State.ClockSeconds);
        State.Gates[State.ActiveGate]=bModelRequestsEnabled ? EDarkFactoryGateState::Inspecting:EDarkFactoryGateState::Waiting;
        State.Message=bModelRequestsEnabled ? TEXT("Moving R4 to inspect camera evidence"):TEXT("Inference disabled; holding at camera gate");
    } else if (Phase==EDarkFactoryProcessPhase::Arriving) State.Message=TEXT("Body entering panel assembly station");
    else if (Phase==EDarkFactoryProcessPhase::AdhesiveApplication) State.Message=TEXT("Panel absent: arm A applies sealant");
    else if (Phase==EDarkFactoryProcessPhase::PanelPlacement) State.Message=TEXT("Arm B picks up the panel and holds it in position");
    else if (Phase==EDarkFactoryProcessPhase::Welding) State.Message=TEXT("Adhesive readiness confirmed: arm C spot welds");
    else if (Phase==EDarkFactoryProcessPhase::HolderRetract) State.Message=TEXT("Welds cooled: arm B releases and retracts");
    else State.Message=TEXT("Required inspections passed: sending body forward");
    if (Agent && State.ActiveGate<0 && Phase!=EDarkFactoryProcessPhase::Arriving) State.Message=TEXT("Agent selected job: ")+Agent->Job;
    RecordEvent(TEXT("phase"));
}
void ADarkFactoryProcessController::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!bReady || !ProcessScene.IsValid() || !WeldingCell.IsValid()) return;
    PumpAgentHarness(); // Equipment heartbeat continues during pause and motion.
    if (bPanelMotionQA) { PanelMotionQATick(DeltaSeconds); return; }
    if (!SensorResourcesReady()) return;
    if (bCaptureQA) { CaptureQATick(); return; }
    StreamingQATick();
    // Only the overall QA run budget changes for edge inference. Per-request
    // deadlines, sensor age and every gate threshold remain unchanged.
    const double ModelQABudgetSeconds=ExpectedModelSource==TEXT("evk_hexagon") ? 480.:240.;
    if (bModelQA && !Agent && FPlatformTime::Seconds()-FMath::Max(SessionStarted,SensorResourcesReadyAt)>ModelQABudgetSeconds) {
        State.Message=TEXT("Model process smoke timed out"); RecordEvent(TEXT("timeout"));
        CancelInspection(); bReady=false; FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if (State.bPaused) return;
    State.ClockSeconds+=FMath::Clamp(double(DeltaSeconds),0.,.1);
    State.StageSeconds=State.ClockSeconds-PhaseStarted;
    double Offset=0.;
    const double Duration=ADarkFactoryProcessScene::GetPhaseDuration(State.Phase);
    State.PhaseProgress=Duration>0 ? FMath::Clamp(State.StageSeconds/Duration,0.,1.):0.;
    if (State.Phase==EDarkFactoryProcessPhase::Arriving)
        Offset=FMath::Lerp(-ADarkFactoryProcessScene::CarriageTravelCm,0.,double(FMath::SmoothStep(0.f,1.f,State.PhaseProgress)));
    else if (State.Phase==EDarkFactoryProcessPhase::Departing)
        Offset=FMath::Lerp(0.,ADarkFactoryProcessScene::CarriageTravelCm,double(FMath::SmoothStep(0.f,1.f,State.PhaseProgress)));
    WeldingCell->SetAnimationEnabled(true);
    ProcessScene->ApplyAssemblyFrame(State.Phase,State.StageSeconds,State.ClockSeconds,Offset,true);
    if (State.Phase==EDarkFactoryProcessPhase::Welding && DarkFactoryEnhancements::Enabled()) {
        State.Message=TEXT("Pinch gun moving between contacts");
        for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
            const double Age=State.StageSeconds-ADarkFactoryWeldingCell::PanelWeldStartSeconds(I);
            if (Age>=-.15 && Age<.70) State.Message=FString::Printf(TEXT("Spot %d / 6: %s"),I+1,
                Age<0 ? TEXT("electrode squeeze"):Age<.5 ? TEXT("current pulse"):Age<.58 ? TEXT("pressure hold"):TEXT("electrode release"));
        }
    }
    // Actions finish their authored motion before the next step receives a frame.
    // Camera gates never advance from a clock or scene ground-truth value.
    if (Agent) { AgentTick(); return; }
    if (State.ActiveGate>=0 || Duration<=0 || State.StageSeconds<Duration) return;
    switch (State.Phase) {
    case EDarkFactoryProcessPhase::Arriving: EnterPhase(EDarkFactoryProcessPhase::PanelPresence); break;
    case EDarkFactoryProcessPhase::AdhesiveApplication: EnterPhase(EDarkFactoryProcessPhase::PanelPlacement); break;
    case EDarkFactoryProcessPhase::PanelPlacement:
        State.bAssemblyPerformed=true; EnterPhase(EDarkFactoryProcessPhase::Adhesive); break;
    case EDarkFactoryProcessPhase::Welding: EnterPhase(EDarkFactoryProcessPhase::PanelSeated); break;
    case EDarkFactoryProcessPhase::HolderRetract: EnterPhase(EDarkFactoryProcessPhase::Departing); break;
    case EDarkFactoryProcessPhase::Departing:
        State.Steps[static_cast<int32>(State.Phase)]=EDarkFactoryStepState::Complete;
        ++State.PassedBodies; if (bModelQA) RecordModelSmokeOutcome();
        ProcessScene->EndBody(); ++CompletedBodies; RecordEvent(TEXT("body_completed"));
        if (bModelQA && CompletedBodies>=3) {
            const bool Saved=WriteReport(); bReady=false;
            FPlatformMisc::RequestExitWithStatus(false,Saved && HasPassedModelSmoke() ? 0:1); return;
        }
        BeginBody(); break;
    default: break;
    }
}
void ADarkFactoryProcessController::TogglePaused() {
    if (!bReady || bCaptureQA) return;
    State.bPaused=!State.bPaused; CancelInspection();
    if (Agent) {
        CancelAgentDecision();
        if (Agent->Job==TEXT("inspect")) {
            auto Interrupted=MakeShared<FJsonObject>(); Interrupted->SetStringField(TEXT("status"),TEXT("cancelled_by_pause"));
            CompleteAgentTool(Interrupted);
        }
        // Mechanical jobs remain paused in place; do not restart or duplicate
        // them. A canceled agent request will receive a new epoch on resume.
        Agent->Held=false; State.bAgentHeld=false; Agent->NextDecision=State.ClockSeconds;
    }
    CoolingRecovery.Reset();
    WeldingCell->SetAnimationEnabled(!State.bPaused);
    State.ConsecutivePasses=0; State.ConsensusCode.Empty();
    if (State.ActiveGate>=0) State.Gates[State.ActiveGate]=EDarkFactoryGateState::Waiting;
    State.Message=State.bPaused ? TEXT("Process paused; outstanding inspection discarded"):bModelRequestsEnabled ? TEXT("Resuming with fresh camera evidence"):TEXT("Inference disabled; holding at camera gate");
    NextInspection=FPlatformTime::Seconds()+.4; RecordEvent(TEXT("pause"));
}
void ADarkFactoryProcessController::CancelInspection() {
    CancelStream();
    for (auto& Receipt:InspectionReceipts) if (Receipt.Id==ActiveRequestId && !Receipt.bComplete) {
        Receipt.bComplete=true; Receipt.bValid=false; Receipt.Action=TEXT("Cancelled / discarded; no acceptance");
    }
    if (Request.IsValid()) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    for (const auto& Light:NIRLights) if (Light) Light->SetVisibility(false);
    Readback.Cancel(); bCaptureActive=false; State.bRequestPending=false; ActiveRequestId.Empty();
    BatchImages.Reset(); BatchInspectionViews.Reset(); PendingInspectionView.Reset(); CaptureSlot=-1; bCapturePoseReady=false;
    // Detached encoder owns only pixels. Poll it before starting any new work.
}
void ADarkFactoryProcessController::StartCapture() {
    if (State.bRequestPending || bCaptureActive || Request.IsValid() || Readback.IsBusy() || EncodeFuture.IsValid() || StageName().IsEmpty()) return;
    CaptureBodyId=State.BodyId; CaptureStage=StageName();
    ExpectedImages=3; BatchImages.Reset(); BatchInspectionViews.Reset(); CaptureSlot=0;
    bCaptureActive=true; State.bRequestPending=true; ++BatchSerial;
    ActiveRequestId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    bBatchStreaming=bStreamingAvailable && ExpectedModelSource==TEXT("evk_hexagon") && !bCaptureQA;
    StreamPreparedImages=0; StreamImageHashes.Reset(); bStreamCommitPending=false;
    BatchStarted=FPlatformTime::Seconds(); CapturedAtMs=BatchStarted*1000.;
    State.Gates[State.ActiveGate]=EDarkFactoryGateState::Inspecting;
    State.Steps[static_cast<int32>(State.Phase)]=EDarkFactoryStepState::Active;
    CaptureNext();
}
void ADarkFactoryProcessController::CaptureNext() {
    const bool NIR=IsNIRStage(CaptureStage);
    bCapturePoseReady=false;
    const int32 TargetIndex=CaptureSlot+(NIR ? 0:3);
    auto* Camera=Cameras[NIR ? 0:1].Get(); Camera->TextureTarget=Targets[TargetIndex];
    PreviewStamps[TargetIndex]=FPreviewStamp();
    if (auto* World=WorldOwner.Get()) {
        if (auto* Cabinet=World->GetControlCabinet()) if (auto* Glass=Cabinet->GetDisplaySurface()) Camera->HideComponent(Glass);
        if (auto* Mechanisms=World->GetCellMechanisms()) Mechanisms->HideProcessIndicatorsFrom(Camera);
    }
    ProcessScene->RequestInspectionView(CaptureStage,CaptureSlot,State.ClockSeconds,bCaptureQA);
    if (bCaptureQA) ProcessScene->ApplyAssemblyFrame(State.Phase,State.StageSeconds,State.ClockSeconds,0.,true);
    for (const auto& Light:NIRLights) if (Light) Light->SetVisibility(false);
    State.Message=FString::Printf(TEXT("Moving R4 to %s view %d / 3"),NIR ? TEXT("NIR"):TEXT("thermal"),CaptureSlot+1);
    SettleFrames=3; SensorWarmupCaptures=0;
}
void ADarkFactoryProcessController::CaptureTick() {
    if (!bReady) return;
    if (bPanelMotionQA) { PanelMotionQACaptureTick(); return; }
    if (!SensorResourcesReady()) return;
    const double Now=FPlatformTime::Seconds();
    // Expire before consuming readbacks/encoders: a completed third image must
    // not submit stale evidence and only then be canceled by the old tail check.
    if (!bCaptureQA && State.bRequestPending && Now-BatchStarted>=MaxCaptureAgeSeconds) {
        CancelInspection(); ApplyDecision(TEXT("A"),false,TEXT("Inspection timed out; holding for fresh evidence"),0,State.Backend);
    }
    if (bModelQA && !bPhaseScreenshotTaken && State.StageSeconds>(State.Phase==EDarkFactoryProcessPhase::Welding ? 7.:.6)) {
        bool CapturesReady=State.ActiveGate<0 || !State.bRequestPending;
        if (State.ActiveGate>=0) for (int32 I=0;I<3;++I) CapturesReady &= GetPreviewTexture(I+(IsNIRStage(StageName()) ? 0:3))!=nullptr;
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
            if (PendingInspectionView.IsValid()) {
                BatchInspectionViews.Add(MakeShared<FJsonValueObject>(PendingInspectionView));
                PendingInspectionView.Reset();
            }
            BatchImages.Add(MoveTemp(Bytes)); ++CaptureSlot;
            PumpStreamPreparation();
            if (bCaptureActive) { if (CaptureSlot>=ExpectedImages) SubmitBatch(); else CaptureNext(); }
        }
    }
    TArray<FColor> Pixels; uint64 Id=0;
    if (Readback.Poll(Pixels,Id)) {
        if (bCaptureActive && Id==CaptureSerial && Pixels.Num()==Width*Height && CaptureBodyId==State.BodyId && CaptureStage==StageName()) {
            // Validate the actual post-process output before grayscale PNG encoding.
            // A cold or missing blendable must never be hidden by CPU conversion.
            if (!HasGrayscaleSensorPixels(Pixels)) {
                CancelInspection();
                if (bCaptureQA) { bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_SOURCE_NOT_GRAYSCALE")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
                ApplyDecision(TEXT("A"),false,TEXT("Sensor post-process unavailable; holding for fresh grayscale evidence"),0,State.Backend);
                return;
            }
            const int32 Slot=CaptureSlot+(IsNIRStage(CaptureStage) ? 0:3);
            PreviewStamps[Slot].Body=CaptureBodyId; PreviewStamps[Slot].Stage=CaptureStage; PreviewStamps[Slot].At=Now;
            EncodeFuture=Async(EAsyncExecution::ThreadPool,[Pixels=MoveTemp(Pixels)]() mutable { return Encode(MoveTemp(Pixels)); });
        }
        else if (bCaptureActive && Pixels.IsEmpty()) {
            CancelInspection();
            if (bCaptureQA) { bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_READBACK_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            ApplyDecision(TEXT("A"),false,TEXT("GPU capture failed"),0,State.Backend);
        }
    }
    if (!bCaptureQA && bModelRequestsEnabled && !State.bPaused && State.ActiveGate>=0 && (!Agent || Agent->Job==TEXT("inspect")) && !bCaptureActive && !Request.IsValid() && Now>=NextInspection) StartCapture();
    if (bCaptureActive && !Readback.IsBusy() && !EncodeFuture.IsValid()) {
        const bool NIR=IsNIRStage(CaptureStage);
        const bool Settled=ProcessScene->IsInspectionViewSettled(CaptureSlot);
        if (!Settled) { bCapturePoseReady=false; SettleFrames=3; SensorWarmupCaptures=0; }
        else {
            auto* Camera=Cameras[NIR ? 0:1].Get();
            if (!bCapturePoseReady) {
                FVector Location; FRotator Rotation; float FOV=40;
                ProcessScene->GetInspectionCameraPose(CaptureStage,CaptureSlot,Location,Rotation,FOV);
                Camera->SetWorldLocationAndRotation(Location,Rotation); Camera->FOVAngle=FOV;
                if (NIR && NIRLights.Num()) { NIRLights[0]->SetWorldLocationAndRotation(Location,Rotation); NIRLights[0]->SetVisibility(true); }
                bCapturePoseReady=true;
                State.Message=FString::Printf(TEXT("Inspecting %s area %d / 3"),NIR ? TEXT("NIR"):TEXT("thermal"),CaptureSlot+1);
            }
            if (!NIR) ThermalRig->PrepareInspectionMaterial(CameraMaterials[1]);
            // These are real sensor renders, not observer-frame delays. Discard
            // three captures at the settled pose before publishing the fourth;
            // this also primes persistent ViewState on the first live/offline use.
            Camera->CaptureScene();
            if (SettleFrames>0) { --SettleFrames; ++SensorWarmupCaptures; }
            else if (!Readback.Enqueue(Camera->TextureTarget,++CaptureSerial)) {
                CancelInspection();
                if (bCaptureQA) { bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_ENQUEUE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
                ApplyDecision(TEXT("A"),false,TEXT("GPU readback busy"),0,State.Backend);
            } else RecordInspectionCapture(Camera);
        }
    }
    if (bCaptureQA && State.bRequestPending && Now-BatchStarted>30.) {
        CancelInspection(); bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_TIMEOUT")); FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
}
void ADarkFactoryProcessController::RecordInspectionCapture(USceneCaptureComponent2D* Camera) {
    // Private capture audit only. No poses, source temperatures or labels enter
    // the allowlisted model request constructed in SubmitBatch.
    auto View=MakeShared<FJsonObject>();
    auto Vec=[](const FVector& P) -> TArray<TSharedPtr<FJsonValue>> {
        return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
    };
    View->SetStringField(TEXT("body_id"),CaptureBodyId); View->SetStringField(TEXT("stage"),CaptureStage);
    View->SetNumberField(TEXT("body_sequence"),State.BodySequence); View->SetNumberField(TEXT("spot"),CaptureSlot);
    View->SetNumberField(TEXT("batch_serial"),BatchSerial);
    View->SetNumberField(TEXT("capture_serial"),CaptureSerial); View->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds);
    View->SetNumberField(TEXT("batch_age_seconds"),FPlatformTime::Seconds()-BatchStarted);
    View->SetArrayField(TEXT("location_cm"),Vec(Camera->GetComponentLocation()));
    const FRotator R=Camera->GetComponentRotation(); View->SetArrayField(TEXT("rotation_deg"),Vec(FVector(R.Pitch,R.Yaw,R.Roll)));
    View->SetNumberField(TEXT("fov_deg"),Camera->FOVAngle);
    View->SetNumberField(TEXT("settle_frames_remaining"),SettleFrames);
    View->SetNumberField(TEXT("sensor_warmup_captures"),SensorWarmupCaptures);
    View->SetBoolField(TEXT("arm_settled"),ProcessScene->IsInspectionViewSettled(CaptureSlot));
    FVector LensLocation; FRotator LensRotation; float LensFOV=0;
    ProcessScene->GetInspectionCameraPose(CaptureStage,CaptureSlot,LensLocation,LensRotation,LensFOV);
    View->SetArrayField(TEXT("lens_location_cm"),Vec(LensLocation));
    View->SetArrayField(TEXT("lens_rotation_deg"),Vec(FVector(LensRotation.Pitch,LensRotation.Yaw,LensRotation.Roll)));
    TArray<FVector> Corners; ProcessScene->GetInspectionFramingCorners(CaptureStage,CaptureSlot,Corners);
    TArray<TSharedPtr<FJsonValue>> ViewCorners;
    for (const FVector& Corner:Corners)
        ViewCorners.Add(MakeShared<FJsonValueArray>(Vec(Camera->GetComponentRotation().UnrotateVector(Corner-Camera->GetComponentLocation()))));
    View->SetArrayField(TEXT("framing_corners_view_cm"),ViewCorners);
    if (CaptureStage==TEXT("adhesive")) {
        TArray<FVector> SurfaceSamples; ProcessScene->GetSealantSurfaceSamples(CaptureSlot,SurfaceSamples);
        TArray<TSharedPtr<FJsonValue>> Projected;
        for (const FVector& Point:SurfaceSamples) Projected.Add(MakeShared<FJsonValueArray>(Vec(Camera->GetComponentRotation().UnrotateVector(Point-Camera->GetComponentLocation()))));
        View->SetArrayField(TEXT("sealant_surface_samples_view_cm"),Projected);
    }
    View->SetObjectField(TEXT("arm"),WeldingCell->GetInspectionScanMetadata());
    PendingInspectionView=View; // Publish audit only when this same PNG commits.
    if (bModelQA) {
        const FString Key=FString::Printf(TEXT("body-%02d-%s-arm-%d"),State.BodySequence,*CaptureStage,CaptureSlot+1);
        if (!SavedInspectionScreenshots.Contains(Key)) {
            SavedInspectionScreenshots.Add(Key);
            const FString Name=FPaths::Combine(OutputRoot,TEXT("screenshots"),SessionId,Key+TEXT(".png"));
            FScreenshotRequest::RequestScreenshot(Name,true,false);
        }
    }
}
void ADarkFactoryProcessController::SubmitBatch() {
    // Recheck at the publication boundary too, in case this frame's work crosses
    // the age limit after CaptureTick sampled its starting time.
    if (!bCaptureQA && FPlatformTime::Seconds()-BatchStarted>=MaxCaptureAgeSeconds) {
        CancelInspection(); ApplyDecision(TEXT("A"),false,TEXT("Completed camera batch expired; holding for fresh evidence"),0,State.Backend); return;
    }
    bCaptureActive=false;
    for (const auto& Light:NIRLights) if (Light) Light->SetVisibility(false);
    if (bBatchStreaming && StreamPreparedImages<3) { bStreamCommitPending=true; return; }
    bStreamCommitPending=false;
    // Keep complete ordered triplets in runtime evidence. A canceled partial
    // scan never survives in this history or leaks into a later model batch.
    if (BatchInspectionViews.Num()==3) {
        InspectionCaptures.Append(BatchInspectionViews);
        while (InspectionCaptures.Num()>192) InspectionCaptures.RemoveAt(0,3);
    }
    if (bCaptureQA) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("sample_id"),CaptureSampleId);
        Row->SetStringField(TEXT("body_id"),State.BodyId); Row->SetStringField(TEXT("stage"),CaptureStage);
        Row->SetStringField(TEXT("contract_version"),ContractVersion); Row->SetStringField(TEXT("prompt_sha256"),PromptHashes[CaptureStage]);
        TArray<TSharedPtr<FJsonValue>> Images;
        for (int32 I=0;I<BatchImages.Num();++I) {
            const FString Camera=CameraIds[I];
            const FString Name=CaptureSampleId+TEXT("_")+Camera+TEXT(".png");
            if (!FFileHelper::SaveArrayToFile(BatchImages[I],*FPaths::Combine(OutputRoot,Name))) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
            auto Image=MakeShared<FJsonObject>(); Image->SetStringField(TEXT("camera_id"),Camera); Image->SetStringField(TEXT("path"),Name);
            Images.Add(MakeShared<FJsonValueObject>(Image));
        }
        Row->SetArrayField(TEXT("images"),Images);
        Row->SetStringField(TEXT("view_recipe"),TEXT("rigid-arm-panel-three-edge-scan-v6"));
        Row->SetStringField(TEXT("thermal_recipe"),TEXT("panel-three-edge-contact-transfer-v8"));
        Row->SetArrayField(TEXT("inspection_views"),BatchInspectionViews);
        auto Truth=ProcessScene->BuildMetadata(); Row->SetObjectField(TEXT("ground_truth"),Truth);
        FString Expected;
        if (!Truth->TryGetStringField(TEXT("capture_expected_decision"),Expected) ||
            (Expected!=TEXT("G") && Expected!=TEXT("A") && Expected!=TEXT("R"))) {
            bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PANEL_CAPTURE_MISSING_EXPECTED_LABEL")); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        Row->SetStringField(TEXT("expected_decision"),Expected);
        Row->SetNumberField(TEXT("nir_illuminator_power_scale"),CaptureNIRPowerScale);
        if (QAEpisodes>0) {
            const int32 Episode=CaptureCase/8;
            Row->SetStringField(TEXT("episode_id"),State.BodyId);
            Row->SetStringField(TEXT("split"),Episode<QAEpisodes*5/8 ? TEXT("train"):Episode<QAEpisodes*13/16 ? TEXT("validation"):TEXT("test"));
            Row->SetNumberField(TEXT("dataset_seed"),QASeed);
            Row->SetStringField(TEXT("capture_recipe"),TEXT("panel-three-edge-contact-episodes-v12"));
        }
        if (bSealantCoverageCapture) {
            Row->SetStringField(TEXT("contract_version"),TEXT("automotive-sealant-coverage-study-v1"));
            Row->SetStringField(TEXT("view_recipe"),TEXT("rigid-arm-preplacement-sealant-nir-v1"));
            Row->SetStringField(TEXT("capture_recipe"),TEXT("sealant-coverage-episodes-v1"));
            Row->SetStringField(TEXT("use"),TEXT("offline_experiment_only"));
            Row->RemoveField(TEXT("thermal_recipe"));
        }
        const FString Line=Json(Row)+LINE_TERMINATOR;
        if (!FFileHelper::SaveStringToFile(Line,*FPaths::Combine(OutputRoot,TEXT("manifest.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append)) {
            bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_CAPTURE_MANIFEST_WRITE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_CAPTURE case=%d stage=%s expected=%s images=%d"),CaptureCase,*CaptureStage,*Expected,BatchImages.Num());
        State.bRequestPending=false; BatchImages.Reset(); bCasePrepared=false; return;
    }
    auto Payload=MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("contract_version"),ContractVersion); Payload->SetStringField(TEXT("request_id"),ActiveRequestId);
    Payload->SetStringField(TEXT("body_id"),CaptureBodyId); Payload->SetStringField(TEXT("stage"),CaptureStage);
    Payload->SetNumberField(TEXT("captured_at_ms"),CapturedAtMs);
    TArray<TSharedPtr<FJsonValue>> Images;
    for (int32 I=0;I<BatchImages.Num();++I) {
        auto Image=MakeShared<FJsonObject>(); Image->SetStringField(TEXT("camera_id"),CameraIds[I]);
        Image->SetStringField(TEXT("png_base64"),FBase64::Encode(BatchImages[I])); Images.Add(MakeShared<FJsonValueObject>(Image));
    }
    Payload->SetArrayField(TEXT("images"),Images); RetainInspectionReceipt(); BatchImages.Reset();
    // Bounded QA-only replay evidence: four latest image-only requests. No
    // labels/temperatures are added, and interactive production does no disk IO.
    if (bModelQA) FFileHelper::SaveStringToFile(Json(Payload),*FPaths::Combine(OutputRoot,TEXT("qa_latest_")+CaptureStage+TEXT("_request.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

    Request=FHttpModule::Get().CreateRequest(); Request->SetURL(Endpoint); Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"),TEXT("application/json")); Request->SetContentAsString(Json(Payload)); Request->SetTimeout(ResponseDeadlineSeconds);
    if (bBatchStreaming) {
        Request->SetHeader(TEXT("X-DF-Stream-Session"),SessionId);
        Request->SetHeader(TEXT("X-DF-Stream-Epoch"),FString::Printf(TEXT("%llu"),BatchSerial));
        StreamEvent(TEXT("submit"));
    }
    RequestStarted=FPlatformTime::Seconds(); const FString Id=ActiveRequestId,Body=CaptureBodyId,Stage=CaptureStage; const double Stamp=CapturedAtMs;
    TWeakObjectPtr<ADarkFactoryProcessController> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda([WeakThis,Id,Body,Stage,Stamp](FHttpRequestPtr,FHttpResponsePtr Response,bool Success) {
        auto* Self=WeakThis.Get(); if (!Self || Self->ActiveRequestId!=Id || Self->State.BodyId!=Body || Self->StageName()!=Stage || Self->State.bPaused) return;
        Self->Request.Reset(); Self->State.bRequestPending=false;
        FString Decision=TEXT("A"),Reason=TEXT("Model connection unavailable"),Backend=Self->ExpectedModelSource==TEXT("evk_hexagon") ? TEXT("Reason2 IQ9 EVK"):TEXT("Reason2 host GPU"); bool Valid=false;
        const double Age=FPlatformTime::Seconds()-Self->BatchStarted;
        float Latency=(FPlatformTime::Seconds()-Self->RequestStarted)*1000.f;
        double Confidence=-1,Mass=-1; bool Bound=false;
        if (Success && Response.IsValid() && Response->GetResponseCode()==200 && Response->GetContentLength()<65536 && Age<Self->MaxCaptureAgeSeconds) {
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
                bool CamerasMatch=Result->TryGetArrayField(TEXT("camera_ids"),ReplyCameras) && ReplyCameras && ReplyCameras->Num()==3;
                if (CamerasMatch) for (int32 I=0;I<ReplyCameras->Num();++I) {
                    FString CameraId; CamerasMatch &= (*ReplyCameras)[I]->TryGetString(CameraId) && CameraId==CameraIds[I];
                }
                const bool Code=Decision==TEXT("G") || Decision==TEXT("A") || Decision==TEXT("R");
                Valid=ClaimedValid && Code && ScoringKnown && CamerasMatch && MessageLayout==TEXT("independent-camera-images-newline-v1") && Modality==(IsNIRStage(Stage) ? TEXT("nir"):TEXT("thermal")) && ReplyId==Id && ReplyBody==Body && ReplyStage==Stage && ReplyContract==Self->ContractVersion &&
                    Hash==Self->PromptHashes[Stage] && FMath::Abs(Captured-Stamp)<.01 && Source==Self->ExpectedModelSource;
                const TSharedPtr<FJsonObject>* Policy=nullptr;
                bool PolicyMatches=Result->TryGetObjectField(TEXT("decision_policy"),Policy) && Policy && Policy->IsValid();
                if (PolicyMatches) {
                    const TCHAR* Keys[]={TEXT("minimum_label_mass"),TEXT("minimum_conditional_probability"),TEXT("consecutive_green"),TEXT("max_reply_age_seconds")};
                    const double Expected[]={Self->MinimumLabelMass,Self->MinimumConditionalProbability,double(Self->State.RequiredConsensusBatches),Self->MaxCaptureAgeSeconds};
                    for (int32 I=0;I<4;++I) { double Value=0; PolicyMatches &= (*Policy)->TryGetNumberField(Keys[I],Value) && FMath::IsFinite(Value) && FMath::IsNearlyEqual(Value,Expected[I],1.e-7); }
                }
                Valid &= PolicyMatches;
                if (Self->bBatchStreaming) Valid &= Self->ValidateStreamReply(Result);
                if (Valid) {
                    const TSharedPtr<FJsonObject>* Scores=nullptr;
                    Bound=ScoringMethod==TEXT("gar-unmasked-topk-intervals-v1");
                    if (Result->TryGetObjectField(Bound ? TEXT("conditional_label_lower_bounds"):TEXT("conditional_label_scores"),Scores) && Scores)
                        (*Scores)->TryGetNumberField(Decision,Confidence);
                    Result->TryGetNumberField(Bound ? TEXT("label_mass_lower_bound"):TEXT("label_mass"),Mass);
                    if (!FMath::IsFinite(Confidence) || Confidence<0 || Confidence>1) Confidence=-1;
                    if (!FMath::IsFinite(Mass) || Mass<0 || Mass>1) Mass=-1;
                }
                if (Valid) {
                    Self->LastModel.Empty(); Self->LastAdapter.Empty(); Self->LastBackendAlias.Empty(); Self->LastScoringMethod=ScoringMethod;
                    Result->TryGetStringField(TEXT("backend_alias"),Self->LastBackendAlias);
                    Result->TryGetStringField(TEXT("model"),Self->LastModel);
                    Result->TryGetStringField(TEXT("adapter"),Self->LastAdapter);
                    Backend=Self->ExpectedModelSource==TEXT("evk_hexagon") ? TEXT("Reason2 IQ9 EVK / LoRA experiment"):(Self->LastAdapter.IsEmpty() ? TEXT("Reason2 host GPU / base experiment"):TEXT("Reason2 host GPU / LoRA experiment"));
                }
                if (!Valid) { Decision=TEXT("A"); Reason=TEXT("Invalid or mismatched model reply; holding"); }
            }
        } else if (Age>=Self->MaxCaptureAgeSeconds) Reason=TEXT("Stale model result discarded");
        if (Self->bBatchStreaming) {
            auto Detail=MakeShared<FJsonObject>(); Detail->SetBoolField(TEXT("valid"),Valid);
            Detail->SetNumberField(TEXT("final_http_ms"),Latency); Detail->SetNumberField(TEXT("capture_to_reply_ms"),Age*1000.);
            Self->StreamEvent(TEXT("complete"),Detail);
            if (Valid) { ++Self->StreamingCompletedBatches; Self->bBatchStreaming=false; }
            else Self->CancelStream();
        }
        Self->ActiveRequestId.Empty(); Self->ApplyDecision(Decision,Valid,Reason,Latency,Backend);
        Self->CompleteInspectionReceipt(Id,Valid,Decision,Latency,Confidence,Mass,Bound);
    });
    if (Request->ProcessRequest()) ++ModelRequestsSubmitted;
    else { CancelInspection(); ApplyDecision(TEXT("A"),false,TEXT("Cannot start model request"),0,State.Backend);
        CompleteInspectionReceipt(Id,false,TEXT("A"),0,-1,-1,false); ActiveRequestId.Empty(); }
}
void ADarkFactoryProcessController::ApplyDecision(const FString& Code,bool Valid,const FString& Reason,float Latency,const FString& Backend) {
    if (State.ActiveGate<0 || State.bPaused) return;
    Valid &= Code==TEXT("G") || Code==TEXT("A") || Code==TEXT("R");
    if (bModelQA) {
        // Bounded private observation AFTER inference. Never used by policy or sent to the model.
        auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("body_sequence"),State.BodySequence);
        Row->SetStringField(TEXT("stage"),StageName()); Row->SetStringField(TEXT("decision"),Valid ? Code:TEXT("A"));
        Row->SetBoolField(TEXT("valid"),Valid); Row->SetStringField(TEXT("reason"),Reason);
        Row->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds); Row->SetNumberField(TEXT("latency_ms"),Latency);
        Row->SetNumberField(TEXT("request_count"),static_cast<double>(ModelRequestsSubmitted));
        if (State.ActiveGate==1 || State.ActiveGate==3) {
            const double Maximum=State.ActiveGate==1 ? ProcessScene->GetMaximumSealantContactApparentC():DarkFactoryThermal::ApparentTemperatureC(ProcessScene->GetMaximumWeldC(),.74f,20.f);
            Row->SetNumberField(TEXT("maximum_apparent_c_at_reply"),Maximum);
        }
        ModelSmokeDecisions.Add(MakeShared<FJsonValueObject>(Row)); if (ModelSmokeDecisions.Num()>256) ModelSmokeDecisions.RemoveAt(0);
    }
    if (Agent) { State.Backend=Backend; AgentInspectionResult(Code,Valid,Reason,Latency); return; }
    if (CoolingRecovery.Observe(Valid,Code,State.Phase==EDarkFactoryProcessPhase::Adhesive)
        && ProcessScene->StartRecoveryCooling()) RecordEvent(TEXT("model_rejections_activated_local_cooling"));
    State.Decision=Valid ? Code:TEXT("A"); State.LastLatencyMs=Latency; State.Backend=Backend;
    State.bRequestPending=false; NextInspection=FPlatformTime::Seconds()+.75;
    const int32 Step=static_cast<int32>(State.Phase);
    const bool Presence=State.Phase==EDarkFactoryProcessPhase::PanelPresence;
    const bool BranchEvidence=Presence && (Code==TEXT("G") || Code==TEXT("R"));
    if (!Valid) {
        State.ConsecutivePasses=0; State.ConsensusCode.Empty();
        State.Gates[State.ActiveGate]=EDarkFactoryGateState::Error; State.Steps[Step]=EDarkFactoryStepState::Exception; State.Message=Reason;
    } else if (Code==TEXT("G") || BranchEvidence) {
        if (State.ConsensusCode!=Code) State.ConsecutivePasses=0;
        State.ConsensusCode=Code; ++State.ConsecutivePasses;
        State.Gates[State.ActiveGate]=EDarkFactoryGateState::Inspecting; State.Steps[Step]=EDarkFactoryStepState::Active;
        State.Message=FString::Printf(TEXT("Fresh %s evidence %d / %d"),*Code,State.ConsecutivePasses,State.RequiredConsensusBatches);
        if (State.ConsecutivePasses>=State.RequiredConsensusBatches) {
            if (Presence) {
                State.bBranchKnown=true; State.bPreassembled=Code==TEXT("G");
                State.Gates[0]=State.bPreassembled ? EDarkFactoryGateState::Passed:EDarkFactoryGateState::BranchChosen;
                if (State.bPreassembled) {
                    for (const auto Skipped:{EDarkFactoryProcessPhase::AdhesiveApplication,EDarkFactoryProcessPhase::PanelPlacement,
                        EDarkFactoryProcessPhase::Welding,EDarkFactoryProcessPhase::PanelSeated,EDarkFactoryProcessPhase::HolderRetract})
                        State.Steps[static_cast<int32>(Skipped)]=EDarkFactoryStepState::Skipped;
                    State.Gates[2]=EDarkFactoryGateState::Skipped;
                }
                RecordEvent(State.bPreassembled ? TEXT("preassembled_branch_selected"):TEXT("assembly_branch_selected"));
                EnterPhase(State.bPreassembled ? EDarkFactoryProcessPhase::Adhesive:EDarkFactoryProcessPhase::AdhesiveApplication); return;
            }
            if (bModelQA && (State.ActiveGate==1 || State.ActiveGate==3)) RecordModelSmokeThermalGate();
            State.Gates[State.ActiveGate]=EDarkFactoryGateState::Passed; RecordEvent(TEXT("gate_passed"));
            if (State.Phase==EDarkFactoryProcessPhase::Adhesive) {
                if (!State.bPreassembled) State.bWeldAuthorized=true;
                EnterPhase(State.bPreassembled ? EDarkFactoryProcessPhase::WeldCooling:EDarkFactoryProcessPhase::Welding);
            } else if (State.Phase==EDarkFactoryProcessPhase::PanelSeated) EnterPhase(EDarkFactoryProcessPhase::WeldCooling);
            else EnterPhase(State.bPreassembled ? EDarkFactoryProcessPhase::Departing:EDarkFactoryProcessPhase::HolderRetract);
            return;
        }
    } else {
        State.ConsecutivePasses=0; State.ConsensusCode.Empty();
        const bool SeatingException=Code==TEXT("R") && State.Phase==EDarkFactoryProcessPhase::PanelSeated;
        State.Gates[State.ActiveGate]=SeatingException ? EDarkFactoryGateState::Rejected:EDarkFactoryGateState::Waiting;
        State.Steps[Step]=SeatingException ? EDarkFactoryStepState::Exception:EDarkFactoryStepState::Hold;
        State.Message=SeatingException ? TEXT("Panel seating exception; holder remains engaged and motion is held"):
            Code==TEXT("R") ? TEXT("Thermal area remains hot; holding and reinspecting"):TEXT("Insufficient visual evidence; holding and reinspecting");
        if (ProcessScene->IsRecoveryCooling()) State.Message=TEXT("Local cooling active after model rejection; holding for one fresh G batch");
    }
    RecordEvent(TEXT("decision"));
}
bool ADarkFactoryProcessController::IsModelSmokeOutcomeCorrect(int32 BodySequence,bool bDeparted,bool bPreassembled,bool bAssembled,bool bWelded) {
    const bool ExpectedPreassembled=BodySequence==2;
    return BodySequence>=1 && BodySequence<=3 && bDeparted && bPreassembled==ExpectedPreassembled
        && bAssembled==!ExpectedPreassembled && bWelded==!ExpectedPreassembled;
}
bool ADarkFactoryProcessController::HasPassedModelSmoke() const {
    return bModelQA && CompletedBodies==3 && ModelSmokeOutcomes.Num()==3 && bModelSmokeOutcomesMatch
        && ModelSmokeThermalChecks.Num()==6 && bModelSmokeThermalLimitsMatch && State.PassedBodies==3;
}
void ADarkFactoryProcessController::RecordModelSmokeThermalGate() {
    // QA observes the accepted action; it never overrides it or enters the model request.
    const bool Adhesive=State.ActiveGate==1;
    const double Maximum=Adhesive ? ProcessScene->GetMaximumSealantContactApparentC():DarkFactoryThermal::ApparentTemperatureC(ProcessScene->GetMaximumWeldC(),.74f,20.f);
    const double Limit=Adhesive ? 45.:55.;
    const bool Ready=FMath::IsFinite(Maximum) && Maximum<Limit;
    bModelSmokeThermalLimitsMatch &= Ready;
    auto Check=MakeShared<FJsonObject>(); Check->SetStringField(TEXT("body_id"),State.BodyId);
    Check->SetNumberField(TEXT("body_sequence"),State.BodySequence); Check->SetStringField(TEXT("stage"),StageName());
    Check->SetNumberField(TEXT("maximum_apparent_c_at_acceptance"),Maximum); Check->SetNumberField(TEXT("limit_c"),Limit);
    Check->SetNumberField(TEXT("clear_ready_limit_c"),Limit-3.);
    Check->SetBoolField(TEXT("clear_ready_at_acceptance"),FMath::IsFinite(Maximum) && Maximum<Limit-3.);
    Check->SetBoolField(TEXT("ready_at_acceptance"),Ready); ModelSmokeThermalChecks.Add(MakeShared<FJsonValueObject>(Check));
}
void ADarkFactoryProcessController::RecordModelSmokeOutcome() {
    // Fixture expectations audit the accepted branch, never drive model inputs or live decisions.
    const bool Departed=State.Phase==EDarkFactoryProcessPhase::Departing;
    const bool Correct=State.bBranchKnown && IsModelSmokeOutcomeCorrect(State.BodySequence,Departed,
        State.bPreassembled,State.bAssemblyPerformed,State.bWeldAuthorized)
        && State.Gates[1]==EDarkFactoryGateState::Passed && State.Gates[3]==EDarkFactoryGateState::Passed
        && State.Gates[2]==(State.bPreassembled ? EDarkFactoryGateState::Skipped:EDarkFactoryGateState::Passed);
    bModelSmokeOutcomesMatch &= Correct;
    auto Outcome=MakeShared<FJsonObject>(); Outcome->SetStringField(TEXT("body_id"),State.BodyId);
    Outcome->SetNumberField(TEXT("body_sequence"),State.BodySequence);
    Outcome->SetStringField(TEXT("expected_destination"),TEXT("exit"));
    Outcome->SetStringField(TEXT("actual_destination"),Departed ? TEXT("exit"):TEXT("held"));
    Outcome->SetStringField(TEXT("expected_branch"),State.BodySequence==2 ? TEXT("preassembled"):TEXT("assembly"));
    Outcome->SetStringField(TEXT("actual_branch"),State.bPreassembled ? TEXT("preassembled"):TEXT("assembly"));
    Outcome->SetBoolField(TEXT("assembly_performed"),State.bAssemblyPerformed); Outcome->SetBoolField(TEXT("welding_authorized"),State.bWeldAuthorized);
    Outcome->SetBoolField(TEXT("correct"),Correct); ModelSmokeOutcomes.Add(MakeShared<FJsonValueObject>(Outcome));
}
void ADarkFactoryProcessController::RecordEvent(const FString& Name) {
    auto Event=MakeShared<FJsonObject>(); Event->SetStringField(TEXT("event"),Name); Event->SetStringField(TEXT("body_id"),State.BodyId);
    Event->SetNumberField(TEXT("body_sequence"),State.BodySequence); Event->SetStringField(TEXT("phase"),PhaseName(State.Phase));
    Event->SetStringField(TEXT("decision"),State.Decision); Event->SetStringField(TEXT("message"),State.Message);
    Event->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds); Event->SetNumberField(TEXT("latency_ms"),State.LastLatencyMs);
    Event->SetStringField(TEXT("model"),LastModel); Event->SetStringField(TEXT("adapter"),LastAdapter);
    Event->SetStringField(TEXT("scoring_method"),LastScoringMethod); Event->SetStringField(TEXT("backend_alias"),LastBackendAlias);
    Event->SetStringField(TEXT("model_source"),ExpectedModelSource);
    Events.Add(MakeShared<FJsonValueObject>(Event)); if (Events.Num()>256) Events.RemoveAt(0);
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_EVENT %s"),*Json(Event)); WriteReport();
}
TSharedRef<FJsonObject> ADarkFactoryProcessController::BuildMetadata() const {
    auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("contract_version"),ContractVersion);
    Report->SetStringField(TEXT("session_id"),SessionId);
    if (bModelQA) Report->SetStringField(TEXT("screenshot_directory"),FPaths::Combine(OutputRoot,TEXT("screenshots"),SessionId));
    Report->SetStringField(TEXT("source"),bPanelMotionQA ? TEXT("physical_qa"):bPolicyQA ? TEXT("policy_test"):bCaptureQA ? TEXT("offline_capture"):bModelRequestsEnabled ? ExpectedModelSource:TEXT("disabled"));
    Report->SetStringField(TEXT("body_id"),State.BodyId); Report->SetStringField(TEXT("phase"),PhaseName(State.Phase));
    Report->SetStringField(TEXT("endpoint"),Endpoint); Report->SetStringField(TEXT("model"),LastModel); Report->SetStringField(TEXT("adapter"),LastAdapter);
    auto Switching=MakeShared<FJsonObject>(); Switching->SetBoolField(TEXT("enabled"),bBackendSwitchEnabled);
    Switching->SetStringField(TEXT("selected"),ExpectedModelSource); Switching->SetNumberField(TEXT("switches"),BackendSwitches);
    Switching->SetBoolField(TEXT("smoke"),bBackendSwitchSmoke);
    Switching->SetBoolField(TEXT("evk_agent_npu"),bEvkAgentNpu); Report->SetObjectField(TEXT("backend_switch"),Switching);
    auto Streaming=MakeShared<FJsonObject>(); Streaming->SetBoolField(TEXT("available"),bStreamingAvailable);
    Streaming->SetNumberField(TEXT("completed_batches"),StreamingCompletedBatches);
    Streaming->SetNumberField(TEXT("cancelled_batches"),StreamingCancelledBatches);
    Streaming->SetArrayField(TEXT("events"),StreamingEvents); Report->SetObjectField(TEXT("streaming"),Streaming);
    Streaming->SetNumberField(TEXT("qa_step"),StreamingQAStep);
    Report->SetStringField(TEXT("scoring_method"),LastScoringMethod); Report->SetStringField(TEXT("backend_alias"),LastBackendAlias);
    Report->SetStringField(TEXT("message_layout"),TEXT("independent-camera-images-newline-v1"));
    auto Hashes=MakeShared<FJsonObject>(); for (const auto& Pair:PromptHashes) Hashes->SetStringField(Pair.Key,Pair.Value); Report->SetObjectField(TEXT("prompt_hashes"),Hashes);
    auto Policy=MakeShared<FJsonObject>(); Policy->SetNumberField(TEXT("minimum_label_mass"),MinimumLabelMass); Policy->SetNumberField(TEXT("minimum_conditional_probability"),MinimumConditionalProbability);
    Policy->SetNumberField(TEXT("consecutive_green"),State.RequiredConsensusBatches); Policy->SetNumberField(TEXT("max_reply_age_seconds"),MaxCaptureAgeSeconds); Report->SetObjectField(TEXT("decision_policy"),Policy);
    auto Execution=MakeShared<FJsonObject>(); Execution->SetNumberField(TEXT("required_consensus_batches"),State.RequiredConsensusBatches);
    Report->SetObjectField(TEXT("execution_policy"),Execution);
    Report->SetStringField(TEXT("decision"),State.Decision); Report->SetStringField(TEXT("message"),State.Message);
    Report->SetBoolField(TEXT("paused"),State.bPaused); Report->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds);
    Report->SetNumberField(TEXT("passed_bodies"),State.PassedBodies);
    Report->SetBoolField(TEXT("branch_known"),State.bBranchKnown); Report->SetBoolField(TEXT("preassembled_branch"),State.bPreassembled);
    Report->SetBoolField(TEXT("assembly_performed"),State.bAssemblyPerformed); Report->SetBoolField(TEXT("welding_authorized"),State.bWeldAuthorized);
    TArray<TSharedPtr<FJsonValue>> Gates,Steps;
    for (const auto Gate:State.Gates) Gates.Add(MakeShared<FJsonValueNumber>(static_cast<int32>(Gate)));
    for (const auto Step:State.Steps) Steps.Add(MakeShared<FJsonValueNumber>(static_cast<int32>(Step)));
    Report->SetArrayField(TEXT("gates"),Gates); Report->SetArrayField(TEXT("steps"),Steps);
    Report->SetNumberField(TEXT("body_sequence"),State.BodySequence); Report->SetArrayField(TEXT("events"),Events);
    Report->SetBoolField(TEXT("model_smoke"),bModelQA); Report->SetNumberField(TEXT("completed_bodies"),CompletedBodies);
    Report->SetBoolField(TEXT("passed"),HasPassedModelSmoke());
    if (bModelQA) {
        Report->SetArrayField(TEXT("body_outcomes"),ModelSmokeOutcomes); Report->SetBoolField(TEXT("body_outcomes_correct"),bModelSmokeOutcomesMatch);
        Report->SetArrayField(TEXT("model_qa_decisions"),ModelSmokeDecisions);
        Report->SetArrayField(TEXT("thermal_gate_checks"),ModelSmokeThermalChecks); Report->SetBoolField(TEXT("thermal_limits_respected"),bModelSmokeThermalLimitsMatch);
        // Private QA telemetry only; never part of an inspection request or verdict.
        TArray<TSharedPtr<FJsonValue>> Portals; Portals.Reserve(2);
        if (UWorld* World=GetWorld()) for (TActorIterator<ADarkFactoryCurtainPortal> It(World); It && Portals.Num()<2; ++It)
            Portals.Add(MakeShared<FJsonValueObject>(It->BuildPhysicsMetadata()));
        Report->SetArrayField(TEXT("curtain_portals"),Portals);
    }
    Report->SetStringField(TEXT("view_recipe"),TEXT("rigid-arm-panel-three-edge-scan-v6"));
    Report->SetArrayField(TEXT("inspection_captures"),InspectionCaptures);
    if (WeldingCell.IsValid()) Report->SetObjectField(TEXT("inspection_scan"),WeldingCell->GetInspectionScanMetadata());
    Report->SetNumberField(TEXT("model_http_requests"),static_cast<double>(ModelRequestsSubmitted));
    TArray<TSharedPtr<FJsonValue>> Receipts;
    for (const auto& Receipt:InspectionReceipts) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("request_id"),Receipt.Id);
        Row->SetStringField(TEXT("body_id"),Receipt.Body); Row->SetStringField(TEXT("stage"),Receipt.Stage);
        Row->SetStringField(TEXT("decision"),Receipt.Decision); Row->SetStringField(TEXT("action"),Receipt.Action);
        Row->SetBoolField(TEXT("valid"),Receipt.bValid); Row->SetBoolField(TEXT("complete"),Receipt.bComplete);
        Row->SetNumberField(TEXT("images"),Receipt.Images.Num()); Row->SetNumberField(TEXT("latency_ms"),Receipt.LatencyMs);
        Row->SetNumberField(TEXT("conditional_score"),Receipt.Confidence); Row->SetNumberField(TEXT("label_mass"),Receipt.LabelMass);
        Receipts.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("observer_inspection_receipts"),Receipts);
    Report->SetBoolField(TEXT("sensor_resources_ready"),bSensorResourcesReady);
    Report->SetNumberField(TEXT("sensor_startup_wait_seconds"),SensorResourcesReadyAt>0 ? SensorResourcesReadyAt-SessionStarted:0.);
    Report->SetNumberField(TEXT("ground_truth_fields_sent_to_model"),0); Report->SetNumberField(TEXT("legacy_bridge_requests"),0);
    AppendAgentMetadata(Report);
    return Report;
}
bool ADarkFactoryProcessController::WriteReport() const {
    const bool Saved=!OutputRoot.IsEmpty() && FFileHelper::SaveStringToFile(Json(BuildMetadata()),*FPaths::Combine(OutputRoot,TEXT("process_runtime.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!Saved) UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_REPORT_SAVE_FAILED %s"),*OutputRoot);
    return Saved;
}
void ADarkFactoryProcessController::EndPlay(const EEndPlayReason::Type Reason) {
    if (StreamHealthRequest) { StreamHealthRequest->OnProcessRequestComplete().Unbind(); StreamHealthRequest->CancelRequest(); StreamHealthRequest.Reset(); }
    CancelAgentDecision();
    CloseAgentHarness();
    CancelInspection();
    // Completed model QA already saved the live scene before shutdown. Portal
    // EndPlay may tear down constraints first; preserve that completed snapshot.
    if (!bModelQA || CompletedBodies<3) WriteReport();
    Super::EndPlay(Reason);
}
void ADarkFactoryProcessController::CaptureQATick() {
    if (bCasePrepared || bCaptureActive || Readback.IsBusy() || EncodeFuture.IsValid()) return;
    ++CaptureCase;
    if (CaptureCase>=QAEndCase) {
        UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_CAPTURE_COMPLETE samples=%d"),QAEndCase-QAStartCase);
        FPlatformMisc::RequestExit(false); bReady=false; return;
    }
    ProcessScene->EndBody(); State.ClockSeconds=0; State.StageSeconds=0;
    const int32 Episode=CaptureCase/8,Kind=CaptureCase%8;
    State.BodySequence=QASeed*1000+Episode+1;
    State.BodyId=FString::Printf(TEXT("dataset-%d-episode-%04d"),QASeed,Episode);
    CaptureSampleId=FString::Printf(TEXT("episode-%04d-case-%d"),Episode,Kind);
    ProcessScene->SetCaptureVariation(QASeed*4099+Episode);
    FRandomStream EpisodeIllumination(QASeed*4099+Episode);
    const float VariedPower=EpisodeIllumination.FRandRange(.8f,1.2f);
    // Include the actual live illuminator power, across every presence/seating
    // case (stride five is coprime to both case cycles), alongside randomization.
    CaptureNIRPowerScale=Episode%5==0 ? 1.f:VariedPower;
    for (const auto& Light:NIRLights) if (Light) Light->SetIntensity(14000.f*CaptureNIRPowerScale);
    ProcessScene->BeginBody(State.BodySequence,0,0);
    int32 CaseIndex=0;
    if (Kind==0) { State.Phase=EDarkFactoryProcessPhase::PanelPresence; CaseIndex=Episode%3; }
    else if (Kind<=3) { State.Phase=EDarkFactoryProcessPhase::Adhesive; CaseIndex=Kind-1; }
    else if (Kind==4) { State.Phase=EDarkFactoryProcessPhase::PanelSeated; CaseIndex=Episode%4; }
    else { State.Phase=EDarkFactoryProcessPhase::WeldCooling; CaseIndex=Kind-5; }
    State.ActiveGate=GateForPhase(State.Phase);
    if (bSealantCoverageCapture) { State.Phase=EDarkFactoryProcessPhase::PanelPresence; State.ActiveGate=0; }
    const bool Prepared=bSealantCoverageCapture
        ? ProcessScene->PrepareSealantCoverageCapture(Kind,QASeed*6151+Episode*71+Kind,State.ClockSeconds)
        : ProcessScene->PrepareCapture(StageName(),CaseIndex,QASeed*6151+Episode*71+Kind,State.ClockSeconds);
    if (!Prepared) {
        bReady=false; UE_LOG(LogTemp,Error,TEXT("DF_PANEL_CAPTURE_PREPARATION_FAILED case=%d"),CaptureCase);
        FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    WeldingCell->SetAnimationEnabled(false); bCasePrepared=true; StartCapture();
}
