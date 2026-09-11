#include "DarkFactoryGameMode.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryThermalRig.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
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
void ADarkFactoryHUD::DrawHUD() {
    Super::DrawHUD();
    if (!Canvas) return;
    ADarkFactoryWorld* World=nullptr;
    for (TActorIterator<ADarkFactoryWorld> It(GetWorld());It;++It) { World=*It; break; }
    if (!World) return;
    const auto* Pawn=PlayerOwner ? Cast<ADarkFactoryPawn>(PlayerOwner->GetPawn()):nullptr;
    if (!bTriedNightVisionMaterial) {
        bTriedNightVisionMaterial=true;
        if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_UI_Thermal.M_UI_Thermal")))
            ThermalDisplayMaterial=UMaterialInstanceDynamic::Create(Parent,this);
        const TCHAR* MaterialPath=Pawn && Pawn->IsNightVisionQuality() ? TEXT("/Game/Materials/M_UI_NightVisionQuality.M_UI_NightVisionQuality") : TEXT("/Game/Materials/M_UI_NightVision.M_UI_NightVision");
        if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,MaterialPath))
            SensorDisplayMaterial=UMaterialInstanceDynamic::Create(Parent,this);
        else UE_LOG(LogTemp,Warning,TEXT("DF_NV_PREVIEW_MISSING: run Scripts/build.ps1"));
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
        DrawRect(FLinearColor(.008,.017,.028,.94),X-14*Scale,Y-8*Scale,PanelWidth+22*Scale,200*Scale);
        Line(TEXT("FIXED THERMAL / 20-120 C / ADAPTER PENDING"),FLinearColor(.95,.69,.4));
        if (ThermalDisplayMaterial && Thermal->GetTarget()) {
            ThermalDisplayMaterial->SetTextureParameterValue(TEXT("SensorImage"),Thermal->GetTarget());
            ThermalDisplayMaterial->SetScalarParameterValue(TEXT("ThermalPalette"),Thermal->DisplayPalette);
            DrawMaterial(ThermalDisplayMaterial,X,Y,280*Scale,160*Scale,0,0,1,1);
        }
        DrawText(Thermal->GetCaseName(),FLinearColor::White,X+290*Scale,Y+12*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawText(Thermal->HasSpatialFields() ? FString::Printf(TEXT("%s: %.0f s"),Thermal->HasHeatTransfer() ? TEXT("Heat exchange"):TEXT("Cooling only"),Thermal->GetCoolingAge()) : TEXT("Uniform comparison"),FLinearColor(.7,.76,.8),X+290*Scale,Y+40*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawText(TEXT("H: case   K: palette"),FLinearColor(.7,.76,.8),X+290*Scale,Y+65*Scale,GEngine->GetSmallFont(),.9f*Scale);
        DrawText(TEXT("U: +15 s   O: transfer"),FLinearColor(.7,.76,.8),X+290*Scale,Y+90*Scale,GEngine->GetSmallFont(),.85f*Scale);
        if (!Thermal->LastSnapshot.IsEmpty()) DrawText(Thermal->LastSnapshot.EndsWith(TEXT(".png")) ? TEXT("Thermal PNG saved") : Thermal->LastSnapshot.Left(27),FLinearColor(.95,.69,.4),X+290*Scale,Y+118*Scale,GEngine->GetSmallFont(),.85f*Scale);
    }
    DrawRect(FLinearColor(.008,.017,.028,.91),Margin-8*Scale,Margin-10*Scale,460*Scale,108*Scale);
    DrawText(TEXT("AUTOMOTIVE INSPECTION LAB"),FLinearColor(.48,.85,1),Margin,Margin,GEngine->GetMediumFont(),Scale);
    DrawText(Pawn && Pawn->IsThermalView() ? TEXT("OBSERVER: THERMAL / APPARENT TEMPERATURE") : Pawn && Pawn->bAssistedView ? TEXT("OBSERVER: INTENSIFIER-STYLE IR VIEW") : TEXT("OBSERVER: VISIBLE / DIM ROOF-WINDOW LIGHT"),FLinearColor::White,Margin,Margin+34*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("Tab / Xbox Y: visible, NIR, thermal. Sensors stay fixed."),FLinearColor(.7,.76,.8),Margin,Margin+62*Scale,GEngine->GetSmallFont(),Scale);
    const float Bottom=Canvas->ClipY-144*Scale;
    DrawRect(FLinearColor(.008,.017,.028,.93),Margin-8*Scale,Bottom-10*Scale,Canvas->ClipX-2*Margin+16*Scale,140*Scale);
    DrawText(FString::Printf(TEXT("NIR SCENARIO: %s  /  seed %d  /  reference label %s (never sent to model)"),*World->GetScenarioName(),World->Seed,*World->GetTruthLabel()),FLinearColor::White,Margin,Bottom,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("1 normal   2 missing   3 misaligned   4 occluded   R new seed   I / N observer IR on/off   Sensor stays IR"),FLinearColor(.73,.83,.88),Margin,Bottom+25*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("WASD + mouse / Xbox sticks: fly & look   Q/E / triggers: down/up   Shift: boost   Home / Menu: home"),FLinearColor(.73,.83,.88),Margin,Bottom+50*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("Tab / Y: view mode   T: thermal   K / D-pad left: palette   H / D-pad right: thermal case   F9: thermal PNG   J: fields   U: +15 s   O: transfer   P: inference"),FLinearColor(.73,.83,.88),Margin,Bottom+75*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(World->LastSaved.IsEmpty() ? (Pawn ? Pawn->GetNightVisionStatus()+TEXT("   |   Model input stays grayscale."):TEXT("Waiting for observer")) : TEXT("Saved: ")+World->LastSaved,FLinearColor(.5,.67,.74),Margin,Bottom+102*Scale,GEngine->GetSmallFont(),Scale);
}
