#include "DarkFactoryThermalRig.h"
#include "DarkFactoryThermalRadiometry.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryProcessController.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FDarkFactoryThermalRangeQA {
    double Started=FPlatformTime::Seconds(),Next=0;
    int32 Sample=0,PrimeRemaining=3;
    bool Initialized=false,Configure=true,Passed=true,Finished=false;
    FString Root;
    float HotSurfaceC=20,HotApparentC=20;
    TArray<TSharedPtr<FJsonValue>> Samples,FixedNames,NIRNames;
    TArray<uint8> FixedReference,NIRReference;
};

namespace {
TArray<uint8> RangePNG(UTextureRenderTarget2D* Target) {
    if (!Target || Target->SizeX!=448 || Target->SizeY!=256) return {};
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return {};
    for (auto& Pixel:Pixels) Pixel.A=255;
    TArray64<uint8> Encoded; FImageUtils::PNGCompressImageArray(448,256,Pixels,Encoded);
    TArray<uint8> Bytes; Bytes.Append(Encoded.GetData(),Encoded.Num()); return Bytes;
}
TArray<TSharedPtr<FJsonValue>> RangeBox(int32 Left,int32 Top,int32 Right,int32 Bottom) {
    return {MakeShared<FJsonValueNumber>(Left),MakeShared<FJsonValueNumber>(Top),MakeShared<FJsonValueNumber>(Right),MakeShared<FJsonValueNumber>(Bottom)};
}
}

void ADarkFactoryThermalRig::TickDisplayRangeSmoke() {
    if (!DisplayRangeQA) DisplayRangeQA=MakeShared<FDarkFactoryThermalRangeQA>();
    auto& QA=*DisplayRangeQA;
    if (QA.Finished) return;
    const double Now=FPlatformTime::Seconds();
    auto Fail=[&](const TCHAR* Message) {
        QA.Passed=false; QA.Finished=true;
        UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_RANGE_FAILED %s"),Message);
        FPlatformMisc::RequestExitWithStatus(false,1);
    };
    if (Now-QA.Started>45.) { Fail(TEXT("45 second fixture timeout")); return; }
    if (!QA.Initialized) {
        if (Now-QA.Started<4.) return;
        auto* World=CellOwner.Get();
        auto* Process=World ? World->GetProcessController():nullptr;
        auto* Cell=World ? World->GetWeldingCell():nullptr;
        ADarkFactoryProcessScene* Scene=nullptr; ADarkFactoryPawn* Observer=nullptr;
        for (TActorIterator<ADarkFactoryProcessScene> It(GetWorld());It;++It) { Scene=*It; break; }
        for (TActorIterator<ADarkFactoryPawn> It(GetWorld());It;++It) { Observer=*It; break; }
        if (!bImportedProfiles || !IsReady() || !Process || !Cell || !Scene || !Observer ||
            !FParse::Param(FCommandLine::Get(),TEXT("DFNoInference"))) {
            Fail(TEXT("requires imported process scene, ready thermal rig and DFNoInference")); return;
        }
        QA.Root=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ThermalRanges"));
        FParse::Value(FCommandLine::Get(),TEXT("DFOutput="),QA.Root);
        QA.Root=FPaths::ConvertRelativePathToFull(QA.Root);
        IFileManager::Get().MakeDirectory(*QA.Root,true);
        if (IFileManager::Get().FileExists(*FPaths::Combine(QA.Root,TEXT("thermal_ranges.json")))) {
            Fail(TEXT("use a fresh output directory; existing range report is preserved")); return;
        }
        if (!Process->GetStatus().bPaused) World->ToggleWeldingAnimation();
        Process->SetActorTickEnabled(false); Cell->SetAnimationEnabled(false); SetFrozen(true);
        Observer->SetCaptureAutomation(true); Observer->SetNightVisionSmokeFrame(11);
        for (TActorIterator<ADarkFactoryIQ9Props> It(GetWorld());It;++It) It->SetLedTimeForQA(109.);
        for (TActorIterator<ADarkFactoryWeldingSparks> It(GetWorld());It;++It) { It->ResetParticles(); It->SetEffectsEnabled(false); }
        for (TActorIterator<ADarkFactoryWeldingSmoke> It(GetWorld());It;++It) { It->ResetParticles(); It->SetEffectsEnabled(false); }
        // Explicit offline pose/heat fixture: seek an active weld, then expose the
        // same frozen surface by withdrawing the arms without advancing its clock.
        Scene->BeginBody(1,100.,0);
        Scene->ApplyAssemblyFrame(EDarkFactoryProcessPhase::PanelPlacement,6.,106.,0.,true);
        Scene->ApplyAssemblyFrame(EDarkFactoryProcessPhase::Welding,1.5,107.5,0.,true);
        Scene->ApplyAssemblyFrame(EDarkFactoryProcessPhase::PanelSeated,0.,107.5,0.,true);
        for (const auto& S:Surfaces) for (const auto& Patch:S.WeldHeatPatches) {
            if (Patch.SurfaceC>QA.HotSurfaceC) {
                QA.HotSurfaceC=Patch.SurfaceC;
                QA.HotApparentC=DarkFactoryThermal::ApparentTemperatureC(Patch.SurfaceC,S.Emissivity,AmbientC);
            }
        }
        if (QA.HotSurfaceC<=1000 || QA.HotApparentC<=1000) { Fail(TEXT("authored process weld did not produce a visible high-temperature sample")); return; }
        FVector Local,Normal;
        if (!Cell->GetPanelContact(0,Local,Normal)) { Fail(TEXT("missing physical weld site")); return; }
        const auto Carriage=Cell->GetCarriageRoot()->GetComponentTransform();
        const FVector Point=Carriage.TransformPosition(Local);
        const FVector WorldNormal=Carriage.TransformVectorNoScale(Normal).GetSafeNormal();
        const FVector Eye=Point+WorldNormal*75.+FVector(0,0,12.);
        const FRotator Rotation=(Point-Eye).Rotation();
        // Offline fixture only: the original overview camera no longer sees the
        // relocated trunk contact. Pin its thermal capture to this new exposed
        // core once, before any range/palette comparisons; keep its sensor
        // encoding 20..120 C. NIR and all interactive camera poses are unchanged.
        ThermalSensor->SetWorldLocationAndRotation(Eye,Rotation); ThermalSensor->FOVAngle=50.f;
        Observer->SetActorLocationAndRotation(Eye,Rotation);
        auto* ObserverCamera=Observer->FindComponentByClass<UCameraComponent>();
        if (!ObserverCamera) { Fail(TEXT("missing observer camera")); return; }
        ObserverCamera->SetFieldOfView(50.f);
        Observer->SetObserverMode(2);
        RangeQATarget=NewObject<UTextureRenderTarget2D>(this);
        RangeQATarget->InitCustomFormat(448,256,PF_B8G8R8A8,true);
        RangeQATarget->TargetGamma=1.f; RangeQATarget->UpdateResourceImmediate(true);
        RangeQACamera=NewObject<USceneCaptureComponent2D>(this);
        AddInstanceComponent(RangeQACamera); RangeQACamera->SetupAttachment(SceneRoot);
        RangeQACamera->RegisterComponent(); RangeQACamera->SetWorldLocationAndRotation(Eye,Rotation);
        RangeQACamera->FOVAngle=50.f; RangeQACamera->CaptureSource=SCS_FinalColorLDR;
        RangeQACamera->TextureTarget=RangeQATarget;
        RangeQACamera->bCaptureEveryFrame=false; RangeQACamera->bCaptureOnMovement=false;
        RangeQACamera->bAlwaysPersistRenderingState=true;
        RangeQACamera->ShowFlags=ThermalSensor->ShowFlags;
        RangeQACamera->PostProcessSettings=ThermalSensor->PostProcessSettings;
        RangeQACamera->PostProcessSettings.WeightedBlendables.Array.Reset();
        RangeQAMaterial=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PP_Thermal.M_PP_Thermal")),this);
        RangeQACamera->AddOrUpdateBlendable(RangeQAMaterial,1.f);
        QA.Initialized=true;
        UE_LOG(LogTemp,Display,TEXT("DF_THERMAL_RANGE_START surface=%.2f apparent=%.2f output=%s"),QA.HotSurfaceC,QA.HotApparentC,*QA.Root);
    }
    static const TCHAR* Names[]={TEXT("white"),TEXT("black"),TEXT("iron"),TEXT("restored_white")};
    ADarkFactoryPawn* Observer=nullptr;
    for (TActorIterator<ADarkFactoryPawn> It(GetWorld());It;++It) { Observer=*It; break; }
    if (!Observer) { Fail(TEXT("observer disappeared")); return; }
    if (QA.Sample<4 && QA.Configure) {
        const int32 BeforeCaptures=GPUCaptures,BeforeReadbacks=Readbacks;
        Observer->SetThermalPalette(QA.Sample==3 ? 0:QA.Sample);
        ApplyObserverMaterial(RangeQAMaterial,DisplayPalette);
        QA.Passed &= BeforeCaptures==GPUCaptures && BeforeReadbacks==Readbacks && MinC==20 && MaxC==120;
        QA.Next=Now+.35; QA.Configure=false; return;
    }
    if (Now<QA.Next) return;
    if (QA.PrimeRemaining>0) {
        CaptureGPU(); RangeQACamera->CaptureScene();
        --QA.PrimeRemaining; QA.Next=Now+.15; return;
    }
    if (QA.Sample<4) {
        const FString Name=Names[QA.Sample];
        const FString FixedName=Name+TEXT("_fixed.png"),NIRName=Name+TEXT("_nir.png"),GPUName=Name+TEXT("_gpu.png"),ScreenName=Name+TEXT("_observer.png");
        CaptureGPU(); const auto FixedBytes=RangePNG(ThermalTarget);
        const auto NIRBytes=RangePNG(CellOwner->GetSensorTexture());
        RangeQACamera->CaptureScene(); const auto GPUBytes=RangePNG(RangeQATarget);
        QA.Passed &= !FixedBytes.IsEmpty() && !NIRBytes.IsEmpty() && !GPUBytes.IsEmpty();
        QA.Passed &= FFileHelper::SaveArrayToFile(FixedBytes,*FPaths::Combine(QA.Root,FixedName));
        QA.Passed &= FFileHelper::SaveArrayToFile(NIRBytes,*FPaths::Combine(QA.Root,NIRName));
        QA.Passed &= FFileHelper::SaveArrayToFile(GPUBytes,*FPaths::Combine(QA.Root,GPUName));
        if (QA.Sample==0) { QA.FixedReference=FixedBytes; QA.NIRReference=NIRBytes; }
        else QA.Passed &= QA.FixedReference==FixedBytes && QA.NIRReference==NIRBytes;
        QA.FixedNames.Add(MakeShared<FJsonValueString>(FixedName)); QA.NIRNames.Add(MakeShared<FJsonValueString>(NIRName));
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Name);
        Row->SetNumberField(TEXT("palette"),DisplayPalette); Row->SetNumberField(TEXT("min_c"),GetObserverMinC()); Row->SetNumberField(TEXT("max_c"),GetObserverMaxC());
        Row->SetStringField(TEXT("screenshot"),ScreenName); Row->SetStringField(TEXT("gpu_png"),GPUName);
        Row->SetArrayField(TEXT("observer_roi"),RangeBox(550,260,1050,660));
        Row->SetNumberField(TEXT("hot_surface_c"),QA.HotSurfaceC); Row->SetNumberField(TEXT("hot_apparent_c"),QA.HotApparentC);
        Row->SetObjectField(TEXT("display"),GetObserverDisplayMetadata());
        QA.Samples.Add(MakeShared<FJsonValueObject>(Row));
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(QA.Root,ScreenName),false,false);
        // Never change the palette or viewpoint until the requested frame
        // has rendered. The next configuration happens on the following tick.
        ++QA.Sample; QA.Configure=true; QA.Next=Now+.25; return;
    }
    auto Report=MakeShared<FJsonObject>();
    for (const auto& Value:QA.Samples) QA.Passed &= IFileManager::Get().FileSize(*FPaths::Combine(QA.Root,Value->AsObject()->GetStringField(TEXT("screenshot"))))>0;
    Report->SetStringField(TEXT("schema"),TEXT("darkfactory-thermal-display-smoke-v2"));
    Report->SetStringField(TEXT("source"),TEXT("offline_gpu_display_qa")); Report->SetBoolField(TEXT("passed"),QA.Passed);
    Report->SetStringField(TEXT("scope"),TEXT("Paused authored process heat, physical surface closeup, fixed 0..120 C observer palettes and offline GPU references; no model requests, no calibrated welding temperature claim"));
    Report->SetStringField(TEXT("fixed_thermal_pose_scope"),TEXT("Offline fixture pinned once to new panel core before four palette comparisons; not an interactive camera change"));
    Report->SetObjectField(TEXT("sensor_encoding"),GetEncodingMetadata());
    Report->SetArrayField(TEXT("samples"),QA.Samples); Report->SetArrayField(TEXT("fixed_raw_files"),QA.FixedNames); Report->SetArrayField(TEXT("nir_raw_files"),QA.NIRNames);
    Report->SetArrayField(TEXT("observer_roi"),RangeBox(550,260,1050,660)); Report->SetArrayField(TEXT("observer_scale_roi"),RangeBox(14,746,294,828));
    Report->SetNumberField(TEXT("model_requests"),0); Report->SetNumberField(TEXT("offline_only_additional_capture_targets"),1);
    Report->SetNumberField(TEXT("interactive_additional_capture_targets"),0);
    FString JSON; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));
    const bool Saved=FFileHelper::SaveStringToFile(JSON,*FPaths::Combine(QA.Root,TEXT("thermal_ranges.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    QA.Finished=true;
    UE_LOG(LogTemp,Display,TEXT("DF_THERMAL_RANGE_DONE passed=%d samples=%d"),QA.Passed && Saved,QA.Samples.Num());
    FPlatformMisc::RequestExitWithStatus(false,QA.Passed && Saved ? 0:1);
}
