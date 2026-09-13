#include "DarkFactoryGameMode.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryWeldingCell.h"
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
    for (TActorIterator<ADarkFactoryWorld> It(GetWorld());It;++It) if (It->IsImportedScene()) { Cell=It->GetWeldingCell(); break; }
    if (!Cell) return O;
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
        DrawProductionStatus(World->GetWeldingCell(),RightX);
        DrawColliderLegend(RightX,true);
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
        const FString Phase=World->GetWeldingCell()->GetCyclePhaseName();
        DrawText(Phase,FLinearColor(.93f,.78f,.51f),RightX+10.f,LabelY,GEngine->GetSmallFont(),.95f);
        DrawRect(FLinearColor(.008f,.015f,.024f,.76f),Margin,Margin,260.f,82.f);
        DrawText(TEXT("Car-body welding cell"),FLinearColor(.87f,.94f,1.f),Margin+10.f,Margin+8.f,GEngine->GetSmallFont(),1.1f);
        DrawText(ObserverView,FLinearColor(.68f,.79f,.86f),Margin+10.f,Margin+31.f,GEngine->GetSmallFont(),1.f);
        DrawText(FrameRateText,FLinearColor(.78f,.90f,.80f),Margin+10.f,Margin+54.f,GEngine->GetSmallFont(),1.f);
        if (Pawn && Pawn->IsThermalView()) DrawObserverThermalScale(World->GetThermalRig(),Margin,Canvas->ClipY-140.f,260.f);
        const float Bottom=Canvas->ClipY-58.f;
        DrawRect(FLinearColor(.008f,.015f,.024f,.75f),Margin,Bottom,Canvas->ClipX-2.f*Margin,44.f);
        DrawText(FString::Printf(TEXT("WASD / sticks: fly & look   Q/E / triggers: height   Shift: boost   %s"),HeadlightStatus),FLinearColor(.76f,.85f,.91f),Margin+10.f,Bottom+5.f,GEngine->GetSmallFont(),.95f);
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
    DrawText(TEXT("WASD + mouse / Xbox sticks: fly & look   Q/E / triggers: down/up   Shift: boost   Home / Menu: home"),FLinearColor(.73,.83,.88),Margin,Bottom+50*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("Tab / Y: 4 views   T: thermal   K / D-pad left: palette   H / D-pad right: thermal case   F9: thermal PNG   J: fields   U: +15 s   O: transfer   P: inference"),FLinearColor(.73,.83,.88),Margin,Bottom+75*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(World->LastSaved.IsEmpty() ? (Pawn ? Pawn->GetNightVisionStatus()+TEXT("   |   Model input stays grayscale."):TEXT("Waiting for observer")) : TEXT("Saved: ")+World->LastSaved,FLinearColor(.5,.67,.74),Margin,Bottom+102*Scale,GEngine->GetSmallFont(),Scale);
}
