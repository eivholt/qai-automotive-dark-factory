#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryThermalRadiometry.h"
#include "DarkFactoryGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
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
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace {
constexpr int32 SensorWidth = 448, SensorHeight = 256;
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
        Light->SetIntensity(RoofLightLumens); Light->SetAttenuationRadius(1500);
        Light->SetSourceWidth(220); Light->SetSourceHeight(180);
        Light->SetLightColor(FLinearColor(.72f,.82f,1.f));
        Light->SetCastShadows(false);
        Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
        RoofWindowLights.Add(Light);
    }
}

void ADarkFactoryWorld::SetRoofLightIntensity(float Lumens) {
    RoofLightLumens=FMath::IsFinite(Lumens) ? FMath::Clamp(Lumens,0.f,20000.f):80.f;
    for (const auto& Light:RoofWindowLights) if (Light) Light->SetIntensity(RoofLightLumens);
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
    bInferenceEnabled = !FParse::Param(FCommandLine::Get(), TEXT("DFNoInference")) && DatasetCount <= 0 && !bSmoke && !bNightVisionSmoke && !bReadbackSmoke && !bThermalSmoke && !bModalityScreenshots;
    OutputRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), bModalityScreenshots ? TEXT("Modalities"):bNightVisionSmoke ? TEXT("NightVision"):TEXT("Dataset")));
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
    BuildCell();
    SensorTarget = NewObject<UTextureRenderTarget2D>(this);
    SensorTarget->RenderTargetFormat = RTF_RGBA8;
    SensorTarget->InitCustomFormat(SensorWidth, SensorHeight, PF_B8G8R8A8, false);
    SensorTarget->ClearColor = FLinearColor::Black;
    SensorTarget->UpdateResourceImmediate(true);
    Sensor->TextureTarget = SensorTarget;
    Seed = BaseSeed;
    SetScenario(0, false);
    ThermalRig=GetWorld()->SpawnActor<ADarkFactoryThermalRig>();
    ThermalRig->Initialize(this,Sensor);
    if (DatasetCount > 0) PrepareDatasetSample();
    NextAutomation = FPlatformTime::Seconds() + (bBridgeSmoke ? 6.0:4.0);
    BridgeSmokeDeadline=FPlatformTime::Seconds()+MaxResultAge+10.0;
    UE_LOG(LogTemp, Display, TEXT("DF_READY sensor=448x256 active_nir_proxy endpoint=%s seed=%d"), *Endpoint, Seed);
}

FString ADarkFactoryWorld::GetScenarioName() const {
    static const TCHAR* Names[] = {TEXT("normal"),TEXT("missing"),TEXT("misaligned"),TEXT("occluded")};
    return Names[FMath::Clamp(Scenario,0,3)];
}
FString ADarkFactoryWorld::GetTruthLabel() const {
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
    bSaveRequested=true;
    LastSaved=TEXT("Capturing sensor asynchronously...");
}

void ADarkFactoryWorld::Inspect() {
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
        } else if (SmokeStep==1) { Raw(TEXT("normal.png")); bThermalSmokePassed &= ThermalRig->SaveFieldSmoke(OutputRoot); bThermalSmokePassed &= ThermalRig->SaveTransferSmoke(OutputRoot); ThermalRig->SetCase(1); }
        else if (SmokeStep==2) { Raw(TEXT("overheated.png")); ThermalRig->SetCase(2); }
        else if (SmokeStep==3) { Raw(TEXT("heater_fault.png")); ThermalRig->SetCase(3); }
        else if (SmokeStep==4) { Raw(TEXT("glue_gap.png")); ThermalRig->SetCase(1); }
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
            ScreenRegions->SetArrayField(TEXT("thermal_preview"),{MakeShared<FJsonValueNumber>(1054),MakeShared<FJsonValueNumber>(535),MakeShared<FJsonValueNumber>(1348),MakeShared<FJsonValueNumber>(703)});
            Report->SetObjectField(TEXT("qa_screen_regions"),ScreenRegions);
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
    PollSensorReadback();
    const bool bOfflineCapture=DatasetCount>0 || bSmoke || bNightVisionSmoke;
    if (Now>=NextCapture) {
        if (bOfflineCapture) CaptureSensor();
        else { Sensor->CaptureScene(); PreviewEpoch=Epoch; PreviewCapturedAt=Now; ++GPUPreviewCaptures; }
        NextCapture=Now+(bOfflineCapture ? .5:1.0/PreviewHz);
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
    if (PendingRequest.IsValid()) { PendingRequest->OnProcessRequestComplete().Unbind(); PendingRequest->CancelRequest(); PendingRequest.Reset(); }
    SensorReadback.Cancel();
    ++Epoch; // Detached encoding jobs own only pixel arrays and scalars.
    UE_LOG(LogTemp,Display,TEXT("DF_CAPTURE_STATS gpu_preview=%d async_readbacks=%d sync_readbacks=%d stale_discarded=%d cpu_preview_uploads=0"),GPUPreviewCaptures,AsyncReadbacks,SyncReadbacks,StaleReadbacksDiscarded);
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



