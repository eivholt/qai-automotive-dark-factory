#include "DarkFactoryGameMode.h"
#include "Dom/JsonObject.h"
#include "InputKeyEventArgs.h"
#include "HighResScreenshot.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

// Opt-in rendered input/timing fixture. No model requests or source-truth reads.
void ADarkFactoryPlayerController::TickControlsSmoke() {
    if (!FParse::Param(FCommandLine::Get(),TEXT("DFHUDSmoke"))) return;
    const double Now=FPlatformTime::Seconds();
    if (ControlsSmokeTime==0) ControlsSmokeTime=Now;
    const double Age=Now-ControlsSmokeTime;
    auto Check=[&](bool Pass,const TCHAR* Name) {
        UE_LOG(LogTemp,Display,TEXT("DF_HUD_SMOKE %s %s"),Pass ? TEXT("PASS"):TEXT("FAIL"),Name);
        if (!Pass) { ControlsSmokeStep=99; FPlatformMisc::RequestExitWithStatus(false,1); }
        return Pass;
    };
    auto Input=[&](const FKey& Key,EInputEvent Event,float Value=1.f) {
        InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Value));
    };
    auto Shot=[&](const TCHAR* Name) {
        FString Output=FPaths::Combine(FPaths::ProjectDir(),TEXT("../artifacts/hud-trim-20261018/render"));
        FParse::Value(FCommandLine::Get(),TEXT("DFOutput="),Output);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output,Name),true,false);
    };
    if (ControlsSmokeStep==0 && Age>1) {
        if (FParse::Param(FCommandLine::Get(),TEXT("DFTaskbarOverlay"))) {
            const auto Window=BuildObserverWindowMetadata();
            if (!Check(Window->GetBoolField(TEXT("foreground")) && Window->GetBoolField(TEXT("topmost")) &&
                Window->GetBoolField(TEXT("above_taskbar")) && Window->GetBoolField(TEXT("capture_size_preserved")),
                TEXT("native window above taskbar, capture size preserved"))) return;
        }
        if (!Check(!UsesGamepadHints() && GetControlsReveal()==0,TEXT("keyboard default, startup hidden"))) return;
        Shot(TEXT("active-hidden.png"));
        Input(EKeys::LeftShift,IE_Pressed);
        ControlsSmokeTime=Now; ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==1 && Age>5.6) {
        if (!Check(GetControlsReveal()==0,TEXT("held key prevents idle reveal"))) return;
        Input(EKeys::LeftShift,IE_Released);
        ControlsSmokeTime=Now; ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==2 && Age>4.9) {
        if (!Check(GetControlsReveal()==0,TEXT("hidden before five seconds"))) return;
        ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==3 && Age>5.12) {
        if (!Check(GetControlsReveal()>0 && GetControlsReveal()<1,TEXT("slide during reveal"))) return;
        Shot(TEXT("keyboard-sliding.png")); ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==4 && Age>5.4) {
        if (!Check(!UsesGamepadHints() && GetControlsReveal()==1,TEXT("keyboard idle revealed"))) return;
        Shot(TEXT("keyboard-idle.png"));
        ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==5 && Age>5.7) {
        Input(EKeys::Gamepad_LeftX,IE_Axis,.02f);
        if (!Check(!UsesGamepadHints() && GetControlsReveal()==1,TEXT("controller drift ignored"))) return;
        Input(EKeys::Gamepad_LeftTrigger,IE_Pressed);
        Input(EKeys::Gamepad_LeftTrigger,IE_Released);
        if (!Check(UsesGamepadHints() && GetControlsReveal()==0,TEXT("gamepad input hides and switches"))) return;
        ControlsSmokeTime=Now; ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==6 && Age>5.4) {
        Input(EKeys::MouseX,IE_Axis,0.f);
        if (!Check(UsesGamepadHints() && GetControlsReveal()==1,TEXT("gamepad idle, zero mouse ignored"))) return;
        Shot(TEXT("gamepad-idle.png")); ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==7 && Age>5.7) {
        Input(EKeys::MouseX,IE_Axis,1.f);
        if (!Check(!UsesGamepadHints() && GetControlsReveal()==0,TEXT("mouse restores keyboard and hides"))) return;
        Shot(TEXT("mouse-active-hidden.png"));
        ControlsSmokeTime=Now; ++ControlsSmokeStep;
    } else if (ControlsSmokeStep==8 && Age>1) {
        UE_LOG(LogTemp,Display,TEXT("DF_HUD_SMOKE COMPLETE"));
        FPlatformMisc::RequestExitWithStatus(false,0); ++ControlsSmokeStep;
    }
}
