#include "DarkFactoryWorld.h"
#include "DarkFactoryGraphics.h"
#include "Components/LightComponent.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessController.h"
#include "DarkFactoryProcessScene.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryEnvironmentDetails.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactorySoundscape.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryCurtainPortal.h"
#include "DarkFactoryFactoryLights.h"
#include "DarkFactoryThermalRadiometry.h"
#include "DarkFactoryGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace {
constexpr int32 SensorWidth = 448, SensorHeight = 256;
// UPhysicsSettings is process-global. Share one saved baseline across overlapping
// game/PIE worlds and restore it only after the last participating actor ends play.
struct FDarkFactoryPhysicsOverride {
    int32 Users = 0;
    bool bPreviousSubstepping = false;
    float PreviousMaxSubstepDelta = 0;
    int32 PreviousMaxSubsteps = 0;
};
FDarkFactoryPhysicsOverride PhysicsOverride;
void AcquirePhysicsOverride() {
    check(IsInGameThread());
    if (PhysicsOverride.Users++ != 0) return;
    auto* Settings=UPhysicsSettings::Get();
    PhysicsOverride.bPreviousSubstepping=Settings->bSubstepping;
    PhysicsOverride.PreviousMaxSubstepDelta=Settings->MaxSubstepDeltaTime;
    PhysicsOverride.PreviousMaxSubsteps=Settings->MaxSubsteps;
    Settings->bSubstepping=true;
    Settings->MaxSubstepDeltaTime=1.f/120.f;
    Settings->MaxSubsteps=12; // Covers the same .1s/frame cap as carriage and sparks.
}
void ReleasePhysicsOverride() {
    check(IsInGameThread());
    if (!ensure(PhysicsOverride.Users>0)) return;
    if (--PhysicsOverride.Users != 0) return;
    auto* Settings=UPhysicsSettings::Get();
    Settings->bSubstepping=PhysicsOverride.bPreviousSubstepping;
    Settings->MaxSubstepDeltaTime=PhysicsOverride.PreviousMaxSubstepDelta;
    Settings->MaxSubsteps=PhysicsOverride.PreviousMaxSubsteps;
}
// Same versioned augmentation for synchronous dataset capture and async live inference.
// No UObject or render-resource access: safe on a background worker.
TArray<uint8> EncodeSensorPng(TArray<FColor> Pixels, int32 Seed, int32 Frame, float Gain, float NoiseStd) {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_EncodeSensorPng);
    if (Pixels.Num()!=SensorWidth*SensorHeight) return {};
    FRandomStream Noise(static_cast<int32>(static_cast<uint32>(Seed) + static_cast<uint32>(Frame)*7919u));
    for (FColor& Pixel : Pixels) {
        float N=0;
        for (int32 K=0;K<12;++K) N+=Noise.FRand();
        N-=6;
        const float Luma=(.2126f*Pixel.R+.7152f*Pixel.G+.0722f*Pixel.B)/255.f;
        const uint8 Gray=static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Luma*Gain+N*NoiseStd)*255),0,255));
        Pixel=FColor(Gray,Gray,Gray,255);
    }
    TArray64<uint8> Compressed;
    FImageUtils::PNGCompressImageArray(SensorWidth,SensorHeight,Pixels,Compressed);
    TArray<uint8> Result;
    Result.Append(Compressed.GetData(),Compressed.Num());
    return Result;
}
FString JsonLine(const TSharedRef<FJsonObject>& Object) {
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    FJsonSerializer::Serialize(Object, Writer);
    return Text;
}
TArray<TSharedPtr<FJsonValue>> VecJson(const FVector& V) {
    return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
}
}

ADarkFactoryWorld::ADarkFactoryWorld() {
    PrimaryActorTick.bCanEverTick = true;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);
    TrayRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BatteryTray"));
    TrayRoot->SetupAttachment(SceneRoot);
    Sensor = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("FixedSensor"));
    Sensor->SetupAttachment(SceneRoot);
    DarkFactoryGraphics::ConfigureSensorCapture(Sensor.Get());
    Sensor->bCaptureEveryFrame = false;
    Sensor->bCaptureOnMovement = false;
    Sensor->CaptureSource = SCS_FinalColorLDR;
    Sensor->FOVAngle = 58;
    Sensor->ViewLightingChannels.bViewChannel0 = false;
    Sensor->ViewLightingChannels.bViewChannel1 = true;
    Sensor->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Sensor->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Sensor->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Sensor->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    Sensor->PostProcessSettings.bOverride_AutoExposureBias = true;
    Sensor->PostProcessSettings.AutoExposureBias = 0;
    Sensor->PostProcessSettings.bOverride_ColorSaturation = true;
    Sensor->PostProcessSettings.ColorSaturation = FVector4(0,0,0,1);
    Sensor->PostProcessSettings.bOverride_VignetteIntensity = true;
    Sensor->PostProcessSettings.VignetteIntensity = 0;
    Sensor->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Sensor->PostProcessSettings.MotionBlurAmount = 0;
    IRLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ActiveNIR_RenderingProxy"));
    IRLight->SetupAttachment(SceneRoot);
    IRLight->SetMobility(EComponentMobility::Movable);
    IRLight->ViewLightingChannels.bViewChannel0 = false;
    IRLight->ViewLightingChannels.bViewChannel1 = true;
    IRLight->SetIntensity(14000);
    IRLight->SetAttenuationRadius(2400);
    IRLight->SetLightColor(FLinearColor::White);
    IRLight->SetCastShadows(true);
    IRLight->SetIndirectLightingIntensity(0);
    // Dim roof-window spill for navigation, independently masked from active NIR.
    for (int32 I=0;I<2;++I) {
        auto* Light=CreateDefaultSubobject<URectLightComponent>(*FString::Printf(TEXT("RoofWindowSpill%d"),I));
        Light->SetupAttachment(SceneRoot); Light->SetMobility(EComponentMobility::Movable);
        Light->SetRelativeLocation(FVector(I==0 ? -320:320,80,520));
        Light->SetRelativeRotation(FRotator(-90,0,0));
        Light->ViewLightingChannels.bViewChannel0=true;
        Light->ViewLightingChannels.bViewChannel1=false;
        Light->ViewLightingChannels.bViewChannel2=false;
        Light->SetIntensityUnits(ELightUnits::Lumens);
        Light->SetIntensity(GetActiveRoofLightIntensity()); Light->SetAttenuationRadius(1500);
        Light->SetSourceWidth(220); Light->SetSourceHeight(180);
        Light->SetLightColor(FLinearColor(.72f,.82f,1.f));
        Light->SetCastShadows(false);
        Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
        RoofWindowLights.Add(Light);
    }
}

void ADarkFactoryWorld::SetFactoryLightsEnabled(bool Enabled) {
    if (FactoryLights) FactoryLights->SetEnabled(Enabled);
    // Scale dim visible illumination without changing the configured window
    // level used in bright factory mode or any infrared light source.
    RoofLightOutputScale=Enabled ? 1.f:DimVisibleLightScale;
    SetRoofLightIntensity(RoofLightLumens);
}
void ADarkFactoryWorld::SetObserverIndirectLightingEnabled(bool Enabled) {
    for (auto* Actor:{static_cast<AActor*>(this),static_cast<AActor*>(FactoryLights.Get())}) if (Actor) {
        TArray<ULightComponent*> Lights; Actor->GetComponents<ULightComponent>(Lights);
        for (auto* Light:Lights) if (Light && Light->ViewLightingChannels.bViewChannel0
            && !Light->ViewLightingChannels.bViewChannel1 && !Light->ViewLightingChannels.bViewChannel2) {
            Light->SetIndirectLightingIntensity(Enabled ? 1.f:0.f);
            Light->SetAffectGlobalIllumination(Enabled); Light->SetAffectReflection(Enabled);
        }
    }
}
void ADarkFactoryWorld::SetRoofLightIntensity(float Lumens) {
    RoofLightLumens=FMath::IsFinite(Lumens) ? FMath::Clamp(Lumens,0.f,20000.f):80.f;
    for (const auto& Light:RoofWindowLights) if (Light) Light->SetIntensity(GetActiveRoofLightIntensity());
}

UStaticMeshComponent* ADarkFactoryWorld::Box(const TCHAR* Name, FVector Position, FVector Size, float Value, USceneComponent* Parent) {
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, Name);
    Mesh->SetupAttachment(Parent ? Parent : SceneRoot.Get());
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(CubeMesh);
    Mesh->SetRelativeLocation(Position);
    Mesh->SetRelativeScale3D(Size / 100.0);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (BaseMaterial) {
        auto* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        Material->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(Value, Value, Value));
        Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
        Mesh->SetMaterial(0, Material);
    }
    Mesh->RegisterComponent();
    return Mesh;
}

void ADarkFactoryWorld::BuildCell() {
    CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!BaseMaterial) {
        BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        UE_LOG(LogTemp, Warning, TEXT("DF: generated material missing; run Scripts/setup_content.py before collecting data"));
    }
    Box(TEXT("Floor"), FVector(0,0,-12), FVector(1800,1400,24), .09);
    Box(TEXT("RearWall"), FVector(0,650,250), FVector(1800,20,500), .18);
    Box(TEXT("LeftWall"), FVector(-890,0,250), FVector(20,1400,500), .16);
    for (int32 I=0; I<5; ++I) {
        Box(*FString::Printf(TEXT("Column%d"),I), FVector(-700+350*I,620,220), FVector(28,28,440), .28);
        Box(*FString::Printf(TEXT("Beam%d"),I), FVector(-700+350*I,100,440), FVector(20,1060,25), .20);
    }
    Box(TEXT("ConveyorBase"), FVector(0,0,48), FVector(700,205,34), .12);
    Box(TEXT("LeftRail"), FVector(0,-109,78), FVector(700,9,12), .42);
    Box(TEXT("RightRail"), FVector(0,109,78), FVector(700,9,12), .42);
    for (int32 I=0; I<21; ++I)
        Box(*FString::Printf(TEXT("Roller%d"),I), FVector(-330+33*I,0,70), FVector(13,190,12), .32);
    for (int32 I=0; I<4; ++I)
        Box(*FString::Printf(TEXT("Leg%d"),I), FVector(I<2 ? -270:270, I%2 ? -75:75,25), FVector(20,20,50), .20);
    Box(TEXT("BatteryTrayBase"), FVector(0,0,86), FVector(220,150,12), .08, TrayRoot);
    Box(TEXT("TrayFront"), FVector(0,-78,96), FVector(232,7,22), .36, TrayRoot);
    Box(TEXT("TrayRear"), FVector(0,78,96), FVector(232,7,22), .36, TrayRoot);
    Box(TEXT("TrayLeft"), FVector(-113,0,96), FVector(7,150,22), .36, TrayRoot);
    Box(TEXT("TrayRight"), FVector(113,0,96), FVector(7,150,22), .36, TrayRoot);
    for (int32 I=0; I<4; ++I) {
        auto* Module = Box(*FString::Printf(TEXT("BatteryModule%d"),I), FVector(-72+48*I,0,109), FVector(40,130,34), .65, TrayRoot);
        Modules.Add(Module);
        // These raised ribs make module orientation apparent without color cues.
        for (int32 J=0; J<3; ++J)
            Box(*FString::Printf(TEXT("ModuleRib%d_%d"),I,J), FVector(0,-32+32*J,53), FVector(82,5,6), .25, Module);
    }
    Occluder = Box(TEXT("OpaqueInspectionCover"), FVector(-15,-5,164), FVector(275,205,12), .20, TrayRoot);
    Box(TEXT("RobotPedestal"), FVector(110,290,70), FVector(85,85,140), .26);
    Box(TEXT("RobotUpperArm"), FVector(110,245,170), FVector(34,115,40), .48);
    Box(TEXT("RobotForearm"), FVector(110,171,146), FVector(30,30,110), .48);
    RejectArm = Box(TEXT("RejectGate"), FVector(245,105,112), FVector(12,210,12), .48);
    RejectArm->SetRelativeRotation(FRotator(0,0,75));
    auto* Mast=Box(TEXT("SensorMast"), FVector(-365,-350,190), FVector(12,12,380), .30);
    Sensor->HideComponent(Mast);
    auto* Housing=Box(TEXT("CameraHousing"), FVector(-350,-340,412), FVector(40,28,24), .22);
    Sensor->HideComponent(Housing);
    Sensor->SetRelativeLocation(FVector(-310,-300,380));
    Sensor->SetRelativeRotation((FVector(0,0,99)-Sensor->GetComponentLocation()).Rotation());
    IRLight->SetRelativeLocation(FVector(-180,-180,340));
}

void ADarkFactoryWorld::BeginPlay() {
    Super::BeginPlay();
    // Load encoding module on the game thread before worker jobs use it.
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("ImageWrapper"));
    GConfig->GetFloat(TEXT("DarkFactory.Sensor"),TEXT("PreviewHz"),PreviewHz,GGameIni);
    FParse::Value(FCommandLine::Get(),TEXT("DFPreviewHz="),PreviewHz);
    PreviewHz=FMath::IsFinite(PreviewHz) ? FMath::Clamp(PreviewHz,1.f,30.f):15.f;
    FParse::Value(FCommandLine::Get(), TEXT("DFEndpoint="), Endpoint);
    FParse::Value(FCommandLine::Get(), TEXT("DFSeed="), BaseSeed);
    FParse::Value(FCommandLine::Get(), TEXT("DFDataset="), DatasetCount);
    FParse::Value(FCommandLine::Get(), TEXT("DFMaxAge="), MaxResultAge);
    MaxResultAge = FMath::Clamp(MaxResultAge, 1.0, 120.0);
    bGraphicsQA=FParse::Param(FCommandLine::Get(),TEXT("DFGraphicsSmoke"));
    bEquipmentHeatQA=FParse::Param(FCommandLine::Get(),TEXT("DFEquipmentHeatSmoke"));
    bCurtainQA=FParse::Param(FCommandLine::Get(),TEXT("DFCurtainSmoke"));
    bWeldingSmoke=FParse::Param(FCommandLine::Get(),TEXT("DFWeldingSmoke"));
    bRealismQA=FParse::Param(FCommandLine::Get(),TEXT("DFRealismSmoke"));
    bColliderQA=FParse::Param(FCommandLine::Get(),TEXT("DFColliderSmoke"));
    bCabinetQA=FParse::Param(FCommandLine::Get(),TEXT("DFCabinetSmoke"));
    bWeldingSmoke |= bRealismQA || bColliderQA || bCabinetQA;
    bProcessEnabled=FParse::Param(FCommandLine::Get(),TEXT("DFProcess")) || FParse::Param(FCommandLine::Get(),TEXT("DFProcessCapture")) || FParse::Param(FCommandLine::Get(),TEXT("DFProcessSmoke")) || FParse::Param(FCommandLine::Get(),TEXT("DFProcessPolicySmoke"));
    bModalityScreenshots=FParse::Param(FCommandLine::Get(),TEXT("DFModalityScreenshots"));
    bThermalSmoke=FParse::Param(FCommandLine::Get(),TEXT("DFThermalSmoke"));
    bSmoke = FParse::Param(FCommandLine::Get(), TEXT("DFSmoke"));
    bNightVisionSmoke = FParse::Param(FCommandLine::Get(), TEXT("DFNightVisionSmoke"));
    bReadbackSmoke = FParse::Param(FCommandLine::Get(), TEXT("DFReadbackSmoke"));
    bBridgeSmoke = FParse::Param(FCommandLine::Get(), TEXT("DFBridgeSmoke"));
    FParse::Value(FCommandLine::Get(), TEXT("DFRoofLightLumens="), RoofLightLumens);
    // Existing zero-light NIR calibration remains an explicit offline condition.
    if (bSmoke || bNightVisionSmoke || DatasetCount>0) RoofLightLumens=0;
    SetRoofLightIntensity(RoofLightLumens);
    FParse::Value(FCommandLine::Get(), TEXT("DFExpected="), BridgeExpected);
    BridgeExpected=BridgeExpected.ToUpper();
    bInferenceEnabled = !FParse::Param(FCommandLine::Get(), TEXT("DFNoInference")) && DatasetCount <= 0 && !bSmoke && !bNightVisionSmoke && !bReadbackSmoke && !bThermalSmoke && !bModalityScreenshots && !bCurtainQA;
    OutputRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), bCurtainQA ? TEXT("CurtainPhysics"):bWeldingSmoke ? TEXT("WeldingPreview"):bModalityScreenshots ? TEXT("Modalities"):bNightVisionSmoke ? TEXT("NightVision"):TEXT("Dataset")));
    FParse::Value(FCommandLine::Get(), TEXT("DFOutput="), OutputRoot);
    FString PlanPath;
    FParse::Value(FCommandLine::Get(), TEXT("DFPlan="), PlanPath);
    FString ContractText;
    if (FFileHelper::LoadFileToString(ContractText, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../configs/inspection_contract.json")))) {
        TSharedPtr<FJsonObject> Contract;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ContractText), Contract) && Contract) {
            Contract->TryGetStringField(TEXT("version"), ContractVersion);
            Contract->TryGetStringField(TEXT("prompt_sha256"), PromptSha256);
        }
    }
    if (!PlanPath.IsEmpty() && !LoadPlan(PlanPath)) { FPlatformMisc::RequestExitWithStatus(false, 1); return; }
    if (DatasetCount > 0) {
        SetRoofLightIntensity(0);
        bInferenceEnabled = false;
        if (IFileManager::Get().FileExists(*FPaths::Combine(OutputRoot,TEXT("manifest.jsonl")))) {
            UE_LOG(LogTemp,Error,TEXT("DF_DATASET_ERROR output manifest exists; choose a new -DFOutput directory"));
            FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
    }
    FString SceneSelection=TEXT("auto");
    const bool ExplicitScene=FParse::Value(FCommandLine::Get(),TEXT("DFScene="),SceneSelection);
    SceneSelection=SceneSelection.ToLower();
    const bool LegacyAutomation=DatasetCount>0 || !PlanPath.IsEmpty() || bSmoke || bBridgeSmoke || bNightVisionSmoke || bReadbackSmoke || bThermalSmoke || bModalityScreenshots;
    const bool UseMapDefaults=bPanelMapDefaults && !ExplicitScene && !LegacyAutomation && !bWeldingSmoke && !bCurtainQA && !bGraphicsQA;
    if (UseMapDefaults) bProcessEnabled=true;
    if (bWeldingSmoke || bProcessEnabled || bCurtainQA) SceneSelection=TEXT("welding");
    if (bGraphicsQA && (!bProcessEnabled || !FParse::Param(FCommandLine::Get(),TEXT("DFNoInference")) || bWeldingSmoke || LegacyAutomation || bCurtainQA || FParse::Param(FCommandLine::Get(),TEXT("DFProcessCapture")) || FParse::Param(FCommandLine::Get(),TEXT("DFProcessSmoke")) || FParse::Param(FCommandLine::Get(),TEXT("DFProcessPolicySmoke")))) {
        UE_LOG(LogTemp,Error,TEXT("DF_GRAPHICS requires DFProcess DFNoInference and no other fixture"));
        FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if ((bCurtainQA && (bProcessEnabled || bWeldingSmoke || LegacyAutomation)) || (bProcessEnabled && bWeldingSmoke) || (SceneSelection!=TEXT("auto") && SceneSelection!=TEXT("procedural") && SceneSelection!=TEXT("welding")) || (SceneSelection==TEXT("welding") && LegacyAutomation)) {
        UE_LOG(LogTemp,Error,TEXT("DF_SCENE_ERROR invalid scene selection or welding preview mixed with procedural training/smoke flags"));
        FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    FString Manifest=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),TEXT("../assets/derived/welding_cell/scene.json")));
    FParse::Value(FCommandLine::Get(),TEXT("DFSceneManifest="),Manifest);
    if (!LegacyAutomation && SceneSelection!=TEXT("procedural") && (SceneSelection==TEXT("welding") || IFileManager::Get().FileExists(*Manifest))) {
        FString Error;
        if (!TryBuildImportedCell(Manifest,Error)) {
            if (SceneSelection==TEXT("welding")) { UE_LOG(LogTemp,Error,TEXT("DF_SCENE_ERROR %s"),*Error); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            UE_LOG(LogTemp,Warning,TEXT("DF_SCENE_FALLBACK using procedural cell: %s"),*Error);
        }
    }
    if (!IsImportedScene()) BuildCell();
    SensorTarget = NewObject<UTextureRenderTarget2D>(this);
    SensorTarget->RenderTargetFormat = RTF_RGBA8;
    SensorTarget->InitCustomFormat(SensorWidth, SensorHeight, PF_B8G8R8A8, false);
    SensorTarget->ClearColor = FLinearColor::Black;
    SensorTarget->UpdateResourceImmediate(true);
    Sensor->TextureTarget = SensorTarget;
    Seed = BaseSeed;
    if (IsImportedScene()) {
        // Desaturation precedes UE's film/color-space transforms, which can tint
        // neutral smoke. Project final sensor RGB to gray on the GPU as well as
        // in the existing PNG encoder; observer/procedural materials are separate.
        auto* NIRParent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingNIRGrayscale.M_WeldingNIRGrayscale"));
        if (!NIRParent) { UE_LOG(LogTemp,Error,TEXT("DF_WELDING_NIR_MISSING_CONTENT run setup_welding_nir.py")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        WeldingNIRMaterial=UMaterialInstanceDynamic::Create(NIRParent,this);
        Sensor->bAlwaysPersistRenderingState=true; // Required for manual-capture blendables.
        Sensor->ShowFlags.SetTemporalAA(false);
        Sensor->ShowFlags.SetMotionBlur(false);
        Sensor->PostProcessSettings.bOverride_FilmGrainIntensity=true;
        Sensor->PostProcessSettings.FilmGrainIntensity=0;
        Sensor->AddOrUpdateBlendable(WeldingNIRMaterial.Get(),1.f);
        // Move kinematic machinery before Chaos, then render the completed physical state.
        SetTickGroup(TG_PostPhysics);
        // In-memory only; overlapping worlds share a baseline until the last release.
        if (!bPhysicsOverrideAcquired) {
            AcquirePhysicsOverride();
            bPhysicsOverrideAcquired=true;
        }
        TArray<FDarkFactoryImportedThermalSurface> Profiles=WeldingCell->GetThermalSurfaces();
        // The source rail mouths and ground surface were measured from its mesh triangles.
        for (double LaneY : {-1204.08264,1219.15393}) {
            const double Sign=LaneY<0 ? -1.:1.;
            auto* Portal=GetWorld()->SpawnActor<ADarkFactoryCurtainPortal>();
            Portal->SetActorTransform(FTransform(FRotator(0,Sign<0 ? 180:0,0),FVector(4.505665,LaneY,6.3477106)));
            const float TunnelCm=(bProcessEnabled || bCurtainQA) ? ADarkFactoryProcessScene::TunnelLengthCm:1100.f;
            if (!Portal->Initialize(420,390,TunnelCm,14,3)) { UE_LOG(LogTemp,Error,TEXT("DF_PORTAL_INITIALIZE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            CurtainPortals.Add(Portal); Profiles.Append(Portal->GetThermalSurfaces()); AddTickPrerequisiteActor(Portal);
        }
        WeldingSparks=GetWorld()->SpawnActor<ADarkFactoryWeldingSparks>();
        if (!WeldingSparks->Initialize(WeldingCell)) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Profiles.Append(WeldingSparks->GetThermalSurfaces()); AddTickPrerequisiteActor(WeldingSparks);
        WeldingSmoke=GetWorld()->SpawnActor<ADarkFactoryWeldingSmoke>();
        if (!WeldingSmoke->Initialize(WeldingCell,WeldingSparks)) { UE_LOG(LogTemp,Error,TEXT("DF_WELDING_SMOKE_INITIALIZE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        AddTickPrerequisiteActor(WeldingSmoke);
        IQ9Props=GetWorld()->SpawnActor<ADarkFactoryIQ9Props>();
        if (!IQ9Props->Initialize(WeldingCell)) { UE_LOG(LogTemp,Error,TEXT("DF_IQ9_PROPS_INITIALIZE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Profiles.Append(IQ9Props->GetThermalSurfaces()); AddTickPrerequisiteActor(IQ9Props);
        if (bProcessEnabled || bCurtainQA) {
            ProcessScene=GetWorld()->SpawnActor<ADarkFactoryProcessScene>();
            if (!ProcessScene || !ProcessScene->Initialize(WeldingCell,Seed)) {
                UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_SCENE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return;
            }
            Profiles.Append(ProcessScene->GetThermalSurfaces());
        }
        ControlCabinet=GetWorld()->SpawnActor<ADarkFactoryControlCabinet>();
        if (!ControlCabinet->Initialize(WeldingCell)) {
            if (bCabinetQA) { UE_LOG(LogTemp,Error,TEXT("DF_CABINET_QA_INITIALIZE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            ControlCabinet->Destroy(); ControlCabinet=nullptr;
        } else { ControlCabinet->SetEnabled(!bWeldingSmoke || bCabinetQA); AddTickPrerequisiteActor(ControlCabinet); }
        EnvironmentDetails=GetWorld()->SpawnActor<ADarkFactoryEnvironmentDetails>();
        CellMechanisms=GetWorld()->SpawnActor<ADarkFactoryCellMechanisms>();
        Soundscape=GetWorld()->SpawnActor<ADarkFactorySoundscape>();
        if (!EnvironmentDetails->Initialize(WeldingCell) || !CellMechanisms->Initialize(WeldingCell) || !Soundscape->Initialize(WeldingCell)) {
            UE_LOG(LogTemp,Error,TEXT("DF_REALISM_INITIALIZE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        AddTickPrerequisiteActor(CellMechanisms); AddTickPrerequisiteActor(Soundscape);
        SetRealismDetailsEnabled(!bWeldingSmoke || bRealismQA || bColliderQA || bCabinetQA);
        ThermalRig=GetWorld()->SpawnActor<ADarkFactoryThermalRig>();
        ThermalRig->SetTickGroup(TG_PostPhysics);
        bInferenceEnabled=false; Source=TEXT("DISABLED");
        EpisodeId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Hold(bProcessEnabled ? TEXT("Initializing camera inspection gates"):TEXT("VISUAL PREVIEW: new scenario adapter not configured"));
        if (!ThermalRig->InitializeImported(this,Sensor,Profiles)) {
            ThermalRig->Destroy(); ThermalRig=nullptr;
            if (bProcessEnabled || bCurtainQA) { UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_THERMAL_UNAVAILABLE")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            Status=TEXT("VISUAL PREVIEW: authored thermal profiles unavailable");
        } else {
            WeldingCell->BindThermalRig(ThermalRig);
            if (ProcessScene) {
                ProcessScene->BindThermalRig(ThermalRig);
            }
            if (ProcessScene && !bCurtainQA) {
                ProcessController=GetWorld()->SpawnActor<ADarkFactoryProcessController>();
                if (!ProcessController || !ProcessController->Initialize(this,WeldingCell,ProcessScene,ThermalRig)) {
                    UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_INITIALIZE_FAILED")); FPlatformMisc::RequestExitWithStatus(false,1); return;
                }
                AddTickPrerequisiteActor(ProcessController);
                WeldingSparks->AddTickPrerequisiteActor(ProcessController);
                WeldingSmoke->AddTickPrerequisiteActor(ProcessController);
                IQ9Props->AddTickPrerequisiteActor(ProcessController);
                CellMechanisms->AddTickPrerequisiteActor(ProcessController);
                if (ControlCabinet) ControlCabinet->AddTickPrerequisiteActor(ProcessController);
                ThermalRig->AddTickPrerequisiteActor(ProcessController);
            }
            ThermalRig->AddTickPrerequisiteActor(WeldingCell); ThermalRig->AddTickPrerequisiteActor(WeldingSparks); ThermalRig->AddTickPrerequisiteActor(WeldingSmoke); ThermalRig->AddTickPrerequisiteActor(IQ9Props);
            ThermalRig->AddTickPrerequisiteActor(CellMechanisms);
            if (ControlCabinet) ThermalRig->AddTickPrerequisiteActor(ControlCabinet);
            for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) ThermalRig->AddTickPrerequisiteActor(Portal);
        }
        if (auto* PC=GetWorld()->GetFirstPlayerController()) if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) {
            Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
            Observer->SetObserverMode(0); Observer->SetCaptureAutomation(bWeldingSmoke);
        }
        if (bWeldingSmoke) { WeldingCell->SetAnimationEnabled(false); WeldingCell->SetMotionTime(0); for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) Portal->SetSimulationEnabled(false); }
    } else {
        SetScenario(0,false);
        ThermalRig=GetWorld()->SpawnActor<ADarkFactoryThermalRig>();
        ThermalRig->Initialize(this,Sensor);
    }
    FactoryLights=GetWorld()->SpawnActor<ADarkFactoryFactoryLights>();
    if (!FactoryLights || !FactoryLights->Initialize(IsImportedScene())) {
        UE_LOG(LogTemp,Error,TEXT("DF_FACTORY_LIGHTS_INITIALIZE_FAILED"));
        FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    AddTickPrerequisiteActor(FactoryLights);
    if (ThermalRig) ThermalRig->AddTickPrerequisiteActor(FactoryLights);
    if (bProcessEnabled) if (auto* PC=GetWorld()->GetFirstPlayerController()) if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) {
        int32 ReviewMode=0; FString ReviewView;
        FParse::Value(FCommandLine::Get(),TEXT("DFProcessObserverMode="),ReviewMode);
        FParse::Value(FCommandLine::Get(),TEXT("DFProcessView="),ReviewView);
        Observer->SetObserverMode(FMath::Clamp(ReviewMode,0,3));
        FVector Location=Observer->GetActorLocation(); FRotator Rotation=Observer->GetActorRotation(); bool Move=false;
        if (ReviewView==TEXT("cabinet") && ControlCabinet) { ControlCabinet->GetInspectionView(Location,Rotation); Move=true; }
        else if (ReviewView==TEXT("inspection") && ProcessScene) { ProcessScene->GetInspectionObserverView(Location,Rotation); Move=true; }
        if (Move) { Observer->SetActorLocation(Location); Observer->SetActorRotation(Rotation); }
    }
    if (DatasetCount > 0) PrepareDatasetSample();
    NextAutomation = FPlatformTime::Seconds() + (bBridgeSmoke ? 6.0:4.0);
    BridgeSmokeDeadline=FPlatformTime::Seconds()+MaxResultAge+10.0;
    if (bGraphicsQA && ProcessController) {
        if (!ProcessController->GetStatus().bPaused) ToggleWeldingAnimation();
        ProcessController->SetActorTickEnabled(false);
    }
    if (bCurtainQA && !InitializeCurtainSmoke()) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    if (IsImportedScene()) { UE_LOG(LogTemp,Display,TEXT("DF_READY welding sensor=448x256 process_gates=%d legacy_inference=disabled"),bProcessEnabled); }
    else { UE_LOG(LogTemp, Display, TEXT("DF_READY sensor=448x256 active_nir_proxy endpoint=%s seed=%d"), *Endpoint, Seed); }
}


bool ADarkFactoryWorld::TryBuildImportedCell(const FString& Manifest,FString& Error) {
    auto* Candidate=GetWorld()->SpawnActor<ADarkFactoryWeldingCell>();
    if (!Candidate || !Candidate->LoadScene(Manifest,Error)) { if (Candidate) Candidate->Destroy(); return false; }
    WeldingCell=Candidate; AddTickPrerequisiteActor(WeldingCell);
    Sensor->SetWorldLocation(WeldingCell->GetSensorLocation()); Sensor->SetWorldRotation(WeldingCell->GetSensorRotation()); Sensor->FOVAngle=WeldingCell->GetSensorFOV();
    IRLight->SetWorldLocation(WeldingCell->GetSensorLocation());
    const FBox Bounds=WeldingCell->GetComponentsBoundingBox(true);
    const FVector Focus=WeldingCell->GetSensorLookAt();
    const double Radius=Bounds.GetExtent().GetMax();
    const double SensorDistance=FVector::Distance(WeldingCell->GetSensorLocation(),Focus);
    IRLight->SetAttenuationRadius(FMath::Clamp(Radius*3,2400.,20000.));
    // Larger-cell active-NIR rendering proxy; distance compensation is authored
    // preview lighting, not a measured sensor response or the battery contract.
    IRLight->SetIntensity(14000.f*FMath::Clamp(FMath::Square(SensorDistance/450.),1.,16.));
    while (RoofWindowLights.Num()<6) {
        auto* Light=NewObject<URectLightComponent>(this,*FString::Printf(TEXT("WeldingRoofSpill%d"),RoofWindowLights.Num()));
        Light->SetupAttachment(SceneRoot); Light->SetMobility(EComponentMobility::Movable);
        Light->ViewLightingChannels.bViewChannel0=true;
        Light->ViewLightingChannels.bViewChannel1=false;
        Light->ViewLightingChannels.bViewChannel2=false;
        Light->SetIntensityUnits(ELightUnits::Lumens);
        Light->SetLightColor(FLinearColor(.72f,.82f,1.f));
        Light->SetCastShadows(false); Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
        Light->RegisterComponent(); RoofWindowLights.Add(Light);
    }
    const double Height=FMath::Max(Bounds.Max.Z+200.,Focus.Z+500.);
    for (int32 I=0;I<RoofWindowLights.Num();++I) {
        auto* Light=RoofWindowLights[I].Get();
        Light->SetWorldLocation(FVector(Focus.X+(I%3-1)*420.,Focus.Y+(I/3==0 ? -350.:350.),Height));
        Light->SetWorldRotation(FRotator(-90,0,0));
        Light->SetAttenuationRadius(2000); Light->SetSourceWidth(300); Light->SetSourceHeight(220);
    }
    float ImportedRoofLumens=80;
    FParse::Value(FCommandLine::Get(),TEXT("DFWeldingRoofLightLumens="),ImportedRoofLumens);
    SetRoofLightIntensity(ImportedRoofLumens);
    UE_LOG(LogTemp,Display,TEXT("DF_WELDING_LIGHTS visible_windows=%d lumens_each=%.0f visible_channel=0 nir_channel=1 nir_distance_cm=%.0f"),RoofWindowLights.Num(),RoofLightLumens,SensorDistance);
    return true;
}
bool ADarkFactoryWorld::GetObserverHome(FVector& Location,FRotator& Rotation) const {
    if (!WeldingCell) return false;
    Location=WeldingCell->GetObserverLocation(); Rotation=WeldingCell->GetObserverRotation(); return true;
}
void ADarkFactoryWorld::ToggleWeldingAnimation() {
    if (ProcessController) {
        ProcessController->TogglePaused();
        for (const auto& Portal:CurtainPortals) Portal->SetSimulationEnabled(!ProcessController->GetStatus().bPaused);
        return;
    }
    if (!WeldingCell) return;
    WeldingCell->SetAnimationEnabled(!WeldingCell->IsAnimating());
    for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) Portal->SetSimulationEnabled(WeldingCell->IsAnimating());
    Status=WeldingCell->IsAnimating() ? TEXT("VISUAL PREVIEW: bounded rigid-link motion") : TEXT("VISUAL PREVIEW: motion paused");
}
TSharedRef<FJsonObject> ADarkFactoryWorld::WeldingSceneSnapshot() const {
    auto Snapshot=WeldingCell->BuildPreviewMetadata();
    auto Transform=[](const FTransform& T) {
        auto Value=MakeShared<FJsonObject>();
        Value->SetArrayField(TEXT("location_cm"),VecJson(T.GetLocation()));
        Value->SetArrayField(TEXT("scale_xyz"),VecJson(T.GetScale3D()));
        const FQuat Q=T.GetRotation();
        Value->SetArrayField(TEXT("rotation_xyzw"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});
        return Value;
    };
    TInlineComponentArray<UStaticMeshComponent*> Meshes(WeldingCell);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (auto* Mesh:Meshes) {
        auto Row=Transform(Mesh->GetComponentTransform()); Row->SetStringField(TEXT("component"),Mesh->GetName());
        Row->SetStringField(TEXT("asset"),Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName():TEXT(""));
        Row->SetBoolField(TEXT("visible"),Mesh->IsVisible()); Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Snapshot->SetArrayField(TEXT("mesh_world_transforms"),Rows);
    Snapshot->SetObjectField(TEXT("fixed_sensor_transform"),Transform(Sensor->GetComponentTransform()));
    Snapshot->SetNumberField(TEXT("fixed_sensor_fov_deg"),Sensor->FOVAngle);
    Snapshot->SetNumberField(TEXT("scene_epoch"),Epoch); Snapshot->SetStringField(TEXT("episode_id"),EpisodeId);
    Snapshot->SetNumberField(TEXT("seed"),Seed); Snapshot->SetNumberField(TEXT("scenario_index"),Scenario);
    return Snapshot;
}
bool ADarkFactoryWorld::SaveWeldingNIRQA(const FString& Name,const FString& StatsName) {
    // Deliberate offline QA only. Never enters LatestPng, the bridge, or a
    // training manifest; live previews continue to sample the GPU target.
    if (!WeldingCell || !SensorTarget || !WeldingSensorStats) return false;
    Sensor->CaptureScene(); ++GPUPreviewCaptures;
    TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    ++WeldingQAReadbacks;
    if (!SensorTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags) || Pixels.Num()!=SensorWidth*SensorHeight) return false;
    double Sum=0,SumSquares=0; int32 Minimum=255,Maximum=0,Spread=0,Black=0,White=0;
    for (auto& Pixel:Pixels) {
        // Keep the native capture diagnostic before conversion. UE's LDR
        // tonemapper can quantize a neutral pixel's RGB channels one code apart.
        Spread=FMath::Max(Spread,FMath::Max3(int32(Pixel.R),int32(Pixel.G),int32(Pixel.B))-FMath::Min3(int32(Pixel.R),int32(Pixel.G),int32(Pixel.B)));
        const int32 Gray=FMath::Clamp(FMath::RoundToInt(.2126*Pixel.R+.7152*Pixel.G+.0722*Pixel.B),0,255);
        Pixel=FColor(Gray,Gray,Gray,255);
        Minimum=FMath::Min(Minimum,Gray); Maximum=FMath::Max(Maximum,Gray);
        Sum+=Gray; SumSquares+=Gray*Gray; Black+=Gray==0; White+=Gray==255;
    }
    const double Mean=Sum/Pixels.Num();
    auto Stats=MakeShared<FJsonObject>(); Stats->SetStringField(TEXT("image"),Name);
    Stats->SetNumberField(TEXT("width"),SensorWidth); Stats->SetNumberField(TEXT("height"),SensorHeight);
    Stats->SetNumberField(TEXT("minimum_u8"),Minimum); Stats->SetNumberField(TEXT("maximum_u8"),Maximum);
    Stats->SetNumberField(TEXT("mean_u8"),Mean); Stats->SetNumberField(TEXT("stddev_u8"),FMath::Sqrt(FMath::Max(0.,SumSquares/Pixels.Num()-Mean*Mean)));
    Stats->SetBoolField(TEXT("gpu_final_grayscale"),WeldingNIRMaterial!=nullptr);
    Stats->SetNumberField(TEXT("preconversion_max_rgb_channel_spread_u8"),Spread);
    Stats->SetNumberField(TEXT("preconversion_neutral_spread_limit_u8"),1);
    Stats->SetBoolField(TEXT("preconversion_neutral_within_one_code"),Spread<=1);
    Stats->SetStringField(TEXT("qa_grayscale_conversion"),TEXT("round(0.2126 R + 0.7152 G + 0.0722 B); repeated into RGB; opaque alpha; no gain or noise"));
    Stats->SetNumberField(TEXT("max_rgb_channel_spread_u8"),0);
    Stats->SetNumberField(TEXT("black_fraction"),double(Black)/Pixels.Num()); Stats->SetNumberField(TEXT("white_fraction"),double(White)/Pixels.Num());
    WeldingSensorStats->SetObjectField(StatsName,Stats);
    TArray64<uint8> Bytes; FImageUtils::PNGCompressImageArray(SensorWidth,SensorHeight,Pixels,Bytes);
    // Save diagnostic grayscale pixels even on a failed source-neutrality check;
    // the smoke still fails when pre-conversion color exceeds one quantized code.
    const bool Saved=!Bytes.IsEmpty() && FFileHelper::SaveArrayToFile(Bytes,*FPaths::Combine(OutputRoot,Name));
    if (Spread>1 || !Saved) UE_LOG(LogTemp,Error,TEXT("DF_WELDING_NIR_QA_FAILED image=%s neutral_spread=%d saved=%d"),*Name,Spread,Saved);
    return Spread<=1 && Saved;
}
void ADarkFactoryWorld::WeldingSmokeTick(double Now) {
    if (bCabinetQA) { CabinetSmokeTick(Now); return; }
    if (bColliderQA) { ColliderSmokeTick(Now); return; }
    if (bRealismQA) { RealismSmokeTick(Now); return; }
    if (!WeldingCell || !WeldingSparks || Now<NextAutomation) return;
    if (SmokeStep==0 && FParse::Param(FCommandLine::Get(),TEXT("DFRobotExpansionOnly"))) bRobotExpansionQA=true;
    if (bRobotExpansionQA) { RobotExpansionSmokeTick(Now); return; }
    if (bWeldingEffectsQA) { WeldingEffectsSmokeTick(Now); return; }
    if (bMotorHeatQA) { MotorHeatSmokeTick(Now); return; }
    if (bRobotChoreographyQA) { RobotChoreographySmokeTick(Now); return; }
    if (bFactoryLightingQA) { FactoryLightingSmokeTick(Now); return; }
    if (bImportedFieldsQA) { ImportedFieldsSmokeTick(Now); return; }
    if (bBodyWeldHeatQA) { BodyWeldHeatSmokeTick(Now); return; }
    if (bToolCoolingQA) { ToolCoolingSmokeTick(Now); return; }
    auto* PC=GetWorld()->GetFirstPlayerController(); auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer) return;
    IFileManager::Get().MakeDirectory(*OutputRoot,true);
    auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
    auto Reached=[&](double Time) { if (WeldingCell->GetMotionTime()<Time) { NextAutomation=Now+.05; return false; } return true; };
    switch (SmokeStep) {
    case 0:
        // Preserve the established 18s regression fixture. The appended timing
        // stage exercises the default randomized 21s production choreography.
        if (!WeldingCell->SetRobotTimingForQA(false)) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        WeldingCell->SetMotionTime(0); WeldingSparks->ResetParticles();
        WeldingRestMetadata=WeldingCell->BuildPreviewMetadata(); WeldingProductionQA=MakeShared<FJsonObject>();
        Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(true);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->ResetSimulation(); Portal->SetSimulationEnabled(true); }
        Shot(TEXT("01_visible_rest.png")); WeldingCell->SetAnimationEnabled(true); break;
    case 1: if (!Reached(1.2)) return; WeldingProductionQA->SetObjectField(TEXT("arrival_pose"),WeldingCell->BuildPreviewMetadata()); Shot(TEXT("10_body_arrival.png")); break;
    case 2: if (!Reached(3.2)) return; Shot(TEXT("11_body_positioned.png")); break;
    case 3:
        if (!Reached(6)) return;
        WeldingMotionMetadata=WeldingCell->BuildPreviewMetadata(); Shot(TEXT("02_visible_motion.png")); break;
    case 4: if (!Reached(8)) return; Observer->SetObserverMode(1); bGreenDisplay=true; break;
    case 5: if (!Reached(8.5)) return; Shot(TEXT("03_nir.png")); break;
    case 6: if (!Reached(10)) return; Observer->SetObserverMode(2); break;
    case 7:
        if (!Reached(10.5)) return;
        Shot(TEXT("04_thermal.png")); WeldingProductionQA->SetObjectField(TEXT("sparks_during_weld"),WeldingSparks->BuildMetadata()); break;
    case 8: if (!Reached(13.6)) return; WeldingProductionQA->SetObjectField(TEXT("parked_pose"),WeldingCell->BuildPreviewMetadata()); Observer->SetObserverMode(0); Shot(TEXT("12_arms_withdrawn.png")); break;
    case 9: if (!Reached(15.7)) return; WeldingProductionQA->SetObjectField(TEXT("departure_pose"),WeldingCell->BuildPreviewMetadata()); Shot(TEXT("13_body_departure.png")); break;
    case 10: {
        if (!Reached(17.5)) return;
        WeldingProductionQA->SetObjectField(TEXT("empty_pose"),WeldingCell->BuildPreviewMetadata());
        WeldingProductionQA->SetObjectField(TEXT("sparks_after_cycle"),WeldingSparks->BuildMetadata());
        TArray<TSharedPtr<FJsonValue>> Portals;
        bool PhysicsPassed=true;
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) {
            Portals.Add(MakeShared<FJsonValueObject>(Portal->BuildPhysicsMetadata()));
            PhysicsPassed &= Portal->HasFinitePhysicsState() && Portal->GetCarriageContactCount()>0 && Portal->GetPeakDeflectionDegrees()>5 && Portal->BuildPhysicsMetadata()->GetNumberField(TEXT("peak_anchor_error_cm"))<10;
        }
        WeldingProductionQA->SetArrayField(TEXT("curtain_portals"),Portals);
        WeldingProductionQA->SetBoolField(TEXT("curtain_contacts_and_deflection_passed"),PhysicsPassed);
        Shot(TEXT("14_empty_station.png")); break;
    }
    case 11:
        if (!Reached(WeldingCell->GetCycleDurationSeconds()+1.2)) return;
        WeldingProductionQA->SetBoolField(TEXT("next_body_cycle_started"),WeldingCell->GetCycleIndex()>=1);
        WeldingProductionQA->SetObjectField(TEXT("next_body_pose"),WeldingCell->BuildPreviewMetadata());
        Shot(TEXT("15_next_body.png"));
        WeldingCell->SetAnimationEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) Portal->SetSimulationEnabled(false);
        break;
    case 12:
        WeldingCell->SetMotionTime(6); WeldingSparks->ResetParticles(); WeldingSparks->AdvanceSimulation(1.6f,true);
        WeldingRoofLumens=RoofLightLumens; WeldingFrozenScene=WeldingSceneSnapshot(); WeldingSensorStats=MakeShared<FJsonObject>();
        WeldingThermalReadbackBaseline=ThermalRig ? ThermalRig->GetReadbacks():0;
        SetRoofLightIntensity(0); Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(false); break;
    case 13:
        bWeldingSensorQA &= SaveWeldingNIRQA(TEXT("sensor_nir_rooflight_off.png"),TEXT("roof_and_headlight_off"));
        bWeldingSensorQA &= ThermalRig && ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("sensor_thermal_rooflight_off.png")));
        Shot(TEXT("06_headlight_off.png")); break;
    case 14: SetRoofLightIntensity(WeldingRoofLumens); Observer->SetObserverHeadlightEnabled(true); break;
    case 15:
        bWeldingSensorQA &= SaveWeldingNIRQA(TEXT("sensor_nir_rooflight_on.png"),TEXT("roof_and_headlight_on"));
        bWeldingSensorQA &= ThermalRig && ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("sensor_thermal_rooflight_on.png")));
        Shot(TEXT("07_headlight_on.png")); break;
    case 16: Observer->SetObserverMode(2); Observer->SetThermalPalette(1); bGreenDisplay=true; break;
    case 17:
        bWeldingSensorQA &= SaveWeldingNIRQA(TEXT("sensor_nir_thermal_view.png"),TEXT("black_hot_observer"));
        bWeldingSensorQA &= ThermalRig && ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("sensor_thermal_thermal_view.png")));
        Shot(TEXT("05_thermal_black_hot.png")); break;
    case 18: {
        const auto Before=WeldingSceneSnapshot();
        TArray<FString> FilesBefore,FilesAfter;
        IFileManager::Get().FindFilesRecursive(FilesBefore,*OutputRoot,TEXT("*"),true,false); FilesBefore.Sort();
        bWeldingGuardsPassed=!bInferenceEnabled && !bSaveRequested && !PendingRequest.IsValid();
        ToggleInference(); bWeldingGuardsPassed &= !bInferenceEnabled;
        ToggleInference(); bWeldingGuardsPassed &= !bInferenceEnabled;
        RequestSample(); bWeldingGuardsPassed &= !bSaveRequested;
        SaveSample(); bWeldingGuardsPassed &= !bLastSaveSucceeded;
        SetScenario(1,true); SetScenario(2,true); SetScenario(3,true);
        bWeldingGuardsPassed &= !BeginSensorReadback(true) && !BeginSensorReadback(false);
        Inspect();
        IFileManager::Get().FindFilesRecursive(FilesAfter,*OutputRoot,TEXT("*"),true,false); FilesAfter.Sort();
        WeldingAfterGuards=WeldingSceneSnapshot();
        bWeldingGuardsPassed &= FilesBefore==FilesAfter && JsonLine(Before)==JsonLine(WeldingAfterGuards.ToSharedRef()) && !bInferenceEnabled && !bSaveRequested && !PendingRequest.IsValid() && NIRInferenceRequests==0 && LatestPng.IsEmpty();
        break;
    }
    default: {
        bool Changed=false,WithinLimits=true;
        if (WeldingRestMetadata && WeldingMotionMetadata) {
            const auto& Rest=WeldingRestMetadata->GetArrayField(TEXT("joints")); const auto& Motion=WeldingMotionMetadata->GetArrayField(TEXT("joints"));
            for (int32 I=0;I<Motion.Num();++I) { const auto J=Motion[I]->AsObject(); const double A=J->GetNumberField(TEXT("angle_deg")); WithinLimits &= A>=J->GetNumberField(TEXT("minimum_deg"))-.001 && A<=J->GetNumberField(TEXT("maximum_deg"))+.001; if (Rest.IsValidIndex(I)) Changed |= FMath::Abs(A-Rest[I]->AsObject()->GetNumberField(TEXT("angle_deg")))>.01; }
        }
        auto SamePNGs=[&](const TCHAR* First,const TCHAR* Second,const TCHAR* Third) {
            TArray<uint8> A,B,C;
            return FFileHelper::LoadFileToArray(A,*FPaths::Combine(OutputRoot,First)) && FFileHelper::LoadFileToArray(B,*FPaths::Combine(OutputRoot,Second)) && FFileHelper::LoadFileToArray(C,*FPaths::Combine(OutputRoot,Third)) && !A.IsEmpty() && A==B && A==C;
        };
        const bool NIRUnchanged=SamePNGs(TEXT("sensor_nir_rooflight_off.png"),TEXT("sensor_nir_rooflight_on.png"),TEXT("sensor_nir_thermal_view.png"));
        const bool ThermalUnchanged=SamePNGs(TEXT("sensor_thermal_rooflight_off.png"),TEXT("sensor_thermal_rooflight_on.png"),TEXT("sensor_thermal_thermal_view.png"));
        const auto FinalScene=WeldingSceneSnapshot();
        const bool SceneUnchanged=WeldingFrozenScene && JsonLine(WeldingFrozenScene.ToSharedRef())==JsonLine(FinalScene);
        WeldingProductionQA->SetObjectField(TEXT("frozen_sparks_before_cooling"),WeldingSparks->BuildMetadata());
        const bool ParticlePhysics=WeldingSparks->GetCollisionCount()>0 && WeldingSparks->GetRestCount()>0 && WeldingSparks->GetAliveCount()>0;
        WeldingSparks->AdvanceSimulation(5.f,false);
        const bool ParticlesCooled=WeldingSparks->GetAliveCount()==0;
        WeldingProductionQA->SetBoolField(TEXT("particles_collided_and_rested"),ParticlePhysics);
        WeldingProductionQA->SetBoolField(TEXT("particles_expired_after_cooling"),ParticlesCooled);
        bool Passed=WeldingRestMetadata && WeldingMotionMetadata && Changed && WithinLimits && NIRInferenceRequests==0 && !bInferenceEnabled && AsyncReadbacks==0 && SyncReadbacks==0 && ThermalRig && ThermalRig->IsReady() && bWeldingSensorQA && NIRUnchanged && ThermalUnchanged && SceneUnchanged && bWeldingGuardsPassed && WeldingQAReadbacks==3;
        bool HoldersFollow=true,BasesParked=true;
        for (const auto& Pose:{WeldingRestMetadata,WeldingMotionMetadata,WeldingProductionQA->GetObjectField(TEXT("departure_pose"))}) {
            HoldersFollow &= Pose->GetNumberField(TEXT("carriage_holder_count"))==2;
            for (const auto& Value:Pose->GetArrayField(TEXT("carriage_holders"))) {
                const auto Holder=Value->AsObject(); const auto& Local=Holder->GetArrayField(TEXT("local_position_cm")); const auto& World=Holder->GetArrayField(TEXT("world_position_cm"));
                HoldersFollow &= Holder->GetBoolField(TEXT("attached_to_carriage")) && FMath::Abs(World[1]->AsNumber()-Local[1]->AsNumber()-Pose->GetNumberField(TEXT("carriage_offset_y_cm")))<.001;
            }
        }
        for (const auto& Value:WeldingProductionQA->GetObjectField(TEXT("empty_pose"))->GetArrayField(TEXT("carriage_holders"))) HoldersFollow &= !Value->AsObject()->GetBoolField(TEXT("visible"));
        int32 ParkedBases=0;
        for (const auto& Value:WeldingProductionQA->GetObjectField(TEXT("parked_pose"))->GetArrayField(TEXT("joints"))) {
            const auto Joint=Value->AsObject(); if (Joint->GetStringField(TEXT("id")).EndsWith(TEXT("_base"))) { ++ParkedBases; BasesParked &= FMath::Abs(FMath::Abs(Joint->GetNumberField(TEXT("angle_deg")))-180)<.001; }
        }
        BasesParked &= ParkedBases==4;
        WeldingProductionQA->SetBoolField(TEXT("holders_follow_carriage"),HoldersFollow);
        WeldingProductionQA->SetBoolField(TEXT("bases_parked_180_degrees"),BasesParked);
        Passed &= HoldersFollow && BasesParked;
        Passed &= ParticlePhysics && ParticlesCooled && WeldingProductionQA->GetBoolField(TEXT("curtain_contacts_and_deflection_passed")) && WeldingProductionQA->GetBoolField(TEXT("next_body_cycle_started"));
        for (const TCHAR* Name:{TEXT("01_visible_rest.png"),TEXT("02_visible_motion.png"),TEXT("03_nir.png"),TEXT("04_thermal.png"),TEXT("05_thermal_black_hot.png"),TEXT("06_headlight_off.png"),TEXT("07_headlight_on.png")}) Passed &= IFileManager::Get().FileSize(*FPaths::Combine(OutputRoot,Name))>100;
        auto Report=WeldingCell->BuildPreviewMetadata(); Report->SetBoolField(TEXT("passed"),Passed);
        Report->SetBoolField(TEXT("joint_angles_changed"),Changed); Report->SetBoolField(TEXT("joint_angles_within_limits"),WithinLimits);
        Report->SetNumberField(TEXT("visible_roof_windows"),RoofWindowLights.Num()); Report->SetNumberField(TEXT("roof_window_lumens_each"),RoofLightLumens);
        Report->SetNumberField(TEXT("nir_inference_requests"),NIRInferenceRequests); Report->SetNumberField(TEXT("model_or_training_readbacks"),AsyncReadbacks+SyncReadbacks);
        Report->SetBoolField(TEXT("thermal_material_ready"),ThermalRig && ThermalRig->IsReady());
        Report->SetNumberField(TEXT("registered_thermal_profiles"),ThermalRig ? ThermalRig->GetProfileCount():0);
        Report->SetBoolField(TEXT("offline_sensor_qa_succeeded"),bWeldingSensorQA);
        Report->SetBoolField(TEXT("nir_unchanged_by_rooflight_and_observer_mode"),NIRUnchanged);
        Report->SetBoolField(TEXT("thermal_unchanged_by_rooflight_and_observer_mode"),ThermalUnchanged);
        Report->SetBoolField(TEXT("headlight_isolated_from_sensors"),NIRUnchanged && ThermalUnchanged);
        Report->SetBoolField(TEXT("scene_geometry_and_identity_unchanged"),SceneUnchanged);
        Report->SetBoolField(TEXT("inference_scenario_training_guards_passed"),bWeldingGuardsPassed);
        Report->SetNumberField(TEXT("offline_nir_qa_readbacks"),WeldingQAReadbacks);
        Report->SetNumberField(TEXT("offline_thermal_qa_readbacks"),ThermalRig ? ThermalRig->GetReadbacks()-WeldingThermalReadbackBaseline:0);
        Report->SetObjectField(TEXT("production_cycle"),WeldingProductionQA.ToSharedRef());
        Report->SetStringField(TEXT("sensor_qa_scope"),TEXT("Frozen machinery/particles/curtains; roof and observer headlamp off/on, then black-hot observer. Shared sparks visible in NIR and thermal; no model/training publication."));
        if (WeldingSensorStats) Report->SetObjectField(TEXT("nir_raw_statistics"),WeldingSensorStats.ToSharedRef());
        if (WeldingFrozenScene) Report->SetObjectField(TEXT("invariance_scene_before"),WeldingFrozenScene.ToSharedRef());
        Report->SetObjectField(TEXT("invariance_scene_after"),FinalScene);
        if (WeldingAfterGuards) Report->SetObjectField(TEXT("scene_after_guard_calls"),WeldingAfterGuards.ToSharedRef());
        if (WeldingRestMetadata) Report->SetObjectField(TEXT("rest_pose"),WeldingRestMetadata.ToSharedRef());
        if (WeldingMotionMetadata) Report->SetObjectField(TEXT("motion_pose"),WeldingMotionMetadata.ToSharedRef());
        if (!FFileHelper::SaveStringToFile(JsonLine(Report),*FPaths::Combine(OutputRoot,TEXT("welding_preview.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) Passed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_WELDING_SMOKE passed=%d meshes=%d joints=%d particle_physics=%d curtain_physics=%d"),Passed,WeldingCell->GetMeshCount(),WeldingCell->GetJointCount(),ParticlePhysics,WeldingProductionQA->GetBoolField(TEXT("curtain_contacts_and_deflection_passed")));
        if (!Passed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bToolCoolingQA=true; ToolCoolingStep=0; NextAutomation=Now+.4; return;
    }}
    ++SmokeStep; NextAutomation=Now+.4;
}

void ADarkFactoryWorld::ToolCoolingSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !ThermalRig) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Geometry=[&]() { auto State=WeldingSceneSnapshot(); State->RemoveField(TEXT("tool_heat")); return JsonLine(State); };
    if (ToolCoolingStep==0) {
        WeldingCell->SetAnimationEnabled(false); WeldingCell->SetMotionTime(6);
        WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) Portal->SetSimulationEnabled(false);
        ToolCoolingGeometrySignature=Geometry();
        ToolCoolingReport=MakeShared<FJsonObject>();
        ToolCoolingReport->SetArrayField(TEXT("stages"),{});
        ToolCoolingReport->SetStringField(TEXT("surface_id_mask"),TEXT("tool_cooling_surface_ids.png"));
        ToolCoolingReport->SetBoolField(TEXT("geometry_unchanged"),true);
        ToolCoolingReport->SetBoolField(TEXT("pause_freezes_tool_heat"),true);
        ToolCoolingReport->SetStringField(TEXT("scope"),TEXT("Same frozen pose, headlamp/roof/sensor settings; only authored tool heat time changes. Sparks/arcs disabled; not training exports."));
    }
    const TCHAR* Names[]={TEXT("hot"),TEXT("warm"),TEXT("cool")};
    const double Times[]={12.,15.,21.};
    if (ToolCoolingStep<18) {
        const int32 StageIndex=ToolCoolingStep/6, Substep=ToolCoolingStep%6;
        const FString Name=Names[StageIndex];
        auto File=[&](const TCHAR* Kind) { return FString::Printf(TEXT("tool_cooling_%s_%s.png"),*Name,Kind); };
        if (Substep==0) {
            WeldingCell->SetToolHeatTimeForQA(Times[StageIndex]);
            Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(true); Observer->SetThermalPalette(2);
            auto Stage=WeldingCell->BuildToolHeatMetadata(); Stage->SetStringField(TEXT("name"),Name);
            ToolCoolingHeatSignature=JsonLine(WeldingCell->BuildToolHeatMetadata());
            auto Images=MakeShared<FJsonObject>();
            for (const TCHAR* Kind:{TEXT("visible"),TEXT("nir"),TEXT("thermal"),TEXT("raw_nir"),TEXT("raw_thermal")}) Images->SetStringField(Kind,File(Kind));
            Stage->SetObjectField(TEXT("images"),Images);
            int32 ViewW=1600,ViewH=900; PC->GetViewportSize(ViewW,ViewH);
            for (const auto& Row:Stage->GetArrayField(TEXT("tools"))) {
                auto Tool=Row->AsObject(); const auto& P=Tool->GetArrayField(TEXT("tip_world_cm"));
                const FVector Point(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());
                auto ROI=[&](const TCHAR* Prefix,FVector2D Pixel,bool InFront,int32 W,int32 H,int32 Radius) {
                    const bool InFrame=InFront && Pixel.X>=0 && Pixel.Y>=0 && Pixel.X<W && Pixel.Y<H;
                    Tool->SetBoolField(FString(Prefix)+TEXT("_in_frame"),InFrame);
                    Tool->SetArrayField(FString(Prefix)+TEXT("_roi_px"),{MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::FloorToInt(Pixel.X)-Radius,0,W)),MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::FloorToInt(Pixel.Y)-Radius,0,H)),MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::CeilToInt(Pixel.X)+Radius,0,W)),MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::CeilToInt(Pixel.Y)+Radius,0,H))});
                };
                FVector2D Screen=FVector2D::ZeroVector;
                const bool Projected=PC->ProjectWorldLocationToScreen(Point,Screen);
                ROI(TEXT("observer"),Screen,Projected,ViewW,ViewH,48);
                const FVector Local=Sensor->GetComponentTransform().InverseTransformPosition(Point);
                const double Focal=SensorWidth*.5/FMath::Tan(FMath::DegreesToRadians(Sensor->FOVAngle*.5));
                const FVector2D Pixel(SensorWidth*.5+Focal*Local.Y/FMath::Max(.01,Local.X),SensorHeight*.5-Focal*Local.Z/FMath::Max(.01,Local.X));
                ROI(TEXT("sensor"),Pixel,Local.X>0,SensorWidth,SensorHeight,12);
            }
            ThermalRig->CaptureGPU();
            Stage->SetObjectField(TEXT("thermal_snapshot"),ThermalRig->GetSnapshotMetadataForQA());
            auto Stages=ToolCoolingReport->GetArrayField(TEXT("stages")); Stages.Add(MakeShared<FJsonValueObject>(Stage)); ToolCoolingReport->SetArrayField(TEXT("stages"),Stages);
        } else if (Substep==1) {
            bToolCoolingPassed &= SaveWeldingNIRQA(File(TEXT("raw_nir")),TEXT("tool_heat_")+Name);
            bToolCoolingPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,File(TEXT("raw_thermal"))));
            if (StageIndex==0) bToolCoolingPassed &= ThermalRig->SaveSurfaceIdsForQA(FPaths::Combine(OutputRoot,TEXT("tool_cooling_surface_ids.png")));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("visible"))),false,false);
        } else if (Substep==2) Observer->SetObserverMode(1);
        else if (Substep==3) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("nir"))),false,false);
        else if (Substep==4) { Observer->SetObserverMode(2); Observer->SetThermalPalette(2); }
        else {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("thermal"))),false,false);
            const bool Still=Geometry()==ToolCoolingGeometrySignature;
            const bool HeatStill=JsonLine(WeldingCell->BuildToolHeatMetadata())==ToolCoolingHeatSignature;
            ToolCoolingReport->SetBoolField(TEXT("geometry_unchanged"),ToolCoolingReport->GetBoolField(TEXT("geometry_unchanged")) && Still);
            ToolCoolingReport->SetBoolField(TEXT("pause_freezes_tool_heat"),ToolCoolingReport->GetBoolField(TEXT("pause_freezes_tool_heat")) && HeatStill);
            bToolCoolingPassed &= Still && HeatStill;
        }
    } else if (ToolCoolingStep<24) {
        // Extra alignment views are outside the fixed-pose cooling comparison.
        auto View=[&](FVector Position,FVector Target) {
            Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(true);
            Observer->SetActorLocation(Position); Observer->SetActorRotation((Target-Position).Rotation());
            if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=false;
        };
        if (ToolCoolingStep==18) {
            // The previous cycle is frozen during a curtain pass. Show the
            // authored resting alignment here after all dynamic QA is recorded.
            for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) Portal->ResetSimulation();
            View(FVector(2100,0,1400),FVector(4.5,0,130));
        }
        else if (ToolCoolingStep==19) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,TEXT("layout_overview.png")),false,false);
        else if (ToolCoolingStep==20) View(FVector(510,-780,350),FVector(4.505665,-1204.08264,180));
        else if (ToolCoolingStep==21) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,TEXT("entry_portal_alignment.png")),false,false);
        else if (ToolCoolingStep==22) View(FVector(510,780,350),FVector(4.505665,1219.15393,180));
        else FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,TEXT("exit_portal_alignment.png")),false,false);
    } else {
        ToolCoolingReport->SetBoolField(TEXT("passed"),bToolCoolingPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(ToolCoolingReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("tool_cooling.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bToolCoolingPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_TOOL_COOLING_SMOKE passed=%d"),bToolCoolingPassed);
        if (!bToolCoolingPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bToolCoolingQA=false; bBodyWeldHeatQA=true; BodyWeldHeatStep=0; NextAutomation=Now+.4; return;
    }
    ++ToolCoolingStep; NextAutomation=Now+.4;
}


void ADarkFactoryWorld::BodyWeldHeatSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !ThermalRig) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Geometry=[&]() { auto State=WeldingSceneSnapshot(); State->RemoveField(TEXT("body_weld_heat")); return JsonLine(State); };
    auto Lighting=[&]() {
        auto State=MakeShared<FJsonObject>(); State->SetNumberField(TEXT("roof_lumens"),RoofLightLumens);
        State->SetBoolField(TEXT("headlamp_enabled"),Observer->IsObserverHeadlightEnabled());
        State->SetStringField(TEXT("observer_pose"),Observer->GetActorTransform().ToString());
        State->SetStringField(TEXT("sensor_pose"),Sensor->GetComponentTransform().ToString());
        State->SetNumberField(TEXT("sensor_fov"),Sensor->FOVAngle); return JsonLine(State);
    };
    if (BodyWeldHeatStep==0 && !BodyWeldHeatReport.IsValid()) {
        WeldingCell->SetAnimationEnabled(false); WeldingCell->SetMotionTime(13);
        WeldingCell->SetToolHeatTimeForQA(0); WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(true); Observer->SetThermalPalette(2);
        if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=true;
        BodyWeldGeometrySignature=Geometry(); BodyWeldLightingSignature=Lighting();
        BodyWeldHeatReport=MakeShared<FJsonObject>(); BodyWeldHeatReport->SetArrayField(TEXT("stages"),{});
        BodyWeldHeatReport->SetStringField(TEXT("surface_id_mask"),TEXT("body_weld_heat_surface_ids.png"));
        BodyWeldHeatReport->SetNumberField(TEXT("pose_time_seconds"),13);
        BodyWeldHeatReport->SetBoolField(TEXT("geometry_unchanged"),true);
        BodyWeldHeatReport->SetBoolField(TEXT("pause_freezes_body_heat"),true);
        BodyWeldHeatReport->SetBoolField(TEXT("lighting_unchanged"),true);
        BodyWeldHeatReport->SetStringField(TEXT("scope"),TEXT("Frozen body and withdrawn arms; only the local car weld heat clock changes. Raw ROIs use exact panel stencil IDs. Observer ROIs are unmasked. Tools and sparks/arcs are cold/disabled; not training images."));
        // Let PlayerCameraManager refresh after the viewpoint move before projecting ROIs.
        NextAutomation=Now+.4; return;
    }
    if (BodyWeldHeatStep<24) {
        const TCHAR* Names[]={TEXT("hot"),TEXT("warm"),TEXT("cool"),TEXT("fresh")};
        const double Times[]={12.,14.5,17.,18.};
        const int32 StageIndex=BodyWeldHeatStep/6, Substep=BodyWeldHeatStep%6;
        const FString Name=Names[StageIndex];
        auto File=[&](const TCHAR* Kind) { return FString::Printf(TEXT("body_weld_heat_%s_%s.png"),*Name,Kind); };
        if (Substep==0) {
            WeldingCell->SetBodyWeldHeatTimeForQA(Times[StageIndex]);
            Observer->SetObserverMode(0); Observer->SetThermalPalette(2);
            auto Stage=WeldingCell->BuildBodyWeldHeatMetadata(); Stage->SetStringField(TEXT("name"),Name);
            BodyWeldHeatSignature=JsonLine(WeldingCell->BuildBodyWeldHeatMetadata());
            auto Images=MakeShared<FJsonObject>();
            for (const TCHAR* Kind:{TEXT("visible"),TEXT("nir"),TEXT("thermal"),TEXT("raw_nir"),TEXT("raw_thermal")}) Images->SetStringField(Kind,File(Kind));
            Stage->SetObjectField(TEXT("images"),Images);
            int32 ViewW=1600,ViewH=900; PC->GetViewportSize(ViewW,ViewH);
            for (const auto& Row:Stage->GetArrayField(TEXT("spots"))) {
                auto Spot=Row->AsObject(); const auto& P=Spot->GetArrayField(TEXT("world_position_cm"));
                const FVector Point(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());
                auto ROI=[&](const TCHAR* Prefix,FVector2D Pixel,bool InFront,int32 W,int32 H,int32 Radius) {
                    const bool InFrame=InFront && Pixel.X>=0 && Pixel.Y>=0 && Pixel.X<W && Pixel.Y<H;
                    Spot->SetBoolField(FString(Prefix)+TEXT("_in_frame"),InFrame);
                    Spot->SetArrayField(FString(Prefix)+TEXT("_roi_px"),{MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::FloorToInt(Pixel.X)-Radius,0,W)),MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::FloorToInt(Pixel.Y)-Radius,0,H)),MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::CeilToInt(Pixel.X)+Radius,0,W)),MakeShared<FJsonValueNumber>(FMath::Clamp(FMath::CeilToInt(Pixel.Y)+Radius,0,H))});
                };
                FVector2D Screen=FVector2D::ZeroVector;
                const bool Projected=PC->ProjectWorldLocationToScreen(Point,Screen);
                ROI(TEXT("observer"),Screen,Projected,ViewW,ViewH,32);
                const FVector Local=Sensor->GetComponentTransform().InverseTransformPosition(Point);
                const double Focal=SensorWidth*.5/FMath::Tan(FMath::DegreesToRadians(Sensor->FOVAngle*.5));
                const FVector2D Pixel(SensorWidth*.5+Focal*Local.Y/FMath::Max(.01,Local.X),SensorHeight*.5-Focal*Local.Z/FMath::Max(.01,Local.X));
                ROI(TEXT("sensor"),Pixel,Local.X>0,SensorWidth,SensorHeight,8);
            }
            ThermalRig->CaptureGPU(); Stage->SetObjectField(TEXT("thermal_snapshot"),ThermalRig->GetSnapshotMetadataForQA());
            auto Stages=BodyWeldHeatReport->GetArrayField(TEXT("stages")); Stages.Add(MakeShared<FJsonValueObject>(Stage)); BodyWeldHeatReport->SetArrayField(TEXT("stages"),Stages);
        } else if (Substep==1) {
            bBodyWeldHeatPassed &= SaveWeldingNIRQA(File(TEXT("raw_nir")),TEXT("body_weld_heat_")+Name);
            bBodyWeldHeatPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,File(TEXT("raw_thermal"))));
            if (StageIndex==0) bBodyWeldHeatPassed &= ThermalRig->SaveSurfaceIdsForQA(FPaths::Combine(OutputRoot,TEXT("body_weld_heat_surface_ids.png")));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("visible"))),false,false);
        } else if (Substep==2) Observer->SetObserverMode(1);
        else if (Substep==3) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("nir"))),false,false);
        else if (Substep==4) { Observer->SetObserverMode(2); Observer->SetThermalPalette(2); }
        else {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("thermal"))),false,false);
            const bool Still=Geometry()==BodyWeldGeometrySignature;
            const bool HeatStill=JsonLine(WeldingCell->BuildBodyWeldHeatMetadata())==BodyWeldHeatSignature;
            const bool LightsStill=Lighting()==BodyWeldLightingSignature;
            BodyWeldHeatReport->SetBoolField(TEXT("geometry_unchanged"),BodyWeldHeatReport->GetBoolField(TEXT("geometry_unchanged")) && Still);
            BodyWeldHeatReport->SetBoolField(TEXT("pause_freezes_body_heat"),BodyWeldHeatReport->GetBoolField(TEXT("pause_freezes_body_heat")) && HeatStill);
            BodyWeldHeatReport->SetBoolField(TEXT("lighting_unchanged"),BodyWeldHeatReport->GetBoolField(TEXT("lighting_unchanged")) && LightsStill);
            bBodyWeldHeatPassed &= Still && HeatStill && LightsStill;
        }
    } else {
        bool ToolsCold=true;
        const auto ToolState=WeldingCell->BuildToolHeatMetadata();
        for (const auto& Tool:ToolState->GetArrayField(TEXT("tools"))) ToolsCold &= Tool->AsObject()->GetNumberField(TEXT("glow_strength"))==0;
        const bool EffectsOff=!WeldingSparks->AreEffectsEnabled() && WeldingSparks->GetAliveCount()==0;
        BodyWeldHeatReport->SetBoolField(TEXT("tool_heat_disabled"),ToolsCold);
        BodyWeldHeatReport->SetBoolField(TEXT("sparks_disabled"),EffectsOff); BodyWeldHeatReport->SetBoolField(TEXT("arcs_disabled"),EffectsOff);
        bBodyWeldHeatPassed &= ToolsCold && EffectsOff;
        BodyWeldHeatReport->SetBoolField(TEXT("passed"),bBodyWeldHeatPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(BodyWeldHeatReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("body_weld_heat.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bBodyWeldHeatPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_BODY_WELD_HEAT_SMOKE passed=%d"),bBodyWeldHeatPassed);
        if (!bBodyWeldHeatPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bBodyWeldHeatQA=false; bImportedFieldsQA=true; ImportedFieldsStep=0; NextAutomation=Now+.4; return;
    }
    ++BodyWeldHeatStep; NextAutomation=Now+.4;
}


void ADarkFactoryWorld::ImportedFieldsSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !ThermalRig) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
    switch (ImportedFieldsStep) {
    case 0:
        WeldingCell->SetAnimationEnabled(false); WeldingCell->SetMotionTime(6);
        WeldingCell->SetToolHeatTimeForQA(0); WeldingCell->SetBodyWeldHeatTimeForQA(0);
        WeldingCell->SetMotorHeatTimeForQA(84);
        WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        Observer->SetObserverMode(2); Observer->SetThermalPalette(2); ThermalRig->SetSpatialFieldsEnabled(true);
        ImportedFieldsGeometrySignature=JsonLine(WeldingSceneSnapshot()); ImportedFieldsReport=MakeShared<FJsonObject>();
        ImportedFieldsReport->SetStringField(TEXT("scope"),TEXT("Frozen imported castings: spatial field versus uniform source profile. Tool and car-weld heat disabled; no bulk heat-exchange simulation or model export."));
        break;
    case 1:
        ThermalRig->CaptureGPU(); ImportedFieldsReport->SetObjectField(TEXT("snapshot_on"),ThermalRig->GetSnapshotMetadataForQA());
        bImportedFieldsPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("imported_fields_on.png")));
        bImportedFieldsPassed &= ThermalRig->SaveSurfaceIdsForQA(FPaths::Combine(OutputRoot,TEXT("imported_fields_ids.png")));
        bImportedFieldsPassed &= SaveWeldingNIRQA(TEXT("imported_fields_nir_on.png"),TEXT("imported_fields_on"));
        Shot(TEXT("imported_fields_color.png")); break;
    case 2: Observer->SetThermalPalette(0); break;
    case 3: Shot(TEXT("imported_fields_monochrome.png")); break;
    case 4: ThermalRig->SetSpatialFieldsEnabled(false); Observer->SetThermalPalette(2); break;
    case 5:
        ThermalRig->CaptureGPU(); ImportedFieldsReport->SetObjectField(TEXT("snapshot_off"),ThermalRig->GetSnapshotMetadataForQA());
        bImportedFieldsPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("imported_fields_off.png")));
        bImportedFieldsPassed &= SaveWeldingNIRQA(TEXT("imported_fields_nir_off.png"),TEXT("imported_fields_off"));
        Shot(TEXT("imported_fields_uniform_color.png")); break;
    case 6: ThermalRig->SetSpatialFieldsEnabled(true); break;
    case 7: bImportedFieldsPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("imported_fields_restored.png"))); break;
    default: {
        auto Same=[&](const TCHAR* A,const TCHAR* B) {
            TArray<uint8> First,Second;
            return FFileHelper::LoadFileToArray(First,*FPaths::Combine(OutputRoot,A)) && FFileHelper::LoadFileToArray(Second,*FPaths::Combine(OutputRoot,B)) && !First.IsEmpty() && First==Second;
        };
        const bool GeometrySame=JsonLine(WeldingSceneSnapshot())==ImportedFieldsGeometrySignature;
        const bool NIRSame=Same(TEXT("imported_fields_nir_on.png"),TEXT("imported_fields_nir_off.png"));
        const bool Restored=Same(TEXT("imported_fields_on.png"),TEXT("imported_fields_restored.png"));
        const bool Changed=!Same(TEXT("imported_fields_on.png"),TEXT("imported_fields_off.png"));
        ImportedFieldsReport->SetBoolField(TEXT("geometry_unchanged"),GeometrySame);
        ImportedFieldsReport->SetBoolField(TEXT("nir_unchanged"),NIRSame);
        ImportedFieldsReport->SetBoolField(TEXT("thermal_restored"),Restored);
        ImportedFieldsReport->SetBoolField(TEXT("thermal_fields_changed_image"),Changed);
        bImportedFieldsPassed &= GeometrySame && NIRSame && Restored && Changed;
        ImportedFieldsReport->SetBoolField(TEXT("passed"),bImportedFieldsPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(ImportedFieldsReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("imported_thermal_fields.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bImportedFieldsPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_IMPORTED_FIELDS_SMOKE passed=%d"),bImportedFieldsPassed);
        if (!bImportedFieldsPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bImportedFieldsQA=false; bFactoryLightingQA=true; FactoryLightingStep=0; NextAutomation=Now+.4; return;
    }}
    ++ImportedFieldsStep; NextAutomation=Now+.4;
}

void ADarkFactoryWorld::FactoryLightingSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !FactoryLights || !ThermalRig) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
    auto Snapshot=[&](const TCHAR* Name) {
        auto State=MakeShared<FJsonObject>();
        State->SetNumberField(TEXT("observer_mode"),Observer->GetObserverMode());
        State->SetBoolField(TEXT("headlamp_requested"),Observer->IsObserverHeadlightEnabled());
        State->SetBoolField(TEXT("headlamp_emitting"),Observer->IsObserverHeadlightEmitting());
        State->SetNumberField(TEXT("roof_window_nominal_lumens"),RoofLightLumens);
        State->SetNumberField(TEXT("roof_window_active_lumens"),GetActiveRoofLightIntensity());
        State->SetNumberField(TEXT("roof_window_output_scale"),RoofLightOutputScale);
        State->SetObjectField(TEXT("lights"),FactoryLights->BuildMetadata());
        FactoryLightingReport->GetObjectField(TEXT("snapshots"))->SetObjectField(Name,State);
    };
    auto Raw=[&](const TCHAR* Stage) {
        bFactoryLightingPassed &= SaveWeldingNIRQA(FString::Printf(TEXT("factory_nir_%s.png"),Stage),FString::Printf(TEXT("factory_%s"),Stage));
        bFactoryLightingPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,FString::Printf(TEXT("factory_thermal_%s.png"),Stage)));
    };
    switch (FactoryLightingStep) {
    case 0:
        WeldingCell->SetAnimationEnabled(false); WeldingCell->SetMotionTime(6);
        WeldingCell->SetToolHeatTimeForQA(0); WeldingCell->SetBodyWeldHeatTimeForQA(0);
        WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(true); ThermalRig->SetSpatialFieldsEnabled(true);
        FactoryLights->SetStartupTimeForQA(-1);
        FactoryLightingGeometrySignature=JsonLine(WeldingSceneSnapshot());
        FactoryLightingReport=MakeShared<FJsonObject>();
        FactoryLightingReport->SetObjectField(TEXT("snapshots"),MakeShared<FJsonObject>());
        FactoryLightingReport->SetStringField(TEXT("scope"),TEXT("Offline observer lighting QA at a frozen production pose; fixed sensor parity, fluorescent startup and headlamp interlock. No model export."));
        break;
    case 1:
        Snapshot(TEXT("dim")); Raw(TEXT("off")); Shot(TEXT("factory_dim.png")); break;
    case 2: {
        Observer->SetObserverMode(3);
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (int32 I=0;I<=40;++I) {
            FactoryLights->SetStartupTimeForQA(float(I)*.05f);
            Samples.Add(MakeShared<FJsonValueObject>(FactoryLights->BuildMetadata()));
        }
        FactoryLightingReport->SetArrayField(TEXT("startup_samples"),Samples);
        FactoryLights->SetStartupTimeForQA(.35f); break;
    }
    case 3:
        Snapshot(TEXT("startup")); Raw(TEXT("startup")); Shot(TEXT("factory_startup.png")); break;
    case 4: FactoryLights->SetStartupTimeForQA(3.f); break;
    case 5:
        Snapshot(TEXT("bright")); Raw(TEXT("on")); Shot(TEXT("factory_bright.png")); break;
    case 6:
        Observer->SetObserverHeadlightEnabled(false); Observer->SetObserverHeadlightEnabled(true);
        Snapshot(TEXT("headlamp_requested"));
        bFactoryLightingPassed &= !Observer->IsObserverHeadlightEmitting() && Observer->IsObserverHeadlightEnabled();
        Observer->SetObserverMode(0); break;
    case 7:
        Snapshot(TEXT("restored")); Raw(TEXT("restored")); Shot(TEXT("factory_off.png")); break;
    case 8: {
        TArray<TSharedPtr<FJsonValue>> Cycle;
        for (int32 I=0;I<=4;++I) {
            const int32 Mode=Observer->GetObserverMode();
            Cycle.Add(MakeShared<FJsonValueNumber>(Mode));
            bFactoryLightingPassed &= Mode==I%4;
            bFactoryLightingPassed &= FactoryLights->IsEnabled()==(Mode==3);
            bFactoryLightingPassed &= Observer->IsObserverHeadlightEmitting()==(Mode==0);
            if (I<4) Observer->CycleView();
        }
        FactoryLightingReport->SetArrayField(TEXT("cycle"),Cycle);
        FactoryLights->SetStartupTimeForQA(-1); Observer->SetObserverMode(3); Snapshot(TEXT("retrigger"));
        Observer->SetObserverMode(0); FactoryLights->SetStartupTimeForQA(-1);
        break;
    }
    case 9:
        Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3.f);
        Observer->SetActorLocation(FVector(1050,-1550,340));
        Observer->SetActorRotation((FVector(0,0,670)-Observer->GetActorLocation()).Rotation());
        break;
    case 10: Shot(TEXT("factory_fixtures.png")); break;
    default: {
        Observer->SetObserverMode(0); FactoryLights->SetStartupTimeForQA(-1);
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        auto Same=[&](const TCHAR* Modality) {
            TArray<uint8> Reference;
            if (!FFileHelper::LoadFileToArray(Reference,*FPaths::Combine(OutputRoot,FString::Printf(TEXT("factory_%s_off.png"),Modality))) || Reference.IsEmpty()) return false;
            for (const TCHAR* Stage:{TEXT("startup"),TEXT("on"),TEXT("restored")}) {
                TArray<uint8> Other;
                if (!FFileHelper::LoadFileToArray(Other,*FPaths::Combine(OutputRoot,FString::Printf(TEXT("factory_%s_%s.png"),Modality,Stage))) || Reference!=Other) return false;
            }
            return true;
        };
        const bool GeometrySame=JsonLine(WeldingSceneSnapshot())==FactoryLightingGeometrySignature;
        const bool NIRSame=Same(TEXT("nir")),ThermalSame=Same(TEXT("thermal"));
        FactoryLightingReport->SetBoolField(TEXT("geometry_unchanged"),GeometrySame);
        FactoryLightingReport->SetBoolField(TEXT("nir_unchanged"),NIRSame);
        FactoryLightingReport->SetBoolField(TEXT("thermal_unchanged"),ThermalSame);
        bFactoryLightingPassed &= GeometrySame && NIRSame && ThermalSame;
        FactoryLightingReport->SetBoolField(TEXT("passed"),bFactoryLightingPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(FactoryLightingReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("factory_lighting.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bFactoryLightingPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_FACTORY_LIGHTING_SMOKE passed=%d nir_unchanged=%d thermal_unchanged=%d"),bFactoryLightingPassed,NIRSame,ThermalSame);
        if (!bFactoryLightingPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bFactoryLightingQA=false; bRobotChoreographyQA=true; RobotChoreographyStep=0; NextAutomation=Now+.4; return;
    }}
    ++FactoryLightingStep; NextAutomation=Now+.4;
}

void ADarkFactoryWorld::RobotChoreographySmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !FactoryLights || !WeldingCell) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    constexpr double Progress[]={0.,.10,.20,.38,.56,.68,.80,.90,1.};
    auto SetPose=[&](double Seconds) {
        WeldingCell->SetMotionTime(Seconds);
        WeldingCell->SetToolHeatTimeForQA(0); WeldingCell->SetBodyWeldHeatTimeForQA(0);
    };
    if (RobotChoreographyStep==0) {
        RobotChoreographyReport=MakeShared<FJsonObject>();
        RobotChoreographyReport->SetStringField(TEXT("scope"),TEXT("Authored dramatic robot choreography: sampled actual joint/tip transforms and rendered approach poses. No industrial motion or continuous collision certification."));
        RobotChoreographyReport->SetNumberField(TEXT("approach_start_seconds"),3.5);
        RobotChoreographyReport->SetNumberField(TEXT("approach_duration_seconds"),1.);
        RobotChoreographyReport->SetNumberField(TEXT("withdraw_start_seconds"),12.);
        RobotChoreographyReport->SetNumberField(TEXT("withdraw_duration_seconds"),1.);
        WeldingCell->SetAnimationEnabled(false); WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3.f);
        TArray<double> Samples;
        for (int32 I=0;I<=100;++I) Samples.Add(double(I)*.01);
        for (double Knot:{.20,.56,.80}) { Samples.Add(Knot-.0001); Samples.Add(Knot+.0001); }
        Samples.Sort();
        TArray<TSharedPtr<FJsonValue>> Trajectory;
        for (double P:Samples) {
            auto Sample=MakeShared<FJsonObject>(); Sample->SetNumberField(TEXT("progress"),P);
            SetPose(3.5+P); Sample->SetObjectField(TEXT("approach"),WeldingCell->BuildPreviewMetadata());
            SetPose(13.-P); Sample->SetObjectField(TEXT("withdraw"),WeldingCell->BuildPreviewMetadata());
            for (const TCHAR* Direction:{TEXT("approach"),TEXT("withdraw")}) {
                const auto Pose=Sample->GetObjectField(Direction);
                for (const auto& Value:Pose->GetArrayField(TEXT("joints"))) {
                    const auto J=Value->AsObject(); const double Angle=J->GetNumberField(TEXT("angle_deg"));
                    bRobotChoreographyPassed &= FMath::IsFinite(Angle) && Angle>=J->GetNumberField(TEXT("minimum_deg"))-.001 && Angle<=J->GetNumberField(TEXT("maximum_deg"))+.001;
                }
            }
            Trajectory.Add(MakeShared<FJsonValueObject>(Sample));
        }
        RobotChoreographyReport->SetArrayField(TEXT("trajectory"),Trajectory);
        RobotChoreographyReport->SetArrayField(TEXT("shots"),{});
        SetPose(3.5);
    } else if (RobotChoreographyStep<=18) {
        const int32 Index=(RobotChoreographyStep-1)/2;
        if ((RobotChoreographyStep&1)==1) SetPose(3.5+Progress[Index]);
        else {
            const FString Name=FString::Printf(TEXT("robot_approach_%02d.png"),Index);
            auto Shot=MakeShared<FJsonObject>(); Shot->SetStringField(TEXT("file"),Name); Shot->SetNumberField(TEXT("progress"),Progress[Index]);
            Shot->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata());
            TArray<TSharedPtr<FJsonValue>> Shots=RobotChoreographyReport->GetArrayField(TEXT("shots"));
            Shots.Add(MakeShared<FJsonValueObject>(Shot)); RobotChoreographyReport->SetArrayField(TEXT("shots"),Shots);
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false);
        }
    } else if (RobotChoreographyStep==19) SetPose(13.);
    else if (RobotChoreographyStep==20) {
        RobotChoreographyReport->SetObjectField(TEXT("withdrawn_pose"),WeldingCell->BuildPreviewMetadata());
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,TEXT("robot_withdrawn.png")),false,false);
    } else {
        Observer->SetObserverMode(0); FactoryLights->SetStartupTimeForQA(-1);
        RobotChoreographyReport->SetBoolField(TEXT("passed"),bRobotChoreographyPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(RobotChoreographyReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("robot_choreography.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bRobotChoreographyPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_ROBOT_CHOREOGRAPHY_SMOKE passed=%d samples=107 screenshots=10"),bRobotChoreographyPassed);
        if (!bRobotChoreographyPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bRobotChoreographyQA=false; bMotorHeatQA=true; MotorHeatStep=0; NextAutomation=Now+.4; return;
    }
    ++RobotChoreographyStep; NextAutomation=Now+.3;
}

void ADarkFactoryWorld::MotorHeatSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !ThermalRig || !WeldingCell) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Geometry=[&]() { auto State=WeldingSceneSnapshot(); State->RemoveField(TEXT("motor_heat")); return JsonLine(State); };
    auto Same=[&](const FString& A,const FString& B) {
        TArray<uint8> First,Second;
        return FFileHelper::LoadFileToArray(First,*FPaths::Combine(OutputRoot,A)) && FFileHelper::LoadFileToArray(Second,*FPaths::Combine(OutputRoot,B)) && !First.IsEmpty() && First==Second;
    };
    if (MotorHeatStep==0 && !MotorHeatReport.IsValid()) {
        WeldingCell->SetAnimationEnabled(false); WeldingCell->SetMotionTime(6);
        WeldingCell->SetToolHeatTimeForQA(0); WeldingCell->SetBodyWeldHeatTimeForQA(0);
        WeldingSparks->ResetParticles(); WeldingSparks->SetEffectsEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        Observer->SetObserverMode(0); Observer->SetObserverHeadlightEnabled(true); ThermalRig->SetSpatialFieldsEnabled(true);
        if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=true;
        MotorHeatGeometrySignature=Geometry(); MotorHeatReport=MakeShared<FJsonObject>();
        MotorHeatReport->SetStringField(TEXT("scope"),TEXT("Same frozen pose with movement-driven authored motor temperatures. Exact stencil masks isolate castings. No imported bulk exchange, measured motor physics or training export."));
        MotorHeatReport->SetArrayField(TEXT("stages"),{});
        MotorHeatReport->SetNumberField(TEXT("pose_time_seconds"),6);
        MotorHeatReport->SetStringField(TEXT("surface_id_mask"),TEXT("motor_heat_surface_ids.png"));
        MotorHeatReport->SetBoolField(TEXT("geometry_unchanged"),true);
        MotorHeatReport->SetBoolField(TEXT("pause_freezes_motor_heat"),true);
        NextAutomation=Now+.4; return;
    }
    if (MotorHeatStep<30) {
        const TCHAR* Names[]={TEXT("cold"),TEXT("working"),TEXT("hot"),TEXT("cooling"),TEXT("cooled")};
        const double Duty[]={0.,12.,84.,84.,84.},Idle[]={0.,0.,0.,8.,30.};
        const int32 StageIndex=MotorHeatStep/6,Substep=MotorHeatStep%6;
        const FString Name=Names[StageIndex];
        auto File=[&](const TCHAR* Kind) { return FString::Printf(TEXT("motor_heat_%s_%s.png"),*Name,Kind); };
        if (Substep==0) {
            WeldingCell->SetMotorHeatTimeForQA(Duty[StageIndex],Idle[StageIndex]);
            Observer->SetObserverMode(0); Observer->SetThermalPalette(2);
            auto Stage=MakeShared<FJsonObject>(); Stage->SetStringField(TEXT("name"),Name);
            Stage->SetNumberField(TEXT("duty_seconds"),Duty[StageIndex]); Stage->SetNumberField(TEXT("idle_seconds"),Idle[StageIndex]);
            Stage->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata());
            Stage->SetObjectField(TEXT("motor_heat"),WeldingCell->BuildMotorHeatMetadata());
            MotorHeatStageSignature=JsonLine(WeldingCell->BuildMotorHeatMetadata());
            if (StageIndex==2) MotorHeatHotSignature=MotorHeatStageSignature;
            auto Images=MakeShared<FJsonObject>();
            for (const TCHAR* Kind:{TEXT("visible"),TEXT("color"),TEXT("monochrome"),TEXT("raw_nir"),TEXT("raw_thermal")}) Images->SetStringField(Kind,File(Kind));
            Stage->SetObjectField(TEXT("images"),Images);
            ThermalRig->CaptureGPU(); Stage->SetObjectField(TEXT("thermal_snapshot"),ThermalRig->GetSnapshotMetadataForQA());
            auto Stages=MotorHeatReport->GetArrayField(TEXT("stages")); Stages.Add(MakeShared<FJsonValueObject>(Stage)); MotorHeatReport->SetArrayField(TEXT("stages"),Stages);
        } else if (Substep==1) {
            bMotorHeatPassed &= SaveWeldingNIRQA(File(TEXT("raw_nir")),TEXT("motor_heat_")+Name);
            bMotorHeatPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,File(TEXT("raw_thermal"))));
            if (StageIndex==0) bMotorHeatPassed &= ThermalRig->SaveSurfaceIdsForQA(FPaths::Combine(OutputRoot,TEXT("motor_heat_surface_ids.png")));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("visible"))),false,false);
        } else if (Substep==2) { Observer->SetObserverMode(2); Observer->SetThermalPalette(2); }
        else if (Substep==3) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("color"))),false,false);
        else if (Substep==4) Observer->SetThermalPalette(0);
        else {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("monochrome"))),false,false);
            const bool GeometryStill=Geometry()==MotorHeatGeometrySignature;
            const bool HeatStill=JsonLine(WeldingCell->BuildMotorHeatMetadata())==MotorHeatStageSignature;
            MotorHeatReport->SetBoolField(TEXT("geometry_unchanged"),MotorHeatReport->GetBoolField(TEXT("geometry_unchanged")) && GeometryStill);
            MotorHeatReport->SetBoolField(TEXT("pause_freezes_motor_heat"),MotorHeatReport->GetBoolField(TEXT("pause_freezes_motor_heat")) && HeatStill);
            bMotorHeatPassed &= GeometryStill && HeatStill;
        }
    } else if (MotorHeatStep==30) {
        WeldingCell->SetMotorHeatTimeForQA(84,0);
        const bool Deterministic=JsonLine(WeldingCell->BuildMotorHeatMetadata())==MotorHeatHotSignature;
        MotorHeatReport->SetBoolField(TEXT("deterministic_seek"),Deterministic); bMotorHeatPassed &= Deterministic;
    } else if (MotorHeatStep==31) {
        bMotorHeatPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("motor_heat_restored_raw_thermal.png")));
    } else {
        bool NIRSame=true;
        for (const TCHAR* Stage:{TEXT("working"),TEXT("hot"),TEXT("cooling"),TEXT("cooled")})
            NIRSame &= Same(TEXT("motor_heat_cold_raw_nir.png"),FString::Printf(TEXT("motor_heat_%s_raw_nir.png"),Stage));
        const bool Restored=Same(TEXT("motor_heat_hot_raw_thermal.png"),TEXT("motor_heat_restored_raw_thermal.png"));
        MotorHeatReport->SetBoolField(TEXT("nir_unchanged"),NIRSame); MotorHeatReport->SetBoolField(TEXT("thermal_restored"),Restored);
        bool ToolsCold=true,BodyCold=true;
        const auto ToolState=WeldingCell->BuildToolHeatMetadata(), BodyState=WeldingCell->BuildBodyWeldHeatMetadata();
        for (const auto& V:ToolState->GetArrayField(TEXT("tools"))) ToolsCold &= V->AsObject()->GetNumberField(TEXT("glow_strength"))==0;
        for (const auto& V:BodyState->GetArrayField(TEXT("spots"))) BodyCold &= V->AsObject()->GetNumberField(TEXT("glow_strength"))==0;
        const bool EffectsOff=!WeldingSparks->AreEffectsEnabled() && WeldingSparks->GetAliveCount()==0;
        MotorHeatReport->SetBoolField(TEXT("tool_heat_disabled"),ToolsCold); MotorHeatReport->SetBoolField(TEXT("body_heat_disabled"),BodyCold);
        MotorHeatReport->SetBoolField(TEXT("sparks_disabled"),EffectsOff); MotorHeatReport->SetBoolField(TEXT("arcs_disabled"),EffectsOff);
        bMotorHeatPassed &= NIRSame && Restored && ToolsCold && BodyCold && EffectsOff;
        MotorHeatReport->SetBoolField(TEXT("passed"),bMotorHeatPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(MotorHeatReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("motor_heat.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bMotorHeatPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_MOTOR_HEAT_SMOKE passed=%d nir_unchanged=%d thermal_restored=%d"),bMotorHeatPassed,NIRSame,Restored);
        if (!bMotorHeatPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bMotorHeatQA=false; bWeldingEffectsQA=true; WeldingEffectsStep=0; NextAutomation=Now+.4; return;
    }
    ++MotorHeatStep; NextAutomation=Now+.35;
}

void ADarkFactoryWorld::WeldingEffectsSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !WeldingSparks || !WeldingSmoke || !ThermalRig) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto ColdPose=[&](double Time) {
        WeldingCell->SetMotionTime(Time); WeldingCell->SetToolHeatTimeForQA(0);
        WeldingCell->SetBodyWeldHeatTimeForQA(0); WeldingCell->SetMotorHeatTimeForQA(0);
    };
    auto Replay=[&](double End) {
        ColdPose(4.5); WeldingSparks->SetEffectsEnabled(true); WeldingSparks->ResetParticles();
        WeldingSmoke->SetEffectsEnabled(true); WeldingSmoke->ResetParticles(); WeldingSmoke->SetRenderingEnabled(true);
        double Time=4.5;
        while (Time<End-1.e-8) {
            const double Dt=FMath::Min(1./90.,End-Time); Time+=Dt; ColdPose(Time);
            WeldingSparks->AdvanceSimulation(static_cast<float>(Dt),true);
            WeldingSmoke->AdvanceSimulation(static_cast<float>(Dt),true);
        }
    };
    auto State=[&]() {
        auto S=WeldingSceneSnapshot(); S->SetObjectField(TEXT("sparks"),WeldingSparks->BuildMetadata());
        S->SetObjectField(TEXT("smoke"),WeldingSmoke->BuildMetadata()); return JsonLine(S);
    };
    auto SmokeState=[&]() {
        auto S=WeldingSmoke->BuildMetadata(); S->RemoveField(TEXT("rendering_enabled"));
        S->RemoveField(TEXT("visible")); return JsonLine(S);
    };
    auto Same=[&](const FString& A,const FString& B) {
        TArray<uint8> First,Second;
        return FFileHelper::LoadFileToArray(First,*FPaths::Combine(OutputRoot,A)) && FFileHelper::LoadFileToArray(Second,*FPaths::Combine(OutputRoot,B)) && !First.IsEmpty() && First==Second;
    };
    if (WeldingEffectsStep==0 && !WeldingEffectsReport.IsValid()) {
        // Offline pixel comparisons need identical stochastic shadow/tonemapper
        // sequences. This does not freeze simulation, lights or render histories.
        if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences"))) {
            WeldingEffectsPreviousTemporalFreeze=Freeze->GetInt();
            Freeze->Set(1,ECVF_SetByCode);
        }
        WeldingCell->SetAnimationEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation());
        Observer->SetObserverHeadlightEnabled(true); Observer->SetThermalPalette(2);
        if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=true;
        WeldingEffectsReport=MakeShared<FJsonObject>();
        WeldingEffectsReport->SetStringField(TEXT("scope"),TEXT("Authored cinematic expulsion phases and optical smoke: frozen multi-view captures, shared world state, exact smoke-only on/off thermal comparison. No calibrated welding or fume transport."));
        WeldingEffectsReport->SetArrayField(TEXT("phases"),{}); WeldingEffectsReport->SetArrayField(TEXT("smoke_pairs"),{});
        for (const TCHAR* Flag:{TEXT("pause_freezes_effects"),TEXT("phase_geometry_fixed_per_capture"),TEXT("smoke_toggle_preserves_state"),TEXT("smoke_thermal_unchanged")}) WeldingEffectsReport->SetBoolField(Flag,true);
        NextAutomation=Now+.35; return;
    }
    if (WeldingEffectsStep<24) {
        const TCHAR* Names[]={TEXT("burst"),TEXT("fountain"),TEXT("fallout"),TEXT("settled")};
        const double ReplayEnds[]={4.56,4.80,5.22,5.32},ExtraAge[]={0.,0.,0.,3.48};
        const int32 Index=WeldingEffectsStep/6,Substep=WeldingEffectsStep%6;
        const FString Name=Names[Index];
        auto File=[&](const TCHAR* Kind) { return FString::Printf(TEXT("welding_effects_%s_%s.png"),*Name,Kind); };
        if (Substep==0) {
            Replay(ReplayEnds[Index]);
            if (ExtraAge[Index]>0) { WeldingSparks->AdvanceSimulation(ExtraAge[Index],false); WeldingSmoke->AdvanceSimulation(ExtraAge[Index],false); }
            Observer->SetObserverMode(0);
            auto Phase=MakeShared<FJsonObject>(); Phase->SetStringField(TEXT("name"),Name);
            Phase->SetNumberField(TEXT("replay_until_cell_seconds"),ReplayEnds[Index]); Phase->SetNumberField(TEXT("emission_free_age_seconds"),ExtraAge[Index]);
            Phase->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata()); Phase->SetObjectField(TEXT("sparks"),WeldingSparks->BuildMetadata()); Phase->SetObjectField(TEXT("smoke"),WeldingSmoke->BuildMetadata());
            auto Images=MakeShared<FJsonObject>();
            for (const TCHAR* Kind:{TEXT("visible"),TEXT("nir"),TEXT("thermal"),TEXT("raw_nir"),TEXT("raw_thermal")}) Images->SetStringField(Kind,File(Kind));
            Phase->SetObjectField(TEXT("images"),Images);
            auto Phases=WeldingEffectsReport->GetArrayField(TEXT("phases")); Phases.Add(MakeShared<FJsonValueObject>(Phase)); WeldingEffectsReport->SetArrayField(TEXT("phases"),Phases);
            WeldingEffectsStateSignature=State();
        } else if (Substep==1) {
            bWeldingEffectsPassed &= SaveWeldingNIRQA(File(TEXT("raw_nir")),TEXT("effects_")+Name);
            bWeldingEffectsPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,File(TEXT("raw_thermal"))));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("visible"))),false,false);
        } else if (Substep==2) Observer->SetObserverMode(1);
        else if (Substep==3) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("nir"))),false,false);
        else if (Substep==4) { Observer->SetObserverMode(2); Observer->SetThermalPalette(2); }
        else {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("thermal"))),false,false);
            const bool Still=State()==WeldingEffectsStateSignature;
            for (const TCHAR* Flag:{TEXT("pause_freezes_effects"),TEXT("phase_geometry_fixed_per_capture")}) WeldingEffectsReport->SetBoolField(Flag,WeldingEffectsReport->GetBoolField(Flag) && Still);
            bWeldingEffectsPassed &= Still;
        }
    } else if (WeldingEffectsStep<48) {
        const int32 Index=(WeldingEffectsStep-24)/8,Substep=(WeldingEffectsStep-24)%8;
        const TCHAR* Names[]={TEXT("early"),TEXT("late"),TEXT("expired")};
        const double CumulativeAge[]={.8,3.8,8.8};
        const FString Name=Names[Index];
        auto File=[&](const TCHAR* Kind) { return FString::Printf(TEXT("welding_smoke_%s_%s.png"),*Name,Kind); };
        if (Substep==0) {
            Replay(5.32); ColdPose(13.); WeldingSparks->ResetParticles();
            double AgeRemaining=CumulativeAge[Index];
            while (AgeRemaining>1.e-7) { const double Dt=FMath::Min(AgeRemaining,1.); WeldingSmoke->AdvanceSimulation(static_cast<float>(Dt),false); AgeRemaining-=Dt; }
            WeldingSmoke->SetRenderingEnabled(true); Observer->SetObserverMode(0);
            auto Pair=MakeShared<FJsonObject>(); Pair->SetStringField(TEXT("name"),Name);
            Pair->SetNumberField(TEXT("replay_until_cell_seconds"),5.32); Pair->SetNumberField(TEXT("emission_free_age_seconds"),CumulativeAge[Index]);
            Pair->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata()); Pair->SetObjectField(TEXT("sparks"),WeldingSparks->BuildMetadata());
            Pair->SetObjectField(TEXT("smoke_on"),WeldingSmoke->BuildMetadata());
            auto Images=MakeShared<FJsonObject>();
            for (const TCHAR* Kind:{TEXT("visible_on"),TEXT("visible_off"),TEXT("nir_on"),TEXT("nir_off"),TEXT("raw_nir_on"),TEXT("raw_nir_off"),TEXT("raw_thermal_on"),TEXT("raw_thermal_off")}) Images->SetStringField(Kind,File(Kind));
            Pair->SetObjectField(TEXT("images"),Images);
            auto Pairs=WeldingEffectsReport->GetArrayField(TEXT("smoke_pairs")); Pairs.Add(MakeShared<FJsonValueObject>(Pair)); WeldingEffectsReport->SetArrayField(TEXT("smoke_pairs"),Pairs);
            WeldingEffectsStateSignature=SmokeState();
        } else if (Substep==1) {
            bWeldingEffectsPassed &= SaveWeldingNIRQA(File(TEXT("raw_nir_on")),TEXT("smoke_on_")+Name);
            bWeldingEffectsPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,File(TEXT("raw_thermal_on"))));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("visible_on"))),false,false);
        } else if (Substep==2) WeldingSmoke->SetRenderingEnabled(false);
        else if (Substep==3) {
            bWeldingEffectsPassed &= SaveWeldingNIRQA(File(TEXT("raw_nir_off")),TEXT("smoke_off_")+Name);
            bWeldingEffectsPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,File(TEXT("raw_thermal_off"))));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("visible_off"))),false,false);
            WeldingEffectsReport->GetArrayField(TEXT("smoke_pairs"))[Index]->AsObject()->SetObjectField(TEXT("smoke_off"),WeldingSmoke->BuildMetadata());
        } else if (Substep==4) { Observer->SetObserverMode(1); WeldingSmoke->SetRenderingEnabled(true); }
        else if (Substep==5) FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("nir_on"))),false,false);
        else if (Substep==6) WeldingSmoke->SetRenderingEnabled(false);
        else {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,File(TEXT("nir_off"))),false,false);
            const bool Still=SmokeState()==WeldingEffectsStateSignature;
            const bool ThermalSame=Same(File(TEXT("raw_thermal_on")),File(TEXT("raw_thermal_off")));
            WeldingEffectsReport->SetBoolField(TEXT("smoke_toggle_preserves_state"),WeldingEffectsReport->GetBoolField(TEXT("smoke_toggle_preserves_state")) && Still);
            WeldingEffectsReport->SetBoolField(TEXT("smoke_thermal_unchanged"),WeldingEffectsReport->GetBoolField(TEXT("smoke_thermal_unchanged")) && ThermalSame);
            bWeldingEffectsPassed &= Still && ThermalSame;
        }
    } else {
        const auto Smoke=WeldingSmoke->BuildMetadata();
        const bool Empty=Smoke->GetNumberField(TEXT("alive_particles"))==0;
        WeldingEffectsReport->SetBoolField(TEXT("expired_smoke_empty"),Empty); bWeldingEffectsPassed &= Empty;
        if (WeldingSensorStats) WeldingEffectsReport->SetObjectField(TEXT("nir_raw_statistics"),WeldingSensorStats.ToSharedRef());
        WeldingEffectsReport->SetBoolField(TEXT("passed"),bWeldingEffectsPassed);
        if (!FFileHelper::SaveStringToFile(JsonLine(WeldingEffectsReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("welding_effects.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bWeldingEffectsPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_WELDING_EFFECTS_SMOKE passed=%d"),bWeldingEffectsPassed);
        if (!bWeldingEffectsPassed) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        bWeldingEffectsQA=false; bRobotExpansionQA=true; RobotExpansionStep=0; NextAutomation=Now+.4; return;
    }
    ++WeldingEffectsStep; NextAutomation=Now+.35;
}

FString ADarkFactoryWorld::GetScenarioName() const {
    if (IsImportedScene()) return bProcessEnabled ? TEXT("automotive camera inspection gates"):TEXT("welding cell visual preview");
    static const TCHAR* Names[] = {TEXT("normal"),TEXT("missing"),TEXT("misaligned"),TEXT("occluded")};
    return Names[FMath::Clamp(Scenario,0,3)];
}
FString ADarkFactoryWorld::GetTruthLabel() const {
    if (IsImportedScene()) return TEXT("UNCONFIGURED");
    if (!bSensorActive || Scenario == 3) return TEXT("A");
    return Scenario == 0 ? TEXT("G") : TEXT("R");
}
void ADarkFactoryWorld::Hold(const FString& Why) {
    Decision = TEXT("A");
    Status = Why;
    bResolved = false;
    ResultAt = 0;
}
void ADarkFactoryWorld::ResetEpisode() {
    ++Epoch;
    if (ThermalRig) ThermalRig->NotifySceneChanged();
    EpisodeId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    LeakageGroup = FString::Printf(TEXT("seed-%d"), Seed);
    DatasetSplit.Empty();
    CaptureIndex = 0;
    LatestPng.Reset();
    bSaveRequested=false;
    LatestPngCapturedAt=0;
    NextCapture=0; // Refresh before a newly labelled episode can be read back.
    Travel = 0;
    TrayRoot->SetRelativeLocation(TrayOffset);
    RejectArm->SetRelativeRotation(FRotator(0,0,75));
    Hold(PendingRequest.IsValid() ? TEXT("HOLD: waiting for obsolete request to finish") : TEXT("HOLD: new episode awaiting inspection"));
    NextRequest = FPlatformTime::Seconds()+1;
}
void ADarkFactoryWorld::SetScenario(int32 Variant, bool bNewSeed) {
    if (IsImportedScene()) { Hold(TEXT("VISUAL PREVIEW: anomaly scenarios are not configured")); return; }
    CurrentPlanRow.Reset();
    TargetModule=1; OcclusionFraction=1; ExposureEV=0;
    Scenario = FMath::Clamp(Variant,0,3);
    if (bNewSeed) ++Seed;
    FRandomStream Random(Seed);
    TrayOffset = FVector(Random.FRandRange(-7,7), Random.FRandRange(-5,5), 0);
    Reflectance = Random.FRandRange(.45,.8);
    Roughness = Random.FRandRange(.35,.85);
    Gain = Random.FRandRange(.92,1.08);
    NoiseStd = Random.FRandRange(.002,.008);
    Misalignment = Random.FRandRange(22,35);
    IRLight->SetIntensity(Random.FRandRange(12000,18000));
    for (int32 I=0; I<Modules.Num(); ++I) {
        auto* Module = Modules[I].Get();
        Module->SetVisibility(!(Scenario==1 && I==1), true);
        Module->SetRelativeLocation(FVector(-72+48*I,0,109));
        Module->SetRelativeRotation(FRotator::ZeroRotator);
        if (auto* M = Cast<UMaterialInstanceDynamic>(Module->GetMaterial(0))) {
            M->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(Reflectance,Reflectance,Reflectance));
            M->SetScalarParameterValue(TEXT("Roughness"), Roughness);
        }
    }
    if (Scenario==2) {
        Modules[1]->SetRelativeLocation(FVector(-24,-22,131));
        Modules[1]->SetRelativeRotation(FRotator(0,Misalignment,12));
    }
    Occluder->SetVisibility(Scenario==3, true);
    ResetEpisode();
    UE_LOG(LogTemp, Display, TEXT("DF_EPISODE id=%s variant=%s seed=%d reference_label=%s"), *EpisodeId,*GetScenarioName(),Seed,*GetTruthLabel());
}
void ADarkFactoryWorld::Randomize() { SetScenario((Scenario+1)%4); }
void ADarkFactoryWorld::ToggleSensorForCalibration() {
    bSensorActive = !bSensorActive;
    Sensor->ViewLightingChannels.bViewChannel0 = !bSensorActive;
    Sensor->ViewLightingChannels.bViewChannel1 = bSensorActive;
    ResetEpisode();
}
void ADarkFactoryWorld::ToggleInference() {
    if (IsImportedScene()) { bInferenceEnabled=false; Hold(TEXT("VISUAL PREVIEW: requires a new scenario/model contract")); return; }
    bInferenceEnabled = !bInferenceEnabled;
    ++Epoch;
    Hold(bInferenceEnabled ? TEXT("HOLD: inference enabled") : TEXT("HOLD: inference paused"));
}

void ADarkFactoryWorld::CaptureSensor() {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_OfflineCapture);
    if (!SensorTarget) return;
    Sensor->CaptureScene();
    PreviewEpoch=Epoch;
    PreviewCapturedAt=FPlatformTime::Seconds();
    ++GPUPreviewCaptures;
    ++SyncReadbacks;
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_UNorm);
    Flags.SetLinearToGamma(false);
    if (!SensorTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) {
        LatestPng.Reset();
        Hold(TEXT("HOLD: sensor readback failed"));
        return;
    }
    const int32 Frame=CurrentPlanRow.IsValid() ? static_cast<int32>(CurrentPlanRow->GetNumberField(TEXT("frame_index"))) : CaptureIndex;
    LatestPng=EncodeSensorPng(MoveTemp(Pixels),Seed,Frame,Gain,NoiseStd);
    LatestPngCapturedAt=FPlatformTime::Seconds();
    ++CaptureIndex;
}

bool ADarkFactoryWorld::BeginSensorReadback(bool bForInference) {
    if (IsImportedScene()) return false; // Imported pixels never use the battery inspection/training contract.
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_EnqueueSensorReadback);
    if (!SensorTarget || SensorReadback.IsBusy() || EncodeFuture.IsValid() ||
        PreviewEpoch!=Epoch || FPlatformTime::Seconds()-PreviewCapturedAt>MaxResultAge) return false;
    // The preview already rendered this target on the GPU. Enqueue its copy in
    // render order, then let the next preview update continue independently.
    if (!SensorReadback.Enqueue(SensorTarget,++ReadbackSerial)) return false;
    ++AsyncReadbacks;
    ReadbackEpoch=Epoch;
    ReadbackSeed=Seed;
    ReadbackFrame=CaptureIndex;
    ReadbackGain=Gain;
    ReadbackNoise=NoiseStd;
    ReadbackStarted=PreviewCapturedAt;
    bReadbackForInference=bForInference;
    return true;
}

void ADarkFactoryWorld::PollSensorReadback() {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_PollSensorReadback);
    TArray<FColor> Pixels;
    uint64 Id=0;
    if (SensorReadback.Poll(Pixels,Id)) {
        if (Id!=ReadbackSerial || ReadbackEpoch!=Epoch) {
            ++StaleReadbacksDiscarded;
        } else if (Pixels.Num()!=SensorWidth*SensorHeight) {
            Hold(TEXT("HOLD: asynchronous sensor readback failed"));
        } else {
            const int32 CapturedSeed=ReadbackSeed, Frame=ReadbackFrame;
            const float CapturedGain=ReadbackGain, CapturedNoise=ReadbackNoise;
            EncodeFuture=Async(EAsyncExecution::ThreadPool,
                [Pixels=MoveTemp(Pixels),CapturedSeed,Frame,CapturedGain,CapturedNoise]() mutable {
                    return EncodeSensorPng(MoveTemp(Pixels),CapturedSeed,Frame,CapturedGain,CapturedNoise);
                });
        }
    }
    if ((SensorReadback.IsBusy() || EncodeFuture.IsValid()) && ReadbackEpoch==Epoch &&
        FPlatformTime::Seconds()-ReadbackStarted>MaxResultAge) {
        SensorReadback.Cancel();
        ++Epoch;
        Hold(TEXT("HOLD: sensor readback expired"));
    }
    if (!EncodeFuture.IsValid() || !EncodeFuture.IsReady()) return;
    TArray<uint8> Encoded=EncodeFuture.Get();
    EncodeFuture=TFuture<TArray<uint8>>();
    if (ReadbackEpoch!=Epoch) { ++StaleReadbacksDiscarded; return; }
    if (Encoded.IsEmpty() || FPlatformTime::Seconds()-ReadbackStarted>MaxResultAge) {
        Hold(TEXT("HOLD: sensor encoding failed or frame expired"));
        return;
    }
    LatestPng=MoveTemp(Encoded);
    LatestPngCapturedAt=ReadbackStarted;
    CaptureIndex=ReadbackFrame+1;
    if (bSaveRequested) { bSaveRequested=false; SaveSample(); }
    if (bReadbackForInference) Inspect();
}

void ADarkFactoryWorld::RequestSample() {
    if (IsImportedScene()) { LastSaved=TEXT("Training export unavailable: welding scenario contract not configured"); return; }
    bSaveRequested=true;
    LastSaved=TEXT("Capturing sensor asynchronously...");
}

void ADarkFactoryWorld::Inspect() {
    if (IsImportedScene()) return;
    if (PendingRequest.IsValid() || LatestPng.IsEmpty() || !bInferenceEnabled || bResolved) return;
    if (bBridgeSmoke && RequestStarted>0) return; // Exactly one real HTTP request in this plumbing check.
    ActiveRequestId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Payload=MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("request_id"),ActiveRequestId);
    Payload->SetStringField(TEXT("episode_id"),EpisodeId);
    Payload->SetStringField(TEXT("image_base64"),FBase64::Encode(LatestPng));
    PendingRequest=FHttpModule::Get().CreateRequest();
    PendingRequest->SetURL(Endpoint);
    PendingRequest->SetVerb(TEXT("POST"));
    PendingRequest->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
    PendingRequest->SetContentAsString(JsonLine(Payload));
    PendingRequest->SetTimeout(static_cast<float>(MaxResultAge));
    RequestStarted=FPlatformTime::Seconds();
    PendingRequest->OnProcessRequestComplete().BindUObject(this,&ADarkFactoryWorld::OnInspect,EpisodeId,Epoch,LatestPngCapturedAt>0 ? LatestPngCapturedAt:RequestStarted);
    Status=TEXT("HOLD: inspecting current sensor frame");
    ++NIRInferenceRequests;
    if (!PendingRequest->ProcessRequest()) {
        PendingRequest.Reset();
        Source=TEXT("DISCONNECTED");
        Hold(TEXT("HOLD: could not send bridge request"));
    }
}
void ADarkFactoryWorld::OnInspect(FHttpRequestPtr Request,FHttpResponsePtr Response,bool bSucceeded,FString RequestedEpisode,int32 RequestedEpoch,double Started) {
    if (PendingRequest != Request) return;
    PendingRequest.Reset();
    NextRequest=FPlatformTime::Seconds()+1;
    if (RequestedEpoch!=Epoch || RequestedEpisode!=EpisodeId || !bInferenceEnabled) {
        Hold(TEXT("HOLD: discarded response from obsolete episode"));
        return;
    }
    if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode()!=200) {
        Source=TEXT("DISCONNECTED");
        Hold(TEXT("HOLD: bridge unavailable or HTTP error"));
        return;
    }
    if (FPlatformTime::Seconds()-Started>MaxResultAge) { Hold(TEXT("HOLD: stale response")); return; }
    TSharedPtr<FJsonObject> Result;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Result) || !Result) {
        Hold(TEXT("HOLD: invalid bridge JSON")); return;
    }
    FString Id,Episode,Label,Backend,Error;
    double Latency=0;
    if (!Result->TryGetStringField(TEXT("request_id"),Id) || Id!=ActiveRequestId ||
        !Result->TryGetStringField(TEXT("episode_id"),Episode) || Episode!=EpisodeId ||
        !Result->TryGetStringField(TEXT("decision"),Label) ||
        !Result->TryGetStringField(TEXT("source"),Backend) ||
        !Result->TryGetNumberField(TEXT("latency_ms"),Latency) || !FMath::IsFinite(Latency) || Latency<0 ||
        (Label!=TEXT("G") && Label!=TEXT("A") && Label!=TEXT("R")) ||
        (Backend!=TEXT("mock") && Backend!=TEXT("evk"))) {
        Hold(TEXT("HOLD: invalid or mismatched bridge response")); return;
    }
    Source=Backend.ToUpper();
    LatencyMs=static_cast<float>(Latency);
    if (Result->HasField(TEXT("error"))) { Result->TryGetStringField(TEXT("error"),Error); Hold(TEXT("HOLD: backend error ")+Error.Left(100)); return; }
    Decision=Label;
    ResultAt=Started; // Expire against image capture time, including readback/encode latency.
    bResolved=Label!=TEXT("A");
    Status=Label==TEXT("G") ? TEXT("PASS: releasing tray") : Label==TEXT("R") ? TEXT("REJECT: tray held, reject gate engaged") : TEXT("HOLD: insufficient visual evidence");
    UE_LOG(LogTemp,Display,TEXT("DF_DECISION id=%s source=%s decision=%s latency_ms=%.1f"),*EpisodeId,*Source,*Decision,LatencyMs);
}

void ADarkFactoryWorld::SaveSample() {
    if (IsImportedScene()) { bLastSaveSucceeded=false; LastSaved=TEXT("No welding training contract configured"); return; }
    bLastSaveSucceeded=false;
    if (LatestPng.IsEmpty()) { LastSaved=TEXT("Capture unavailable; wait for sensor"); return; }
    if (Travel>0) { LastSaved=TEXT("Reset the episode before collecting a moved tray"); return; }
    const FString SampleId=CurrentPlanRow.IsValid() ? CurrentPlanRow->GetStringField(TEXT("sample_id")) : EpisodeId+FString::Printf(TEXT("-f%04d"),CaptureIndex);
    const FString RelativeImage=CurrentPlanRow.IsValid() ? CurrentPlanRow->GetStringField(TEXT("image")) : TEXT("images/")+SampleId+TEXT(".png");
    const FString File=FPaths::Combine(OutputRoot,RelativeImage);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    if (!FFileHelper::SaveArrayToFile(LatestPng,*File)) { LastSaved=TEXT("Could not write PNG"); return; }
    auto Meta=MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("schema_version"),1);
    Meta->SetStringField(TEXT("image"),RelativeImage);
    Meta->SetStringField(TEXT("sample_id"),SampleId);
    Meta->SetStringField(TEXT("episode_id"),EpisodeId);
    Meta->SetStringField(TEXT("leakage_group"),LeakageGroup);
    if (!DatasetSplit.IsEmpty()) Meta->SetStringField(TEXT("split"),DatasetSplit);
    Meta->SetNumberField(TEXT("frame_index"),CaptureIndex);
    Meta->SetStringField(TEXT("label"),GetTruthLabel());
    Meta->SetNumberField(TEXT("seed"),Seed);
    Meta->SetStringField(TEXT("contract_version"),ContractVersion);
    Meta->SetStringField(TEXT("prompt_sha256"),PromptSha256);
    auto SensorMeta=MakeShared<FJsonObject>();
    SensorMeta->SetStringField(TEXT("mode"),bSensorActive ? TEXT("active_nir_proxy"):TEXT("visible_dark"));
    SensorMeta->SetNumberField(TEXT("width"),SensorWidth);
    SensorMeta->SetNumberField(TEXT("height"),SensorHeight);
    SensorMeta->SetNumberField(TEXT("fov"),Sensor->FOVAngle);
    SensorMeta->SetNumberField(TEXT("gain"),Gain);
    SensorMeta->SetNumberField(TEXT("noise_std"),NoiseStd);
    SensorMeta->SetNumberField(TEXT("illumination_intensity"),bSensorActive ? IRLight->Intensity:0);
    SensorMeta->SetNumberField(TEXT("exposure_ev"),ExposureEV);
    Meta->SetObjectField(TEXT("sensor"),SensorMeta);
    auto Randomization=MakeShared<FJsonObject>();
    Randomization->SetArrayField(TEXT("tray_offset_cm"),VecJson(TrayOffset));
    Randomization->SetNumberField(TEXT("module_reflectance"),Reflectance);
    Randomization->SetNumberField(TEXT("material_roughness"),Roughness);
    Randomization->SetArrayField(TEXT("camera_position_jitter_cm"),VecJson(CameraPositionJitter));
    Randomization->SetArrayField(TEXT("camera_rotation_jitter_deg"),VecJson(CameraRotationJitter));
    Meta->SetObjectField(TEXT("randomization"),Randomization);
    auto State=MakeShared<FJsonObject>();
    State->SetStringField(TEXT("variant"),bSensorActive ? GetScenarioName():TEXT("sensor_off"));
    State->SetNumberField(TEXT("module_index"),TargetModule);
    State->SetNumberField(TEXT("misalignment_deg"),Scenario==2 ? Misalignment:0);
    State->SetNumberField(TEXT("occlusion_fraction"),Scenario==3 ? OcclusionFraction:0);
    Meta->SetObjectField(TEXT("state"),State);
    if (CurrentPlanRow.IsValid()) Meta=CurrentPlanRow.ToSharedRef();
    Meta->SetStringField(TEXT("capture_status"),TEXT("captured"));
    Meta->SetStringField(TEXT("renderer"),TEXT("UnrealEngine5.8-active-nir-rendering-proxy-v1"));
    if (!FFileHelper::SaveStringToFile(JsonLine(Meta)+TEXT("\n"),*FPaths::Combine(OutputRoot,TEXT("manifest.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append)) {
        LastSaved=TEXT("PNG saved but manifest failed"); return;
    }
    LastSaved=RelativeImage;
    bLastSaveSucceeded=true;
    UE_LOG(LogTemp,Display,TEXT("DF_SAMPLE image=%s label=%s"),*File,*GetTruthLabel());
}
void ADarkFactoryWorld::PrepareDatasetSample() {
    if (!PlanRows.IsEmpty()) { ApplyPlanRow(); return; }
    const int32 Group=DatasetIndex/3;
    Seed=BaseSeed+Group;
    const int32 Slot=DatasetIndex%3;
    SetScenario(Slot==0 ? 0 : Slot==1 ? 3 : (Group%2==0 ? 1:2),false);
    LeakageGroup=FString::Printf(TEXT("seed-%d"),Seed);
    EpisodeId=FString::Printf(TEXT("seed-%d-%s"),Seed,*GetScenarioName());
    const int32 Bucket=static_cast<int32>(static_cast<uint32>(Seed)*2654435761u%10);
    DatasetSplit=Bucket==0 ? TEXT("test") : Bucket==1 ? TEXT("validation") : TEXT("train");
}
void ADarkFactoryWorld::AutomationTick(double Now) {
    if (Now<NextAutomation) return;
    if (bModalityScreenshots) { CaptureModalitiesTick(Now); return; }
    if (bThermalSmoke) {
        auto* PC=GetWorld()->GetFirstPlayerController();
        auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
        if (!ThermalRig || !ThermalRig->IsReady() || !Observer) {
            UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_SMOKE missing material/camera"));
            FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        auto Raw=[&](const TCHAR* Name) { bThermalSmokePassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,Name)); };
        auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
        if (SmokeStep==0) {
            bThermalPreviewGpuOnly=ThermalRig->GetGPUCaptures()>0 && ThermalRig->GetReadbacks()==0;
            ThermalRig->SetFrozen(true); ThermalRig->SetHeatTransferEnabled(false); ThermalRig->SetCase(0);
            ThermalSmokeNIRRequests=NIRInferenceRequests;
            ThermalSmokeIntensity=IRLight->Intensity;
            ThermalSmokeRoofLightIntensity=GetRoofLightIntensity();
            NightVisionCaptureIndex=CaptureIndex;
            CaptureSensor(); NightVisionFrozenPng=LatestPng;
            NextCapture=TNumericLimits<double>::Max();
            bThermalSmokePassed &= FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("nir_before.png")));
            Observer->SetObserverMode(2); Observer->SetThermalPalette(0);
        } else if (SmokeStep==1) { Raw(TEXT("normal.png")); bThermalSmokePassed &= ThermalRig->SaveFieldSmoke(OutputRoot); bThermalSmokePassed &= ThermalRig->SaveTransferSmoke(OutputRoot);
            if (FParse::Param(FCommandLine::Get(),TEXT("DFThermalReflectionSmoke"))) bThermalSmokePassed &= ThermalRig->SaveReflectionSmoke(OutputRoot);
            ThermalRig->SetCase(1); }
        else if (SmokeStep==2) { Raw(TEXT("overheated.png")); ThermalRig->SetCase(2); }
        else if (SmokeStep==3) { Raw(TEXT("heater_fault.png")); ThermalRig->SetCase(3); }
        else if (SmokeStep==4) { Raw(TEXT("sealant_gap.png")); ThermalRig->SetCase(1); }
        else if (SmokeStep==5) { SetRoofLightIntensity(0); IRLight->SetIntensity(0); Raw(TEXT("light_off.png")); SetRoofLightIntensity(ThermalSmokeRoofLightIntensity); IRLight->SetIntensity(24000); IRLight->ViewLightingChannels.bViewChannel0=true; IRLight->MarkRenderStateDirty(); }
        else if (SmokeStep==6) { Raw(TEXT("light_on.png")); IRLight->SetIntensity(ThermalSmokeIntensity); IRLight->ViewLightingChannels.bViewChannel0=false; IRLight->MarkRenderStateDirty(); ThermalRig->SetCase(4); }
        else if (SmokeStep==7) { Raw(TEXT("occluded.png")); ThermalRig->SetCase(1); }
        else if (SmokeStep==8) { ThermalRig->CaptureGPU(); Observer->SetThermalPalette(0); Shot(TEXT("white_hot.png")); }
        else if (SmokeStep==9) { Observer->SetThermalPalette(1); Shot(TEXT("black_hot.png")); }
        else if (SmokeStep==10) { Observer->SetThermalPalette(2); Shot(TEXT("iron.png")); }
        else if (SmokeStep==11) { ThermalRig->SetCase(0); Observer->SetThermalPalette(0); Observer->SetObserverMode(1); }
        else if (SmokeStep==12) {
            ThermalSmokeReadbackBaseline=ThermalRig->GetReadbacks();
            ThermalRig->RequestSnapshot(); ReadbackSmokeDeadline=Now+10;
            ++SmokeStep; NextAutomation=Now; return;
        } else if (SmokeStep==13) {
            // World ticks before the rig. Invalidate after enqueue, before the rig can poll/publish.
            if (ThermalRig->GetReadbacks()<=ThermalSmokeReadbackBaseline && Now<ReadbackSmokeDeadline) { NextAutomation=Now; return; }
            ThermalRig->NotifySceneChanged();
            bThermalStalePreviewRejected=ThermalRig->GetTarget()==nullptr;
            ++SmokeStep; NextAutomation=Now; return;
        } else if (SmokeStep==14) {
            if (ThermalRig->GetStaleSnapshotDiscards()==0 && Now<ReadbackSmokeDeadline) { NextAutomation=Now+.05; return; }
            bThermalSmokePassed &= ThermalRig->GetStaleSnapshotDiscards()==1 && bThermalStalePreviewRejected;
            ThermalRig->RequestSnapshot(); ReadbackSmokeDeadline=Now+10;
        } else {
            if (ThermalRig->LastSnapshot==TEXT("Capturing thermal frame...") && Now<ReadbackSmokeDeadline) { NextAutomation=Now+.1; return; }
            TArray<uint8> AsyncThermal,ReferenceThermal;
            const bool SnapshotParity=FFileHelper::LoadFileToArray(AsyncThermal,*ThermalRig->LastSnapshot)
                && FFileHelper::LoadFileToArray(ReferenceThermal,*FPaths::Combine(OutputRoot,TEXT("normal.png")))
                && AsyncThermal==ReferenceThermal && !AsyncThermal.IsEmpty();
            bThermalSmokePassed &= SnapshotParity;

            CaptureIndex=NightVisionCaptureIndex; CaptureSensor();
            const bool Unchanged=LatestPng==NightVisionFrozenPng && !LatestPng.IsEmpty();
            bThermalSmokePassed &= FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("nir_after.png")));
            auto Report=MakeShared<FJsonObject>();
            Report->SetBoolField(TEXT("observer_material_loaded"),Observer->HasThermalMaterial());
            Report->SetBoolField(TEXT("sensor_material_loaded"),ThermalRig->IsReady());
            Report->SetBoolField(TEXT("temperature_lut_loaded"),ThermalRig->GetProfileCount()>0);
            Report->SetBoolField(TEXT("emissivity_lut_loaded"),ThermalRig->GetProfileCount()>0);
            Report->SetBoolField(TEXT("thermal_sent_to_nir_contract"),false);
            Report->SetBoolField(TEXT("nir_contract_unchanged"),true);
            Report->SetBoolField(TEXT("nir_png_unchanged"),Unchanged);
            Report->SetBoolField(TEXT("adapter_ready"),false);
            Report->SetBoolField(TEXT("radiometry_self_test"),DarkFactoryThermal::SelfTest());
            Report->SetBoolField(TEXT("async_snapshot_matches_raw"),SnapshotParity);
            Report->SetBoolField(TEXT("stale_snapshot_rejected"),ThermalRig->GetStaleSnapshotDiscards()==1);
            Report->SetBoolField(TEXT("stale_preview_rejected"),bThermalStalePreviewRejected);
            Report->SetStringField(TEXT("async_snapshot"),ThermalRig->LastSnapshot);
            auto ScreenRegions=MakeShared<FJsonObject>();
            if (PC) if (const auto* ThermalQAHUD=Cast<ADarkFactoryHUD>(PC->GetHUD())) ScreenRegions=ThermalQAHUD->GetThermalQARegions();
            Report->SetObjectField(TEXT("qa_screen_regions"),ScreenRegions);
            Report->SetStringField(TEXT("qa_screen_region_source"),TEXT("hud-draw-bounds-v1"));
            Report->SetNumberField(TEXT("nir_inference_requests_before"),ThermalSmokeNIRRequests);
            Report->SetNumberField(TEXT("nir_inference_requests_after"),NIRInferenceRequests);
            Report->SetNumberField(TEXT("thermal_inference_requests"),0);
            Report->SetNumberField(TEXT("cpu_readbacks_in_live_thermal_preview"),bThermalPreviewGpuOnly ? 0:-1);
            Report->SetStringField(TEXT("nir_contract_version"),ContractVersion);
            Report->SetStringField(TEXT("nir_prompt_sha256"),PromptSha256);
            Report->SetObjectField(TEXT("thermal_encoding"),ThermalRig->GetEncodingMetadata());
            Report->SetObjectField(TEXT("qa_regions"),ThermalRig->GetQARegions());
            Report->SetNumberField(TEXT("registered_profiles"),ThermalRig->GetProfileCount());
            Report->SetNumberField(TEXT("gpu_captures"),ThermalRig->GetGPUCaptures());
            Report->SetNumberField(TEXT("explicit_test_readbacks"),ThermalRig->GetReadbacks());
            bThermalSmokePassed &= Unchanged && bThermalPreviewGpuOnly && Observer->HasThermalMaterial() && NIRInferenceRequests==ThermalSmokeNIRRequests && DarkFactoryThermal::SelfTest();
            Report->SetBoolField(TEXT("passed"),bThermalSmokePassed);
            FFileHelper::SaveStringToFile(JsonLine(Report),*FPaths::Combine(OutputRoot,TEXT("runtime.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogTemp,Display,TEXT("DF_THERMAL_SMOKE passed=%d nir_unchanged=%d"),bThermalSmokePassed,Unchanged);
            FPlatformMisc::RequestExitWithStatus(false,bThermalSmokePassed ? 0:1);
        }
        ++SmokeStep; NextAutomation=Now+1.5; return;
    }
    if (bReadbackSmoke) {
        if (SmokeStep==0) {
            IFileManager::Get().MakeDirectory(*OutputRoot,true);
            bReadbackSmokeNoCpuPreview=GPUPreviewCaptures>0 && SyncReadbacks==0 && AsyncReadbacks==0;
            NightVisionCaptureIndex=CaptureIndex;
            double Start=FPlatformTime::Seconds();
            CaptureSensor(); // Test reference only; live browsing never calls this.
            ReadbackSmokeSyncMs=(FPlatformTime::Seconds()-Start)*1000;
            NightVisionFrozenPng=LatestPng;
            FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("model_reference.png")));
            LatestPng.Reset();
            CaptureIndex=NightVisionCaptureIndex;
            NextCapture=TNumericLimits<double>::Max();
            Start=FPlatformTime::Seconds();
            BeginSensorReadback(false);
            ReadbackSmokeEnqueueMs=(FPlatformTime::Seconds()-Start)*1000;
            ReadbackSmokeDeadline=Now+10;
            ++SmokeStep;
        } else if (SmokeStep==1 || SmokeStep==2) {
            if (SensorReadback.IsBusy() || EncodeFuture.IsValid()) {
                if (Now>ReadbackSmokeDeadline) {
                    UE_LOG(LogTemp,Error,TEXT("DF_READBACK_SMOKE timeout"));
                    FPlatformMisc::RequestExitWithStatus(false,1);
                }
                NextAutomation=Now+.05;
                return;
            }
            if (SmokeStep==1) {
                bReadbackParityPassed=!LatestPng.IsEmpty() && LatestPng==NightVisionFrozenPng;
                FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("model_async.png")));
                LatestPng.Reset();
                BeginSensorReadback(false);
                ++Epoch; // A completed obsolete job must never publish its PNG.
                ++SmokeStep;
            } else {
                const bool bStaleRejected=StaleReadbacksDiscarded==1 && LatestPng.IsEmpty();
                const bool bObsoletePreviewRejected=!BeginSensorReadback(false);
                const bool bSensorIR=bSensorActive && Sensor->ViewLightingChannels.bViewChannel1;
                if (auto* PC=GetWorld()->GetFirstPlayerController())
                    if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) Observer->ToggleView();
                auto Report=MakeShared<FJsonObject>();
                const bool bPixelsIdentical=bReadbackParityPassed;
                bReadbackParityPassed=bReadbackParityPassed && bStaleRejected && bObsoletePreviewRejected && bReadbackSmokeNoCpuPreview && bSensorIR;
                Report->SetBoolField(TEXT("passed"),bReadbackParityPassed);
                Report->SetBoolField(TEXT("gpu_preview_without_cpu_readback"),bReadbackSmokeNoCpuPreview);
                Report->SetBoolField(TEXT("sync_async_png_identical"),!NightVisionFrozenPng.IsEmpty() && bPixelsIdentical);
                Report->SetBoolField(TEXT("stale_capture_discarded"),bStaleRejected);
                Report->SetBoolField(TEXT("obsolete_preview_rejected"),bObsoletePreviewRejected);
                Report->SetBoolField(TEXT("sensor_stays_infrared"),bSensorIR);
                Report->SetNumberField(TEXT("gpu_preview_captures"),GPUPreviewCaptures);
                Report->SetNumberField(TEXT("async_readbacks"),AsyncReadbacks);
                Report->SetNumberField(TEXT("offline_reference_readbacks"),SyncReadbacks);
                Report->SetNumberField(TEXT("cpu_preview_uploads"),0);
                Report->SetNumberField(TEXT("single_sync_capture_encode_game_thread_ms"),ReadbackSmokeSyncMs);
                Report->SetNumberField(TEXT("single_async_enqueue_game_thread_ms"),ReadbackSmokeEnqueueMs);
                Report->SetStringField(TEXT("timing_scope"),TEXT("One diagnostic sample; asynchronous completion latency and GPU work excluded from enqueue time. Not an FPS or GPU speedup benchmark."));
                FFileHelper::SaveStringToFile(JsonLine(Report),*FPaths::Combine(OutputRoot,TEXT("readback.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,TEXT("observer_dark.png")),false,false);
                UE_LOG(LogTemp,Display,TEXT("DF_READBACK_SMOKE passed=%d sync_ms=%.3f enqueue_ms=%.3f"),bReadbackParityPassed,ReadbackSmokeSyncMs,ReadbackSmokeEnqueueMs);
                ++SmokeStep;
            }
        } else FPlatformMisc::RequestExitWithStatus(false,bReadbackParityPassed ? 0:1);
        NextAutomation=Now+.5;
        return;
    }
    if (bNightVisionSmoke) {
        auto* PC=GetWorld()->GetFirstPlayerController();
        auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
        auto* DisplayHUD=PC ? Cast<ADarkFactoryHUD>(PC->GetHUD()):nullptr;
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        auto Screenshot=[&](const TCHAR* Name) {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false);
        };
        if (SmokeStep==0) {
            NightVisionCaptureIndex=CaptureIndex;
            CaptureSensor();
            NightVisionFrozenPng=LatestPng;
            // Freeze the SAME input while changing only display shader parameters.
            NextCapture=TNumericLimits<double>::Max();
            bGreenDisplay=true;
            if (Observer) Observer->SetNightVisionSmokeFrame(12);
            FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("model_before.png")));
            Screenshot(TEXT("green.png"));
        } else if (SmokeStep==1) {
            if (Observer) Observer->SetNightVisionSmokeFrame(97);
            Screenshot(TEXT("green_next_frame.png"));
        } else if (SmokeStep==2) {
            bGreenDisplay=false;
            Screenshot(TEXT("monochrome.png"));
        } else if (SmokeStep==3) {
            bGreenDisplay=true;
            if (Observer) Observer->ToggleView();
            Screenshot(TEXT("observer_dark.png"));
        } else {
            const bool bUnchanged=!LatestPng.IsEmpty() && NightVisionFrozenPng==LatestPng;
            // Re-render the independent sensor with identical augmentation noise.
            CaptureIndex=NightVisionCaptureIndex;
            CaptureSensor();
            const bool bRecaptureUnchanged=!LatestPng.IsEmpty() && NightVisionFrozenPng==LatestPng;
            const bool bObserverShader=Observer && Observer->HasNightVisionMaterial();
            const bool bPreviewShader=DisplayHUD && DisplayHUD->HasNightVisionMaterial();
            const bool bSaved=FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("model_after.png")));
            auto Report=MakeShared<FJsonObject>();
            Report->SetBoolField(TEXT("sensor_infrared_locked"),bSensorActive && Sensor->ViewLightingChannels.bViewChannel1 && !Sensor->ViewLightingChannels.bViewChannel0);
            Report->SetBoolField(TEXT("model_png_unchanged"),bUnchanged);
            Report->SetBoolField(TEXT("model_recapture_unchanged"),bRecaptureUnchanged);
            Report->SetBoolField(TEXT("observer_material_loaded"),bObserverShader);
            Report->SetBoolField(TEXT("preview_material_loaded"),bPreviewShader);
            Report->SetBoolField(TEXT("passed"),bUnchanged && bRecaptureUnchanged && bObserverShader && bPreviewShader && bSaved);
            Report->SetStringField(TEXT("scope"),TEXT("Display shader smoke; no model inference"));
            FFileHelper::SaveStringToFile(JsonLine(Report),*FPaths::Combine(OutputRoot,TEXT("runtime.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogTemp,Display,TEXT("DF_NIGHT_VISION_SMOKE unchanged=%d recaptured=%d observer=%d preview=%d"),bUnchanged,bRecaptureUnchanged,bObserverShader,bPreviewShader);
            FPlatformMisc::RequestExitWithStatus(false,bUnchanged && bRecaptureUnchanged && bObserverShader && bPreviewShader && bSaved ? 0:1);
        }
        ++SmokeStep;
        NextAutomation=Now+1.5;
        return;
    }
    if (bBridgeSmoke) {
        if (bBridgeSmokeFinished) { FPlatformMisc::RequestExitWithStatus(false,bBridgeSmokePassed ? 0:1); return; }
        const bool bReplyArrived=ResultAt>0 || (RequestStarted>0 && !PendingRequest.IsValid());
        if (!bReplyArrived && Now<BridgeSmokeDeadline) { NextAutomation=Now+.5; return; }
        const float GateRoll=RejectArm->GetRelativeRotation().Roll;
        if (BridgeExpected==TEXT("G")) bBridgeSmokePassed=Decision==TEXT("G") && ResultAt>0 && Travel>5;
        else if (BridgeExpected==TEXT("R")) bBridgeSmokePassed=Decision==TEXT("R") && ResultAt>0 && Travel==0 && FMath::Abs(GateRoll)<1;
        else if (BridgeExpected==TEXT("A")) bBridgeSmokePassed=Decision==TEXT("A") && ResultAt>0 && Travel==0;
        else if (BridgeExpected==TEXT("ERROR")) bBridgeSmokePassed=Decision==TEXT("A") && ResultAt==0 && Travel==0 && RequestStarted>0 && !PendingRequest.IsValid() && Status.StartsWith(TEXT("HOLD:"));
        auto Report=MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("expected"),BridgeExpected);
        Report->SetBoolField(TEXT("passed"),bBridgeSmokePassed);
        Report->SetStringField(TEXT("decision"),Decision);
        Report->SetStringField(TEXT("source"),Source);
        Report->SetStringField(TEXT("status"),Status);
        Report->SetNumberField(TEXT("travel_cm"),Travel);
        Report->SetNumberField(TEXT("reject_gate_roll_deg"),GateRoll);
        Report->SetNumberField(TEXT("latency_ms"),LatencyMs);
        Report->SetBoolField(TEXT("real_model_accuracy_evidence"),false);
        const FString Stem=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Smoke"),TEXT("bridge_")+BridgeExpected);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Stem),true);
        if (!FFileHelper::SaveStringToFile(JsonLine(Report),*(Stem+TEXT(".json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bBridgeSmokePassed=false;
        FScreenshotRequest::RequestScreenshot(Stem+TEXT(".png"),false,false);
        UE_LOG(LogTemp,Display,TEXT("DF_BRIDGE_SMOKE expected=%s passed=%s decision=%s travel=%.1f source=%s"),*BridgeExpected,bBridgeSmokePassed ? TEXT("true"):TEXT("false"),*Decision,Travel,*Source);
        bBridgeSmokeFinished=true;
        bInferenceEnabled=false;
        NextAutomation=Now+1;
    } else if (DatasetCount>0) {
        CaptureSensor(); SaveSample();
        if (!bLastSaveSucceeded) { UE_LOG(LogTemp,Error,TEXT("DF_DATASET_ERROR %s"),*LastSaved); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        ++DatasetIndex;
        if (DatasetIndex>=DatasetCount) { UE_LOG(LogTemp,Display,TEXT("DF_DATASET_COMPLETE count=%d"),DatasetIndex); FPlatformMisc::RequestExit(false); }
        else { PrepareDatasetSample(); NextAutomation=Now+1.0; }
    } else if (bSmoke) {
        if (SmokeStep==0) {
            CaptureSensor(); SaveSample();
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Smoke/observer_ir.png")),false,false);
        } else if (SmokeStep==1) {
            if (auto* PC=GetWorld()->GetFirstPlayerController())
                if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) Observer->ToggleView();
        } else if (SmokeStep==2) {
            CaptureSensor(); SaveSample();
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Smoke/observer_dark_sensor_ir.png")),false,false);
        } else if (SmokeStep==3) {
            if (auto* PC=GetWorld()->GetFirstPlayerController())
                if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) Observer->ToggleView();
            ToggleSensorForCalibration();
        } else if (SmokeStep==4) { CaptureSensor(); SaveSample(); }
        else if (SmokeStep==5) { ToggleSensorForCalibration(); SetScenario(1,false); }
        else if (SmokeStep==6) { CaptureSensor(); SaveSample(); }
        else if (SmokeStep==7) { SetScenario(2,false); }
        else if (SmokeStep==8) { CaptureSensor(); SaveSample(); }
        else if (SmokeStep==9) { SetScenario(3,false); }
        else if (SmokeStep==10) { CaptureSensor(); SaveSample(); }
        else { UE_LOG(LogTemp,Display,TEXT("DF_SMOKE_COMPLETE")); FPlatformMisc::RequestExit(false); }
        ++SmokeStep; NextAutomation=Now+1.0;
    }
}
void ADarkFactoryWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    const double Now=FPlatformTime::Seconds();
    if (bGraphicsQA) { GraphicsSmokeTick(Now); return; }
    if (bEquipmentHeatQA) { EquipmentHeatSmokeTick(Now); return; }
    if (bCurtainQA) { CurtainSmokeTick(DeltaSeconds,Now); return; }
    PollSensorReadback();
    const bool bOfflineCapture=DatasetCount>0 || bSmoke || bNightVisionSmoke;
    if (Now>=NextCapture) {
        if (bOfflineCapture) CaptureSensor();
        else { Sensor->CaptureScene(); PreviewEpoch=Epoch; PreviewCapturedAt=Now; ++GPUPreviewCaptures; }
        NextCapture=Now+(bOfflineCapture ? .5:1.0/PreviewHz);
    }
    if (IsImportedScene()) {
        bInferenceEnabled=false;
        if (ProcessController) {
            ProcessController->CaptureTick();
            const auto& Process=ProcessController->GetStatus();
            if (ControlCabinet) ControlCabinet->SetProcessStatus(Process);
            if (CellMechanisms) CellMechanisms->SetProcessStatus(Process);
            Source=Process.Backend; Decision=Process.Decision; Status=Process.Message; LatencyMs=Process.LastLatencyMs;
        }
        if (bWeldingSmoke) WeldingSmokeTick(Now);
        return;
    }
    if (!bOfflineCapture && !bReadbackSmoke && !bThermalSmoke) {
        if (bSaveRequested) BeginSensorReadback(false);
        if (Now>=NextRequest && bInferenceEnabled && !PendingRequest.IsValid() && !bResolved) {
            if (BeginSensorReadback(true)) NextRequest=Now+1;
        }
    } else if (bOfflineCapture && Now>=NextRequest) { Inspect(); NextRequest=Now+1; }
    if (ResultAt>0 && Now-ResultAt>MaxResultAge) Hold(TEXT("HOLD: decision freshness expired"));
    if (bResolved && Decision==TEXT("G")) {
        Travel=FMath::Min(Travel+DeltaSeconds*70.f,290.f);
        TrayRoot->SetRelativeLocation(TrayOffset+FVector(Travel,0,0));
    }
    if (bResolved && Decision==TEXT("R")) RejectArm->SetRelativeRotation(FRotator(0,0,0));
    if (DatasetCount>0 || bSmoke || bBridgeSmoke || bNightVisionSmoke || bReadbackSmoke || bThermalSmoke || bModalityScreenshots) AutomationTick(Now);
}
void ADarkFactoryWorld::EndPlay(const EEndPlayReason::Type Reason) {
    if (WeldingEffectsPreviousTemporalFreeze>=0) {
        if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences")))
            Freeze->Set(WeldingEffectsPreviousTemporalFreeze,ECVF_SetByCode);
        WeldingEffectsPreviousTemporalFreeze=-1;
    }
    if (PendingRequest.IsValid()) { PendingRequest->OnProcessRequestComplete().Unbind(); PendingRequest->CancelRequest(); PendingRequest.Reset(); }
    SensorReadback.Cancel();
    ++Epoch; // Detached encoding jobs own only pixel arrays and scalars.
    UE_LOG(LogTemp,Display,TEXT("DF_CAPTURE_STATS gpu_preview=%d async_readbacks=%d sync_readbacks=%d stale_discarded=%d cpu_preview_uploads=0"),GPUPreviewCaptures,AsyncReadbacks,SyncReadbacks,StaleReadbacksDiscarded);
    if (bPhysicsOverrideAcquired) {
        bPhysicsOverrideAcquired=false;
        ReleasePhysicsOverride();
    }
    Super::EndPlay(Reason);
}





bool ADarkFactoryWorld::LoadPlan(const FString& Path) {
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines,*Path)) { UE_LOG(LogTemp,Error,TEXT("DF_PLAN_ERROR cannot read %s"),*Path); return false; }
    TSet<FString> Images;
    for (const FString& Line : Lines) {
        if (Line.TrimStartAndEnd().IsEmpty()) continue;
        TSharedPtr<FJsonObject> Row;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line),Row) || !Row) return false;
        for (const TCHAR* Field : {TEXT("sample_id"),TEXT("episode_id"),TEXT("leakage_group"),TEXT("split"),TEXT("image"),TEXT("label"),TEXT("contract_version"),TEXT("prompt_sha256")}) {
            FString Value;
            if (!Row->TryGetStringField(Field,Value) || Value.IsEmpty()) { UE_LOG(LogTemp,Error,TEXT("DF_PLAN_ERROR missing %s"),Field); return false; }
        }
        for (const TCHAR* Field : {TEXT("seed"),TEXT("frame_index")}) {
            double Number=0;
            if (!Row->TryGetNumberField(Field,Number) || !FMath::IsFinite(Number) || Number<0 || Number>MAX_int32) return false;
        }
        const TSharedPtr<FJsonObject>* SensorData=nullptr;
        const TSharedPtr<FJsonObject>* RandomData=nullptr;
        const TSharedPtr<FJsonObject>* StateData=nullptr;
        if (!Row->TryGetObjectField(TEXT("sensor"),SensorData) || !Row->TryGetObjectField(TEXT("randomization"),RandomData) || !Row->TryGetObjectField(TEXT("state"),StateData)) return false;
        for (const TCHAR* Field : {TEXT("width"),TEXT("height"),TEXT("fov"),TEXT("gain"),TEXT("noise_std"),TEXT("illumination_intensity"),TEXT("exposure_ev")}) {
            double Number=0;
            if (!(*SensorData)->TryGetNumberField(Field,Number) || !FMath::IsFinite(Number)) return false;
        }
        for (const TCHAR* Field : {TEXT("module_reflectance"),TEXT("material_roughness")}) {
            double Number=0;
            if (!(*RandomData)->TryGetNumberField(Field,Number) || !FMath::IsFinite(Number)) return false;
        }
        for (const TCHAR* Field : {TEXT("module_index"),TEXT("misalignment_deg"),TEXT("occlusion_fraction")}) {
            double Number=0;
            if (!(*StateData)->TryGetNumberField(Field,Number) || !FMath::IsFinite(Number)) return false;
        }
        for (const TCHAR* Field : {TEXT("tray_offset_cm"),TEXT("camera_position_jitter_cm"),TEXT("camera_rotation_jitter_deg")}) {
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
            if (!(*RandomData)->TryGetArrayField(Field,Values) || Values->Num()<(FCString::Strcmp(Field,TEXT("tray_offset_cm"))==0 ? 2:3)) return false;
            for (const auto& Value : *Values) { double Number=0; if (!Value->TryGetNumber(Number) || !FMath::IsFinite(Number)) return false; }
        }
        const FString Image=Row->GetStringField(TEXT("image"));
        const FString Variant=(*StateData)->GetStringField(TEXT("variant"));
        const FString Label=Row->GetStringField(TEXT("label"));
        const FString Mode=(*SensorData)->GetStringField(TEXT("mode"));
        const FString Expected=Variant==TEXT("normal") ? TEXT("G") : (Variant==TEXT("missing") || Variant==TEXT("misaligned")) ? TEXT("R") : (Variant==TEXT("occluded") || Variant==TEXT("sensor_off")) ? TEXT("A") : TEXT("INVALID");
        if (!FPaths::IsRelative(Image) || Image.Contains(TEXT("..")) || Image.Contains(TEXT(":")) || !Image.EndsWith(TEXT(".png")) || Images.Contains(Image) ||
            Row->GetStringField(TEXT("contract_version"))!=ContractVersion || Row->GetStringField(TEXT("prompt_sha256"))!=PromptSha256 ||
            Label!=Expected || (*SensorData)->GetNumberField(TEXT("width"))!=SensorWidth || (*SensorData)->GetNumberField(TEXT("height"))!=SensorHeight ||
            (Mode!=TEXT("active_nir_proxy") && Mode!=TEXT("visible_dark")) || (Mode==TEXT("visible_dark") && Variant!=TEXT("sensor_off")) ||
            (*SensorData)->GetNumberField(TEXT("fov"))<20 || (*SensorData)->GetNumberField(TEXT("fov"))>100 ||
            (*StateData)->GetNumberField(TEXT("module_index"))<0 || (*StateData)->GetNumberField(TEXT("module_index"))>3) {
            UE_LOG(LogTemp,Error,TEXT("DF_PLAN_ERROR invalid image, contract, sensor or state at %s"),*Image); return false;
        }
        Images.Add(Image);
        PlanRows.Add(Row);
    }
    DatasetCount=PlanRows.Num();
    UE_LOG(LogTemp,Display,TEXT("DF_PLAN_LOADED count=%d path=%s"),DatasetCount,*Path);
    return DatasetCount>0;
}
void ADarkFactoryWorld::ApplyPlanRow() {
    const TSharedPtr<FJsonObject> Row=PlanRows[DatasetIndex];
    const auto& State=Row->GetObjectField(TEXT("state"));
    const auto& Nuisance=Row->GetObjectField(TEXT("randomization"));
    const auto& SensorData=Row->GetObjectField(TEXT("sensor"));
    const FString Variant=State->GetStringField(TEXT("variant"));
    Seed=static_cast<int32>(Row->GetNumberField(TEXT("seed")));
    SetScenario(Variant==TEXT("missing") ? 1 : Variant==TEXT("misaligned") ? 2 : Variant==TEXT("occluded") ? 3:0,false);
    CurrentPlanRow=Row;
    EpisodeId=Row->GetStringField(TEXT("episode_id"));
    LeakageGroup=Row->GetStringField(TEXT("leakage_group"));
    DatasetSplit=Row->GetStringField(TEXT("split"));
    TargetModule=static_cast<int32>(State->GetNumberField(TEXT("module_index")));
    Misalignment=State->GetNumberField(TEXT("misalignment_deg"));
    OcclusionFraction=State->GetNumberField(TEXT("occlusion_fraction"));
    auto ReadVector=[](const TArray<TSharedPtr<FJsonValue>>& Array) {
        return FVector(Array[0]->AsNumber(),Array[1]->AsNumber(),Array.Num()>2 ? Array[2]->AsNumber():0);
    };
    TrayOffset=ReadVector(Nuisance->GetArrayField(TEXT("tray_offset_cm")));
    CameraPositionJitter=ReadVector(Nuisance->GetArrayField(TEXT("camera_position_jitter_cm")));
    CameraRotationJitter=ReadVector(Nuisance->GetArrayField(TEXT("camera_rotation_jitter_deg")));
    Reflectance=Nuisance->GetNumberField(TEXT("module_reflectance"));
    Roughness=Nuisance->GetNumberField(TEXT("material_roughness"));
    Gain=SensorData->GetNumberField(TEXT("gain"));
    NoiseStd=SensorData->GetNumberField(TEXT("noise_std"));
    ExposureEV=SensorData->GetNumberField(TEXT("exposure_ev"));
    IRLight->SetIntensity(SensorData->GetNumberField(TEXT("illumination_intensity")));
    Sensor->FOVAngle=SensorData->GetNumberField(TEXT("fov"));
    Sensor->PostProcessSettings.AutoExposureBias=-ExposureEV;
    bSensorActive=SensorData->GetStringField(TEXT("mode"))==TEXT("active_nir_proxy");
    Sensor->ViewLightingChannels.bViewChannel0=!bSensorActive;
    Sensor->ViewLightingChannels.bViewChannel1=bSensorActive;
    const FVector SensorHome(-310,-300,380);
    Sensor->SetRelativeLocation(SensorHome+CameraPositionJitter);
    const FRotator SensorAim=(FVector(0,0,99)-SensorHome).Rotation();
    Sensor->SetRelativeRotation(SensorAim+FRotator(CameraRotationJitter.X,CameraRotationJitter.Y,CameraRotationJitter.Z));
    TrayRoot->SetRelativeLocation(TrayOffset);
    for (int32 I=0;I<Modules.Num();++I) {
        Modules[I]->SetVisibility(!(Scenario==1 && I==TargetModule),true);
        Modules[I]->SetRelativeLocation(FVector(-72+48*I,0,109));
        Modules[I]->SetRelativeRotation(FRotator::ZeroRotator);
        if (auto* Material=Cast<UMaterialInstanceDynamic>(Modules[I]->GetMaterial(0))) {
            Material->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(Reflectance,Reflectance,Reflectance));
            Material->SetScalarParameterValue(TEXT("Roughness"),Roughness);
        }
    }
    if (Scenario==2) {
        Modules[TargetModule]->SetRelativeLocation(FVector(-72+48*TargetModule,-22,131));
        Modules[TargetModule]->SetRelativeRotation(FRotator(0,Misalignment,12));
    }
    // Fraction is geometric cover extent, not a measured image segmentation ratio.
    Occluder->SetRelativeScale3D(FVector(2.75*OcclusionFraction,2.05,.12));
    Occluder->SetRelativeLocation(FVector(-15-137.5*(1-OcclusionFraction),-5,164));
    UE_LOG(LogTemp,Display,TEXT("DF_PLAN_APPLIED sample=%s label=%s target_module=%d"),*Row->GetStringField(TEXT("sample_id")),*Row->GetStringField(TEXT("label")),TargetModule);
}




void ADarkFactoryWorld::SetRealismDetailsEnabled(bool Enabled) {
    bRealismDetailsEnabled=Enabled;
    if (EnvironmentDetails) EnvironmentDetails->SetEnabled(Enabled);
    if (CellMechanisms) CellMechanisms->SetEnabled(Enabled);
    if (Soundscape) Soundscape->SetEnabled(Enabled);
    if (WeldingCell) WeldingCell->SetWeldWitnessEnabled(Enabled);
}
