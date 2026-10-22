#include "DarkFactoryGameMode.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

void ADarkFactoryPlayerController::UpdateObserverWindow() {
#if PLATFORM_WINDOWS
    if (!FParse::Param(FCommandLine::Get(),TEXT("DFTaskbarOverlay")) ||
        FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))) return;
    auto* Client=GetWorld() ? GetWorld()->GetGameViewport():nullptr;
    const auto Window=Client ? Client->GetWindow():nullptr;
    if (!Window || Window->GetWindowMode()!=EWindowMode::Windowed) return;
    const auto Native=Window->GetNativeWindow();
    if (!Native) return;
    const auto Handle=static_cast<HWND>(Native->GetOSWindowHandle());
    if (!Handle || !IsWindow(Handle)) return;
    if (ObserverWindowHandle!=Handle) {
        ObserverWindowHandle=Handle; ObserverWindowTopmost=-1;
        RECT ClientRect; GetClientRect(Handle,&ClientRect);
        ObserverWindowInitialClient=FIntPoint(ClientRect.right,ClientRect.bottom);
        // A visible fixture needs focus to test focused-window stacking. Normal
        // launches never force activation or steal focus from another app.
        if (FParse::Param(FCommandLine::Get(),TEXT("DFHUDSmoke"))) Window->HACK_ForceToFront();
    }
    const int32 Focused=GetForegroundWindow()==Handle ? 1:0;
    if (ObserverWindowTopmost==Focused) return;
    RECT Bounds; GetWindowRect(Handle,&Bounds);
    int32 X=Bounds.left,Y=Bounds.top;
    UINT Flags=SWP_NOACTIVATE | SWP_NOSIZE;
    if (ObserverWindowTopmost<0) {
        // Center against the full monitor, not its taskbar-reduced work area.
        // Keep the requested native client size: no fullscreen mode or resize.
        MONITORINFO Monitor={}; Monitor.cbSize=sizeof(Monitor);
        if (GetMonitorInfo(MonitorFromWindow(Handle,MONITOR_DEFAULTTONEAREST),&Monitor)) {
            X=Monitor.rcMonitor.left+FMath::Max(0L,(Monitor.rcMonitor.right-Monitor.rcMonitor.left-(Bounds.right-Bounds.left))/2);
            Y=Monitor.rcMonitor.top+FMath::Max(0L,(Monitor.rcMonitor.bottom-Monitor.rcMonitor.top-(Bounds.bottom-Bounds.top))/2);
        }
    } else Flags |= SWP_NOMOVE;
    if (SetWindowPos(Handle,Focused ? HWND_TOPMOST:HWND_NOTOPMOST,X,Y,0,0,Flags)) {
        ObserverWindowTopmost=Focused;
        UE_LOG(LogTemp,Display,TEXT("DF_OBSERVER_WINDOW topmost=%d client=%dx%d"),Focused,
            ObserverWindowInitialClient.X,ObserverWindowInitialClient.Y);
    }
#endif
}

TSharedRef<FJsonObject> ADarkFactoryPlayerController::BuildObserverWindowMetadata() const {
    auto Result=MakeShared<FJsonObject>();
#if PLATFORM_WINDOWS
    const auto Handle=static_cast<HWND>(ObserverWindowHandle);
    if (Handle && IsWindow(Handle)) {
        RECT ClientRect,Bounds; GetClientRect(Handle,&ClientRect); GetWindowRect(Handle,&Bounds);
        Result->SetBoolField(TEXT("foreground"),GetForegroundWindow()==Handle);
        Result->SetBoolField(TEXT("topmost"),(GetWindowLongPtr(Handle,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0);
        Result->SetBoolField(TEXT("capture_size_preserved"),ObserverWindowInitialClient==FIntPoint(ClientRect.right,ClientRect.bottom));
        Result->SetNumberField(TEXT("width"),ClientRect.right); Result->SetNumberField(TEXT("height"),ClientRect.bottom);
        Result->SetNumberField(TEXT("x"),Bounds.left); Result->SetNumberField(TEXT("y"),Bounds.top);
        // Check actual native stacking against the taskbar on this monitor.
        const auto Monitor=MonitorFromWindow(Handle,MONITOR_DEFAULTTONEAREST);
        bool Above=true;
        for (auto Other=GetTopWindow(nullptr);Other;Other=GetWindow(Other,GW_HWNDNEXT)) {
            if (Other==Handle) break;
            TCHAR Class[64]={}; GetClassNameW(Other,Class,UE_ARRAY_COUNT(Class));
            if ((FCString::Strcmp(Class,TEXT("Shell_TrayWnd"))==0 || FCString::Strcmp(Class,TEXT("Shell_SecondaryTrayWnd"))==0) &&
                MonitorFromWindow(Other,MONITOR_DEFAULTTONEAREST)==Monitor && IsWindowVisible(Other)) { Above=false; break; }
        }
        Result->SetBoolField(TEXT("above_taskbar"),Above);
    }
#endif
    return Result;
}
