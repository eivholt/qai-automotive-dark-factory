#include "DarkFactoryGraphics.h"
#include "Components/SceneCaptureComponent2D.h"
#include "HAL/IConsoleManager.h"
#include "RenderUtils.h"
#include "RHI.h"

void DarkFactoryGraphics::ConfigureSensorCapture(USceneCaptureComponent2D* Capture) {
    if (!Capture) return;
    Capture->bUseRayTracingIfEnabled=false;
    auto& PP=Capture->PostProcessSettings;
    PP.bOverride_DynamicGlobalIlluminationMethod=true;
    PP.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    PP.bOverride_ReflectionMethod=true; PP.ReflectionMethod=EReflectionMethod::None;
    PP.bOverride_RayTracingAO=true; PP.RayTracingAO=false;
    Capture->ShowFlags.SetGlobalIllumination(false);
    Capture->ShowFlags.SetLumenGlobalIllumination(false);
    Capture->ShowFlags.SetLumenReflections(false);
    Capture->ShowFlags.SetReflectionEnvironment(false);
    Capture->ShowFlags.SetScreenSpaceReflections(false);
    Capture->ShowFlags.SetScreenPercentage(false);
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetMotionBlur(false);
}
bool DarkFactoryGraphics::HardwareRayTracingSupported() {
#if RHI_RAYTRACING
    return IsRayTracingEnabled() && (GRHISupportsInlineRayTracing || GRHISupportsRayTracingShaders);
#else
    return false;
#endif
}
bool DarkFactoryGraphics::SoftwareLumenSupported() {
    const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.GenerateMeshDistanceFields"));
    return C && C->GetInt()!=0;
}
