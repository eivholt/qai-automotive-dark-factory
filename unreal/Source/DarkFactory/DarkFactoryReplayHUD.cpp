#include "DarkFactoryGameMode.h"
#include "DarkFactoryProcessController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"

void ADarkFactoryHUD::DrawInspectionReplay(const ADarkFactoryProcessController* Process,float X,float PanelWidth) {
    const FLinearColor Pale(.84f,.92f,.97f),Muted(.60f,.73f,.82f),Amber(1.f,.69f,.25f);
    DrawRect(FLinearColor(.008f,.015f,.024f,.95f),X,14,PanelWidth,Canvas->ClipY-98);
    float Y=23;
    auto Line=[&](const FString& Text,FLinearColor Color,float Scale=.8f) {
        FString Fit=Text.Replace(TEXT("\n"),TEXT(" ")).Replace(TEXT("\r"),TEXT(" ")); float W=0,H=0;
        GetTextSize(Fit,W,H,GEngine->GetSmallFont(),Scale);
        while (W>PanelWidth-20 && Fit.Len()>3) { Fit.LeftChopInline(1); GetTextSize(Fit+TEXT("..."),W,H,GEngine->GetSmallFont(),Scale); }
        if (Fit!=Text) Fit+=TEXT("...");
        DrawText(Fit,Color,X+10,Y,GEngine->GetSmallFont(),Scale); Y+=18;
    };
    Line(TEXT("INSPECTION REPLAY / historical"),Amber,.90f);
    Line(TEXT("F5: live / PgUp-PgDn: history"),Muted);
    const auto* Receipt=Process->GetInspectionReceipt();
    if (!Receipt) { Line(TEXT("No retained requests yet"),Pale); Line(TEXT("Launch with -Enhancements"),Muted); return; }
    Line((Receipt->Stage==TEXT("adhesive") ? FString(TEXT("sealant")):Receipt->Stage)+TEXT(" / ")+Receipt->Body.Right(12),Pale);
    Line(TEXT("Request ")+Receipt->Id.Left(12),Muted);
    Line(FString::Printf(TEXT("Captured %.2f s (monotonic)"),Receipt->CaptureMs/1000.),Muted,.72f);
    Line(Receipt->bComplete ? FString::Printf(TEXT("%s / %s / %.0f ms"),*Receipt->Decision,
        Receipt->bValid ? TEXT("valid reply"):TEXT("invalid / hold"),Receipt->LatencyMs):TEXT("Request pending / no decision"),Pale);
    Line(Receipt->Confidence>=0 ? FString::Printf(TEXT("P(label|G,A,R) %s%.2f%%"),Receipt->bProbabilityBound ? TEXT(">= "):TEXT(""),Receipt->Confidence*100.):TEXT("Conditional score unavailable"),Muted);
    Line(Receipt->LabelMass>=0 ? FString::Printf(TEXT("Label mass %s%.3f"),Receipt->bProbabilityBound ? TEXT(">= "):TEXT(""),Receipt->LabelMass):TEXT("Label mass unavailable"),Muted);
    const float Height=FMath::Min((PanelWidth-20)/1.75f,FMath::Max(24.f,(Canvas->ClipY-Y-250)/3.f));
    for (int32 I=0;I<Receipt->Images.Num();++I) {
        Line(FString::Printf(TEXT("spot %d / exact submitted grayscale"),I+1),Muted,.73f);
        DrawTexture(Receipt->Images[I],X+(PanelWidth-Height*1.75f)*.5f,Y,Height*1.75f,Height,0,0,1,1,FLinearColor::White,BLEND_Opaque);
        Y+=Height+5;
    }
    Line(TEXT("Controller action:"),Amber);
    // Wrap the entire actual action; never invent a model explanation.
    FString Remaining=Receipt->Action;
    while (!Remaining.IsEmpty() && Y<Canvas->ClipY-110) {
        int32 Count=FMath::Min(42,Remaining.Len());
        if (Count<Remaining.Len()) { int32 Space=Remaining.Left(Count).Find(TEXT(" "),ESearchCase::CaseSensitive,ESearchDir::FromEnd); if (Space>10) Count=Space; }
        Line(Remaining.Left(Count),Pale,.75f); Remaining=Remaining.Mid(Count).TrimStart();
    }
}
