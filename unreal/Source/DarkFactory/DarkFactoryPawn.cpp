#include "DarkFactoryPawn.h"
#include "DarkFactoryGraphics.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "TemporalUpscaler.h"
#include "Async/Async.h"
#if WITH_DF_DLSS
#include "DLSSLibrary.h"
#endif
#include "DarkFactoryWorld.h"
#include "DarkFactoryGameMode.h"
#include "DarkFactoryProcessController.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactorySoundscape.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Materials/MaterialInstanceDynamic.h"

class FDarkFactoryViewExtension : public FSceneViewExtensionBase {
public:
    FDarkFactoryViewExtension(const FAutoRegister& AutoRegister, ADarkFactoryPawn* InPawn)
        : FSceneViewExtensionBase(AutoRegister), Pawn(InPawn) {}
    virtual int32 GetPriority() const override { return -100; }
    virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override {
        if (View.bIsSceneCapture) {
            View.FinalPostProcessSettings.UserFlags &= ~256;
            View.bAllowRayTracing=false; View.bSceneCaptureUsesRayTracing=false;
            View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
            View.FinalPostProcessSettings.ReflectionMethod=EReflectionMethod::None;
            Family.EngineShowFlags.SetGlobalIllumination(false);
            Family.EngineShowFlags.SetLumenGlobalIllumination(false);
            Family.EngineShowFlags.SetLumenReflections(false);
            Family.EngineShowFlags.SetReflectionEnvironment(false);
            Family.EngineShowFlags.SetScreenSpaceReflections(false);
            Family.EngineShowFlags.SetScreenPercentage(false);
        }
        if (!View.bIsSceneCapture && Pawn.IsValid()) {
            const bool Lumen=Pawn->IsObserverLumenEnabled(),DLSS=Pawn->IsObserverDLSSEnabled();
            View.bAllowRayTracing=Pawn->IsObserverHardwareRayTracingEnabled();
            View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod=Lumen ? EDynamicGlobalIlluminationMethod::Lumen:EDynamicGlobalIlluminationMethod::None;
            View.FinalPostProcessSettings.ReflectionMethod=Lumen ? EReflectionMethod::Lumen:EReflectionMethod::None;
            Family.EngineShowFlags.SetGlobalIllumination(Lumen);
            Family.EngineShowFlags.SetLumenGlobalIllumination(Lumen);
            Family.EngineShowFlags.SetLumenReflections(Lumen);
            Family.EngineShowFlags.SetReflectionEnvironment(Lumen);
            Family.EngineShowFlags.SetScreenSpaceReflections(false);
            // The NVIDIA plugin supplies the temporal upscaler; FXAA/native is
            // retained when disabled, including both infrared display modes.
            View.AntiAliasingMethod=DLSS ? AAM_TemporalAA:AAM_FXAA;
            View.PrimaryScreenPercentageMethod=DLSS ? EPrimaryScreenPercentageMethod::TemporalUpscale:EPrimaryScreenPercentageMethod::SpatialUpscale;
            Family.EngineShowFlags.SetTemporalAA(DLSS);
        }
        if (View.bIsSceneCapture && Pawn.IsValid()) {
            for (TActorIterator<ADarkFactoryWorld> It(Pawn->GetWorld());It;++It) {
                if (It->IsImportedScene()) {
                    // All imported sensors, including physical R4 captures, use
                    // the same fixed shadow/quantization sequence. Persistent
                    // view state is still needed for the grayscale blendable;
                    // GPU frame number is not a sensor augmentation parameter.
                    View.OverrideFrameIndexValue=0u;
                    View.OverrideOutputFrameIndexValue=0u;
                    break;
                }
            }
        }
        if (!View.bIsSceneCapture && Pawn.IsValid()) {
            if (FParse::Param(FCommandLine::Get(),TEXT("DFGraphicsSmoke"))) {
                // Frozen display comparison only. Interactive observer grain
                // and ordinary temporal reconstruction continue to advance.
                View.OverrideFrameIndexValue=0u;
                View.OverrideOutputFrameIndexValue=0u;
            }
            View.ViewLightingChannelMask=Pawn->GetObserverViewLightingMask();
            if (LastMask != View.ViewLightingChannelMask) {
                LastMask = View.ViewLightingChannelMask;
                UE_LOG(LogTemp, Display, TEXT("DF_OBSERVER view_mask=%d pawn=%s eye=%s rotation=%s"), LastMask, *Pawn->GetActorLocation().ToString(), *View.ViewLocation.ToString(), *View.ViewRotation.ToString());
            }
        }
    }
    virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder,FSceneViewFamily& Family) override {
        if (Family.Views.IsEmpty() || Family.Views[0]->bIsSceneCapture || TelemetryPending->Exchange(true)) return;
        // NVIDIA's debug name reads a render-thread-only CVar. Observe it here,
        // then copy plain values back; never access Pawn/UObjects on this thread.
        const auto* Upscaler=Family.GetTemporalUpscalerInterface();
        FString Name=Upscaler ? Upscaler->GetDebugName():TEXT("none");
        const bool RayTracingAllowed=Family.Views[0]->IsRayTracingAllowedForView();
        const TWeakObjectPtr<ADarkFactoryPawn> WeakPawn=Pawn;
        const auto Pending=TelemetryPending;
        AsyncTask(ENamedThreads::GameThread,[WeakPawn,Pending,Name=MoveTemp(Name),RayTracingAllowed]() {
            if (WeakPawn.IsValid()) WeakPawn->RecordGraphicsView(Name,RayTracingAllowed);
            Pending->Store(false);
        });
    }
private:
    // At most one telemetry notification can wait on the game thread.
    TSharedRef<TAtomic<bool>,ESPMode::ThreadSafe> TelemetryPending=MakeShared<TAtomic<bool>,ESPMode::ThreadSafe>(false);
    uint8 LastMask = 255;
    TWeakObjectPtr<ADarkFactoryPawn> Pawn;
};
namespace {
ADarkFactoryWorld* Cell(UWorld* World) {
    for (TActorIterator<ADarkFactoryWorld> It(World);It;++It) return *It;
    return nullptr;
}
}
ADarkFactoryPawn::ADarkFactoryPawn() {
    PrimaryActorTick.bCanEverTick=true;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("ObserverCamera"));
    SetRootComponent(Camera);
    Camera->FieldOfView=72;
    Camera->PostProcessSettings.bOverride_AutoExposureMethod=true;
    Camera->PostProcessSettings.AutoExposureMethod=AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias=true;
    Camera->PostProcessSettings.AutoExposureBias=0;
    Camera->PostProcessSettings.bOverride_ColorSaturation=true;
    Camera->PostProcessSettings.ColorSaturation=FVector4(0,0,0,1);
    ObserverHeadlight=CreateDefaultSubobject<USpotLightComponent>(TEXT("ObserverHeadlight"));
    ObserverHeadlight->SetupAttachment(Camera.Get());
    ObserverHeadlight->SetMobility(EComponentMobility::Movable);
    ObserverHeadlight->SetRelativeLocation(FVector(15,0,-6));
    ObserverHeadlight->ViewLightingChannels.bViewChannel0=true;
    ObserverHeadlight->ViewLightingChannels.bViewChannel1=false;
    ObserverHeadlight->ViewLightingChannels.bViewChannel2=false;
    ObserverHeadlight->SetIntensityUnits(ELightUnits::Lumens);
    ObserverHeadlight->SetIntensity(1500.f*ADarkFactoryWorld::DimVisibleLightScale);
    ObserverHeadlight->SetAttenuationRadius(2400);
    ObserverHeadlight->SetInnerConeAngle(24);
    ObserverHeadlight->SetOuterConeAngle(44);
    ObserverHeadlight->SetCastShadows(true);
    // View channels isolate direct opaque lighting. Disable shared indirect,
    // reflection and translucency paths so this observer aid cannot illuminate NIR.
    ObserverHeadlight->SetIndirectLightingIntensity(0);
    ObserverHeadlight->SetVolumetricScatteringIntensity(0);
    ObserverHeadlight->SetAffectGlobalIllumination(false);
    ObserverHeadlight->SetAffectReflection(false);
    ObserverHeadlight->SetAffectTranslucentLighting(false);
    ObserverHeadlight->SetVisibility(false);
    ObserverNIRIlluminator=CreateDefaultSubobject<USpotLightComponent>(TEXT("ObserverNIRIlluminator"));
    ObserverNIRIlluminator->SetupAttachment(Camera.Get());
    ObserverNIRIlluminator->SetMobility(EComponentMobility::Movable);
    ObserverNIRIlluminator->SetRelativeLocation(FVector(15,0,-6));
    ObserverNIRIlluminator->ViewLightingChannels.bViewChannel0=false;
    ObserverNIRIlluminator->ViewLightingChannels.bViewChannel1=false;
    ObserverNIRIlluminator->ViewLightingChannels.bViewChannel2=true;
    ObserverNIRIlluminator->SetIntensityUnits(ELightUnits::Lumens);
    ObserverNIRIlluminator->SetIntensity(1500);
    ObserverNIRIlluminator->SetAttenuationRadius(2400);
    ObserverNIRIlluminator->SetInnerConeAngle(24);
    ObserverNIRIlluminator->SetOuterConeAngle(44);
    ObserverNIRIlluminator->SetLightColor(FLinearColor::White);
    ObserverNIRIlluminator->SetCastShadows(true);
    ObserverNIRIlluminator->SetIndirectLightingIntensity(0);
    ObserverNIRIlluminator->SetVolumetricScatteringIntensity(0);
    ObserverNIRIlluminator->SetAffectGlobalIllumination(false);
    ObserverNIRIlluminator->SetAffectReflection(false);
    ObserverNIRIlluminator->SetAffectTranslucentLighting(false);
    ObserverNIRIlluminator->SetVisibility(false);
}
void ADarkFactoryPawn::BeginPlay() {
    Super::BeginPlay();
    bAutomation = FParse::Param(FCommandLine::Get(), TEXT("DFWeldingSmoke")) || FParse::Param(FCommandLine::Get(), TEXT("DFSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFBridgeSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFNightVisionSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFReadbackSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFThermalSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFModalityScreenshots")) ||
        FString(FCommandLine::Get()).Contains(TEXT("DFPlan=")) ||
        FString(FCommandLine::Get()).Contains(TEXT("DFDataset="));
    bGraphicsLegacyFixture=bAutomation && !FParse::Param(FCommandLine::Get(),TEXT("DFGraphicsSmoke"));
    bDLSSRequested=!FParse::Param(FCommandLine::Get(),TEXT("DFNoDLSS"));
    bLumenRequested=!FParse::Param(FCommandLine::Get(),TEXT("DFNoLumen"));
    bHardwareRayTracingRequested=!FParse::Param(FCommandLine::Get(),TEXT("DFNoHardwareRayTracing"));
    bHardwareRayTracingAvailable=DarkFactoryGraphics::HardwareRayTracingSupported();
#if WITH_DF_DLSS
    bDLSSAvailable=UDLSSLibrary::IsDLSSSupported();
    DLSSSupportStatus=StaticEnum<UDLSSSupport>()->GetNameStringByValue(static_cast<int64>(UDLSSLibrary::QueryDLSSSupport()));
    if (bDLSSAvailable) {
        bool Supported=false,Fixed=false; float Min=0,Max=100,Sharpness=0;
        UDLSSLibrary::GetDLSSModeInformation(UDLSSMode::Quality,FVector2D(1600,900),Supported,DLSSScreenPercentage,Fixed,Min,Max,Sharpness);
        bDLSSAvailable=Supported && FMath::IsFinite(DLSSScreenPercentage) && DLSSScreenPercentage>0 && DLSSScreenPercentage<100;
    }
#else
    DLSSSupportStatus=TEXT("official plugin not installed at build time");
#endif
    bGraphicsInitialized=true;
    UpdateObserverGraphics();
    // Calibration captures begin with no observer light; welding QA explicitly tests it.
    SetObserverHeadlightEnabled(FParse::Param(FCommandLine::Get(),TEXT("DFWeldingSmoke")));
    auto ReadSetting=[&](const TCHAR* Name, float& Value, float Minimum, float Maximum) {
        GConfig->GetFloat(TEXT("DarkFactory.NightVision"),Name,Value,GGameIni);
        FParse::Value(FCommandLine::Get(),*(FString(TEXT("DFNV"))+Name+TEXT("=")),Value);
        Value=FMath::IsFinite(Value) ? FMath::Clamp(Value,Minimum,Maximum):Minimum;
    };
    ReadSetting(TEXT("Gain"),NightVisionGain,.25f,3.f);
    ReadSetting(TEXT("Grain"),NightVisionGrain,0.f,.12f);
    ReadSetting(TEXT("Vignette"),NightVisionVignette,0.f,.8f);
    ReadSetting(TEXT("Halo"),NightVisionHalo,0.f,.5f);
    ReadSetting(TEXT("Seed"),NightVisionSeed,0.f,10000.f);
    GConfig->GetBool(TEXT("DarkFactory.NightVision"),TEXT("Quality"),bNightVisionQuality,GGameIni);
    if (FParse::Param(FCommandLine::Get(),TEXT("DFNVQuality"))) bNightVisionQuality=true;
    const TCHAR* MaterialPath=bNightVisionQuality ? TEXT("/Game/Materials/M_PP_NightVisionQuality.M_PP_NightVisionQuality") : TEXT("/Game/Materials/M_PP_NightVision.M_PP_NightVision");
    if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,MaterialPath)) {
        NightVisionMaterial=UMaterialInstanceDynamic::Create(Parent,this);
        Camera->AddOrUpdateBlendable(NightVisionMaterial.Get(),1.f);
        ApplyNightVision(NightVisionMaterial);
    } else UE_LOG(LogTemp,Warning,TEXT("DF_NV_MISSING: run Scripts/build.ps1 to generate night-vision materials"));
    if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PP_Thermal.M_PP_Thermal"))) {
        ThermalMaterial=UMaterialInstanceDynamic::Create(Parent,this);
        Camera->AddOrUpdateBlendable(ThermalMaterial.Get(),0.f);
    }
    Home();
    ViewExtension=FSceneViewExtensions::NewExtension<FDarkFactoryViewExtension>(this);
    if (auto* PC=Cast<APlayerController>(GetController())) {
        PC->bShowMouseCursor=false;
        PC->SetInputMode(FInputModeGameOnly());
    }
}
void ADarkFactoryPawn::Home() {
    bBookmarkMoving=false; ObserverBookmarkIndex=0; SmoothedUp=0;
    FVector Location; FRotator Rotation;
    if (const auto* W=Cell(GetWorld())) if (W->GetObserverHome(Location,Rotation)) { SetActorLocation(Location); SetActorRotation(Rotation); return; }
    SetActorLocation(FVector(-300,-780,380));
    SetActorRotation((FVector(0,25,95)-GetActorLocation()).Rotation());
}
void ADarkFactoryPawn::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (NightVisionMaterial) {
        ApplyNightVision(NightVisionMaterial);
    }
    if (bThermalView && ThermalMaterial) {
        if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->ApplyObserverMaterial(ThermalMaterial,Thermal->DisplayPalette);
    }
    if (bAutomation) return;
    if (bBookmarkMoving) {
        if (FMath::Abs(Forward)+FMath::Abs(Right)+FMath::Abs(Up)+FMath::Abs(Yaw)+FMath::Abs(Pitch)>.04f) CancelObserverBookmark();
        else {
            BookmarkElapsed+=DeltaSeconds;
            const float T=FMath::Clamp(BookmarkElapsed/1.65f,0.f,1.f);
            const float Ease=T*T*T*(T*(T*6.f-15.f)+10.f);
            const FVector Position=FMath::Lerp(BookmarkStart,BookmarkEnd,Ease)+FVector::UpVector*(FMath::Sin(PI*Ease)*180.);
            SetActorLocationAndRotation(Position,FQuat::Slerp(BookmarkRotationStart,BookmarkRotationEnd,Ease));
            if (T>=1) bBookmarkMoving=false;
            return;
        }
    }
    // Ease keyboard elevation and trigger input, with quicker braking on release.
    // Exponential response keeps the feel consistent across frame rates.
    const float TargetUp=FMath::Clamp(Up,-1.f,1.f);
    const float Response=FMath::IsNearlyZero(TargetUp) ? 16.f:10.f;
    SmoothedUp=FMath::Lerp(SmoothedUp,TargetUp,1.f-FMath::Exp(-Response*DeltaSeconds));
    if (FMath::Abs(SmoothedUp)<.0001f) SmoothedUp=0;
    const FVector Direction=GetActorForwardVector()*Forward+GetActorRightVector()*Right+FVector::UpVector*SmoothedUp;
    const auto* PC=Cast<APlayerController>(GetController());
    const bool bBoost=PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
    const float Speed=bBoost ? 2700.f:900.f;
    SetActorLocation(GetActorLocation()+Direction.GetClampedToMaxSize(1)*Speed*DeltaSeconds);
    FRotator Rotation=GetActorRotation();
    Rotation.Yaw+=Yaw*95*DeltaSeconds;
    Rotation.Pitch=FMath::Clamp(Rotation.Pitch+Pitch*75*DeltaSeconds,-85.f,85.f);
    Rotation.Roll=0;
    SetActorRotation(Rotation);
}
void ADarkFactoryPawn::MouseYaw(float Value) { if (bAutomation) return; if (FMath::Abs(Value)>.001f) CancelObserverBookmark(); FRotator R=GetActorRotation(); R.Yaw+=Value*.16f; SetActorRotation(R); }
void ADarkFactoryPawn::MousePitch(float Value) { if (bAutomation) return; if (FMath::Abs(Value)>.001f) CancelObserverBookmark(); FRotator R=GetActorRotation(); R.Pitch=FMath::Clamp(R.Pitch-Value*.16f,-85.f,85.f); SetActorRotation(R); }
void ADarkFactoryPawn::SetObserverMode(int32 Mode) {
    auto* W=Cell(GetWorld());
    if (Mode<0 || Mode>3) Mode=0;
    if (Mode==2 && W && W->IsImportedScene() && !W->GetThermalRig()) Mode=0;
    const bool ModeChanged=ObserverMode!=Mode;
    ObserverMode=Mode;
    UpdateObserverGraphics();
    const bool ColorView=Mode==3 || (W && W->IsImportedScene() && Mode==0);
    Camera->PostProcessSettings.ColorSaturation=ColorView ? FVector4(1,1,1,1):FVector4(0,0,0,1);
    bThermalView=Mode==2; bAssistedView=Mode==1;
    UpdateObserverLighting();
    if (W) W->SetFactoryLightsEnabled(Mode==3);
    if (ModeChanged && !bAutomation && W && W->GetSoundscape()) W->GetSoundscape()->NotifyObserverMode(Mode);
    if (NightVisionMaterial) Camera->AddOrUpdateBlendable(NightVisionMaterial.Get(),bAssistedView ? 1.f:0.f);
    if (ThermalMaterial) {
        if (W) if (auto* Thermal=W->GetThermalRig()) Thermal->ApplyObserverMaterial(ThermalMaterial,Thermal->DisplayPalette);
        Camera->AddOrUpdateBlendable(ThermalMaterial.Get(),bThermalView ? 1.f:0.f);
    }
}
void ADarkFactoryPawn::SetObserverHeadlightEnabled(bool Enabled) {
    bObserverHeadlightEnabled=Enabled;
    UpdateObserverLighting();
}
void ADarkFactoryPawn::ToggleObserverHeadlight() {
    if (IsFactoryLightsView()) return; // Bright mode preserves the saved dim-view preference.
    SetObserverHeadlightEnabled(!bObserverHeadlightEnabled);
}
bool ADarkFactoryPawn::IsObserverHeadlightEmitting() const {
    return ObserverHeadlight && ObserverHeadlight->IsVisible();
}
bool ADarkFactoryPawn::IsObserverNIRIlluminatorEmitting() const {
    return ObserverNIRIlluminator && ObserverNIRIlluminator->IsVisible();
}
void ADarkFactoryPawn::SetObserverNIRIlluminatorForQA(bool Enabled) {
    if (bAutomation && ObserverMode==1 && ObserverNIRIlluminator) ObserverNIRIlluminator->SetVisibility(Enabled);
}
void ADarkFactoryPawn::UpdateObserverLighting() {
    if (ObserverHeadlight) ObserverHeadlight->SetVisibility(bObserverHeadlightEnabled && ObserverMode==0);
    if (ObserverNIRIlluminator) ObserverNIRIlluminator->SetVisibility(ObserverMode==1);
}
void ADarkFactoryPawn::ToggleView() { SetObserverMode(bAssistedView ? 0:1); }
void ADarkFactoryPawn::CycleView() { SetObserverMode((ObserverMode+1)%4); }
void ADarkFactoryPawn::ToggleThermal() { SetObserverMode(bThermalView ? 1:2); }
void ADarkFactoryPawn::SetThermalPalette(int32 Palette) {
    if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) {
        Thermal->DisplayPalette=FMath::Clamp(Palette,0,2);
        Thermal->ApplyObserverMaterial(ThermalMaterial,Thermal->DisplayPalette);
    }
}
void ADarkFactoryPawn::CycleThermalPalette() {
    if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) SetThermalPalette((Thermal->DisplayPalette+1)%3);
}
void ADarkFactoryPawn::NextThermalCase() {
    if (const auto* W=Cell(GetWorld())) if (W->IsImportedScene()) return;
    if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) {
        const bool LeavingTransfer=Thermal->IsTransferCase();
        Thermal->NextCase();
        if (Thermal->IsTransferCase()) {
            SetActorLocation(FVector(160,-40,390));
            SetActorRotation((FVector(0,27,350)-GetActorLocation()).Rotation());
        } else if (LeavingTransfer) Home();
    }
}
void ADarkFactoryPawn::ToggleHeatTransfer() { if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->SetHeatTransferEnabled(!Thermal->HasHeatTransfer()); }
void ADarkFactoryPawn::ToggleThermalFields() { if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->SetSpatialFieldsEnabled(!Thermal->HasSpatialFields()); }
void ADarkFactoryPawn::CoolThermal() { if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->AdvanceCooling(); }
void ADarkFactoryPawn::SaveThermal() { if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->RequestSnapshot(); }
void ADarkFactoryPawn::ApplyNightVision(UMaterialInstanceDynamic* Material) const {
    if (!Material) return;
    const auto* W=Cell(GetWorld());
    const float Frame=NightVisionFrameOverride>=0 ? NightVisionFrameOverride :
        bAutomation ? 12.f:FMath::FloorToFloat(GetWorld()->GetTimeSeconds()*24.f);
    Material->SetScalarParameterValue(TEXT("NVGain"),NightVisionGain);
    Material->SetScalarParameterValue(TEXT("NVGrain"),NightVisionGrain);
    Material->SetScalarParameterValue(TEXT("NVVignette"),NightVisionVignette);
    Material->SetScalarParameterValue(TEXT("NVHalo"),NightVisionHalo);
    Material->SetScalarParameterValue(TEXT("NVSeed"),NightVisionSeed);
    Material->SetScalarParameterValue(TEXT("NVFrame"),Frame);
    Material->SetScalarParameterValue(TEXT("NVPhosphor"),!W || W->bGreenDisplay ? 1.f:0.f);
}
FString ADarkFactoryPawn::GetNightVisionStatus() const {
    return FString::Printf(TEXT("Display gain %.2f  /  grain %.3f"),NightVisionGain,NightVisionGrain);
}
void ADarkFactoryPawn::GainDown() { NightVisionGain=FMath::Max(.25f,NightVisionGain-.1f); }
void ADarkFactoryPawn::GainUp() { NightVisionGain=FMath::Min(3.f,NightVisionGain+.1f); }
void ADarkFactoryPawn::GrainDown() { NightVisionGrain=FMath::Max(0.f,NightVisionGrain-.005f); }
void ADarkFactoryPawn::GrainUp() { NightVisionGrain=FMath::Min(.12f,NightVisionGrain+.005f); }
bool ADarkFactoryPawn::SetObserverBookmark(int32 Index,bool Immediate) {
    const auto* W=Cell(GetWorld());
    if (!W || !W->IsImportedScene() || Index<0 || Index>=ObserverBookmarkCount) return false;
    SmoothedUp=0;
    FVector HomePosition; FRotator HomeRotation;
    W->GetObserverHome(HomePosition,HomeRotation);
    const FVector Positions[]={HomePosition,FVector(580,-480,430),FVector(-980,-700,540),FVector(880,1040,580),FVector(1450,-1100,1040)};
    const FVector Targets[]={FVector(0,0,230),FVector(0,-70,210),FVector(0,0,230),FVector(0,240,220),FVector(0,0,420)};
    ObserverBookmarkIndex=Index; BookmarkStart=GetActorLocation(); BookmarkEnd=Positions[Index];
    BookmarkRotationStart=GetActorQuat(); BookmarkRotationEnd=(Targets[Index]-Positions[Index]).Rotation().Quaternion();
    BookmarkElapsed=0; bBookmarkMoving=!Immediate;
    if (Immediate) SetActorLocationAndRotation(BookmarkEnd,BookmarkRotationEnd);
    return true;
}
FString ADarkFactoryPawn::GetObserverBookmarkName() const {
    static const TCHAR* Names[]={TEXT("Line overview"),TEXT("Weld inspection"),TEXT("Opposite side"),TEXT("Line exit"),TEXT("Utilities overview")};
    return ObserverBookmarkIndex>=0 && ObserverBookmarkIndex<ObserverBookmarkCount ? Names[ObserverBookmarkIndex]:TEXT("Free flight");
}
void ADarkFactoryPawn::CancelObserverBookmark() { bBookmarkMoving=false; ObserverBookmarkIndex=-1; }
void ADarkFactoryPawn::CycleObserverBookmark() { SetObserverBookmark((ObserverBookmarkIndex+1)%ObserverBookmarkCount); }
void ADarkFactoryPawn::ControllerBookmark() { if (const auto* W=Cell(GetWorld()); W && W->IsImportedScene()) CycleObserverBookmark(); else Next(); }
void ADarkFactoryPawn::ToggleFactorySound() { if (auto* W=Cell(GetWorld())) if (auto* Sound=W->GetSoundscape()) Sound->SetMuted(!Sound->IsMuted()); }
void ADarkFactoryPawn::ToggleColliderOverlay() {
    if (auto* PC=Cast<APlayerController>(GetController())) if (auto* HUD=Cast<ADarkFactoryHUD>(PC->GetHUD())) HUD->CycleColliderOverlay();
}
void ADarkFactoryPawn::ToggleWeldingMotion() { if (auto* W=Cell(GetWorld())) W->ToggleWeldingAnimation(); }
void ADarkFactoryPawn::ToggleInspectionReplay() { if (auto* W=Cell(GetWorld())) if (auto* P=W->GetProcessController()) P->ToggleInspectionReplay(); }
void ADarkFactoryPawn::ToggleModelBackend() { if (auto* W=Cell(GetWorld())) if (auto* P=W->GetProcessController()) P->ToggleModelBackend(); }
void ADarkFactoryPawn::ControllerModelBackend() {
    if (auto* W=Cell(GetWorld())) if (auto* P=W->GetProcessController()) { P->ToggleModelBackend(); return; }
    ToggleHeatTransfer();
}
void ADarkFactoryPawn::PreviousInspectionReceipt() { if (auto* W=Cell(GetWorld())) if (auto* P=W->GetProcessController()) P->StepInspectionReplay(-1); }
void ADarkFactoryPawn::NextInspectionReceipt() { if (auto* W=Cell(GetWorld())) if (auto* P=W->GetProcessController()) P->StepInspectionReplay(1); }
void ADarkFactoryPawn::Normal() { if(auto* W=Cell(GetWorld())) W->SetScenario(0); }
void ADarkFactoryPawn::Missing() { if(auto* W=Cell(GetWorld())) W->SetScenario(1); }
void ADarkFactoryPawn::Misaligned() { if(auto* W=Cell(GetWorld())) W->SetScenario(2); }
void ADarkFactoryPawn::Occluded() { if(auto* W=Cell(GetWorld())) W->SetScenario(3); }
void ADarkFactoryPawn::Randomize() { if(auto* W=Cell(GetWorld())) W->SetScenario(W->Scenario); }
void ADarkFactoryPawn::Next() { if(auto* W=Cell(GetWorld())) W->Randomize(); }
void ADarkFactoryPawn::Green() { if(auto* W=Cell(GetWorld())) W->bGreenDisplay=!W->bGreenDisplay; }
void ADarkFactoryPawn::Capture() { if(auto* W=Cell(GetWorld())) W->RequestSample(); }
void ADarkFactoryPawn::Inference() { if(auto* W=Cell(GetWorld())) W->ToggleInference(); }
void ADarkFactoryPawn::QuitDemo() {
    UKismetSystemLibrary::QuitGame(this,Cast<APlayerController>(GetController()),EQuitPreference::Quit,false);
}
void ADarkFactoryPawn::SetupPlayerInputComponent(UInputComponent* Input) {
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::Escape,IE_Pressed,this,&ADarkFactoryPawn::QuitDemo).bExecuteWhenPaused=true;
    Input->BindAxis("Forward",this,&ADarkFactoryPawn::MoveForward);
    Input->BindAxis("Right",this,&ADarkFactoryPawn::MoveRight);
    Input->BindAxis("Up",this,&ADarkFactoryPawn::MoveUp);
    Input->BindAxis("MouseYaw",this,&ADarkFactoryPawn::MouseYaw);
    Input->BindAxis("MousePitch",this,&ADarkFactoryPawn::MousePitch);
    Input->BindAxis("StickYaw",this,&ADarkFactoryPawn::StickYaw);
    Input->BindAxis("StickPitch",this,&ADarkFactoryPawn::StickPitch);
    Input->BindKey(EKeys::One,IE_Pressed,this,&ADarkFactoryPawn::Normal);
    Input->BindKey(EKeys::Two,IE_Pressed,this,&ADarkFactoryPawn::Missing);
    Input->BindKey(EKeys::Three,IE_Pressed,this,&ADarkFactoryPawn::Misaligned);
    Input->BindKey(EKeys::Four,IE_Pressed,this,&ADarkFactoryPawn::Occluded);
    Input->BindKey(EKeys::R,IE_Pressed,this,&ADarkFactoryPawn::Randomize);
    Input->BindKey(EKeys::I,IE_Pressed,this,&ADarkFactoryPawn::ToggleView);
    Input->BindKey(EKeys::N,IE_Pressed,this,&ADarkFactoryPawn::ToggleView);
    Input->BindKey(EKeys::Tab,IE_Pressed,this,&ADarkFactoryPawn::CycleView);
    Input->BindKey(EKeys::T,IE_Pressed,this,&ADarkFactoryPawn::ToggleThermal);
    Input->BindKey(EKeys::K,IE_Pressed,this,&ADarkFactoryPawn::CycleThermalPalette);
    Input->BindKey(EKeys::H,IE_Pressed,this,&ADarkFactoryPawn::NextThermalCase);
    Input->BindKey(EKeys::J,IE_Pressed,this,&ADarkFactoryPawn::ToggleThermalFields);
    Input->BindKey(EKeys::O,IE_Pressed,this,&ADarkFactoryPawn::ToggleHeatTransfer);
    // SetHeatTransferEnabled explicitly returns in imported mode.
    Input->BindKey(EKeys::Gamepad_RightThumbstick,IE_Pressed,this,&ADarkFactoryPawn::ControllerModelBackend);
    Input->BindKey(EKeys::U,IE_Pressed,this,&ADarkFactoryPawn::CoolThermal);
    Input->BindKey(EKeys::Gamepad_DPad_Up,IE_Pressed,this,&ADarkFactoryPawn::ToggleThermalFields);
    Input->BindKey(EKeys::Gamepad_DPad_Down,IE_Pressed,this,&ADarkFactoryPawn::CoolThermal);
    Input->BindKey(EKeys::F9,IE_Pressed,this,&ADarkFactoryPawn::SaveThermal);
    Input->BindKey(EKeys::Gamepad_DPad_Left,IE_Pressed,this,&ADarkFactoryPawn::CycleThermalPalette);
    Input->BindKey(EKeys::Gamepad_DPad_Right,IE_Pressed,this,&ADarkFactoryPawn::NextThermalCase);
    Input->BindKey(EKeys::V,IE_Pressed,this,&ADarkFactoryPawn::Green);
    Input->BindKey(EKeys::LeftBracket,IE_Pressed,this,&ADarkFactoryPawn::GainDown);
    Input->BindKey(EKeys::RightBracket,IE_Pressed,this,&ADarkFactoryPawn::GainUp);
    Input->BindKey(EKeys::Comma,IE_Pressed,this,&ADarkFactoryPawn::GrainDown);
    Input->BindKey(EKeys::Period,IE_Pressed,this,&ADarkFactoryPawn::GrainUp);
    Input->BindKey(EKeys::Gamepad_LeftShoulder,IE_Pressed,this,&ADarkFactoryPawn::GainDown);
    Input->BindKey(EKeys::Gamepad_RightShoulder,IE_Pressed,this,&ADarkFactoryPawn::GainUp);
    Input->BindKey(EKeys::C,IE_Pressed,this,&ADarkFactoryPawn::Capture);
    Input->BindKey(EKeys::P,IE_Pressed,this,&ADarkFactoryPawn::Inference);
    Input->BindKey(EKeys::B,IE_Pressed,this,&ADarkFactoryPawn::CycleObserverBookmark);
    Input->BindKey(EKeys::M,IE_Pressed,this,&ADarkFactoryPawn::ToggleFactorySound);
    Input->BindKey(EKeys::F6,IE_Pressed,this,&ADarkFactoryPawn::ToggleDLSS);
    Input->BindKey(EKeys::F7,IE_Pressed,this,&ADarkFactoryPawn::ToggleLumen);
    Input->BindKey(EKeys::F11,IE_Pressed,this,&ADarkFactoryPawn::ToggleHardwareRayTracing);
    Input->BindKey(EKeys::F8,IE_Pressed,this,&ADarkFactoryPawn::ToggleColliderOverlay);
    Input->BindKey(EKeys::F5,IE_Pressed,this,&ADarkFactoryPawn::ToggleInspectionReplay);
    Input->BindKey(EKeys::F4,IE_Pressed,this,&ADarkFactoryPawn::ToggleModelBackend);
    Input->BindKey(EKeys::PageUp,IE_Pressed,this,&ADarkFactoryPawn::PreviousInspectionReceipt);
    Input->BindKey(EKeys::PageDown,IE_Pressed,this,&ADarkFactoryPawn::NextInspectionReceipt);
    Input->BindKey(EKeys::Gamepad_LeftThumbstick,IE_Pressed,this,&ADarkFactoryPawn::ToggleColliderOverlay);
    Input->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ADarkFactoryPawn::ToggleWeldingMotion);
    Input->BindKey(EKeys::Gamepad_Special_Left,IE_Pressed,this,&ADarkFactoryPawn::ToggleWeldingMotion);
    Input->BindKey(EKeys::L,IE_Pressed,this,&ADarkFactoryPawn::ToggleObserverHeadlight);
    Input->BindKey(EKeys::Home,IE_Pressed,this,&ADarkFactoryPawn::Home);
    Input->BindKey(EKeys::Gamepad_FaceButton_Bottom,IE_Pressed,this,&ADarkFactoryPawn::Randomize);
    Input->BindKey(EKeys::Gamepad_FaceButton_Left,IE_Pressed,this,&ADarkFactoryPawn::ControllerBookmark);
    Input->BindKey(EKeys::Gamepad_FaceButton_Top,IE_Pressed,this,&ADarkFactoryPawn::CycleView);
    Input->BindKey(EKeys::Gamepad_FaceButton_Right,IE_Pressed,this,&ADarkFactoryPawn::ToggleView);
    Input->BindKey(EKeys::Gamepad_Special_Right,IE_Pressed,this,&ADarkFactoryPawn::Home);
}

bool ADarkFactoryPawn::IsObserverDLSSEnabled() const {
    return bGraphicsInitialized && !bGraphicsLegacyFixture && (ObserverMode==0 || ObserverMode==3) && bDLSSRequested && bDLSSAvailable;
}
bool ADarkFactoryPawn::IsObserverLumenEnabled() const {
    return bGraphicsInitialized && !bGraphicsLegacyFixture && (ObserverMode==0 || ObserverMode==3) && bLumenRequested
        && (IsObserverHardwareRayTracingEnabled() || DarkFactoryGraphics::SoftwareLumenSupported());
}
bool ADarkFactoryPawn::IsObserverHardwareRayTracingEnabled() const {
    return bGraphicsInitialized && !bGraphicsLegacyFixture && (ObserverMode==0 || ObserverMode==3)
        && bLumenRequested && bHardwareRayTracingRequested && bHardwareRayTracingAvailable;
}
void ADarkFactoryPawn::SetGraphicsOptions(bool DLSS,bool Lumen,bool HardwareRayTracing) {
    bDLSSRequested=DLSS; bLumenRequested=Lumen; bHardwareRayTracingRequested=HardwareRayTracing; UpdateObserverGraphics();
}
void ADarkFactoryPawn::ToggleDLSS() { bDLSSRequested=!bDLSSRequested; UpdateObserverGraphics(); }
void ADarkFactoryPawn::ToggleLumen() { bLumenRequested=!bLumenRequested; UpdateObserverGraphics(); }
void ADarkFactoryPawn::ToggleHardwareRayTracing() { bHardwareRayTracingRequested=!bHardwareRayTracingRequested; UpdateObserverGraphics(); }
void ADarkFactoryPawn::UpdateObserverGraphics() {
    if (!bGraphicsInitialized) return;
    // Recheck after asynchronous engine/plugin startup, before a QA or user
    // requests the feature; unsupported builds retain the explicit fallback.
    bHardwareRayTracingAvailable=DarkFactoryGraphics::HardwareRayTracingSupported();
#if WITH_DF_DLSS
    if (!bDLSSAvailable && UDLSSLibrary::IsDLSSSupported()) {
        bool Supported=false,Fixed=false; float Min=0,Max=100,Sharpness=0;
        UDLSSLibrary::GetDLSSModeInformation(UDLSSMode::Quality,FVector2D(1600,900),Supported,DLSSScreenPercentage,Fixed,Min,Max,Sharpness);
        bDLSSAvailable=Supported && FMath::IsFinite(DLSSScreenPercentage) && DLSSScreenPercentage>0 && DLSSScreenPercentage<100;
        DLSSSupportStatus=StaticEnum<UDLSSSupport>()->GetNameStringByValue(static_cast<int64>(UDLSSLibrary::QueryDLSSSupport()));
    }
#endif
    const bool DLSS=IsObserverDLSSEnabled(),Lumen=IsObserverLumenEnabled();
    // Existing bright-floor exposure was authored for direct light only. Keep
    // the added indirect illumination from clipping white panels; sensor and
    // legacy exposure settings are independent and remain unchanged.
    Camera->PostProcessSettings.AutoExposureBias=ObserverMode==3 && Lumen && !bGraphicsLegacyFixture ? -1.2f:0.f;
    Camera->PostProcessSettings.bOverride_UserFlags=true;
    const bool Finish=(ObserverMode==0 || ObserverMode==3) && !bGraphicsLegacyFixture && !FParse::Param(FCommandLine::Get(),TEXT("DFNoIndustrialFinish"));
    Camera->PostProcessSettings.UserFlags=(Camera->PostProcessSettings.UserFlags & ~256) | (Finish ? 256:0);

#if WITH_DF_DLSS
    if (bDLSSAvailable) UDLSSLibrary::EnableDLSS(DLSS);
#endif
    if (auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))) C->Set(DLSS ? DLSSScreenPercentage:100.f,ECVF_SetByCode);
    Camera->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod=true;
    Camera->PostProcessSettings.DynamicGlobalIlluminationMethod=Lumen ? EDynamicGlobalIlluminationMethod::Lumen:EDynamicGlobalIlluminationMethod::None;
    Camera->PostProcessSettings.bOverride_ReflectionMethod=true;
    Camera->PostProcessSettings.ReflectionMethod=Lumen ? EReflectionMethod::Lumen:EReflectionMethod::None;
    Camera->PostProcessSettings.bOverride_RayTracingAO=true; Camera->PostProcessSettings.RayTracingAO=false;
    ObserverHeadlight->SetIndirectLightingIntensity(Lumen ? 1.f:0.f);
    ObserverHeadlight->SetAffectGlobalIllumination(Lumen); ObserverHeadlight->SetAffectReflection(Lumen);
    if (auto* W=Cell(GetWorld())) W->SetObserverIndirectLightingEnabled(Lumen);
    UE_LOG(LogTemp,Display,TEXT("DF_GRAPHICS dlss=%d lumen=%d hwrt=%d dlss_support=%s screen_percentage=%.3f mode=%d legacy_fixture=%d"),
        DLSS,Lumen,IsObserverHardwareRayTracingEnabled(),*DLSSSupportStatus,DLSS ? DLSSScreenPercentage:100.f,ObserverMode,bGraphicsLegacyFixture);
}
void ADarkFactoryPawn::RecordGraphicsView(const FString& Upscaler,bool RayTracingAllowed) {
    LastObserverUpscaler=Upscaler; bLastViewRayTracing=RayTracingAllowed; ++ObserverGraphicsViews;
}
FString ADarkFactoryPawn::GetGraphicsStatus() const {
    if (ObserverMode==1 || ObserverMode==2) return TEXT("Infrared raster | Visible preferences retained | F6 DLSS / F7 Lumen / F11 RT");
    const TCHAR* DLSS=IsObserverDLSSEnabled() ? (LastObserverUpscaler.Contains(TEXT("DLSS")) ? TEXT("DLSS Quality"):TEXT("DLSS initializing")):bDLSSRequested && !bDLSSAvailable ? TEXT("DLSS unavailable"):TEXT("native");
    const TCHAR* Lumen=IsObserverLumenEnabled() ? (IsObserverHardwareRayTracingEnabled() ? TEXT("Lumen hardware RT"):TEXT("Lumen software RT")):TEXT("Lumen off");
    return FString::Printf(TEXT("%s | %s | F6 DLSS / F7 Lumen / F11 RT"),DLSS,Lumen);
}
TSharedRef<FJsonObject> ADarkFactoryPawn::BuildGraphicsMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("schema"),TEXT("observer-graphics-v1"));
    O->SetBoolField(TEXT("dlss_requested"),bDLSSRequested); O->SetBoolField(TEXT("dlss_supported"),bDLSSAvailable);
    O->SetBoolField(TEXT("dlss_enabled"),IsObserverDLSSEnabled()); O->SetStringField(TEXT("dlss_support"),DLSSSupportStatus);
    O->SetStringField(TEXT("actual_temporal_upscaler"),LastObserverUpscaler); O->SetNumberField(TEXT("rendered_view_count"),ObserverGraphicsViews);
    O->SetNumberField(TEXT("screen_percentage"),IsObserverDLSSEnabled() ? DLSSScreenPercentage:100.f);
    O->SetBoolField(TEXT("lumen_requested"),bLumenRequested); O->SetBoolField(TEXT("lumen_enabled"),IsObserverLumenEnabled());
    O->SetBoolField(TEXT("hardware_rt_requested"),bHardwareRayTracingRequested); O->SetBoolField(TEXT("hardware_rt_supported"),bHardwareRayTracingAvailable);
    O->SetBoolField(TEXT("hardware_rt_enabled"),IsObserverHardwareRayTracingEnabled()); O->SetBoolField(TEXT("actual_view_ray_tracing_allowed"),bLastViewRayTracing);
    O->SetNumberField(TEXT("observer_exposure_ev"),Camera->PostProcessSettings.AutoExposureBias);
    O->SetNumberField(TEXT("observer_mode"),ObserverMode); O->SetBoolField(TEXT("legacy_fixture"),bGraphicsLegacyFixture); return O;
}
