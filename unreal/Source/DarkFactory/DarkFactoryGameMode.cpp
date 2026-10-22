#include "DarkFactoryGameMode.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryEditorPreview.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessController.h"
#include "DarkFactoryThermalRig.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Canvas.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/InputSettings.h"
#include "Materials/MaterialInstanceDynamic.h"

ADarkFactoryGameMode::ADarkFactoryGameMode() {
    DefaultPawnClass=ADarkFactoryPawn::StaticClass();
    HUDClass=ADarkFactoryHUD::StaticClass();
    PlayerControllerClass=ADarkFactoryPlayerController::StaticClass();
}
void ADarkFactoryPlayerController::BeginPlay() {
    Super::BeginPlay();
    // F11 belongs to the observer RT toggle. Also override older saved input
    // settings so the viewport cannot consume it as a fullscreen transition.
    GetMutableDefault<UInputSettings>()->bF11TogglesFullscreen=false;
    // Start the idle interval on the first usable frame, after scene loading.
    LastObserverInputSeconds=0;
}
bool ADarkFactoryPlayerController::InputKey(const FInputKeyEventArgs& Params) {
    const FKey Key=Params.Key;
    const bool Axis=Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D();
    // Ignore idle device reports and stick drift, not sustained intentional input.
    const bool Trigger=Key==EKeys::Gamepad_LeftTriggerAxis || Key==EKeys::Gamepad_RightTriggerAxis;
    const float Threshold=Key.IsGamepadKey() ? (Trigger ? .01f:.18f):.001f;
    const bool Meaningful=Axis ? FMath::Abs(Params.AmountDepressed)>Threshold
        : Params.Event==IE_Pressed || Params.Event==IE_Repeat || Params.Event==IE_Released;
    if (Meaningful) {
        LastObserverInputSeconds=FPlatformTime::Seconds();
        bGamepadHints=Key.IsGamepadKey();
    }
    if (!Axis) {
        if (Params.Event==IE_Pressed) HeldObserverKeys.Add(Key);
        else if (Params.Event==IE_Released) HeldObserverKeys.Remove(Key);
    }
    return Super::InputKey(Params);
}
void ADarkFactoryPlayerController::PlayerTick(float DeltaTime) {
    if (LastObserverInputSeconds==0) LastObserverInputSeconds=FPlatformTime::Seconds();
    Super::PlayerTick(DeltaTime);
    UpdateObserverWindow();
    // Focus loss flushes PlayerInput; do not leave a phantom held key behind.
    for (auto It=HeldObserverKeys.CreateIterator();It;++It)
        if (!IsInputKeyDown(*It)) It.RemoveCurrent();
    if (!HeldObserverKeys.IsEmpty()) LastObserverInputSeconds=FPlatformTime::Seconds();
    TickControlsSmoke();
}
float ADarkFactoryPlayerController::GetControlsReveal() const {
    if (LastObserverInputSeconds==0) return 0;
    const float T=FMath::Clamp(float((FPlatformTime::Seconds()-LastObserverInputSeconds-5.)/.3),0.f,1.f);
    return T*T*(3.f-2.f*T);
}
void ADarkFactoryGameMode::StartPlay() {
    // Editor preview actors never participate in runtime physics or sensor views.
    TArray<AActor*> PreviewActors;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (It->ActorHasTag(ADarkFactoryEditorPreview::GeneratedTag()) || It->IsA<ADarkFactoryEditorPreview>()) PreviewActors.Add(*It);
    for (AActor* Actor:PreviewActors) Actor->Destroy();
    Super::StartPlay();
    auto* Scene=GetWorld()->SpawnActorDeferred<ADarkFactoryWorld>(ADarkFactoryWorld::StaticClass(),FTransform::Identity);
    if (Scene) {
        Scene->bPanelMapDefaults=bPanelMapDefaults;
        Scene->FinishSpawning(FTransform::Identity);
    }
}
ADarkFactoryPanelGameMode::ADarkFactoryPanelGameMode() { bPanelMapDefaults=true; }
ADarkFactoryHUD::ADarkFactoryHUD() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
}
void ADarkFactoryHUD::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (LastFrameNumber==GFrameCounter) return;
    LastFrameNumber=GFrameCounter;
    const double Now=FPlatformTime::Seconds();
    const double FrameSeconds=Now-LastFrameTimeSeconds;
    if (LastFrameTimeSeconds>0 && FMath::IsFinite(FrameSeconds) && FrameSeconds>0) {
        // Smooth frame duration, then invert: averaging FPS itself biases high.
        const double Weight=1.-FMath::Exp(-FrameSeconds/.5);
        SmoothedFrameSeconds=FrameRateSamples>0 ? FMath::Lerp(SmoothedFrameSeconds,FrameSeconds,Weight):FrameSeconds;
        ++FrameRateSamples;
    }
    LastFrameTimeSeconds=Now;
}
TSharedRef<FJsonObject> ADarkFactoryHUD::GetThermalQARegions() const {
    auto Result=MakeShared<FJsonObject>();
    auto Add=[&](const TCHAR* Name,const FIntRect& Bounds) {
        if (Bounds.Width()<=0 || Bounds.Height()<=0) return;
        Result->SetArrayField(Name,{MakeShared<FJsonValueNumber>(Bounds.Min.X),MakeShared<FJsonValueNumber>(Bounds.Min.Y),
            MakeShared<FJsonValueNumber>(Bounds.Max.X),MakeShared<FJsonValueNumber>(Bounds.Max.Y)});
    };
    Add(TEXT("observer"),ThermalObserverQARegion);
    Add(TEXT("thermal_preview"),ThermalPreviewQARegion);
    return Result;
}
void ADarkFactoryHUD::InitializeThermalScales(UMaterialInterface* Parent) {
    // One 1 KiB linear scalar ramp, uploaded once. Reuse the sensor-preview GPU
    // palette exactly; no duplicated color stops, image readback or scene capture.
    ThermalScaleTexture=UTexture2D::CreateTransient(256,1,PF_B8G8R8A8);
    ThermalScaleTexture->SRGB=false;
    ThermalScaleTexture->Filter=TF_Bilinear;
    ThermalScaleTexture->AddressX=TA_Clamp; ThermalScaleTexture->AddressY=TA_Clamp;
    ThermalScaleTexture->NeverStream=true;
    auto& Mip=ThermalScaleTexture->GetPlatformData()->Mips[0];
    auto* Pixels=static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    for (int32 I=0;I<256;++I) Pixels[I]=FColor(I,I,I,255);
    Mip.BulkData.Unlock(); ThermalScaleTexture->UpdateResource();
    for (int32 Palette=0;Palette<3;++Palette) {
        auto* Material=UMaterialInstanceDynamic::Create(Parent,this);
        Material->SetTextureParameterValue(TEXT("SensorImage"),ThermalScaleTexture);
        Material->SetScalarParameterValue(TEXT("ThermalPalette"),Palette);
        ThermalScaleMaterials.Add(Material);
    }
}
void ADarkFactoryHUD::DrawThermalScale(int32 Palette,float MinC,float MaxC,float X,float Y,float Width,float Scale) {
    if (!ThermalScaleMaterials.IsValidIndex(Palette) || !FMath::IsFinite(MinC) || !FMath::IsFinite(MaxC) || MaxC<=MinC) return;
    const float Height=8.f*Scale;
    DrawRect(FLinearColor(.38f,.43f,.48f,1),X-Scale,Y-Scale,Width+2*Scale,Height+2*Scale);
    DrawMaterial(ThermalScaleMaterials[Palette],X,Y,Width,Height,.5f/256.f,0,255.f/256.f,1);
    for (int32 I=0;I<3;++I) {
        const float Fraction=I*.5f;
        const FString Value=FString::Printf(TEXT("%g"),FMath::Lerp(MinC,MaxC,Fraction))+(I==1 ? TEXT(" \u00b0C"):I==2 ? TEXT("+"):TEXT(""));
        float TextWidth=0,TextHeight=0;
        MeasureHUDText(Value,TextWidth,TextHeight,GEngine->GetSmallFont(),.85f*Scale);
        const float TickX=X+Width*Fraction;
        DrawRect(FLinearColor(.75f,.81f,.86f,1),TickX-Scale*.5f,Y+Height,Scale,3*Scale);
        DrawHUDText(Value,FLinearColor(.84f,.9f,.94f,1),TickX-TextWidth*Fraction,Y+Height+4*Scale,GEngine->GetSmallFont(),.85f*Scale);
    }
}
void ADarkFactoryHUD::DrawObserverThermalScale(const ADarkFactoryThermalRig* Thermal,float X,float Y,float Width,float Scale) {
    if (!Thermal) return;
    DrawRect(FLinearColor(.008f,.015f,.024f,.86f),X,Y,Width,50.f*Scale);
    DrawThermalScale(Thermal->DisplayPalette,Thermal->GetObserverMinC(),Thermal->GetObserverMaxC(),X+10*Scale,Y+9*Scale,Width-20*Scale,Scale);
}
namespace {
const TCHAR* GateLabel(EDarkFactoryGateState State) {
    switch (State) {
        case EDarkFactoryGateState::Pending: return TEXT("Pending");
        case EDarkFactoryGateState::Inspecting: return TEXT("Inspecting");
        case EDarkFactoryGateState::Waiting: return TEXT("Waiting / HOLD");
        case EDarkFactoryGateState::Passed: return TEXT("Passed");
        case EDarkFactoryGateState::Rejected: return TEXT("Exception / HOLD");
        case EDarkFactoryGateState::BranchChosen: return TEXT("Assembly selected");
        case EDarkFactoryGateState::Skipped: return TEXT("Skipped");
        case EDarkFactoryGateState::Error: return TEXT("Unavailable / HOLD");
    }
    return TEXT("Unknown / HOLD");
}
FLinearColor GateColor(EDarkFactoryGateState State) {
    switch (State) {
        case EDarkFactoryGateState::Inspecting:
        case EDarkFactoryGateState::Waiting: return FLinearColor(1.f,.68f,.22f);
        case EDarkFactoryGateState::Passed: return FLinearColor(.27f,.9f,.52f);
        case EDarkFactoryGateState::BranchChosen: return FLinearColor(.40f,.75f,1.f);
        case EDarkFactoryGateState::Rejected:
        case EDarkFactoryGateState::Error: return FLinearColor(1.f,.30f,.26f);
        default: return FLinearColor(.52f,.63f,.71f);
    }
}
const TCHAR* ProcessPhaseLabel(EDarkFactoryProcessPhase Phase) {
    switch (Phase) {
        case EDarkFactoryProcessPhase::Arriving: return TEXT("Body arriving");
        case EDarkFactoryProcessPhase::PanelPresence: return TEXT("R4 / panel presence");
        case EDarkFactoryProcessPhase::AdhesiveApplication: return TEXT("Arm A / applying sealant");
        case EDarkFactoryProcessPhase::PanelPlacement: return TEXT("Arm B / placing and holding panel");
        case EDarkFactoryProcessPhase::Adhesive: return TEXT("R4 / sealant cooling inspection");
        case EDarkFactoryProcessPhase::Welding: return TEXT("Arm C / spot welding");
        case EDarkFactoryProcessPhase::PanelSeated: return TEXT("R4 / panel seating inspection");
        case EDarkFactoryProcessPhase::WeldCooling: return TEXT("R4 / weld cooling inspection");
        case EDarkFactoryProcessPhase::HolderRetract: return TEXT("Arm B / releasing and retracting");
        case EDarkFactoryProcessPhase::Departing: return TEXT("Passing body forward");
        case EDarkFactoryProcessPhase::AgentIdle: return TEXT("Agent decision / idle");
    }
    return TEXT("Process hold");
}
const TCHAR* ProcessStepLabel(EDarkFactoryStepState Step) {
    switch (Step) {
        case EDarkFactoryStepState::Pending:return TEXT("pending");
        case EDarkFactoryStepState::Active:return TEXT("active");
        case EDarkFactoryStepState::Complete:return TEXT("done");
        case EDarkFactoryStepState::Skipped:return TEXT("skip");
        case EDarkFactoryStepState::Hold:return TEXT("hold");
        case EDarkFactoryStepState::Exception:return TEXT("exception");
    }
    return TEXT("hold");
}
const TCHAR* RobotPhaseLabel(const FDarkFactoryRobotTiming& Timing) {
    const double T=Timing.CycleSeconds;
    if (!Timing.Valid) return TEXT("Unavailable");
    if (T<Timing.ApproachStartSeconds) return TEXT("Rest");
    if (T<Timing.ApproachEndSeconds) return TEXT("Approach");
    if (T<Timing.WeldStartSeconds) return TEXT("At contact");
    if (T<Timing.WeldEndSeconds) return TEXT("Welding");
    if (T<Timing.WithdrawEndSeconds) return TEXT("Withdraw");
    return TEXT("Parked");
}
FLinearColor RobotPhaseColor(const FDarkFactoryRobotTiming& Timing) {
    const double T=Timing.CycleSeconds;
    if (T>=Timing.WeldStartSeconds && T<Timing.WeldEndSeconds) return FLinearColor(1.f,.68f,.26f);
    if ((T>=Timing.ApproachStartSeconds && T<Timing.ApproachEndSeconds) || (T>=Timing.WeldEndSeconds && T<Timing.WithdrawEndSeconds)) return FLinearColor(.49f,.79f,1.f);
    return FLinearColor(.70f,.78f,.84f);
}
}
TSharedRef<FJsonObject> ADarkFactoryHUD::BuildProductionStatusMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetBoolField(TEXT("available"),false);
    const ADarkFactoryWeldingCell* Cell=nullptr;
    const ADarkFactoryProcessController* Process=nullptr;
    for (TActorIterator<ADarkFactoryWorld> It(GetWorld());It;++It) if (It->IsImportedScene()) {
        Cell=It->GetWeldingCell(); Process=It->GetProcessController(); break;
    }
    if (!Cell) return O;
    if (Process && Process->GetStatus().bEnabled) {
        const auto& State=Process->GetStatus();
        O->SetBoolField(TEXT("available"),true); O->SetBoolField(TEXT("process_enabled"),true);
        O->SetStringField(TEXT("scope"),TEXT("Camera-only process-controller decisions; UI presentation excluded from model inputs"));
        O->SetStringField(TEXT("phase"),ProcessPhaseLabel(State.Phase)); O->SetStringField(TEXT("body_id"),State.BodyId);
        O->SetNumberField(TEXT("body_sequence"),State.BodySequence); O->SetBoolField(TEXT("paused"),State.bPaused);
        O->SetBoolField(TEXT("request_pending"),State.bRequestPending); O->SetNumberField(TEXT("active_gate"),State.ActiveGate);
        O->SetNumberField(TEXT("consecutive_passes"),State.ConsecutivePasses); O->SetNumberField(TEXT("required_passes"),State.RequiredConsensusBatches);
        O->SetNumberField(TEXT("passed_bodies"),State.PassedBodies);
        O->SetBoolField(TEXT("branch_known"),State.bBranchKnown); O->SetBoolField(TEXT("preassembled_branch"),State.bPreassembled);
        O->SetStringField(TEXT("backend"),State.Backend); O->SetStringField(TEXT("decision"),State.Decision);
        O->SetStringField(TEXT("message"),State.Message); O->SetNumberField(TEXT("latency_ms"),State.LastLatencyMs);
        TArray<TSharedPtr<FJsonValue>> Gates;
        for (int32 I=0;I<FDarkFactoryProcessStatus::GateCount;++I) {
            auto Gate=MakeShared<FJsonObject>(); Gate->SetNumberField(TEXT("index"),I);
            Gate->SetStringField(TEXT("state"),GateLabel(State.Gates[I])); Gates.Add(MakeShared<FJsonValueObject>(Gate));
        }
        O->SetArrayField(TEXT("gates"),Gates);
        TArray<TSharedPtr<FJsonValue>> Steps;
        for (int32 I=0;I<FDarkFactoryProcessStatus::StepCount;++I) {
            auto Step=MakeShared<FJsonObject>(); Step->SetNumberField(TEXT("index"),I);
            Step->SetStringField(TEXT("name"),ProcessPhaseLabel(static_cast<EDarkFactoryProcessPhase>(I)));
            Step->SetStringField(TEXT("state"),ProcessStepLabel(State.Steps[I])); Steps.Add(MakeShared<FJsonValueObject>(Step));
        }
        O->SetArrayField(TEXT("steps"),Steps); O->SetNumberField(TEXT("sensor_readbacks"),0);
        return O;
    }
    O->SetBoolField(TEXT("available"),true); O->SetStringField(TEXT("scope"),TEXT("Simulation progress, not inspection quality or live EVK status"));
    O->SetNumberField(TEXT("motion_seconds"),Cell->GetMotionTime()); O->SetNumberField(TEXT("body_sequence"),Cell->GetCycleIndex()+1);
    O->SetNumberField(TEXT("bodies_exited"),Cell->GetCycleIndex()+(Cell->GetCycleSeconds()>=Cell->GetCarriageDepartureEndSeconds() ? 1:0));
    O->SetNumberField(TEXT("cycle_seconds"),Cell->GetCycleSeconds()); O->SetNumberField(TEXT("cycle_duration_seconds"),Cell->GetCycleDurationSeconds());
    O->SetNumberField(TEXT("cycle_progress"),Cell->GetCycleSeconds()/Cell->GetCycleDurationSeconds()); O->SetBoolField(TEXT("paused"),!Cell->IsAnimating());
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 I=0;I<4;++I) {
        const auto Timing=Cell->GetRobotTiming(I,Cell->GetMotionTime()); auto R=MakeShared<FJsonObject>();
        R->SetNumberField(TEXT("index"),I); R->SetStringField(TEXT("id"),Timing.RobotId); R->SetStringField(TEXT("phase"),RobotPhaseLabel(Timing));
        Rows.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("robots"),Rows); O->SetNumberField(TEXT("sensor_readbacks"),0); return O;
}
void ADarkFactoryHUD::DrawProductionStatus(const ADarkFactoryWeldingCell* Cell,float RightPanelX) {
    if (!Cell) return;
    float X=290.f,Y=14.f,Width=RightPanelX-X-14.f;
    // Small windows use a second top row instead of covering the title or sensors.
    if (Width<550.f) { X=14.f; Y=106.f; Width=RightPanelX-X-14.f; }
    if (Width<330.f) return;
    Width=FMath::Min(Width,880.f);
    const float TextScale=Width<580.f ? .78f:.91f;
    DrawRect(FLinearColor(.008f,.015f,.024f,.82f),X,Y,Width,82.f);
    const int32 Exited=Cell->GetCycleIndex()+(Cell->GetCycleSeconds()>=Cell->GetCarriageDepartureEndSeconds() ? 1:0);
    const float Progress=static_cast<float>(Cell->GetCycleSeconds()/Cell->GetCycleDurationSeconds());
    DrawHUDText(FString::Printf(TEXT("Production preview   Body %03d   Exited %d   %s%.1f / %.0f s"),Cell->GetCycleIndex()+1,Exited,Cell->IsAnimating() ? TEXT(""):TEXT("PAUSED  "),Cell->GetCycleSeconds(),Cell->GetCycleDurationSeconds()),FLinearColor(.85f,.91f,.96f),X+10.f,Y+7.f,GEngine->GetSmallFont(),TextScale);
    DrawRect(FLinearColor(.11f,.17f,.21f,1),X+10.f,Y+32.f,Width-20.f,4.f);
    DrawRect(FLinearColor(.36f,.65f,.81f,1),X+10.f,Y+32.f,(Width-20.f)*Progress,4.f);
    const float Column=(Width-20.f)/4.f;
    for (int32 I=0;I<4;++I) {
        const auto Timing=Cell->GetRobotTiming(I,Cell->GetMotionTime()); const float Left=X+10.f+I*Column;
        DrawHUDText(FString::Printf(TEXT("R%d  %s"),I+1,RobotPhaseLabel(Timing)),RobotPhaseColor(Timing),Left,Y+46.f,GEngine->GetSmallFont(),TextScale);
    }
}
void ADarkFactoryHUD::DrawProcessStatus(const ADarkFactoryProcessController* Process,float RightPanelX) {
    if (!Process || !Process->GetStatus().bEnabled) return;
    const auto& State=Process->GetStatus();
    float X=290.f,Y=14.f,Width=RightPanelX-X-14.f;
    if (Width<550.f) { X=14.f; Y=106.f; Width=RightPanelX-X-14.f; }
    if (ColliderOverlayMode) Y=FMath::Max(Y,RightPanelX-304.f<550.f ? 252.f:160.f);
    if (Width<330.f) return;
    Width=FMath::Min(Width,940.f);
    const float Scale=Width<580.f ? .76f:.90f;
    const FLinearColor Pale(.84f,.92f,.97f),Muted(.58f,.71f,.80f),Amber(1.f,.68f,.22f);
    DrawRect(FLinearColor(.008f,.015f,.024f,.90f),X,Y,Width,State.bAgentControl ? 252.f:233.f);
    auto Fit=[&](const FString& Text,float MaxWidth,float TextScale) {
        FString Result=Text.Left(192).Replace(TEXT("\n"),TEXT(" ")).Replace(TEXT("\r"),TEXT(" ")).Replace(TEXT("\t"),TEXT(" ")); float W=0,H=0; MeasureHUDText(Result,W,H,GEngine->GetSmallFont(),TextScale);
        if (W>MaxWidth) {
            while (Result.Len()>3 && W>MaxWidth) { Result.LeftChopInline(1); MeasureHUDText(Result+TEXT("..."),W,H,GEngine->GetSmallFont(),TextScale); }
            Result+=TEXT("...");
        }
        return Result;
    };
    const TCHAR* RunLabel=State.bAgentControl ? (State.bEdgeAgentHarness ? TEXT("IQ9 AGENT / REMOTE TOOLS"):TEXT("LLM AGENT / LIVE TOOLS")):State.Backend.Contains(TEXT("PHYSICAL QA")) ? TEXT("PHYSICAL QA / NO MODEL")
        :State.Backend.Contains(TEXT("POLICY TEST")) ? TEXT("POLICY TEST")
        :State.Backend.Contains(TEXT("OFFLINE")) ? TEXT("CAPTURE FIXTURE"):TEXT("MODEL-GATED PROCESS");
    DrawHUDText(Fit(FString::Printf(TEXT("%s   Body %03d   %s%s"),RunLabel,State.BodySequence,
        State.bPaused ? TEXT("PAUSED / "):TEXT(""),ProcessPhaseLabel(State.Phase)),Width-20,Scale),State.bPaused ? Amber:Pale,X+10,Y+7,GEngine->GetSmallFont(),Scale);
    const FString Backend=State.Backend.IsEmpty() ? TEXT("CONNECTING"):State.Backend;
    if (Process->CanSwitchModelBackend()) {
        const FString Label=Fit(Process->GetModelBackendLabel(),Width-20,.82f*Scale);
        DrawHUDText(Label,Muted,X+10,Y+29,GEngine->GetSmallFont(),.82f*Scale);
    } else DrawHUDText(Fit(TEXT("Inspection backend: ")+Backend,Width-20,.82f*Scale),Muted,X+10,Y+29,GEngine->GetSmallFont(),.82f*Scale);
    const TCHAR* Names[]={TEXT("Panel / NIR"),TEXT("Sealant / thermal"),TEXT("Seating / NIR"),TEXT("Weld / thermal")};
    const float Column=(Width-20.f)/4.f;
    for (int32 I=0;I<4;++I) {
        const float Left=X+10+Column*I; const auto Color=GateColor(State.Gates[I]);
        DrawRect(FLinearColor(.025f,.052f,.071f,.94f),Left,Y+54,Column-6,65);
        DrawRect(Color,Left,Y+54,Column-6,2);
        DrawHUDText(Fit(Names[I],Column-16,.90f*Scale),Pale,Left+7,Y+62,GEngine->GetSmallFont(),.90f*Scale);
        FString ObservationLabel=GateLabel(State.Gates[I]);
        if (State.bAgentControl && !State.AgentGateCodes[I].IsEmpty() && State.ActiveGate!=I) {
            const auto& Code=State.AgentGateCodes[I];
            ObservationLabel=Code==TEXT("invalid") ? TEXT("Invalid observation"):Code==TEXT("A") ? TEXT("A / unclear"):
                I==0 ? (Code==TEXT("G") ? TEXT("G / panel present"):TEXT("R / panel missing")):
                I==2 ? (Code==TEXT("G") ? TEXT("G / seated"):TEXT("R / seating defect")):
                Code==TEXT("G") ? TEXT("G / cool"):TEXT("R / hot");
        }
        DrawHUDText(Fit(ObservationLabel,Column-16,.88f*Scale),Color,Left+7,Y+83,GEngine->GetSmallFont(),.88f*Scale);
        if (State.ActiveGate==I)
            DrawHUDText(FString::Printf(TEXT("%s  %d / %d%s"),State.ConsensusCode.IsEmpty() ? TEXT("fresh"):*State.ConsensusCode,
                State.ConsecutivePasses,State.RequiredConsensusBatches,State.bRequestPending ? TEXT("  scanning"):TEXT("")),Muted,Left+7,Y+103,GEngine->GetSmallFont(),.69f*Scale);
    }
    const TCHAR* Steps[]={TEXT("Arrive"),TEXT("Panel check"),TEXT("Apply sealant"),TEXT("Place / hold"),TEXT("Sealant cool"),
        TEXT("Spot weld"),TEXT("Seat check"),TEXT("Weld cool"),TEXT("Release"),TEXT("Depart")};
    const float StepWidth=(Width-20.f)/5.f;
    for (int32 I=0;I<10;++I) {
        const auto Step=State.Steps[I]; const float Left=X+10+(I%5)*StepWidth,Top=Y+127+(I/5)*29;
        const FLinearColor Color=Step==EDarkFactoryStepState::Complete ? FLinearColor(.27f,.9f,.52f):
            Step==EDarkFactoryStepState::Active ? Pale:Step==EDarkFactoryStepState::Hold ? Amber:
            Step==EDarkFactoryStepState::Exception ? FLinearColor(1.f,.30f,.26f):Muted;
        DrawRect(Color,Left,Top,3,21);
        DrawHUDText(Fit(FString::Printf(TEXT("%d %s"),I+1,Steps[I]),StepWidth-13,.77f*Scale),Color,Left+6,Top-1,GEngine->GetSmallFont(),.77f*Scale);
        DrawHUDText(ProcessStepLabel(Step),Color,Left+6,Top+12,GEngine->GetSmallFont(),.66f*Scale);
    }
    bool Error=false;
    for (const auto Gate:State.Gates) Error |= Gate==EDarkFactoryGateState::Error;
    const FString Detail=State.bPaused ? TEXT("PAUSED: motion, heating and inspections held.")
        :State.bAgentControl ? State.Message
        :Error ? TEXT("MODEL UNAVAILABLE / HOLD: ")+State.Message:State.Message;
    DrawHUDText(Fit(Detail,Width-20,.90f*Scale),Error ? FLinearColor(1.f,.30f,.26f):State.bPaused ? Amber:Pale,X+10,Y+190,GEngine->GetSmallFont(),.90f*Scale);
    const FString Latency=State.LastLatencyMs>0 ? FString::Printf(TEXT("%.0f ms"),State.LastLatencyMs):TEXT("--");
    const TCHAR* Branch=!State.bBranchKnown ? TEXT("Branch undecided"):State.bPreassembled ? TEXT("Preassembled / assembly skipped"):TEXT("Missing panel / assemble");
    const FString Summary=State.bAgentControl ? FString::Printf(TEXT("Agent %.1f s | Reason2 %s | Audit errors %d | Exited %d"),State.AgentLatencyMs/1000.f,*Latency,State.AgentAuditFailures,State.PassedBodies)
        :FString::Printf(TEXT("%s   Last %s / %s   Exited %d"),Branch,*State.Decision,*Latency,State.PassedBodies);
    DrawHUDText(Fit(Summary,Width-20,.82f*Scale),Muted,X+10,Y+211,GEngine->GetSmallFont(),.82f*Scale);
    if (State.bAgentControl)
        DrawHUDText(Fit(State.AgentAction+TEXT(": ")+State.AgentReason,Width-20,.74f*Scale),Muted,X+10,Y+231,GEngine->GetSmallFont(),.74f*Scale);
}
void ADarkFactoryHUD::DrawProcessCaptures(const ADarkFactoryProcessController* Process,const ADarkFactoryWorld* World,float RightPanelX,float PanelWidth,float MaxBottom) {
    if (!Process || !World || !Canvas) return;
    if (Process->IsInspectionReplayVisible()) { DrawInspectionReplay(Process,RightPanelX,PanelWidth); return; }
    const auto& State=Process->GetStatus();
    const bool NIR=State.bAgentControl ? (State.LastInspectionStage.IsEmpty() || State.LastInspectionStage==TEXT("panel_presence") || State.LastInspectionStage==TEXT("panel_seated")):
        (State.Phase<=EDarkFactoryProcessPhase::PanelPlacement || State.Phase==EDarkFactoryProcessPhase::PanelSeated);
    const int32 Count=3;
    const float Margin=14.f,Header=12.f,Footer=12.f,Extra=NIR ? 36.f:62.f;
    const float Height=FMath::Min((PanelWidth-20.f)/1.75f,FMath::Max(32.f,
        (MaxBottom-Margin-Header-Footer-Count*Extra)/Count));
    const float Width=Height*1.75f,ImageX=RightPanelX+(PanelWidth-Width)*.5f;
    const FLinearColor Muted(.55f,.70f,.79f);
    DrawRect(FLinearColor(.008f,.015f,.024f,.88f),RightPanelX,Margin,PanelWidth,Header+Count*(Height+Extra)+Footer);
    float Y=Margin+Header;
    for (int32 I=0;I<Count;++I) {
        const FString Name=NIR ? FString::Printf(TEXT("R4 NIR / spot %d"),I+1):FString::Printf(TEXT("R4 thermal / spot %d / color"),I+1);
        DrawHUDText(Name,NIR ? FLinearColor(.70f,.90f,.78f):FLinearColor(.93f,.75f,.51f),RightPanelX+10,Y,GEngine->GetSmallFont(),.95f);
        Y+=22;
        auto* Target=Process->GetPreviewTexture(NIR ? I:I+3);
        if (!Target) {
            DrawRect(FLinearColor(.018f,.034f,.047f,1.f),ImageX,Y,Width,Height);
            DrawHUDText(TEXT("Awaiting this gate's capture"),Muted,ImageX+8,Y+Height*.5f-7,GEngine->GetSmallFont(),.82f);
        } else if (!NIR && ProcessThermalDisplayMaterials.IsValidIndex(I)) {
            // Each tile owns a MID: updating one texture cannot change another
            // tile's deferred draw. All inputs remain the original GPU targets.
            auto* Material=ProcessThermalDisplayMaterials[I].Get();
            Material->SetTextureParameterValue(TEXT("SensorImage"),Target);
            Material->SetScalarParameterValue(TEXT("ThermalPalette"),2);
            DrawMaterial(Material,ImageX,Y,Width,Height,0,0,1,1);
        } else DrawTexture(Target,ImageX,Y,Width,Height,0,0,1,1,FLinearColor::White,BLEND_Opaque);
        if (!NIR) {
            const auto* Thermal=World->GetThermalRig();
            DrawThermalScale(2,Thermal ? Thermal->MinC:20.f,Thermal ? Thermal->MaxC:120.f,ImageX,Y+Height+5,Width,.9f);
        }
        Y+=Height+(NIR ? 14.f:40.f);
    }
}
float ADarkFactoryHUD::DrawObserverControls(const ADarkFactoryProcessController* Process,float Margin) {
    const auto* PC=Cast<ADarkFactoryPlayerController>(PlayerOwner);
    if (!PC) return Canvas->ClipY-Margin-48.f;
    const bool Gamepad=PC->UsesGamepadHints();
    TArray<FString> Items;
    if (Gamepad) {
        Items={TEXT("[Left stick]: fly"),TEXT("[Right stick]: look"),TEXT("[LT / RT]: height"),
            TEXT("[Y]: dim, NIR, thermal, bright"),TEXT("[D-pad left]: palette"),
            TEXT("[View]: motion"),TEXT("[X]: viewpoint"),TEXT("[Menu]: home"),TEXT("[LS]: colliders")};
        if (Process && Process->CanSwitchModelBackend()) Items.Add(TEXT("[RS]: switch EVK / GPU"));
    } else {
        const auto* Pawn=Cast<ADarkFactoryPawn>(PlayerOwner->GetPawn());
        const TCHAR* Headlamp=Pawn && Pawn->IsFactoryLightsView() ? TEXT("[L]: headlamp disabled in bright view"):
            Pawn && Pawn->IsObserverHeadlightEnabled() ? TEXT("[L]: headlamp on"):TEXT("[L]: headlamp off");
        Items={TEXT("[WASD / mouse]: fly & look"),TEXT("[Q / E]: height"),TEXT("[Shift]: boost"),Headlamp,
            TEXT("[Esc]: quit"),TEXT("[Tab]: dim, NIR, thermal, bright"),TEXT("[K]: palette"),
            TEXT("[Space]: motion"),TEXT("[B]: viewpoint"),TEXT("[M]: sound"),TEXT("[Home]: home"),TEXT("[F8]: colliders"),
            TEXT("[F6]: DLSS"),TEXT("[F7]: Lumen"),TEXT("[F11]: ray tracing")};
        if (Process && Process->CanSwitchModelBackend()) Items.Add(TEXT("[F4]: switch EVK / GPU"));
        if (Process) {
            Items.Add(TEXT("[F5]: inspection replay"));
            if (Process->IsInspectionReplayVisible()) Items.Add(TEXT("[PageUp / PageDown]: replay history"));
        }
    }
    // Wrap whole bindings instead of shrinking text or clipping narrow windows.
    TArray<FString> Lines; FString Line;
    for (const FString& Item:Items) {
        const FString Candidate=Line.IsEmpty() ? Item:Line+TEXT("   ")+Item;
        FString Measured=Candidate.Replace(TEXT("["),TEXT("")).Replace(TEXT("]"),TEXT(""));
        float Width=0,Height=0; MeasureHUDText(Measured,Width,Height,GEngine->GetSmallFont(),.95f);
        if (!Line.IsEmpty() && Width>Canvas->ClipX-2*Margin-20) { Lines.Add(Line); Line=Item; }
        else Line=Candidate;
    }
    if (!Line.IsEmpty()) Lines.Add(Line);
    const float Height=Lines.Num()*20.f+8.f;
    const float RestingTop=Canvas->ClipY-Margin-Height;
    const float Reveal=PC->GetControlsReveal();
    if (Reveal>0) {
        const float Bottom=FMath::Lerp(Canvas->ClipY,RestingTop,Reveal);
        DrawRect(FLinearColor(.008f,.015f,.024f,.75f),Margin,Bottom,Canvas->ClipX-2*Margin,Height);
        for (int32 I=0;I<Lines.Num();++I) DrawHUDControls(Lines[I],Margin+10,Bottom+4+I*20.f,.95f);
    }
    return RestingTop;
}
void ADarkFactoryHUD::DrawHUD() {
    if (!bShowHUD) return;
    Super::DrawHUD();
    if (!Canvas) return;
    ThermalPreviewQARegion=ThermalObserverQARegion=FIntRect(0,0,0,0);
    ADarkFactoryWorld* World=nullptr;
    for (TActorIterator<ADarkFactoryWorld> It(GetWorld());It;++It) { World=*It; break; }
    if (!World) return;
    const auto* Pawn=PlayerOwner ? Cast<ADarkFactoryPawn>(PlayerOwner->GetPawn()):nullptr;
    DrawColliderOverlay();
    const FString FrameRateText=FrameRateSamples>0 ? FString::Printf(TEXT("FPS %.0f"),GetSmoothedFramesPerSecond()):TEXT("FPS --");
    const TCHAR* ObserverView=Pawn && Pawn->IsFactoryLightsView() ? TEXT("Bright factory floor")
        : Pawn && Pawn->IsThermalView() ? TEXT("Thermal") : Pawn && Pawn->bAssistedView ? TEXT("NIR / camera illuminator") : TEXT("Dim visible");
    const TCHAR* HeadlightStatus=Pawn && Pawn->IsFactoryLightsView() ? TEXT("[L]: headlamp disabled in bright view")
        : Pawn && Pawn->IsObserverHeadlightEmitting() ? TEXT("[L]: headlamp on")
        : Pawn && Pawn->IsObserverHeadlightEnabled() ? TEXT("[L]: headlamp saved on") : TEXT("[L]: headlamp off");
    if (!bTriedNightVisionMaterial) {
        bTriedNightVisionMaterial=true;
        if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_UI_Thermal.M_UI_Thermal"))) {
            ThermalDisplayMaterial=UMaterialInstanceDynamic::Create(Parent,this);
            ThermalMonochromeDisplayMaterial=UMaterialInstanceDynamic::Create(Parent,this);
            for (int32 I=0;I<3;++I) ProcessThermalDisplayMaterials.Add(UMaterialInstanceDynamic::Create(Parent,this));
            InitializeThermalScales(Parent);
        }
        const TCHAR* MaterialPath=Pawn && Pawn->IsNightVisionQuality() ? TEXT("/Game/Materials/M_UI_NightVisionQuality.M_UI_NightVisionQuality") : TEXT("/Game/Materials/M_UI_NightVision.M_UI_NightVision");
        if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,MaterialPath))
            SensorDisplayMaterial=UMaterialInstanceDynamic::Create(Parent,this);
        else UE_LOG(LogTemp,Warning,TEXT("DF_NV_PREVIEW_MISSING: run Scripts/build.ps1"));
    }
    if (World->IsImportedScene()) {
        const float Margin=14.f;
        const float PanelWidth=FMath::Clamp(Canvas->ClipX*.22f,224.f,336.f);
        const float InnerWidth=PanelWidth-20.f;
        // Fit three previews above the controls, preserving the sensor aspect ratio.
        const float ImageHeight=FMath::Min(InnerWidth/1.75f,FMath::Max(32.f,(Canvas->ClipY-293.f)/3.f));
        const float ImageWidth=ImageHeight*1.75f;
        const float RightX=Canvas->ClipX-PanelWidth-Margin;
        const float ImageX=RightX+(PanelWidth-ImageWidth)*.5f;
        const auto* Process=World->GetProcessController();
        const bool ProcessEnabled=Process && Process->GetStatus().bEnabled;
        const float ControlsTop=DrawObserverControls(Process,Margin);
        if (ProcessEnabled) DrawProcessStatus(Process,RightX);
        else DrawProductionStatus(World->GetWeldingCell(),RightX);
        DrawColliderLegend(RightX,true);
        if (ProcessEnabled) DrawProcessCaptures(Process,World,RightX,PanelWidth,ControlsTop-(Pawn && Pawn->IsThermalView() ? 66.f:8.f));
        else {
        const float PanelHeight=3.f*ImageHeight+207.f;
        DrawRect(FLinearColor(.008f,.015f,.024f,.82f),RightX,Margin,PanelWidth,PanelHeight);
        float LabelY=Margin+10.f;
        DrawHUDText(TEXT("Fixed NIR sensor"),FLinearColor(.70f,.90f,.78f),RightX+10.f,LabelY,GEngine->GetSmallFont(),1.f);
        LabelY+=22.f;
        if (World->GetSensorTexture()) {
            if (SensorDisplayMaterial && Pawn) {
                Pawn->ApplyNightVision(SensorDisplayMaterial);
                SensorDisplayMaterial->SetTextureParameterValue(TEXT("SensorImage"),World->GetSensorTexture());
                DrawMaterial(SensorDisplayMaterial,ImageX,LabelY,ImageWidth,ImageHeight,0,0,1,1);
            } else DrawTexture(World->GetSensorTexture(),ImageX,LabelY,ImageWidth,ImageHeight,0,0,1,1,FLinearColor::White,BLEND_Opaque);
        }
        // Separate display instances share one raw camera target. No extra capture/readback.
        LabelY+=ImageHeight+15.f;
        auto ThermalPreview=[&](const TCHAR* Label,UMaterialInstanceDynamic* Material,int32 Palette) {
            DrawHUDText(Label,FLinearColor(.93f,.75f,.51f),RightX+10.f,LabelY,GEngine->GetSmallFont(),1.f);
            LabelY+=22.f;
            if (auto* Thermal=World->GetThermalRig()) {
                if (Material && Thermal->GetTarget()) {
                    Material->SetTextureParameterValue(TEXT("SensorImage"),Thermal->GetTarget());
                    Material->SetScalarParameterValue(TEXT("ThermalPalette"),Palette);
                    DrawMaterial(Material,ImageX,LabelY,ImageWidth,ImageHeight,0,0,1,1);
                }
                DrawThermalScale(Palette,Thermal->MinC,Thermal->MaxC,ImageX,LabelY+ImageHeight+5.f,ImageWidth,.95f);
            }
            LabelY+=ImageHeight+47.f;
        };
        ThermalPreview(TEXT("Thermal / color / 120+ clipped"),ThermalDisplayMaterial,2);
        ThermalPreview(TEXT("Thermal / mono / 120+ clipped"),ThermalMonochromeDisplayMaterial,0);
        const FString Phase=ProcessEnabled ? ProcessPhaseLabel(Process->GetStatus().Phase):World->GetWeldingCell()->GetCyclePhaseName();
        DrawHUDText(Phase,FLinearColor(.93f,.78f,.51f),RightX+10.f,LabelY,GEngine->GetSmallFont(),.95f);
        }
        if (Pawn && Pawn->IsThermalView())
            DrawObserverThermalScale(World->GetThermalRig(),RightX,ControlsTop-58.f,PanelWidth);
        return;
    }
    const float Scale=FMath::Clamp(Canvas->ClipX/1500.f,.7f,1.3f);
    const float Margin=24*Scale;
    const float PanelWidth=490*Scale;
    const float X=Canvas->ClipX-PanelWidth-Margin;
    float Y=Margin;
    DrawRect(FLinearColor(.008,.017,.028,.94),X-14*Scale,Y-12*Scale,PanelWidth+22*Scale,458*Scale);
    auto Line=[&](const FString& Text,FLinearColor Color,float Size=1.f) {
        DrawHUDText(Text,Color,X,Y,GEngine->GetSmallFont(),Size*Scale,false);
        Y+=23*Scale;
    };
    Line(TEXT("QAI / DARK FACTORY"),FLinearColor(.48,.85,1),1.5);
    Y+=10*Scale;
    Line(TEXT("FIXED SENSOR / INFRARED / 448 x 256"),FLinearColor::White);
    Line(World->bSensorActive ? (World->bGreenDisplay ? TEXT("ACTIVE NIR / GREEN PHOSPHOR DISPLAY"):TEXT("ACTIVE NIR / MONOCHROME DISPLAY")) : TEXT("VISIBLE DARK - ILLUMINATOR NOT OBSERVED"),FLinearColor(.68,.78,.84));
    if (World->GetSensorTexture()) {
        if (SensorDisplayMaterial && Pawn) {
            Pawn->ApplyNightVision(SensorDisplayMaterial);
            SensorDisplayMaterial->SetTextureParameterValue(TEXT("SensorImage"),World->GetSensorTexture());
            DrawMaterial(SensorDisplayMaterial,X,Y,448*Scale,256*Scale,0,0,1,1);
        } else DrawTexture(World->GetSensorTexture(),X,Y,448*Scale,256*Scale,0,0,1,1,FLinearColor::White,BLEND_Opaque);
    }
    Y+=265*Scale;
    const FLinearColor DecisionColor=World->Decision==TEXT("G") ? FLinearColor(.25,1,.43) : World->Decision==TEXT("R") ? FLinearColor(1,.27,.18) : FLinearColor(1,.72,.19);
    Line(World->Decision+TEXT(" / ")+World->Status.Left(67),DecisionColor);
    Line(World->Source==TEXT("MOCK") ? TEXT("MOCK BACKEND - DEMO SIGNALS, NO MODEL INFERENCE") : FString::Printf(TEXT("BACKEND: %s  /  %.0f ms"),*World->Source,World->LatencyMs),World->Source==TEXT("MOCK") ? FLinearColor(1,.4,.85):FLinearColor::White);
    Line(TEXT("LIVE CAMERA PREVIEW / MODEL PNG SAVED SEPARATELY"),FLinearColor(.64f,.75f,.78f),.85f);
    if (auto* Thermal=World->GetThermalRig()) {
        Y=478*Scale;
        DrawRect(FLinearColor(.008,.017,.028,.94),X-14*Scale,Y-8*Scale,PanelWidth+22*Scale,216*Scale);
        Line(TEXT("FIXED THERMAL / APPARENT TEMPERATURE"),FLinearColor(.95,.69,.4));
        if (ThermalDisplayMaterial && Thermal->GetTarget()) {
            ThermalDisplayMaterial->SetTextureParameterValue(TEXT("SensorImage"),Thermal->GetTarget());
            ThermalDisplayMaterial->SetScalarParameterValue(TEXT("ThermalPalette"),Thermal->DisplayPalette);
            DrawMaterial(ThermalDisplayMaterial,X,Y,252*Scale,144*Scale,0,0,1,1);
            // Inset from the actual image, not the surrounding preview panel.
            ThermalPreviewQARegion=FIntRect(FMath::CeilToInt(X+2*Scale),FMath::CeilToInt(Y+2*Scale),
                FMath::FloorToInt(X+250*Scale),FMath::FloorToInt(Y+142*Scale));
            DrawThermalScale(Thermal->DisplayPalette,Thermal->MinC,Thermal->MaxC,X,Y+149*Scale,252*Scale,Scale);
        }
        DrawHUDText(Thermal->GetCaseName(),FLinearColor::White,X+290*Scale,Y+12*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawHUDText(Thermal->HasSpatialFields() ? FString::Printf(TEXT("%s: %.0f s"),Thermal->HasHeatTransfer() ? TEXT("Heat exchange"):TEXT("Cooling only"),Thermal->GetCoolingAge()) : TEXT("Uniform comparison"),FLinearColor(.7,.76,.8),X+290*Scale,Y+40*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawHUDControls(TEXT("[H]: case   [K]: palette"),X+290*Scale,Y+65*Scale,.9f*Scale);
        DrawHUDControls(TEXT("[U]: +15 s   [O]: transfer"),X+290*Scale,Y+90*Scale,.85f*Scale);
        if (!Thermal->LastSnapshot.IsEmpty()) DrawHUDText(Thermal->LastSnapshot.EndsWith(TEXT(".png")) ? TEXT("Thermal PNG saved") : Thermal->LastSnapshot.Left(27),FLinearColor(.95,.69,.4),X+290*Scale,Y+118*Scale,GEngine->GetSmallFont(),.85f*Scale);
    }
    // Reserve the genuine scene area between the header, right preview panels
    // and observer scale. Keep this coupled to their actual layout expressions.
    ThermalObserverQARegion=FIntRect(FMath::CeilToInt(Margin+4*Scale),FMath::CeilToInt(Margin+126*Scale),
        FMath::FloorToInt(X-20*Scale),FMath::FloorToInt(Canvas->ClipY-234*Scale));
    DrawRect(FLinearColor(.008,.017,.028,.91),Margin-8*Scale,Margin-10*Scale,460*Scale,132*Scale);
    DrawHUDText(TEXT("AUTOMOTIVE INSPECTION LAB"),FLinearColor(.48,.85,1),Margin,Margin,GEngine->GetMediumFont(),Scale);
    DrawHUDText(FString::Printf(TEXT("OBSERVER: %s"),ObserverView),FLinearColor::White,Margin,Margin+34*Scale,GEngine->GetSmallFont(),Scale);
    DrawHUDControls(TEXT("[Tab / Y]: dim, NIR, thermal, bright. Sensors stay fixed."),Margin,Margin+62*Scale,Scale);
    DrawHUDControls(HeadlightStatus,Margin,Margin+84*Scale,.85f*Scale);
    DrawHUDText(FrameRateText,FLinearColor(.78f,.90f,.80f),Margin,Margin+106*Scale,GEngine->GetSmallFont(),Scale);
    if (Pawn && Pawn->IsThermalView()) DrawObserverThermalScale(World->GetThermalRig(),Margin-8*Scale,Canvas->ClipY-228*Scale,260*Scale,Scale);
    DrawColliderLegend(X-14*Scale,false);
    const float Bottom=Canvas->ClipY-144*Scale;
    DrawRect(FLinearColor(.008,.017,.028,.93),Margin-8*Scale,Bottom-10*Scale,Canvas->ClipX-2*Margin+16*Scale,140*Scale);
    DrawHUDText(FString::Printf(TEXT("NIR SCENARIO: %s  /  seed %d  /  reference label %s (never sent to model)"),*World->GetScenarioName(),World->Seed,*World->GetTruthLabel()),FLinearColor::White,Margin,Bottom,GEngine->GetSmallFont(),Scale);
    DrawHUDControls(TEXT("[1]: normal   [2]: missing   [3]: misaligned   [4]: occluded   [R]: new seed   [I / N]: observer IR on/off   Sensor stays IR"),Margin,Bottom+25*Scale,Scale);
    DrawHUDControls(TEXT("[WASD + mouse / Xbox sticks]: fly & look   [Q/E / triggers]: down/up   [Shift]: boost   [Home / Menu]: home   [Esc]: quit"),Margin,Bottom+50*Scale,Scale);
    DrawHUDControls(TEXT("[Tab / Y]: 4 views   [T]: thermal   [K / D-pad left]: palette   [H / D-pad right]: thermal case   [F9]: thermal PNG   [J]: fields   [U]: +15 s   [O]: transfer   [P]: inference"),Margin,Bottom+75*Scale,Scale);
    DrawHUDText(World->LastSaved.IsEmpty() ? (Pawn ? Pawn->GetNightVisionStatus()+TEXT("   |   Model input stays grayscale."):TEXT("Waiting for observer")) : TEXT("Saved: ")+World->LastSaved,FLinearColor(.5,.67,.74),Margin,Bottom+102*Scale,GEngine->GetSmallFont(),Scale);
}
