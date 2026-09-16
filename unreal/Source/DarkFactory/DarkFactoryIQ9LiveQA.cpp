#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryProcessController.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

struct FDarkFactoryLedClockQA {
    double Started=FPlatformTime::Seconds(),PhaseStarted=0,Pose=0,Clock=0;
    int32 Phase=0,Samples=0,Transitions=0,Previous=-1,PausedSignature=0,SeenOn=0,SeenOff=0;
    bool Passed=true,GreenShot=false,AmberShot=false;
    FString Root;
};

void ADarkFactoryIQ9Props::LiveClockSmokeTick() {
    if (!LedClockQA) {
        LedClockQA=MakeShared<FDarkFactoryLedClockQA>();
        FParse::Value(FCommandLine::Get(),TEXT("DFOutput="),LedClockQA->Root);
        IFileManager::Get().MakeDirectory(*LedClockQA->Root,true);
    }
    auto& Q=*LedClockQA; const double Now=FPlatformTime::Seconds();
    auto Finish=[&](bool Success) {
        auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("passed"),Success && Q.Passed);
        R->SetStringField(TEXT("scope"),TEXT("Actual process ticks in a no-inference inspection hold, pause and resume; no manually advanced or pinned LED clock"));
        R->SetNumberField(TEXT("samples"),Q.Samples); R->SetNumberField(TEXT("transitions"),Q.Transitions);
        R->SetNumberField(TEXT("phase"),Q.Phase); R->SetBoolField(TEXT("led_time_pinned"),LedTimeOverride>=0);
        R->SetBoolField(TEXT("green_screenshot"),Q.GreenShot); R->SetBoolField(TEXT("amber_screenshot"),Q.AmberShot);
        FString Text; FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));
        const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Q.Root,TEXT("iq9_live_clock.json")));
        bLiveClockSmoke=false; FPlatformMisc::RequestExitWithStatus(false,Success && Q.Passed && Saved ? 0:1);
    };
    if (Now-Q.Started>90.) { Finish(false); return; }
    ADarkFactoryProcessController* Controller=nullptr;
    for (TActorIterator<ADarkFactoryProcessController> It(GetWorld());It;++It) { Controller=*It; break; }
    auto* PC=GetWorld()->GetFirstPlayerController(); auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Controller || !Observer || !SourceCell.IsValid() || Mounts.IsEmpty()) return;
    if (Q.Phase==0) {
        if (Now-Q.Started<18. || Controller->GetStatus().Phase!=EDarkFactoryProcessPhase::PanelPresence) return;
        if (!FParse::Param(FCommandLine::Get(),TEXT("DFNoInference"))) { Finish(false); return; }
        const auto Transform=Mounts[0].Anchor->GetComponentTransform();
        const FVector Center=Transform.GetLocation();
        Observer->SetObserverMode(3); Observer->SetObserverHeadlightEnabled(false);
        Observer->SetActorLocation(Center+Transform.GetUnitAxis(EAxis::Z)*45.+Transform.GetUnitAxis(EAxis::Y)*12.);
        Observer->SetActorRotation((Center-Observer->GetActorLocation()).Rotation());
        Observer->FindComponentByClass<UCameraComponent>()->PostProcessSettings.AutoExposureBias=-2;
        Q.Pose=SourceCell->GetMotionTime(); Q.Clock=AppliedLedSeconds; Q.PhaseStarted=Now; Q.Phase=1;
        return;
    }
    int32 Signature=0; for (int32 I=0;I<LedStates.Num();++I) if (LedStates[I]) Signature|=1<<I;
    ++Q.Samples; Q.Passed &= LedTimeOverride<0 && FMath::IsNearlyEqual(SourceCell->GetMotionTime(),Q.Pose,1.e-6);
    Q.Passed &= FMath::IsNearlyEqual(AppliedLedSeconds,SourceCell->GetEffectTime(),1.e-6);
    if (Q.Previous>=0 && Signature!=Q.Previous) ++Q.Transitions;
    Q.Previous=Signature;
    const double Elapsed=Now-Q.PhaseStarted;
    if (Q.Phase==2) {
        Q.Passed &= Controller->GetStatus().bPaused && Signature==Q.PausedSignature && AppliedLedSeconds==Q.Clock;
        if (Elapsed>1.6) { Controller->TogglePaused(); Q.Phase=3; Q.PhaseStarted=Now; Q.SeenOn=Q.SeenOff=0; }
        return;
    }
    Q.SeenOn|=Signature; Q.SeenOff|=(~Signature)&4095;
    if (Elapsed>.6 && !Q.GreenShot && (Signature&7)==6) {
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Q.Root,TEXT("live_green_pulse.png")),false,false); Q.GreenShot=true;
    }
    if (Elapsed>.6 && !Q.AmberShot && (Signature&7)==1) {
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Q.Root,TEXT("live_amber_pulse.png")),false,false); Q.AmberShot=true;
    }
    if (Elapsed<3.4) return;
    Q.Passed &= Q.SeenOn==4095 && Q.SeenOff==4095 && AppliedLedSeconds>Q.Clock+2.;
    if (Q.Phase==1) {
        Controller->TogglePaused(); Q.PausedSignature=Signature; Q.Clock=AppliedLedSeconds; Q.Phase=2; Q.PhaseStarted=Now;
    } else Finish(Q.GreenShot && Q.AmberShot && Q.Transitions>12);
}
