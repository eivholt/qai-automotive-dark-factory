#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryProcessController.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryFactoryLights.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/InputSettings.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

struct FDarkFactoryGraphicsQA {
    double Started=FPlatformTime::Seconds(),ReadySince=0,ActiveStarted=0,Next=0;
    int32 Index=0,PrimeRemaining=3;
    bool Initialized=false,Configure=true,Passed=true,Finished=false;
    FString Root;
    TArray<TSharedPtr<FJsonValue>> Samples;
    TArray<TArray<uint8>> Reference;
    TArray<TWeakObjectPtr<USceneCaptureComponent2D>> Cameras;
    TSharedPtr<FJsonObject> StartupGraphics;
    FIntPoint ViewportSize=FIntPoint::ZeroValue;
    EWindowMode::Type WindowMode=EWindowMode::Windowed;
};
namespace {
TArray<uint8> GraphicsPNG(UTextureRenderTarget2D* Target) {
    if (!Target || Target->SizeX!=448 || Target->SizeY!=256) return {};
    TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return {};
    for (auto& Pixel:Pixels) Pixel.A=255;
    TArray64<uint8> Encoded; FImageUtils::PNGCompressImageArray(448,256,Pixels,Encoded);
    TArray<uint8> Bytes; Bytes.Append(Encoded.GetData(),Encoded.Num()); return Bytes;
}
TSharedRef<FJsonObject> CaptureFlags(const USceneCaptureComponent2D* C) {
    auto O=MakeShared<FJsonObject>();
    O->SetBoolField(TEXT("hardware_rt"),C->bUseRayTracingIfEnabled);
    O->SetBoolField(TEXT("gi"),C->ShowFlags.GlobalIllumination || C->ShowFlags.LumenGlobalIllumination);
    O->SetBoolField(TEXT("reflections"),C->ShowFlags.LumenReflections || C->ShowFlags.ReflectionEnvironment || C->ShowFlags.ScreenSpaceReflections);
    O->SetBoolField(TEXT("temporal_aa"),C->ShowFlags.TemporalAA);
    O->SetBoolField(TEXT("screen_percentage"),C->ShowFlags.ScreenPercentage);
    O->SetBoolField(TEXT("pp_gi"),C->PostProcessSettings.DynamicGlobalIlluminationMethod!=EDynamicGlobalIlluminationMethod::None);
    O->SetBoolField(TEXT("pp_reflections"),C->PostProcessSettings.ReflectionMethod!=EReflectionMethod::None);
    return O;
}
}
void ADarkFactoryWorld::GraphicsSmokeTick(double Now) {
    if (!GraphicsQA) GraphicsQA=MakeShared<FDarkFactoryGraphicsQA>();
    auto& QA=*GraphicsQA; if (QA.Finished) return;
    auto Finish=[&](const FString& Error) {
        QA.Finished=true; QA.Passed &= Error.IsEmpty();
        auto Report=MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("schema"),TEXT("observer-graphics-smoke-v1"));
        Report->SetStringField(TEXT("source"),TEXT("frozen_rendering_qa_no_inference"));
        Report->SetStringField(TEXT("error"),Error);
        Report->SetNumberField(TEXT("capture_prime_frames"),3-QA.PrimeRemaining);
        Report->SetNumberField(TEXT("sensor_render_sequence"),0);
        Report->SetStringField(TEXT("observer_thermal_range"),TEXT("0-120 C"));
        Report->SetNumberField(TEXT("startup_wait_seconds"),QA.ActiveStarted>0 ? QA.ActiveStarted-QA.Started:Now-QA.Started);
        Report->SetNumberField(TEXT("model_http_requests"),ProcessController ? ProcessController->BuildMetadata()->GetNumberField(TEXT("model_http_requests")):-1);
        Report->SetArrayField(TEXT("samples"),QA.Samples);
        if (QA.StartupGraphics) Report->SetObjectField(TEXT("startup_graphics"),QA.StartupGraphics);
        Report->SetArrayField(TEXT("observer_scene_roi"),{MakeShared<FJsonValueNumber>(410),MakeShared<FJsonValueNumber>(320),MakeShared<FJsonValueNumber>(1200),MakeShared<FJsonValueNumber>(770)});
        QA.Passed &= QA.Samples.Num()==32 && Report->GetNumberField(TEXT("model_http_requests"))==0;
        for (const auto& Sample:QA.Samples) QA.Passed &= IFileManager::Get().FileExists(*FPaths::Combine(QA.Root,Sample->AsObject()->GetStringField(TEXT("screenshot"))));
        Report->SetBoolField(TEXT("passed"),QA.Passed);
        FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        const bool Saved=!QA.Root.IsEmpty() && FFileHelper::SaveStringToFile(Text,*FPaths::Combine(QA.Root,TEXT("observer_graphics.json")));
        UE_LOG(LogTemp,Display,TEXT("DF_GRAPHICS_SMOKE passed=%d states=%d error=%s"),QA.Passed && Saved,QA.Samples.Num(),*Error);
        FPlatformMisc::RequestExitWithStatus(false,QA.Passed && Saved ? 0:1);
    };
    if (QA.Root.IsEmpty()) {
        QA.Root=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ObserverGraphics"));
        FParse::Value(FCommandLine::Get(),TEXT("DFOutput="),QA.Root);
        QA.Root=FPaths::ConvertRelativePathToFull(QA.Root);
        IFileManager::Get().MakeDirectory(*QA.Root,true);
        if (IFileManager::Get().FileExists(*FPaths::Combine(QA.Root,TEXT("observer_graphics.json")))) {
            // Do not overwrite an earlier proof, even on an invalid new launch.
            QA.Finished=true; UE_LOG(LogTemp,Error,TEXT("DF_GRAPHICS existing report: use fresh output")); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
    }
    if (Now-QA.Started>720. || (QA.ActiveStarted>0 && Now-QA.ActiveStarted>150.)) { Finish(TEXT("shader/startup or active fixture timeout")); return; }
    ADarkFactoryPawn* Observer=nullptr;
    for (TActorIterator<ADarkFactoryPawn> It(GetWorld());It;++It) { Observer=*It; break; }
    if (!Observer || !ProcessController || !ProcessScene || !ThermalRig || !WeldingCell) { Finish(TEXT("missing process rendering dependency")); return; }
    if (!QA.Initialized) {
        // Compilation is polled without blocking the game or rendering threads.
        // Static meshes, shaders and distance fields must all finish before the
        // first frozen frame; then allow streaming and Lumen caches to converge.
        bool Compiling=false;
#if WITH_EDITOR
        Compiling=FAssetCompilingManager::Get().GetNumRemainingAssets()>0 || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling());
#endif
        if (Compiling) { QA.ReadySince=0; return; }
        if (QA.ReadySince==0) QA.ReadySince=Now;
        if (Now-QA.ReadySince<5.) return;
        QA.StartupGraphics=Observer->BuildGraphicsMetadata();
        if (auto* Viewport=GetWorld()->GetGameViewport()->Viewport) {
            QA.ViewportSize=Viewport->GetSizeXY(); QA.WindowMode=Viewport->GetWindowMode();
        }
        ProcessController->SetActorTickEnabled(false); WeldingCell->SetAnimationEnabled(false); ThermalRig->SetFrozen(true);
        Observer->SetCaptureAutomation(true); Observer->SetNightVisionSmokeFrame(11);
        if (IQ9Props) IQ9Props->SetLedTimeForQA(109.);
        if (WeldingSparks) { WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false); }
        if (WeldingSmoke) { WeldingSmoke->ResetParticles(); WeldingSmoke->SetEffectsEnabled(false); }
        double Clock=100.;
        if (!ProcessScene->PrepareCapture(TEXT("adhesive"),0,9031,Clock)) { Finish(TEXT("could not prepare frozen physical heat sample")); return; }
        ProcessScene->RequestInspectionView(TEXT("adhesive"),0,Clock,true);
        ProcessScene->ApplyAssemblyFrame(EDarkFactoryProcessPhase::Adhesive,0.,Clock,0.,true);
        FVector Lens; FRotator LensRotation; float FOV=0;
        ProcessScene->GetInspectionCameraPose(TEXT("adhesive"),0,Lens,LensRotation,FOV);
        TArray<USceneCaptureComponent2D*> ModelCameras; ProcessController->GetComponents(ModelCameras);
        for (const TCHAR* Name:{TEXT("R4NIRCapture"),TEXT("R4ThermalCapture")}) {
            USceneCaptureComponent2D* Found=nullptr;
            for (auto* C:ModelCameras) if (C->GetName()==Name) { Found=C; break; }
            if (!Found) { Finish(TEXT("missing physical R4 modality capture")); return; }
            Found->SetWorldLocationAndRotation(Lens,LensRotation); Found->FOVAngle=FOV;
            for (const auto& B:Found->PostProcessSettings.WeightedBlendables.Array) if (auto* M=Cast<UMaterialInstanceDynamic>(B.Object)) {
                if (FString(Name).Contains(TEXT("Thermal"))) ThermalRig->PrepareInspectionMaterial(M);
            }
            QA.Cameras.Add(Found);
        }
        TArray<USpotLightComponent*> Lights; ProcessController->GetComponents(Lights);
        for (auto* Light:Lights) { Light->SetWorldLocationAndRotation(Lens,LensRotation); Light->SetVisibility(true); }
        QA.Cameras.Add(Sensor.Get());
        TArray<USceneCaptureComponent2D*> ThermalCameras; ThermalRig->GetComponents(ThermalCameras);
        if (ThermalCameras.Num()!=1) { Finish(TEXT("unexpected fixed thermal camera count")); return; }
        QA.Cameras.Add(ThermalCameras[0]); QA.Reference.SetNum(4);
        const FVector Eye(-850,-1040,720),Aim(0,-220,260);
        Observer->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        if (auto* C=Observer->FindComponentByClass<UCameraComponent>()) C->SetFieldOfView(70.f);
        QA.Initialized=true; QA.ActiveStarted=Now; QA.Next=Now+.1;
        UE_LOG(LogTemp,Display,TEXT("DF_GRAPHICS_SMOKE ready shader_assets=0 output=%s"),*QA.Root);
        return;
    }
    if (Now<QA.Next) return;
    if (QA.PrimeRemaining>0) {
        // The first capture allocates persistent view state and can precede
        // blendable/primitive render-state publication. Prime actual cameras,
        // one pass per rendered frame, rather than only warming the observer.
        ThermalRig->CaptureGPU();
        for (int32 I=0;I<3;++I) if (auto* C=QA.Cameras[I].Get()) C->CaptureScene();
        --QA.PrimeRemaining;
        QA.Next=Now+(QA.PrimeRemaining==0 ? 8.:.15);
        return;
    }
    if (QA.Index>=32) { Finish(TEXT("")); return; }
    static const int32 Modes[]={0,3,1,2};
    const int32 Mode=Modes[QA.Index/8],Bits=QA.Index%8;
    if (QA.Configure) {
        // Exercise the actual viewport F11 route, including the fullscreen
        // shortcut that runs BEFORE PlayerController input, for every state.
        Observer->SetGraphicsOptions((Bits&1)!=0,(Bits&2)!=0,(Bits&4)==0);
        Observer->SetObserverMode(Mode);
        auto* Client=GetWorld()->GetGameViewport();
        if (!Client || !Client->Viewport) { Finish(TEXT("missing input viewport")); return; }
        const auto Device=IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
        Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F11,IE_Pressed,1.f,1,Device,false,Client->Viewport));
        Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F11,IE_Released,0.f,1,Device,false,Client->Viewport));
        if (FactoryLights) FactoryLights->SetStartupTimeForQA(3.f);
        QA.Configure=false; QA.Next=Now+2.; return;
    }
    auto Sample=Observer->BuildGraphicsMetadata();
    const auto* Viewport=GetWorld()->GetGameViewport()->Viewport;
    Sample->SetBoolField(TEXT("f11_fullscreen_disabled"),!GetDefault<UInputSettings>()->bF11TogglesFullscreen);
    Sample->SetBoolField(TEXT("window_mode_unchanged"),Viewport && Viewport->GetWindowMode()==QA.WindowMode);
    Sample->SetBoolField(TEXT("viewport_size_unchanged"),Viewport && Viewport->GetSizeXY()==QA.ViewportSize);
    QA.Passed &= Sample->GetBoolField(TEXT("f11_fullscreen_disabled")) && Sample->GetBoolField(TEXT("window_mode_unchanged")) && Sample->GetBoolField(TEXT("viewport_size_unchanged"));
    const FString Stem=FString::Printf(TEXT("mode%d_d%d_l%d_r%d"),Mode,Bits&1,(Bits>>1)&1,(Bits>>2)&1);
    const FString Screen=Stem+TEXT("_observer.png"); Sample->SetStringField(TEXT("screenshot"),Screen);
    const bool Infrared=Mode==1 || Mode==2;
    const bool ExpectedDLSS=!Infrared && (Bits&1)!=0 && Sample->GetBoolField(TEXT("dlss_supported"));
    const bool ExpectedLumen=!Infrared && (Bits&2)!=0;
    const bool ExpectedRT=ExpectedLumen && (Bits&4)!=0 && Sample->GetBoolField(TEXT("hardware_rt_supported"));
    QA.Passed &= Sample->GetBoolField(TEXT("dlss_enabled"))==ExpectedDLSS && Sample->GetBoolField(TEXT("lumen_enabled"))==ExpectedLumen;
    QA.Passed &= Sample->GetBoolField(TEXT("hardware_rt_enabled"))==ExpectedRT && Sample->GetBoolField(TEXT("actual_view_ray_tracing_allowed"))==ExpectedRT;
    QA.Passed &= Sample->GetStringField(TEXT("actual_temporal_upscaler")).Contains(TEXT("DLSS"))==ExpectedDLSS;
    QA.Passed &= Sample->GetNumberField(TEXT("rendered_view_count"))>0;
    TArray<TSharedPtr<FJsonValue>> Sensors;
    static const TCHAR* Names[]={TEXT("r4_nir"),TEXT("r4_thermal"),TEXT("fixed_nir"),TEXT("fixed_thermal")};
    ThermalRig->CaptureGPU();
    for (int32 I=0;I<QA.Cameras.Num();++I) {
        auto* C=QA.Cameras[I].Get(); if (!C) { Finish(TEXT("sensor component disappeared")); return; }
        if (I!=3) C->CaptureScene();
        const auto Bytes=GraphicsPNG(C->TextureTarget);
        const FString File=Stem+TEXT("_")+Names[I]+TEXT(".png");
        const bool Saved=Bytes.Num()>0 && FFileHelper::SaveArrayToFile(Bytes,*FPaths::Combine(QA.Root,File));
        if (QA.Index==0) QA.Reference[I]=Bytes;
        const bool Parity=Saved && Bytes==QA.Reference[I]; QA.Passed &= Parity;
        auto S=CaptureFlags(C); S->SetStringField(TEXT("name"),Names[I]); S->SetStringField(TEXT("image"),File); S->SetBoolField(TEXT("byte_parity"),Parity);
        for (const TCHAR* Key:{TEXT("hardware_rt"),TEXT("gi"),TEXT("reflections"),TEXT("temporal_aa"),TEXT("screen_percentage"),TEXT("pp_gi"),TEXT("pp_reflections")}) QA.Passed &= !S->GetBoolField(Key);
        Sensors.Add(MakeShared<FJsonValueObject>(S));
    }
    Sample->SetArrayField(TEXT("sensors"),Sensors); QA.Samples.Add(MakeShared<FJsonValueObject>(Sample));
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(QA.Root,Screen),true,false);
    UE_LOG(LogTemp,Display,TEXT("DF_GRAPHICS_SAMPLE %s actual_upscaler=%s parity=%d"),*Stem,*Sample->GetStringField(TEXT("actual_temporal_upscaler")),QA.Passed);
    ++QA.Index; QA.Configure=true; QA.Next=Now+.1;
}
