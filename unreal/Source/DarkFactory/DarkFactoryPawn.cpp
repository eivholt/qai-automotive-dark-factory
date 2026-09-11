#include "DarkFactoryPawn.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryThermalRig.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
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
    virtual void SetupView(FSceneViewFamily&, FSceneView& View) override {
        if (!View.bIsSceneCapture && Pawn.IsValid()) {
            View.ViewLightingChannelMask=Pawn->bAssistedView ? 2:1;
            if (LastMask != View.ViewLightingChannelMask) {
                LastMask = View.ViewLightingChannelMask;
                UE_LOG(LogTemp, Display, TEXT("DF_OBSERVER view_mask=%d pawn=%s eye=%s rotation=%s"), LastMask, *Pawn->GetActorLocation().ToString(), *View.ViewLocation.ToString(), *View.ViewRotation.ToString());
            }
        }
    }
private:
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
}
void ADarkFactoryPawn::BeginPlay() {
    Super::BeginPlay();
    bAutomation = FParse::Param(FCommandLine::Get(), TEXT("DFSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFBridgeSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFNightVisionSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFReadbackSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFThermalSmoke")) ||
        FParse::Param(FCommandLine::Get(), TEXT("DFModalityScreenshots")) ||
        FString(FCommandLine::Get()).Contains(TEXT("DFPlan=")) ||
        FString(FCommandLine::Get()).Contains(TEXT("DFDataset="));
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
    SetActorLocation(FVector(-300,-780,380));
    SetActorRotation((FVector(0,25,95)-GetActorLocation()).Rotation());
}
void ADarkFactoryPawn::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (NightVisionMaterial) {
        ApplyNightVision(NightVisionMaterial);
    }
    if (bThermalView && ThermalMaterial) {
        if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->ApplyMaterial(ThermalMaterial,Thermal->DisplayPalette);
    }
    if (bAutomation) return;
    const FVector Direction=GetActorForwardVector()*Forward+GetActorRightVector()*Right+FVector::UpVector*Up;
    const auto* PC=Cast<APlayerController>(GetController());
    const float Speed=PC && PC->IsInputKeyDown(EKeys::LeftShift) ? 900:300;
    SetActorLocation(GetActorLocation()+Direction.GetClampedToMaxSize(1)*Speed*DeltaSeconds);
    FRotator Rotation=GetActorRotation();
    Rotation.Yaw+=Yaw*95*DeltaSeconds;
    Rotation.Pitch=FMath::Clamp(Rotation.Pitch+Pitch*75*DeltaSeconds,-85.f,85.f);
    Rotation.Roll=0;
    SetActorRotation(Rotation);
}
void ADarkFactoryPawn::MouseYaw(float Value) { if (bAutomation) return; FRotator R=GetActorRotation(); R.Yaw+=Value*.16f; SetActorRotation(R); }
void ADarkFactoryPawn::MousePitch(float Value) { if (bAutomation) return; FRotator R=GetActorRotation(); R.Pitch=FMath::Clamp(R.Pitch+Value*.16f,-85.f,85.f); SetActorRotation(R); }
void ADarkFactoryPawn::SetObserverMode(int32 Mode) {
    bThermalView=Mode==2; bAssistedView=Mode==1;
    if (NightVisionMaterial) Camera->AddOrUpdateBlendable(NightVisionMaterial.Get(),bAssistedView ? 1.f:0.f);
    if (ThermalMaterial) {
        if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) Thermal->ApplyMaterial(ThermalMaterial,Thermal->DisplayPalette);
        Camera->AddOrUpdateBlendable(ThermalMaterial.Get(),bThermalView ? 1.f:0.f);
    }
}
void ADarkFactoryPawn::ToggleView() { SetObserverMode(bAssistedView ? 0:1); }
void ADarkFactoryPawn::CycleView() { SetObserverMode(bThermalView ? 0:bAssistedView ? 2:1); }
void ADarkFactoryPawn::ToggleThermal() { SetObserverMode(bThermalView ? 1:2); }
void ADarkFactoryPawn::SetThermalPalette(int32 Palette) {
    if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) {
        Thermal->DisplayPalette=FMath::Clamp(Palette,0,2);
        Thermal->ApplyMaterial(ThermalMaterial,Thermal->DisplayPalette);
    }
}
void ADarkFactoryPawn::CycleThermalPalette() {
    if (auto* W=Cell(GetWorld())) if (auto* Thermal=W->GetThermalRig()) SetThermalPalette((Thermal->DisplayPalette+1)%3);
}
void ADarkFactoryPawn::NextThermalCase() {
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
void ADarkFactoryPawn::Normal() { if(auto* W=Cell(GetWorld())) W->SetScenario(0); }
void ADarkFactoryPawn::Missing() { if(auto* W=Cell(GetWorld())) W->SetScenario(1); }
void ADarkFactoryPawn::Misaligned() { if(auto* W=Cell(GetWorld())) W->SetScenario(2); }
void ADarkFactoryPawn::Occluded() { if(auto* W=Cell(GetWorld())) W->SetScenario(3); }
void ADarkFactoryPawn::Randomize() { if(auto* W=Cell(GetWorld())) W->SetScenario(W->Scenario); }
void ADarkFactoryPawn::Next() { if(auto* W=Cell(GetWorld())) W->Randomize(); }
void ADarkFactoryPawn::Green() { if(auto* W=Cell(GetWorld())) W->bGreenDisplay=!W->bGreenDisplay; }
void ADarkFactoryPawn::Capture() { if(auto* W=Cell(GetWorld())) W->RequestSample(); }
void ADarkFactoryPawn::Inference() { if(auto* W=Cell(GetWorld())) W->ToggleInference(); }
void ADarkFactoryPawn::SetupPlayerInputComponent(UInputComponent* Input) {
    Super::SetupPlayerInputComponent(Input);
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
    Input->BindKey(EKeys::Gamepad_RightThumbstick,IE_Pressed,this,&ADarkFactoryPawn::ToggleHeatTransfer);
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
    Input->BindKey(EKeys::Home,IE_Pressed,this,&ADarkFactoryPawn::Home);
    Input->BindKey(EKeys::Gamepad_FaceButton_Bottom,IE_Pressed,this,&ADarkFactoryPawn::Randomize);
    Input->BindKey(EKeys::Gamepad_FaceButton_Left,IE_Pressed,this,&ADarkFactoryPawn::Next);
    Input->BindKey(EKeys::Gamepad_FaceButton_Top,IE_Pressed,this,&ADarkFactoryPawn::CycleView);
    Input->BindKey(EKeys::Gamepad_FaceButton_Right,IE_Pressed,this,&ADarkFactoryPawn::ToggleView);
    Input->BindKey(EKeys::Gamepad_Special_Right,IE_Pressed,this,&ADarkFactoryPawn::Home);
}
