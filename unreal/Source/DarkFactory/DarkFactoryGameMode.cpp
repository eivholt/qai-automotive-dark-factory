#include "DarkFactoryGameMode.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryWorld.h"
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
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

ADarkFactoryGameMode::ADarkFactoryGameMode() {
    DefaultPawnClass=ADarkFactoryPawn::StaticClass();
    HUDClass=ADarkFactoryHUD::StaticClass();
}
void ADarkFactoryGameMode::StartPlay() {
    Super::StartPlay();
    GetWorld()->SpawnActor<ADarkFactoryWorld>();
}
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
        const FString Value=FString::Printf(TEXT("%g"),FMath::Lerp(MinC,MaxC,Fraction))+(I==1 ? TEXT(" \u00b0C"):TEXT(""));
        float TextWidth=0,TextHeight=0;
        GetTextSize(Value,TextWidth,TextHeight,GEngine->GetSmallFont(),.85f*Scale);
        const float TickX=X+Width*Fraction;
        DrawRect(FLinearColor(.75f,.81f,.86f,1),TickX-Scale*.5f,Y+Height,Scale,3*Scale);
        DrawText(Value,FLinearColor(.84f,.9f,.94f,1),TickX-TextWidth*Fraction,Y+Height+4*Scale,GEngine->GetSmallFont(),.85f*Scale);
    }
}
void ADarkFactoryHUD::DrawObserverThermalScale(const ADarkFactoryThermalRig* Thermal,float X,float Y,float Width,float Scale) {
    if (!Thermal) return;
    DrawRect(FLinearColor(.008f,.015f,.024f,.86f),X,Y,Width,68.f*Scale);
    DrawText(TEXT("Apparent temperature"),FLinearColor(.85f,.9f,.94f),X+10*Scale,Y+7*Scale,GEngine->GetSmallFont(),.9f*Scale);
    DrawThermalScale(Thermal->DisplayPalette,Thermal->MinC,Thermal->MaxC,X+10*Scale,Y+29*Scale,Width-20*Scale,Scale);
}
namespace {
const TCHAR* GateLabel(EDarkFactoryGateState State) {
    switch (State) {
        case EDarkFactoryGateState::Pending: return TEXT("Pending");
        case EDarkFactoryGateState::Inspecting: return TEXT("Inspecting");
        case EDarkFactoryGateState::Waiting: return TEXT("Waiting / HOLD");
        case EDarkFactoryGateState::Passed: return TEXT("Passed");
        case EDarkFactoryGateState::Rejected: return TEXT("Rejected");
        case EDarkFactoryGateState::Error: return TEXT("Unavailable / HOLD");
    }
    return TEXT("Unknown / HOLD");
}
FLinearColor GateColor(EDarkFactoryGateState State) {
    switch (State) {
        case EDarkFactoryGateState::Inspecting:
        case EDarkFactoryGateState::Waiting: return FLinearColor(1.f,.68f,.22f);
        case EDarkFactoryGateState::Passed: return FLinearColor(.27f,.9f,.52f);
        case EDarkFactoryGateState::Rejected:
        case EDarkFactoryGateState::Error: return FLinearColor(1.f,.30f,.26f);
        default: return FLinearColor(.52f,.63f,.71f);
    }
}
const TCHAR* ProcessPhaseLabel(EDarkFactoryProcessPhase Phase) {
    switch (Phase) {
        case EDarkFactoryProcessPhase::Arriving: return TEXT("Body arriving");
        case EDarkFactoryProcessPhase::Doors: return TEXT("Door inspection");
        case EDarkFactoryProcessPhase::Adhesive: return TEXT("Adhesive inspection");
        case EDarkFactoryProcessPhase::Welding: return TEXT("Robotic welding");
        case EDarkFactoryProcessPhase::WeldCooling: return TEXT("Weld cooling inspection");
        case EDarkFactoryProcessPhase::Departing: return TEXT("Passing body forward");
        case EDarkFactoryProcessPhase::Returning: return TEXT("Returning body to entry");
    }
    return TEXT("Process hold");
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
        O->SetNumberField(TEXT("consecutive_passes"),State.ConsecutivePasses); O->SetNumberField(TEXT("required_passes"),2);
        O->SetNumberField(TEXT("passed_bodies"),State.PassedBodies); O->SetNumberField(TEXT("returned_bodies"),State.ReturnedBodies);
        O->SetStringField(TEXT("backend"),State.Backend); O->SetStringField(TEXT("decision"),State.Decision);
        O->SetStringField(TEXT("message"),State.Message); O->SetNumberField(TEXT("latency_ms"),State.LastLatencyMs);
        TArray<TSharedPtr<FJsonValue>> Gates;
        for (int32 I=0;I<3;++I) {
            auto Gate=MakeShared<FJsonObject>(); Gate->SetNumberField(TEXT("index"),I);
            Gate->SetStringField(TEXT("state"),GateLabel(State.Gates[I])); Gates.Add(MakeShared<FJsonValueObject>(Gate));
        }
        O->SetArrayField(TEXT("gates"),Gates); O->SetNumberField(TEXT("sensor_readbacks"),0);
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
    DrawText(FString::Printf(TEXT("Production preview   Body %03d   Exited %d   %s%.1f / %.0f s"),Cell->GetCycleIndex()+1,Exited,Cell->IsAnimating() ? TEXT(""):TEXT("PAUSED  "),Cell->GetCycleSeconds(),Cell->GetCycleDurationSeconds()),FLinearColor(.85f,.91f,.96f),X+10.f,Y+7.f,GEngine->GetSmallFont(),TextScale);
    DrawRect(FLinearColor(.11f,.17f,.21f,1),X+10.f,Y+32.f,Width-20.f,4.f);
    DrawRect(FLinearColor(.36f,.65f,.81f,1),X+10.f,Y+32.f,(Width-20.f)*Progress,4.f);
    const float Column=(Width-20.f)/4.f;
    for (int32 I=0;I<4;++I) {
        const auto Timing=Cell->GetRobotTiming(I,Cell->GetMotionTime()); const float Left=X+10.f+I*Column;
        DrawText(FString::Printf(TEXT("R%d  %s"),I+1,RobotPhaseLabel(Timing)),RobotPhaseColor(Timing),Left,Y+46.f,GEngine->GetSmallFont(),TextScale);
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
    DrawRect(FLinearColor(.008f,.015f,.024f,.90f),X,Y,Width,190.f);
    auto Fit=[&](const FString& Text,float MaxWidth,float TextScale) {
        FString Result=Text.Left(192).Replace(TEXT("\n"),TEXT(" ")).Replace(TEXT("\r"),TEXT(" ")).Replace(TEXT("\t"),TEXT(" ")); float W=0,H=0; GetTextSize(Result,W,H,GEngine->GetSmallFont(),TextScale);
        if (W>MaxWidth) {
            while (Result.Len()>3 && W>MaxWidth) { Result.LeftChopInline(1); GetTextSize(Result+TEXT("..."),W,H,GEngine->GetSmallFont(),TextScale); }
            Result+=TEXT("...");
        }
        return Result;
    };
    const TCHAR* RunLabel=State.Backend.Contains(TEXT("POLICY TEST")) ? TEXT("POLICY TEST")
        :State.Backend.Contains(TEXT("OFFLINE")) ? TEXT("CAPTURE FIXTURE"):TEXT("MODEL-GATED PROCESS");
    DrawText(Fit(FString::Printf(TEXT("%s   Body %03d   %s%s"),RunLabel,State.BodySequence,
        State.bPaused ? TEXT("PAUSED / "):TEXT(""),ProcessPhaseLabel(State.Phase)),Width-20,Scale),State.bPaused ? Amber:Pale,X+10,Y+7,GEngine->GetSmallFont(),Scale);
    const FString Backend=State.Backend.IsEmpty() ? TEXT("CONNECTING"):State.Backend;
    DrawText(Fit(TEXT("Inspection backend: ")+Backend,Width-20,.82f*Scale),Muted,X+10,Y+29,GEngine->GetSmallFont(),.82f*Scale);
    const TCHAR* Names[]={TEXT("1  Doors / NIR"),TEXT("2  Adhesive / thermal"),TEXT("3  Weld cooling / thermal")};
    const float Column=(Width-20.f)/3.f;
    for (int32 I=0;I<3;++I) {
        const float Left=X+10+Column*I;
        const auto Color=GateColor(State.Gates[I]);
        DrawRect(FLinearColor(.025f,.052f,.071f,.94f),Left,Y+54,Column-6,69);
        DrawRect(Color,Left,Y+54,Column-6,2);
        DrawText(Fit(Names[I],Column-20,Scale),Pale,Left+7,Y+62,GEngine->GetSmallFont(),Scale);
        DrawText(GateLabel(State.Gates[I]),Color,Left+7,Y+85,GEngine->GetSmallFont(),Scale);
        if (State.ActiveGate==I)
            DrawText(FString::Printf(TEXT("Fresh G: %d / 2%s"),State.ConsecutivePasses,State.bRequestPending ? TEXT("   analyzing"):TEXT("")),Muted,Left+7,Y+106,GEngine->GetSmallFont(),.73f*Scale);
    }
    bool Error=false;
    for (const auto Gate:State.Gates) Error |= Gate==EDarkFactoryGateState::Error;
    FString Detail=State.bPaused ? TEXT("PAUSED: motion, heating and inspections held.")
        : Error ? TEXT("MODEL UNAVAILABLE / HOLD: ")+State.Message:State.Message;
    DrawText(Fit(Detail,Width-20,Scale),Error ? FLinearColor(1.f,.30f,.26f):State.bPaused ? Amber:Pale,X+10,Y+132,GEngine->GetSmallFont(),Scale);
    const FString Latency=State.LastLatencyMs>0 ? FString::Printf(TEXT("%.0f ms"),State.LastLatencyMs):TEXT("--");
    DrawText(Fit(FString::Printf(TEXT("Last decision %s   %s   Passed %d   Returned %d   G proceed / A hold / R return or wait"),
        *State.Decision,*Latency,State.PassedBodies,State.ReturnedBodies),Width-20,.82f*Scale),Muted,X+10,Y+156,GEngine->GetSmallFont(),.82f*Scale);
    DrawText(TEXT("Sensor inputs exclude HUD labels and controller state."),Muted,X+10,Y+175,GEngine->GetSmallFont(),.74f*Scale);
}
void ADarkFactoryHUD::DrawProcessCaptures(const ADarkFactoryProcessController* Process,const ADarkFactoryWorld* World,float RightPanelX,float PanelWidth) {
    if (!Process || !World || !Canvas) return;
    const auto& State=Process->GetStatus();
    const bool NIR=State.Phase==EDarkFactoryProcessPhase::Arriving || State.Phase==EDarkFactoryProcessPhase::Doors
        || State.Phase==EDarkFactoryProcessPhase::Returning;
    const int32 Count=NIR ? 2:3;
    const float Margin=14.f,Header=58.f,Footer=28.f,Extra=NIR ? 36.f:62.f;
    const float Height=FMath::Min((PanelWidth-20.f)/1.75f,FMath::Max(32.f,
        (Canvas->ClipY-2*Margin-70.f-Header-Footer-Count*Extra)/Count));
    const float Width=Height*1.75f,ImageX=RightPanelX+(PanelWidth-Width)*.5f;
    const FLinearColor Pale(.82f,.92f,.97f),Muted(.55f,.70f,.79f);
    DrawRect(FLinearColor(.008f,.015f,.024f,.88f),RightPanelX,Margin,PanelWidth,Header+Count*(Height+Extra)+Footer);
    DrawText(TEXT("Gate camera captures"),Pale,RightPanelX+10,Margin+9,GEngine->GetSmallFont(),1.f);
    DrawText(State.bPaused ? TEXT("Paused / latest captured frames"):TEXT("Updates on inspection"),Muted,RightPanelX+10,Margin+31,GEngine->GetSmallFont(),.85f);
    float Y=Margin+Header;
    for (int32 I=0;I<Count;++I) {
        const FString Name=NIR ? (I==0 ? TEXT("Door NIR / left"):TEXT("Door NIR / right"))
            : FString::Printf(TEXT("R4 thermal / spot %d / color"),I+1);
        DrawText(Name,NIR ? FLinearColor(.70f,.90f,.78f):FLinearColor(.93f,.75f,.51f),RightPanelX+10,Y,GEngine->GetSmallFont(),.95f);
        Y+=22;
        auto* Target=Process->GetPreviewTexture(NIR ? I:I+2);
        if (!Target) {
            DrawRect(FLinearColor(.018f,.034f,.047f,1.f),ImageX,Y,Width,Height);
            DrawText(TEXT("Awaiting this gate's capture"),Muted,ImageX+8,Y+Height*.5f-7,GEngine->GetSmallFont(),.82f);
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
    DrawText(NIR ? TEXT("Raw grayscale model inputs"):TEXT("Color display / model uses grayscale"),Muted,RightPanelX+10,Y+3,GEngine->GetSmallFont(),.82f);
}
void ADarkFactoryHUD::DrawHUD() {
    Super::DrawHUD();
    if (!Canvas) return;
    ADarkFactoryWorld* World=nullptr;
    for (TActorIterator<ADarkFactoryWorld> It(GetWorld());It;++It) { World=*It; break; }
    if (!World) return;
    const auto* Pawn=PlayerOwner ? Cast<ADarkFactoryPawn>(PlayerOwner->GetPawn()):nullptr;
    DrawColliderOverlay();
    const FString FrameRateText=FrameRateSamples>0 ? FString::Printf(TEXT("FPS %.0f"),GetSmoothedFramesPerSecond()):TEXT("FPS --");
    const TCHAR* ObserverView=Pawn && Pawn->IsFactoryLightsView() ? TEXT("Bright factory floor")
        : Pawn && Pawn->IsThermalView() ? TEXT("Thermal") : Pawn && Pawn->bAssistedView ? TEXT("NIR / camera illuminator") : TEXT("Dim visible");
    const TCHAR* HeadlightStatus=Pawn && Pawn->IsFactoryLightsView() ? TEXT("L: headlamp disabled in bright view")
        : Pawn && Pawn->IsObserverHeadlightEmitting() ? TEXT("L: headlamp on")
        : Pawn && Pawn->IsObserverHeadlightEnabled() ? TEXT("L: headlamp saved on") : TEXT("L: headlamp off");
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
        if (ProcessEnabled) DrawProcessStatus(Process,RightX);
        else DrawProductionStatus(World->GetWeldingCell(),RightX);
        DrawColliderLegend(RightX,true);
        if (ProcessEnabled) DrawProcessCaptures(Process,World,RightX,PanelWidth);
        else {
        const float PanelHeight=3.f*ImageHeight+207.f;
        DrawRect(FLinearColor(.008f,.015f,.024f,.82f),RightX,Margin,PanelWidth,PanelHeight);
        float LabelY=Margin+10.f;
        DrawText(TEXT("Fixed NIR sensor"),FLinearColor(.70f,.90f,.78f),RightX+10.f,LabelY,GEngine->GetSmallFont(),1.f);
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
            DrawText(Label,FLinearColor(.93f,.75f,.51f),RightX+10.f,LabelY,GEngine->GetSmallFont(),1.f);
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
        ThermalPreview(TEXT("Thermal / color"),ThermalDisplayMaterial,2);
        ThermalPreview(TEXT("Thermal / monochrome"),ThermalMonochromeDisplayMaterial,0);
        const FString Phase=ProcessEnabled ? ProcessPhaseLabel(Process->GetStatus().Phase):World->GetWeldingCell()->GetCyclePhaseName();
        DrawText(Phase,FLinearColor(.93f,.78f,.51f),RightX+10.f,LabelY,GEngine->GetSmallFont(),.95f);
        }
        DrawRect(FLinearColor(.008f,.015f,.024f,.76f),Margin,Margin,260.f,82.f);
        DrawText(ProcessEnabled ? TEXT("Automotive inspection cell"):TEXT("Car-body welding cell"),FLinearColor(.87f,.94f,1.f),Margin+10.f,Margin+8.f,GEngine->GetSmallFont(),1.1f);
        DrawText(ObserverView,FLinearColor(.68f,.79f,.86f),Margin+10.f,Margin+31.f,GEngine->GetSmallFont(),1.f);
        DrawText(FrameRateText,FLinearColor(.78f,.90f,.80f),Margin+10.f,Margin+54.f,GEngine->GetSmallFont(),1.f);
        if (Pawn && Pawn->IsThermalView()) DrawObserverThermalScale(World->GetThermalRig(),Margin,Canvas->ClipY-140.f,260.f);
        const float Bottom=Canvas->ClipY-58.f;
        DrawRect(FLinearColor(.008f,.015f,.024f,.75f),Margin,Bottom,Canvas->ClipX-2.f*Margin,44.f);
        DrawText(FString::Printf(TEXT("WASD / sticks: fly & look   Q/E / triggers: height   Shift: boost   %s   Esc: quit"),HeadlightStatus),FLinearColor(.76f,.85f,.91f),Margin+10.f,Bottom+5.f,GEngine->GetSmallFont(),.95f);
        DrawText(TEXT("Tab / Y: dim, NIR, thermal, bright   K / D-pad left: palette   Space / View: motion   B / X: viewpoint   M: sound   Home / Menu: home   F8 / LS: colliders"),FLinearColor(.76f,.85f,.91f),Margin+10.f,Bottom+24.f,GEngine->GetSmallFont(),.95f);
        return;
    }
    const float Scale=FMath::Clamp(Canvas->ClipX/1500.f,.7f,1.3f);
    const float Margin=24*Scale;
    const float PanelWidth=490*Scale;
    const float X=Canvas->ClipX-PanelWidth-Margin;
    float Y=Margin;
    DrawRect(FLinearColor(.008,.017,.028,.94),X-14*Scale,Y-12*Scale,PanelWidth+22*Scale,458*Scale);
    auto Line=[&](const FString& Text,FLinearColor Color,float Size=1.f) {
        DrawText(Text,Color,X,Y,GEngine->GetSmallFont(),Size*Scale,false);
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
            DrawThermalScale(Thermal->DisplayPalette,Thermal->MinC,Thermal->MaxC,X,Y+149*Scale,252*Scale,Scale);
        }
        DrawText(Thermal->GetCaseName(),FLinearColor::White,X+290*Scale,Y+12*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawText(Thermal->HasSpatialFields() ? FString::Printf(TEXT("%s: %.0f s"),Thermal->HasHeatTransfer() ? TEXT("Heat exchange"):TEXT("Cooling only"),Thermal->GetCoolingAge()) : TEXT("Uniform comparison"),FLinearColor(.7,.76,.8),X+290*Scale,Y+40*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawText(TEXT("H: case   K: palette"),FLinearColor(.7,.76,.8),X+290*Scale,Y+65*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawText(TEXT("U: +15 s   O: transfer"),FLinearColor(.7,.76,.8),X+290*Scale,Y+90*Scale,GEngine->GetSmallFont(),.85f*Scale);
        if (!Thermal->LastSnapshot.IsEmpty()) DrawText(Thermal->LastSnapshot.EndsWith(TEXT(".png")) ? TEXT("Thermal PNG saved") : Thermal->LastSnapshot.Left(27),FLinearColor(.95,.69,.4),X+290*Scale,Y+118*Scale,GEngine->GetSmallFont(),.85f*Scale);
    }
    DrawRect(FLinearColor(.008,.017,.028,.91),Margin-8*Scale,Margin-10*Scale,460*Scale,132*Scale);
    DrawText(TEXT("AUTOMOTIVE INSPECTION LAB"),FLinearColor(.48,.85,1),Margin,Margin,GEngine->GetMediumFont(),Scale);
    DrawText(FString::Printf(TEXT("OBSERVER: %s"),ObserverView),FLinearColor::White,Margin,Margin+34*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("Tab / Y: dim, NIR, thermal, bright. Sensors stay fixed."),FLinearColor(.7,.76,.8),Margin,Margin+62*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(HeadlightStatus,FLinearColor(.7,.76,.8),Margin,Margin+84*Scale,GEngine->GetSmallFont(),.85f*Scale);
    DrawText(FrameRateText,FLinearColor(.78f,.90f,.80f),Margin,Margin+106*Scale,GEngine->GetSmallFont(),Scale);
    if (Pawn && Pawn->IsThermalView()) DrawObserverThermalScale(World->GetThermalRig(),Margin-8*Scale,Canvas->ClipY-228*Scale,260*Scale,Scale);
    DrawColliderLegend(X-14*Scale,false);
    const float Bottom=Canvas->ClipY-144*Scale;
    DrawRect(FLinearColor(.008,.017,.028,.93),Margin-8*Scale,Bottom-10*Scale,Canvas->ClipX-2*Margin+16*Scale,140*Scale);
    DrawText(FString::Printf(TEXT("NIR SCENARIO: %s  /  seed %d  /  reference label %s (never sent to model)"),*World->GetScenarioName(),World->Seed,*World->GetTruthLabel()),FLinearColor::White,Margin,Bottom,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("1 normal   2 missing   3 misaligned   4 occluded   R new seed   I / N observer IR on/off   Sensor stays IR"),FLinearColor(.73,.83,.88),Margin,Bottom+25*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("WASD + mouse / Xbox sticks: fly & look   Q/E / triggers: down/up   Shift: boost   Home / Menu: home   Esc: quit"),FLinearColor(.73,.83,.88),Margin,Bottom+50*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("Tab / Y: 4 views   T: thermal   K / D-pad left: palette   H / D-pad right: thermal case   F9: thermal PNG   J: fields   U: +15 s   O: transfer   P: inference"),FLinearColor(.73,.83,.88),Margin,Bottom+75*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(World->LastSaved.IsEmpty() ? (Pawn ? Pawn->GetNightVisionStatus()+TEXT("   |   Model input stays grayscale."):TEXT("Waiting for observer")) : TEXT("Saved: ")+World->LastSaved,FLinearColor(.5,.67,.74),Margin,Bottom+102*Scale,GEngine->GetSmallFont(),Scale);
}
