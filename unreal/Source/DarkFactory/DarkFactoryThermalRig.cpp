#include "DarkFactoryThermalRig.h"
#include "DarkFactoryWorld.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRadiometry.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraTypes.h"
#include "SceneView.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace {
constexpr int32 ThermalWidth=448, ThermalHeight=256, LUTWidth=256, LUTRows=22;
constexpr int32 WeldHeatPatchCount=4, WeldHeatFirstRow=14;
TArray<uint8> CompressThermal(TArray<FColor> Pixels) {
    // SceneCapture alpha is not radiometric data. Publish an opaque RGB image.
    for (FColor& Pixel:Pixels) Pixel.A=255;
    TArray64<uint8> Encoded;
    FImageUtils::PNGCompressImageArray(ThermalWidth,ThermalHeight,Pixels,Encoded);
    TArray<uint8> Result; Result.Append(Encoded.GetData(),Encoded.Num()); return Result;
}
}
ADarkFactoryThermalRig::ADarkFactoryThermalRig() {
    PrimaryActorTick.bCanEverTick=true;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); SetRootComponent(SceneRoot);
    ThermalSensor=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("FixedThermalCamera"));
    ThermalSensor->SetupAttachment(SceneRoot);
    ThermalSensor->bCaptureEveryFrame=false;
    ThermalSensor->bCaptureOnMovement=false;
    // Manual captures need a persistent View.State for Unreal material blendables.
    ThermalSensor->bAlwaysPersistRenderingState=true;
    ThermalSensor->CaptureSource=SCS_FinalColorLDR;
    ThermalSensor->ViewLightingChannels.bViewChannel0=true;
    ThermalSensor->ViewLightingChannels.bViewChannel1=false;
    auto& PP=ThermalSensor->PostProcessSettings;
    PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_VignetteIntensity=true; PP.VignetteIntensity=0;
    PP.bOverride_MotionBlurAmount=true; PP.MotionBlurAmount=0;
    ThermalSensor->ShowFlags.SetLighting(false);
    ThermalSensor->ShowFlags.SetDynamicShadows(false);
    ThermalSensor->ShowFlags.SetFog(false);
    ThermalSensor->ShowFlags.SetAtmosphere(false);
    ThermalSensor->ShowFlags.SetBloom(false);
    ThermalSensor->ShowFlags.SetAmbientOcclusion(false);
    ThermalSensor->ShowFlags.SetAntiAliasing(false);
}
bool ADarkFactoryThermalRig::InitializeCaptureResources(ADarkFactoryWorld* Cell, USceneCaptureComponent2D* NIRCamera) {
    if (!Cell || !NIRCamera || ThermalTarget || !Surfaces.IsEmpty()) {
        UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_INITIALIZE_FAILED invalid owner/camera or already initialized"));
        return false;
    }
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PP_Thermal.M_PP_Thermal"));
    if (!Parent) {
        UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_INITIALIZE_FAILED missing thermal material"));
        return false;
    }
    CellOwner=Cell;
    AddTickPrerequisiteActor(Cell); // Capture after the cell has applied this frame's motion.
    GConfig->GetFloat(TEXT("DarkFactory.Thermal"),TEXT("CaptureHz"),CaptureHz,GGameIni);
    FParse::Value(FCommandLine::Get(),TEXT("DFThermalHz="),CaptureHz);
    CaptureHz=FMath::IsFinite(CaptureHz) ? FMath::Clamp(CaptureHz,1.f,30.f):10.f;
    ThermalSensor->SetWorldTransform(NIRCamera->GetComponentTransform());
    ThermalSensor->FOVAngle=NIRCamera->FOVAngle;
    ThermalTarget=NewObject<UTextureRenderTarget2D>(this);
    ThermalTarget->InitCustomFormat(ThermalWidth,ThermalHeight,PF_B8G8R8A8,true);
    ThermalTarget->TargetGamma=1.f;
    ThermalTarget->ClearColor=FLinearColor::Black;
    ThermalTarget->UpdateResourceImmediate(true);
    ThermalSensor->TextureTarget=ThermalTarget;
    ThermalLUT=UTexture2D::CreateTransient(LUTWidth,LUTRows,PF_A32B32G32R32F);
    if (!ThermalLUT) {
        ThermalSensor->TextureTarget=nullptr; ThermalTarget=nullptr;
        UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_INITIALIZE_FAILED could not allocate LUT"));
        return false;
    }
    ThermalLUT->SRGB=false; ThermalLUT->Filter=TF_Nearest;
    ThermalLUT->AddressX=TA_Clamp; ThermalLUT->AddressY=TA_Clamp;
    ThermalLUT->NeverStream=true;
    ThermalLUT->UpdateResource();
    LUTEntries.Init(FLinearColor::Transparent,LUTWidth*LUTRows);
    ThermalMaterial=UMaterialInstanceDynamic::Create(Parent,this);
    if (!ThermalMaterial) return false;
    ThermalSensor->AddOrUpdateBlendable(ThermalMaterial.Get(),1.f);
    SnapshotRoot=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Thermal")));
    return true;
}
void ADarkFactoryThermalRig::Initialize(ADarkFactoryWorld* Cell, USceneCaptureComponent2D* NIRCamera) {
    if (!InitializeCaptureResources(Cell,NIRCamera)) return;
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Cell);
    for (auto* Mesh : Meshes) {
        const FString Name=Mesh->GetName();
        float Temp=27, Emissivity=.7f;
        if (Name.Contains(TEXT("Floor"))) { Temp=22; Emissivity=.95f; }
        else if (Name.Contains(TEXT("Wall")) || Name.Contains(TEXT("Column")) || Name.Contains(TEXT("Beam"))) { Temp=24; Emissivity=.9f; }
        else if (Name.Contains(TEXT("BatteryModule")) || Name.Contains(TEXT("ModuleRib"))) { Temp=45; Emissivity=.92f; }
        else if (Name.Contains(TEXT("Robot"))) { Temp=Name.Contains(TEXT("Pedestal")) ? 38:48; Emissivity=.85f; }
        if (Name==TEXT("BatteryModule1")) TargetModule=Mesh;
        RegisterSurface(Mesh,Temp,Emissivity);
        if (Name.Contains(TEXT("Mast")) || Name.Contains(TEXT("CameraHousing"))) ThermalSensor->HideComponent(Mesh);
    }
    Fixture(TEXT("ThermalWindowPane"),FVector(-190,120,150),FVector(110,8,90),30,.94f);
    for (int32 I=0;I<3;++I) {
        auto* Strip=Fixture(*FString::Printf(TEXT("WindowHeater%d"),I),FVector(-225+35*I,114,150),FVector(20,5,66),65,.94f);
        if (I==1) HeaterFaultStrip=Strip;
    }
    Fixture(TEXT("GlueInspectionPlate"),FVector(210,-5,100),FVector(100,105,7),28,.8f);
    for (int32 I=0;I<3;++I) {
        auto* Bead=Fixture(*FString::Printf(TEXT("WarmGlue%d"),I),FVector(180+30*I,-43,106),FVector(25,13,6),58,.96f);
        if (I==1) GlueGapSegment=Bead;
        if (I==0) GlueIntactSegment=Bead;
    }
    // Not registered in CustomDepth: this deliberately tests SceneDepth rejection.
    DepthTestCover=Fixture(TEXT("ThermalDepthTestCover"),FVector(-15,-5,164),FVector(275,205,12),24,.9f,false);
    DepthTestCover->SetVisibility(false,true);
    TransferSource=Fixture(TEXT("HeatTransferSource"),FVector(0,0,350),FVector(60,4,60),110,.94f);
    TransferReceiver=Fixture(TEXT("HeatTransferReceiver"),FVector(0,54,350),FVector(60,4,60),24,.94f);
    TransferBlocker=Fixture(TEXT("HeatTransferBlocker"),FVector(0,27,350),FVector(100,2,100),24,.94f);
    for (auto* Mesh : {TransferSource.Get(),TransferReceiver.Get(),TransferBlocker.Get()}) Mesh->SetVisibility(false,true);
    SetCase(0);
    UE_LOG(LogTemp,Display,TEXT("DF_THERMAL_READY profiles=%d radiometry_selftest=%d fixed_sensor=448x256 linear_u8 band_um=8-14 capture_hz=%.1f"),Surfaces.Num(),DarkFactoryThermal::SelfTest(),CaptureHz);
}
bool ADarkFactoryThermalRig::InitializeImported(ADarkFactoryWorld* Cell,USceneCaptureComponent2D* NIRCamera,
    const TArray<FDarkFactoryImportedThermalSurface>& Profiles) {
    // Material-group bounds may contain disconnected bodies: no mass, contact or cooling inference.
    if (!Cell || !NIRCamera || Profiles.IsEmpty() || Profiles.Num()>255 || ThermalTarget || !Surfaces.IsEmpty()) {
        UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_IMPORTED_FAILED require fresh rig, valid camera and 1..255 profiles (received %d)"),Profiles.Num());
        return false;
    }
    if (NIRCamera->GetComponentTransform().ContainsNaN() || !FMath::IsFinite(NIRCamera->FOVAngle) ||
        NIRCamera->FOVAngle<=1.f || NIRCamera->FOVAngle>=179.f) {
        UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_IMPORTED_FAILED invalid camera pose or field of view"));
        return false;
    }
    TSet<UStaticMeshComponent*> Unique;
    for (int32 Index=0;Index<Profiles.Num();++Index) {
        const auto& P=Profiles[Index];
        if (!IsValid(P.Mesh) || !P.Mesh->GetStaticMesh() || Unique.Contains(P.Mesh) ||
            P.Mesh->GetComponentTransform().ContainsNaN() || P.Mesh->GetComponentScale().GetAbs().GetMin()<=UE_SMALL_NUMBER ||
            !P.Mesh->GetStaticMesh()->GetBoundingBox().IsValid || P.Mesh->GetStaticMesh()->GetBoundingBox().Min.ContainsNaN() ||
            P.Mesh->GetStaticMesh()->GetBoundingBox().Max.ContainsNaN() ||
            !FMath::IsFinite(P.TemperatureC) || P.TemperatureC < -100.f || P.TemperatureC > 500.f ||
            !FMath::IsFinite(P.Emissivity) || P.Emissivity<0.f || P.Emissivity>1.f ||
            !FMath::IsFinite(P.CoreEdgeDropC) || P.CoreEdgeDropC<0.f || P.TemperatureC-P.CoreEdgeDropC < -100.f) {
            UE_LOG(LogTemp,Error,TEXT("DF_THERMAL_IMPORTED_FAILED invalid or duplicate profile index=%d"),Index);
            return false; // No mesh stencil state has been modified yet.
        }
        Unique.Add(P.Mesh);
    }
    if (!InitializeCaptureResources(Cell,NIRCamera)) return false;
    bImportedProfiles=true;
    bHeatTransferEnabled=false;
    AmbientC=20.f;
    CoolingAgeSeconds=0;
    for (const auto& P:Profiles) {
        FDarkFactoryThermalSurface Entry;
        Entry.Mesh=P.Mesh;
        Entry.BaseC=Entry.CurrentC=P.TemperatureC;
        Entry.Emissivity=P.Emissivity;
        Entry.AuthoredCoreEdgeDropC=P.CoreEdgeDropC;
        Entry.LocalBounds=P.Mesh->GetStaticMesh()->GetBoundingBox();
        const int32 Id=Surfaces.Add(Entry)+1;
        P.Mesh->SetRenderCustomDepth(true); P.Mesh->SetCustomDepthStencilValue(Id);
    }
    UpdateLUT();
    UE_LOG(LogTemp,Display,TEXT("DF_THERMAL_IMPORTED_READY profiles=%d source=authored_illustrative exchange=0 cooling=0 fixed_sensor=448x256"),Surfaces.Num());
    return true;
}
bool ADarkFactoryThermalRig::SetImportedToolHeat(UStaticMeshComponent* Mesh,float BodyC,float TipC,
    FVector TipLocalCm,float RadiusCm) {
    if (!IsInGameThread() || !bImportedProfiles || !IsReady() || !IsValid(Mesh) ||
        !FMath::IsFinite(BodyC) || !FMath::IsFinite(TipC) || BodyC < -100.f || BodyC > 500.f ||
        TipC < BodyC || TipC > 500.f || TipLocalCm.ContainsNaN() ||
        !FMath::IsFinite(RadiusCm) || RadiusCm<=0.f || Mesh->GetComponentTransform().ContainsNaN()) return false;
    const float InverseRadiusSquared=static_cast<float>(1.0/(double(RadiusCm)*RadiusCm));
    const FVector World=Mesh->GetComponentTransform().TransformPosition(TipLocalCm);
    if (!FMath::IsFinite(InverseRadiusSquared) || InverseRadiusSquared<=0.f ||
        !FMath::IsFinite(static_cast<float>(World.X)) || !FMath::IsFinite(static_cast<float>(World.Y)) ||
        !FMath::IsFinite(static_cast<float>(World.Z))) return false;
    const int32 Index=Surfaces.IndexOfByPredicate([Mesh](const FDarkFactoryThermalSurface& S) { return S.Mesh.Get()==Mesh; });
    if (Index==INDEX_NONE) return false;
    auto& S=Surfaces[Index];
    if (BodyC-S.AuthoredCoreEdgeDropC < -100.f) return false;
    if (S.CurrentC==BodyC && S.TipSurfaceC==TipC && S.HotspotLocalCm==TipLocalCm && S.HotspotRadiusCm==RadiusCm) return true;
    S.CurrentC=BodyC; // BaseC remains the authored source value.
    S.TipSurfaceC=TipC; S.HotspotLocalCm=TipLocalCm; S.HotspotRadiusCm=RadiusCm;
    UpdateProfile(Index); // Keep capture metadata and radiometry in the same state immediately.
    bLUTDirty=true; // One upload with transform changes before the next render/capture.
    return true; // Continuous cooling does not invalidate a pending capture-time snapshot.
}
bool ADarkFactoryThermalRig::SetImportedWeldHeat(UStaticMeshComponent* Mesh,
    const TArray<FDarkFactoryWeldHeatPatch>& Patches) {
    if (!IsInGameThread() || !bImportedProfiles || !IsReady() || !IsValid(Mesh) ||
        Patches.Num()>WeldHeatPatchCount || !FMath::IsFinite(AmbientC) ||
        Mesh->GetComponentTransform().ContainsNaN()) return false;
    const int32 Index=Surfaces.IndexOfByPredicate([Mesh](const FDarkFactoryThermalSurface& S) { return S.Mesh.Get()==Mesh; });
    if (Index==INDEX_NONE) return false;
    const FTransform Transform=Mesh->GetComponentTransform();
    for (const auto& Patch:Patches) {
        if (Patch.LocalPositionCm.ContainsNaN() || !FMath::IsFinite(Patch.RadiusCm) || Patch.RadiusCm<=0.f ||
            !FMath::IsFinite(Patch.SurfaceC) || Patch.SurfaceC<AmbientC || Patch.SurfaceC>500.f) return false;
        const float InverseRadiusSquared=static_cast<float>(1.0/(double(Patch.RadiusCm)*Patch.RadiusCm));
        const FVector World=Transform.TransformPosition(Patch.LocalPositionCm);
        if (!FMath::IsFinite(InverseRadiusSquared) || InverseRadiusSquared<=0.f ||
            !FMath::IsFinite(static_cast<float>(World.X)) || !FMath::IsFinite(static_cast<float>(World.Y)) ||
            !FMath::IsFinite(static_cast<float>(World.Z))) return false;
    }
    auto& S=Surfaces[Index];
    bool Unchanged=S.WeldHeatPatches.Num()==Patches.Num();
    for (int32 I=0;Unchanged && I<Patches.Num();++I) {
        const auto& A=S.WeldHeatPatches[I]; const auto& B=Patches[I];
        Unchanged=A.LocalPositionCm==B.LocalPositionCm && A.RadiusCm==B.RadiusCm && A.SurfaceC==B.SurfaceC;
    }
    if (Unchanged) return true;
    S.WeldHeatPatches=Patches; // Validate all before replacing any existing heat state.
    UpdateProfile(Index);
    bLUTDirty=true; // Shared upload with moving tool/car transforms; no capture invalidation.
    return true;
}
bool ADarkFactoryThermalRig::SetImportedCastingTemperature(UStaticMeshComponent* Mesh,float TemperatureC) {
    if (!IsInGameThread() || !bImportedProfiles || !IsReady() || !IsValid(Mesh) ||
        !FMath::IsFinite(TemperatureC) || TemperatureC<AmbientC || TemperatureC>100.f) return false;
    const int32 Index=Surfaces.IndexOfByPredicate([Mesh](const FDarkFactoryThermalSurface& S){return S.Mesh.Get()==Mesh;});
    if (Index==INDEX_NONE || Surfaces[Index].AuthoredCoreEdgeDropC<=0) return false;
    auto& S=Surfaces[Index];
    if (S.CurrentC==TemperatureC && S.bMotorRadiometryCached) return true;
    S.CurrentC=TemperatureC; S.bMotorRadiometryCached=true; // Keep authored BaseC and local gradient.
    UpdateProfile(Index); bLUTDirty=true; // Shared upload; no scene epoch/capture reset.
    return true;
}
bool ADarkFactoryThermalRig::SetImportedSurfaceTemperature(UStaticMeshComponent* Mesh,float TemperatureC) {
    if (!IsInGameThread() || !bImportedProfiles || !IsReady() || !IsValid(Mesh) ||
        !FMath::IsFinite(TemperatureC) || TemperatureC<AmbientC || TemperatureC>500.f) return false;
    const int32 Index=Surfaces.IndexOfByPredicate([Mesh](const FDarkFactoryThermalSurface& S){return S.Mesh.Get()==Mesh;});
    if (Index==INDEX_NONE) return false;
    auto& S=Surfaces[Index];
    if (S.CurrentC==TemperatureC) return true;
    S.CurrentC=TemperatureC;
    UpdateProfile(Index); bLUTDirty=true;
    return true;
}
void ADarkFactoryThermalRig::PrepareInspectionMaterial(UMaterialInstanceDynamic* Material) {
    if (!IsReady() || !IsInGameThread()) return;
    UpdateTransforms();
    ApplyMaterial(Material,0); // Linear grayscale temperature; observer palette is independent.
}
float ADarkFactoryThermalRig::ProfileApparentTemperature(const FDarkFactoryThermalSurface& Surface,float TemperatureC) {
    if (!Surface.bMotorRadiometryCached || TemperatureC<20.f || TemperatureC>100.f)
        return DarkFactoryThermal::ApparentTemperatureC(TemperatureC,Surface.Emissivity,AmbientC);
    int32 Index=MotorRadiometryTables.IndexOfByPredicate([&](const FMotorRadiometryTable& T){return T.Emissivity==Surface.Emissivity && T.AmbientC==AmbientC;});
    if (Index==INDEX_NONE) {
        // Bounded cache; a changed emissivity/ambient pair always gets a fresh table.
        if (MotorRadiometryTables.Num()>=32) MotorRadiometryTables.Reset();
        FMotorRadiometryTable Table; Table.Emissivity=Surface.Emissivity; Table.AmbientC=AmbientC;
        Table.Samples.SetNumUninitialized(81);
        for (int32 I=0;I<=80;++I) Table.Samples[I]=DarkFactoryThermal::ApparentTemperatureC(20.+I,Surface.Emissivity,AmbientC);
        for (int32 I=0;I<80;++I) {
            const float Exact=DarkFactoryThermal::ApparentTemperatureC(20.5+I,Surface.Emissivity,AmbientC);
            Table.MaximumMidpointErrorC=FMath::Max(Table.MaximumMidpointErrorC,FMath::Abs(Exact-.5f*(Table.Samples[I]+Table.Samples[I+1])));
        }
        Table.bValidated=FMath::IsFinite(Table.MaximumMidpointErrorC) && Table.MaximumMidpointErrorC<.005f;
        Index=MotorRadiometryTables.Add(MoveTemp(Table));
    }
    const auto& Table=MotorRadiometryTables[Index];
    if (!Table.bValidated) return DarkFactoryThermal::ApparentTemperatureC(TemperatureC,Surface.Emissivity,AmbientC);
    const int32 Lower=FMath::Clamp(FMath::FloorToInt(TemperatureC-20.f),0,79);
    return FMath::Lerp(Table.Samples[Lower],Table.Samples[Lower+1],TemperatureC-20.f-Lower);
}
TSharedRef<FJsonObject> ADarkFactoryThermalRig::MotorRadiometryMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("maximum_tables"),32); Result->SetNumberField(TEXT("table_count"),MotorRadiometryTables.Num());
    Result->SetNumberField(TEXT("minimum_temperature_c"),20); Result->SetNumberField(TEXT("maximum_temperature_c"),100);
    Result->SetNumberField(TEXT("step_c"),1); Result->SetStringField(TEXT("scope"),TEXT("Dynamic imported castings only; linear apparent-temperature interpolation, native Planck midpoint validation once per emissivity/ambient key; exact fallback elsewhere"));
    bool Valid=true; double MaxError=0; TArray<TSharedPtr<FJsonValue>> Tables;
    for (const auto& Table:MotorRadiometryTables) {
        auto T=MakeShared<FJsonObject>(); T->SetNumberField(TEXT("emissivity"),Table.Emissivity); T->SetNumberField(TEXT("ambient_temperature_c"),Table.AmbientC);
        T->SetNumberField(TEXT("sample_count"),Table.Samples.Num()); T->SetNumberField(TEXT("maximum_midpoint_error_c"),Table.MaximumMidpointErrorC); T->SetBoolField(TEXT("validated"),Table.bValidated);
        Valid &= Table.bValidated; MaxError=FMath::Max(MaxError,double(Table.MaximumMidpointErrorC)); Tables.Add(MakeShared<FJsonValueObject>(T));
    }
    Result->SetBoolField(TEXT("validated"),Valid); Result->SetNumberField(TEXT("maximum_midpoint_error_c"),MaxError); Result->SetArrayField(TEXT("tables"),Tables);
    return Result;
}
UStaticMeshComponent* ADarkFactoryThermalRig::Fixture(const TCHAR* Name,FVector Position,FVector Size,float TempC,float Emissivity,bool Registered) {
    auto* Mesh=NewObject<UStaticMeshComponent>(this,Name);
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetRelativeLocation(Position); Mesh->SetRelativeScale3D(Size/100.f);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"))) {
        auto* Mat=UMaterialInstanceDynamic::Create(Parent,this);
        Mat->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.28f,.28f,.28f));
        Mat->SetScalarParameterValue(TEXT("Roughness"),.75f);
        Mesh->SetMaterial(0,Mat);
    }
    Mesh->RegisterComponent();
    if (Registered) RegisterSurface(Mesh,TempC,Emissivity);
    return Mesh;
}
int32 ADarkFactoryThermalRig::RegisterSurface(UStaticMeshComponent* Mesh,float TemperatureC,float Emissivity) {
    if (bImportedProfiles || !Mesh || !Mesh->GetStaticMesh() || Surfaces.Num()>=255) return 0;
    for (int32 I=0;I<Surfaces.Num();++I) if (Surfaces[I].Mesh==Mesh) return I+1;
    FDarkFactoryThermalSurface Entry; Entry.Mesh=Mesh;
    Entry.BaseC=Entry.CurrentC=FMath::IsFinite(TemperatureC) ? FMath::Clamp(TemperatureC,-100.f,500.f):24.f;
    Entry.Emissivity=FMath::IsFinite(Emissivity) ? FMath::Clamp(Emissivity,0.f,1.f):1.f;
    const FString Name=Mesh->GetName();
    Entry.bPowered=Name.StartsWith(TEXT("WindowHeater")) || Name==TEXT("HeatTransferSource") || Name==TEXT("HeatTransferBlocker");
    if (Entry.bPowered) { Entry.EdgeFraction=.55f; Entry.EdgeFalloff=1.1f; }
    else if (Name.StartsWith(TEXT("WarmGlue"))) { Entry.EdgeFraction=.32f; Entry.EdgeFalloff=1.8f; }
    Entry.LocalBounds=Mesh->GetStaticMesh()->GetBoundingBox();
    const int32 Id=Surfaces.Add(Entry)+1;
    Mesh->SetRenderCustomDepth(true); Mesh->SetCustomDepthStencilValue(Id);
    return Id;
}
void ADarkFactoryThermalRig::UpdateProfile(int32 Index) {
    auto& S=Surfaces[Index];
    const auto* Mesh=S.Mesh.Get();
    if (!Mesh) return;
    const bool Exchange=!bImportedProfiles && bHeatTransferEnabled && HeatExchange.IsReady(Surfaces.Num());
    if (bImportedProfiles) {
        // Explicit surface values may be updated by the authored tool cooling state.
        // Optional fields are visual approximations, not a body-volume heat simulation.
        S.CharacteristicLengthCm=0;
        S.CoolingRetention=1;
        S.CoreSurfaceC=S.CurrentC;
        // Clamp warm edges to ambient without warming a future below-ambient part.
        S.EdgeSurfaceC=FMath::Min(S.CurrentC,FMath::Max(AmbientC,S.CurrentC-S.AuthoredCoreEdgeDropC));
        const FVector Extent=S.LocalBounds.GetExtent();
        S.ImportedCoreFieldAxis=Extent.X>=Extent.Y && Extent.X>=Extent.Z ? 0:Extent.Y>=Extent.Z ? 1:2;
    } else {
        const FVector Size=S.LocalBounds.GetSize()*Mesh->GetComponentScale().GetAbs();
        const double Area=2*(Size.X*Size.Y+Size.X*Size.Z+Size.Y*Size.Z);
        S.CharacteristicLengthCm=FMath::Max(.01,Size.X*Size.Y*Size.Z/FMath::Max(Area,.001));
        // Legacy isolated cooling remains available as an explicit comparison.
        S.CoolingRetention=S.bPowered ? 1.f:FMath::Exp(-CoolingAgeSeconds/(30.f*S.CharacteristicLengthCm));
        S.CoreSurfaceC=Exchange ? HeatExchange.BulkC(Index):AmbientC+(S.CurrentC-AmbientC)*S.CoolingRetention;
        if (Exchange) S.CoolingRetention=FMath::Abs(S.CurrentC-AmbientC)>.001f ? (S.CoreSurfaceC-AmbientC)/(S.CurrentC-AmbientC):1.f;
        S.EdgeSurfaceC=AmbientC+(S.CoreSurfaceC-AmbientC)*S.EdgeFraction;
    }
    S.ApparentC=ProfileApparentTemperature(S,S.CurrentC);
    float MinDelta=0,MaxDelta=0;
    for (int Face=0;Face<6;++Face) {
        S.FaceDeltaC[Face]=Exchange ? HeatExchange.FaceC(Index,Face)-S.CoreSurfaceC:0;
        MinDelta=FMath::Min(MinDelta,S.FaceDeltaC[Face]); MaxDelta=FMath::Max(MaxDelta,S.FaceDeltaC[Face]);
        LUTEntries[(5+Face)*LUTWidth+Index+1]=FLinearColor(0,0,.65f,S.FaceDeltaC[Face]);
    }
    const float FitMin=FMath::Min(S.EdgeSurfaceC,S.CoreSurfaceC+MinDelta);
    float FitMax=FMath::Max(S.EdgeSurfaceC,S.CoreSurfaceC+MaxDelta);
    if (S.HotspotRadiusCm>0.f) FitMax=FMath::Max(FitMax,S.TipSurfaceC);
    for (const auto& Patch:S.WeldHeatPatches) FitMax=FMath::Max(FitMax,Patch.SurfaceC);
    const float Low=ProfileApparentTemperature(S,FitMin);
    const float Mid=ProfileApparentTemperature(S,(FitMin+FitMax)*.5f);
    const float High=ProfileApparentTemperature(S,FitMax);
    LUTEntries[Index+1]=FLinearColor(Low,Mid,High,1);
    LUTEntries[4*LUTWidth+Index+1]=FLinearColor(S.EdgeFalloff,S.ApparentC,S.CoolingRetention,S.EdgeFraction);
    LUTEntries[11*LUTWidth+Index+1]=FLinearColor(S.EdgeSurfaceC,S.CoreSurfaceC,FitMin,FitMax-FitMin>.00001f ? 1.f/(FitMax-FitMin):0.f);
    const bool ImportedCoreField=bImportedProfiles && S.AuthoredCoreEdgeDropC>0.f;
    // Row13 shares the existing tool-tip row; no texture dimensions/pass changes.
    LUTEntries[13*LUTWidth+Index+1]=FLinearColor(S.HotspotRadiusCm>0.f ? S.TipSurfaceC:0.f,
        ImportedCoreField ? 1.f:0.f,ImportedCoreField ? float(S.ImportedCoreFieldAxis):0.f,
        ImportedCoreField ? S.ImportedCoreFieldOffset:0.f);
    UpdateHotspotTransform(Index);
    UpdateWeldHeatTransforms(Index);
}
void ADarkFactoryThermalRig::UpdateHotspotTransform(int32 Index) {
    const auto& S=Surfaces[Index];
    auto& Entry=LUTEntries[12*LUTWidth+Index+1];
    if (S.HotspotRadiusCm<=0.f || !S.Mesh.IsValid()) { Entry=FLinearColor::Transparent; return; }
    const FVector World=S.Mesh->GetComponentTransform().TransformPosition(S.HotspotLocalCm);
    Entry=FLinearColor(World.X,World.Y,World.Z,static_cast<float>(1.0/(double(S.HotspotRadiusCm)*S.HotspotRadiusCm)));
}
void ADarkFactoryThermalRig::UpdateWeldHeatTransforms(int32 Index) {
    const auto& S=Surfaces[Index];
    const auto* Mesh=S.Mesh.Get();
    for (int32 I=0;I<WeldHeatPatchCount;++I) {
        auto& Position=LUTEntries[(WeldHeatFirstRow+2*I)*LUTWidth+Index+1];
        auto& Temperature=LUTEntries[(WeldHeatFirstRow+2*I+1)*LUTWidth+Index+1];
        Position=Temperature=FLinearColor::Transparent; // Clearing/reducing patches erases old rows.
        if (!Mesh || !S.WeldHeatPatches.IsValidIndex(I)) continue;
        const auto& Patch=S.WeldHeatPatches[I];
        const FVector World=Mesh->GetComponentTransform().TransformPosition(Patch.LocalPositionCm);
        Position=FLinearColor(World.X,World.Y,World.Z,static_cast<float>(1.0/(double(Patch.RadiusCm)*Patch.RadiusCm)));
        Temperature=FLinearColor(Patch.SurfaceC,0,0,0);
    }
}
void ADarkFactoryThermalRig::UploadLUT() {
    if (!ThermalLUT) return;
    const uint32 Bytes=LUTEntries.Num()*sizeof(FLinearColor);
    auto* Data=static_cast<uint8*>(FMemory::Malloc(Bytes));
    FMemory::Memcpy(Data,LUTEntries.GetData(),Bytes);
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,LUTWidth,LUTRows);
    ThermalLUT->UpdateTextureRegions(0,1,Region,LUTWidth*sizeof(FLinearColor),sizeof(FLinearColor),Data,
        [](uint8* Source,const FUpdateTextureRegion2D* Rectangle) { FMemory::Free(Source); delete Rectangle; });
    ++LUTUploads; // 88 KiB upload, no texture recreation and no image readback.
}
void ADarkFactoryThermalRig::UpdateTransforms(bool Force) {
    // Resizing is an authored geometry reset: capacities must match the new volume.
    // Translation/rotation keep stored energy and only change future exchange links.
    if (bHeatTransferEnabled && HeatExchange.IsReady(Surfaces.Num())) {
        bool Resized=false;
        for (const auto& S:Surfaces) if (const auto* Mesh=S.Mesh.Get())
            Resized |= S.bTransformCached && !Mesh->GetComponentScale().Equals(S.LastTransform.GetScale3D());
        if (Resized) {
            ResetExchange();
            for (int32 Index=0;Index<Surfaces.Num();++Index) UpdateProfile(Index);
            Force=true; ++Revision; NextCapture=0;
        }
    }
    bool Changed=Force || bLUTDirty;
    for (int32 Index=0;Index<Surfaces.Num();++Index) {
        auto& S=Surfaces[Index]; const auto* Mesh=S.Mesh.Get();
        if (!Mesh) continue;
        const FTransform Transform=Mesh->GetComponentTransform();
        if (!Force && S.bTransformCached && Transform.Equals(S.LastTransform)) continue;
        if (!S.bTransformCached || !Transform.GetScale3D().Equals(S.LastTransform.GetScale3D())) UpdateProfile(Index);
        const FMatrix Inverse=Transform.ToInverseMatrixWithScale();
        const FVector Center=S.LocalBounds.GetCenter(),Extent=S.LocalBounds.GetExtent().ComponentMax(FVector(.001));
        for (int32 Axis=0;Axis<3;++Axis)
            LUTEntries[(Axis+1)*LUTWidth+Index+1]=FLinearColor(Inverse.M[0][Axis]/Extent[Axis],Inverse.M[1][Axis]/Extent[Axis],Inverse.M[2][Axis]/Extent[Axis],(Inverse.M[3][Axis]-Center[Axis])/Extent[Axis]);
        UpdateHotspotTransform(Index);
        UpdateWeldHeatTransforms(Index);
        S.LastTransform=Transform; S.bTransformCached=true; Changed=true;
    }
    if (Changed) { UploadLUT(); bLUTDirty=false; }
}
void ADarkFactoryThermalRig::UpdateLUT() {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_ThermalProfileUpdate);
    for (int32 Index=0;Index<Surfaces.Num();++Index) UpdateProfile(Index);
    UpdateTransforms(true);
    ++Revision;
    ApplyMaterial(ThermalMaterial,0);
    NextCapture=0;
}
void ADarkFactoryThermalRig::SetSpatialFieldsEnabled(bool Enabled) {
    if (bSpatialFields==Enabled) return;
    bSpatialFields=Enabled; NotifySceneChanged(); ApplyMaterial(ThermalMaterial,0);
}
void ADarkFactoryThermalRig::ResetExchange() {
    if (bImportedProfiles) return;
    TArray<FDarkFactoryHeatInput> Inputs;
    for (const auto& S:Surfaces) Inputs.Add({S.Mesh,S.CurrentC,S.Emissivity,S.bPowered});
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Blockers;
    if (DepthTestCover) Blockers.Add(DepthTestCover.Get());
    HeatExchange.Reset(Inputs,Blockers,AmbientC);
    if (CoolingAgeSeconds>0 && !HeatExchange.Advance(CoolingAgeSeconds))
        UE_LOG(LogTemp,Error,TEXT("DF_HEAT_SOLVER_FAILED reset age=%.1f"),CoolingAgeSeconds);
}
void ADarkFactoryThermalRig::SetHeatTransferEnabled(bool Enabled) {
    if (bImportedProfiles) return;
    if (bHeatTransferEnabled==Enabled) return;
    bHeatTransferEnabled=Enabled;
    if (Enabled) ResetExchange();
    UpdateLUT();
}
void ADarkFactoryThermalRig::SetCoolingAge(float Seconds) {
    if (bImportedProfiles) return;
    const float Requested=FMath::IsFinite(Seconds) ? FMath::Clamp(Seconds,0.f,1800.f):15.f;
    const float Previous=CoolingAgeSeconds;
    if (bHeatTransferEnabled) {
        if (Requested<Previous || !HeatExchange.IsReady(Surfaces.Num())) {
            CoolingAgeSeconds=Requested; ResetExchange();
        } else if (!HeatExchange.Advance(Requested-Previous)) {
            UE_LOG(LogTemp,Error,TEXT("DF_HEAT_SOLVER_FAILED advance=%.1f"),Requested-Previous);
            return;
        }
    }
    CoolingAgeSeconds=Requested;
    UpdateLUT();
}
void ADarkFactoryThermalRig::SetCase(int32 Case) {
    if (bImportedProfiles) return;
    CaseIndex=FMath::Clamp(Case,0,5);
    CoolingAgeSeconds=15;
    if (DepthTestCover) DepthTestCover->SetVisibility(CaseIndex==4,true);
    if (GlueGapSegment) GlueGapSegment->SetVisibility(CaseIndex!=3,true);
    if (TransferSource) TransferSource->SetVisibility(CaseIndex==5,true);
    if (TransferReceiver) TransferReceiver->SetVisibility(CaseIndex==5,true);
    if (TransferBlocker) TransferBlocker->SetVisibility(false,true);
    for (auto& Surface : Surfaces) {
        Surface.CurrentC=Surface.BaseC;
        if (!Surface.Mesh.IsValid()) continue;
        const FString Name=Surface.Mesh->GetName();
        Surface.bPowered=Name.StartsWith(TEXT("WindowHeater")) || Name==TEXT("HeatTransferSource") || Name==TEXT("HeatTransferBlocker");
        if ((CaseIndex==1 || CaseIndex==4) && (Name==TEXT("BatteryModule1") || Name.StartsWith(TEXT("ModuleRib1_")))) Surface.CurrentC=95;
        if (CaseIndex==2 && Surface.Mesh==HeaterFaultStrip) { Surface.CurrentC=AmbientC; Surface.bPowered=false; }
    }
    if (bHeatTransferEnabled) ResetExchange();
    if (ThermalLUT) UpdateLUT();
}
FString ADarkFactoryThermalRig::GetCaseName() const {
    if (bImportedProfiles) return TEXT("authored welding-cell temperatures");
    static const TCHAR* Names[]={TEXT("normal"),TEXT("overheated module"),TEXT("heater fault"),TEXT("glue gap"),TEXT("opaque cover test"),TEXT("nearby heat transfer")};
    return Names[CaseIndex];
}
void ADarkFactoryThermalRig::ApplyMaterial(UMaterialInstanceDynamic* Material,int32 Palette) const {
    if (!Material || !ThermalLUT) return;
    Material->SetTextureParameterValue(TEXT("ThermalLUT"),ThermalLUT);
    Material->SetScalarParameterValue(TEXT("ThermalAmbientC"),AmbientC);
    Material->SetScalarParameterValue(TEXT("ThermalMinC"),MinC);
    Material->SetScalarParameterValue(TEXT("ThermalMaxC"),MaxC);
    Material->SetScalarParameterValue(TEXT("ThermalDepthToleranceCm"),.2f);
    Material->SetScalarParameterValue(TEXT("ThermalPalette"),Palette);
    Material->SetScalarParameterValue(TEXT("ThermalSpatialEnabled"),bSpatialFields ? 1.f:0.f);
    Material->SetScalarParameterValue(TEXT("ThermalTransferEnabled"),bHeatTransferEnabled ? 1.f:0.f);
}
void ADarkFactoryThermalRig::CaptureGPU() {
    if (!ThermalMaterial || !ThermalTarget) return;
    UpdateTransforms();
    ThermalSensor->CaptureScene(); ++GPUCaptures; CaptureRevision=Revision;
}
bool ADarkFactoryThermalRig::SaveRawForSmoke(const FString& Path) {
    if (!IsReady()) return false;
    CaptureGPU();
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    ++Readbacks;
    if (!ThermalTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return false;
    return FFileHelper::SaveArrayToFile(CompressThermal(MoveTemp(Pixels)),*Path);
}
bool ADarkFactoryThermalRig::SaveSurfaceIdsForQA(const FString& Path) {
    if (!IsReady()) return false;
    ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),1.f);
    const bool Saved=SaveRawForSmoke(Path);
    ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),0.f);
    CaptureGPU(); // Restore a normal temperature image before the target is displayed.
    return Saved;
}
void ADarkFactoryThermalRig::RequestSnapshot() { if (!IsReady()) { LastSnapshot=TEXT("Thermal renderer unavailable"); return; } bSnapshotRequested=true; LastSnapshot=TEXT("Capturing thermal frame..."); }
void ADarkFactoryThermalRig::NotifySceneChanged() {
    ++Revision;
    NextCapture=0;
    // Keep any worker bounded and let its revision check discard obsolete data.
    // GetTarget hides the obsolete preview until the new scene has been captured.
}
void ADarkFactoryThermalRig::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!ThermalMaterial) return;
    UpdateTransforms(); // Observer fields follow motion even between fixed-sensor captures.
    if (!bFrozen && FPlatformTime::Seconds()>=NextCapture) {
        CaptureGPU(); NextCapture=FPlatformTime::Seconds()+1.0/CaptureHz;
    }
    TArray<FColor> Pixels; uint64 Id=0;
    if (Readback.Poll(Pixels,Id)) {
        if (Id!=Revision || Id!=PendingRevision) {
            ++StaleSnapshotDiscards;
            PendingSnapshotMetadata.Reset();
            LastSnapshot=TEXT("Thermal state changed; capture again");
        } else if (Pixels.Num()!=ThermalWidth*ThermalHeight) {
            PendingSnapshotMetadata.Reset();
            LastSnapshot=TEXT("Thermal readback failed; capture again");
        } else {
            // Worker owns only pixels; metadata and all UObject reads stay on GT.
            EncodeFuture=Async(EAsyncExecution::ThreadPool,[Pixels=MoveTemp(Pixels)]() mutable { return CompressThermal(MoveTemp(Pixels)); });
        }
    }
    if (EncodeFuture.IsValid() && EncodeFuture.IsReady()) {
        auto Bytes=EncodeFuture.Get(); EncodeFuture=TFuture<TArray<uint8>>();
        if (PendingRevision==Revision) SaveEncodedSnapshot(MoveTemp(Bytes),MoveTemp(PendingSnapshotMetadata));
        else {
            ++StaleSnapshotDiscards;
            PendingSnapshotMetadata.Reset();
            LastSnapshot=TEXT("Thermal state changed; capture again");
        }
    }
    if (bSnapshotRequested && !Readback.IsBusy() && !EncodeFuture.IsValid()) {
        // A snapshot never pairs current metadata with the previous 10 Hz frame.
        // These operations run consecutively on GT, before later scene updates.
        auto Metadata=BuildSnapshotMetadata();
        CaptureGPU();
        if (CaptureRevision==Revision && Readback.Enqueue(ThermalTarget,Revision)) {
            ++Readbacks; PendingRevision=Revision; bSnapshotRequested=false;
            PendingSnapshotMetadata=MoveTemp(Metadata);
            NextCapture=FPlatformTime::Seconds()+1.0/CaptureHz;
        }
    }
}
void ADarkFactoryThermalRig::EndPlay(const EEndPlayReason::Type Reason) {
    Readback.Cancel();
    PendingSnapshotMetadata.Reset();
    UE_LOG(LogTemp,Display,TEXT("DF_THERMAL_STATS gpu_captures=%d readbacks=%d cpu_preview_uploads=0 thermal_inference_requests=0"),GPUCaptures,Readbacks);
    Super::EndPlay(Reason);
}
TSharedRef<FJsonObject> ADarkFactoryThermalRig::GetEncodingMetadata() const {
    auto Data=MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("width"),ThermalWidth); Data->SetNumberField(TEXT("height"),ThermalHeight);
    Data->SetNumberField(TEXT("min_temperature_c"),MinC); Data->SetNumberField(TEXT("max_temperature_c"),MaxC);
    Data->SetNumberField(TEXT("ambient_temperature_c"),AmbientC);
    Data->SetArrayField(TEXT("band_um"),{MakeShared<FJsonValueNumber>(8),MakeShared<FJsonValueNumber>(14)});
    Data->SetStringField(TEXT("quantity"),TEXT("apparent_temperature")); Data->SetBoolField(TEXT("linear_u8"),true);
    Data->SetStringField(TEXT("model"),bImportedProfiles
        ? TEXT("Authored illustrative component temperatures, optional tool cooling/hotspot state and up to four car weld patches per mesh; flat-response 8-14um Planck band with graybody emissivity and uniform reflected ambient; no material-group heat capacity or heat-exchange simulation")
        : TEXT("Flat-response 8-14um Planck band; graybody emissivity + reflected ambient; object-local surface field; bulk/face heat network with contact and direct radiation; illustrative coefficients"));
    Data->SetStringField(TEXT("field_model"),bImportedProfiles ? TEXT("authored-imported-surface-hotspot-v2"):
        bHeatTransferEnabled ? TEXT("object-local-heat-transfer-v1"):TEXT("object-local-cooling-v1"));
    Data->SetStringField(TEXT("temperature_source"),bImportedProfiles ? TEXT("authored_illustrative_not_measured"):TEXT("procedural_simulation"));
    Data->SetObjectField(TEXT("motor_radiometry_cache"),MotorRadiometryMetadata());
    Data->SetNumberField(TEXT("lut_width"),LUTWidth);
    Data->SetNumberField(TEXT("lut_rows"),LUTRows);
    Data->SetNumberField(TEXT("lut_bytes"),LUTWidth*LUTRows*sizeof(FLinearColor));
    Data->SetStringField(TEXT("hotspot_model"),TEXT("s=saturate(1-distance_squared/world_radius_squared); weight=s*s*(3-2*s); lerp(surface_c,tip_c,weight); disabled in uniform comparison"));
    Data->SetNumberField(TEXT("weld_heat_patch_limit"),WeldHeatPatchCount);
    Data->SetStringField(TEXT("weld_heat_model"),TEXT("s=saturate(1-distance_squared/world_radius_squared); weight=s*s*(3-2*s); max(base_surface_c,lerp(base_surface_c,patch_surface_c,weight) for each patch); disabled in uniform comparison"));
    Data->SetBoolField(TEXT("spatial_fields_enabled"),bSpatialFields);
    Data->SetBoolField(TEXT("heat_transfer_enabled"),bHeatTransferEnabled);
    if (bHeatTransferEnabled) Data->SetObjectField(TEXT("heat_exchange"),HeatExchange.Metadata());
    Data->SetNumberField(TEXT("cooling_age_seconds"),CoolingAgeSeconds);
    if (!bImportedProfiles) Data->SetNumberField(TEXT("cooling_time_constant_seconds_per_cm"),30);
    return Data;
}
TSharedRef<FJsonObject> ADarkFactoryThermalRig::BuildSnapshotMetadata() const {
    check(IsInGameThread());
    auto Meta=GetEncodingMetadata();
    Meta->SetStringField(TEXT("case"),GetCaseName());
    Meta->SetStringField(TEXT("sensor_contract"),TEXT("dark-factory-thermal-sensor-v3"));
    Meta->SetBoolField(TEXT("adapter_ready"),false);
    Meta->SetBoolField(TEXT("training_manifest"),false);
    Meta->SetStringField(TEXT("scene_revision"),FString::Printf(TEXT("%llu"),Revision));
    if (const auto* Cell=CellOwner.Get()) {
        Meta->SetStringField(TEXT("episode_id"),Cell->EpisodeId);
        Meta->SetNumberField(TEXT("seed"),Cell->Seed);
        Meta->SetStringField(TEXT("nir_scenario"),Cell->GetScenarioName());
        if (bImportedProfiles) if (const auto* Imported=Cell->GetWeldingCell())
            Meta->SetObjectField(TEXT("imported_preview"),Imported->BuildPreviewMetadata());
    }
    auto TransformMetadata=[](const FTransform& Transform) {
        auto Data=MakeShared<FJsonObject>();
        const FVector Position=Transform.GetLocation(),Scale=Transform.GetScale3D();
        const FQuat Rotation=Transform.GetRotation();
        Data->SetArrayField(TEXT("position_cm"),{MakeShared<FJsonValueNumber>(Position.X),MakeShared<FJsonValueNumber>(Position.Y),MakeShared<FJsonValueNumber>(Position.Z)});
        Data->SetArrayField(TEXT("rotation_xyzw"),{MakeShared<FJsonValueNumber>(Rotation.X),MakeShared<FJsonValueNumber>(Rotation.Y),MakeShared<FJsonValueNumber>(Rotation.Z),MakeShared<FJsonValueNumber>(Rotation.W)});
        Data->SetArrayField(TEXT("scale_xyz"),{MakeShared<FJsonValueNumber>(Scale.X),MakeShared<FJsonValueNumber>(Scale.Y),MakeShared<FJsonValueNumber>(Scale.Z)});
        return Data;
    };
    Meta->SetStringField(TEXT("pose_space"),TEXT("Unreal world; centimetres; left-handed X-forward Y-right Z-up"));
    Meta->SetObjectField(TEXT("camera_world_transform"),TransformMetadata(ThermalSensor->GetComponentTransform()));
    Meta->SetNumberField(TEXT("camera_horizontal_fov_deg"),ThermalSensor->FOVAngle);
    TArray<TSharedPtr<FJsonValue>> Profiles;
    for (int32 Index=0;Index<Surfaces.Num();++Index) {
        const auto& S=Surfaces[Index];
        const auto* Mesh=S.Mesh.Get();
        if (!Mesh) continue;
        auto P=MakeShared<FJsonObject>();
        P->SetNumberField(TEXT("stencil_id"),Index+1);
        P->SetStringField(TEXT("surface"),Mesh->GetName());
        P->SetNumberField(TEXT("source_temperature_c"),S.CurrentC);
        P->SetNumberField(TEXT("emissivity"),S.Emissivity);
        P->SetNumberField(TEXT("uniform_apparent_temperature_c"),S.ApparentC);
        P->SetNumberField(TEXT("core_surface_temperature_c"),S.CoreSurfaceC);
        P->SetNumberField(TEXT("edge_surface_temperature_c"),S.EdgeSurfaceC);
        const bool ImportedCoreField=bImportedProfiles && S.AuthoredCoreEdgeDropC>0.f;
        P->SetBoolField(TEXT("imported_core_field_enabled"),ImportedCoreField && bSpatialFields);
        P->SetNumberField(TEXT("core_field_axis"),ImportedCoreField ? S.ImportedCoreFieldAxis:-1);
        P->SetNumberField(TEXT("core_field_offset_normalized"),ImportedCoreField ? S.ImportedCoreFieldOffset:0.f);
        P->SetStringField(TEXT("core_field_kernel"),ImportedCoreField
            ? TEXT("imported-core-v1: q[axis]-=offset; radius_squared=0.45*dot(q,q)+0.55*q[axis]^2; weight=exp2(-falloff*radius_squared)")
            : TEXT("legacy-box-v1: weight=exp2(-falloff*max(dot(q,q)-1,0))"));
        TArray<TSharedPtr<FJsonValue>> Deltas;
        for (float Delta:S.FaceDeltaC) Deltas.Add(MakeShared<FJsonValueNumber>(Delta));
        P->SetArrayField(TEXT("face_delta_temperature_c"),Deltas);
        P->SetNumberField(TEXT("characteristic_length_cm"),S.CharacteristicLengthCm);
        P->SetNumberField(TEXT("cooling_retention"),S.CoolingRetention);
        if (bImportedProfiles) {
            P->SetNumberField(TEXT("authored_core_edge_drop_c"),S.AuthoredCoreEdgeDropC);
            P->SetBoolField(TEXT("simulate_exchange"),false);
            P->SetBoolField(TEXT("tool_hotspot_enabled"),S.HotspotRadiusCm>0.f);
            if (S.HotspotRadiusCm>0.f) {
                const FVector World=Mesh->GetComponentTransform().TransformPosition(S.HotspotLocalCm);
                P->SetNumberField(TEXT("tool_tip_surface_temperature_c"),S.TipSurfaceC);
                P->SetNumberField(TEXT("tool_hotspot_radius_world_cm"),S.HotspotRadiusCm);
                P->SetArrayField(TEXT("tool_hotspot_local_cm"),{MakeShared<FJsonValueNumber>(S.HotspotLocalCm.X),MakeShared<FJsonValueNumber>(S.HotspotLocalCm.Y),MakeShared<FJsonValueNumber>(S.HotspotLocalCm.Z)});
                P->SetArrayField(TEXT("tool_hotspot_world_cm"),{MakeShared<FJsonValueNumber>(World.X),MakeShared<FJsonValueNumber>(World.Y),MakeShared<FJsonValueNumber>(World.Z)});
            }
            TArray<TSharedPtr<FJsonValue>> WeldPatches;
            for (const auto& Patch:S.WeldHeatPatches) {
                auto W=MakeShared<FJsonObject>();
                const FVector World=Mesh->GetComponentTransform().TransformPosition(Patch.LocalPositionCm);
                W->SetArrayField(TEXT("local_position_cm"),{MakeShared<FJsonValueNumber>(Patch.LocalPositionCm.X),MakeShared<FJsonValueNumber>(Patch.LocalPositionCm.Y),MakeShared<FJsonValueNumber>(Patch.LocalPositionCm.Z)});
                W->SetArrayField(TEXT("world_position_cm"),{MakeShared<FJsonValueNumber>(World.X),MakeShared<FJsonValueNumber>(World.Y),MakeShared<FJsonValueNumber>(World.Z)});
                W->SetNumberField(TEXT("radius_cm"),Patch.RadiusCm);
                W->SetNumberField(TEXT("surface_temperature_c"),Patch.SurfaceC);
                WeldPatches.Add(MakeShared<FJsonValueObject>(W));
            }
            P->SetArrayField(TEXT("weld_heat_patches"),WeldPatches);
        } else P->SetNumberField(TEXT("edge_fraction"),S.EdgeFraction);
        P->SetNumberField(TEXT("edge_falloff"),S.EdgeFalloff);
        const FVector BoundsCenter=S.LocalBounds.GetCenter(),BoundsExtent=S.LocalBounds.GetExtent();
        P->SetArrayField(TEXT("local_bounds_center"),{MakeShared<FJsonValueNumber>(BoundsCenter.X),MakeShared<FJsonValueNumber>(BoundsCenter.Y),MakeShared<FJsonValueNumber>(BoundsCenter.Z)});
        P->SetArrayField(TEXT("local_bounds_extent"),{MakeShared<FJsonValueNumber>(BoundsExtent.X),MakeShared<FJsonValueNumber>(BoundsExtent.Y),MakeShared<FJsonValueNumber>(BoundsExtent.Z)});
        P->SetBoolField(TEXT("powered"),S.bPowered);
        P->SetBoolField(TEXT("component_visible"),Mesh->IsVisible());
        P->SetObjectField(TEXT("world_transform"),TransformMetadata(Mesh->GetComponentTransform()));
        Profiles.Add(MakeShared<FJsonValueObject>(P));
    }
    Meta->SetArrayField(TEXT("profiles"),Profiles);
    if (DepthTestCover) {
        auto Cover=TransformMetadata(DepthTestCover->GetComponentTransform());
        Cover->SetBoolField(TEXT("component_visible"),DepthTestCover->IsVisible());
        Meta->SetObjectField(TEXT("unregistered_test_cover"),Cover);
    }
    // Submission timestamps describe this simulated capture, not GPU completion.
    Meta->SetStringField(TEXT("capture_submitted_at_utc"),FDateTime::UtcNow().ToIso8601());
    Meta->SetNumberField(TEXT("capture_submitted_monotonic_seconds"),FPlatformTime::Seconds());
    return Meta;
}
void ADarkFactoryThermalRig::SaveEncodedSnapshot(TArray<uint8> Bytes,TSharedPtr<FJsonObject> Metadata) {
    check(IsInGameThread());
    if (Bytes.IsEmpty() || !Metadata) { LastSnapshot=TEXT("Thermal encoding or metadata failed"); return; }
    IFileManager::Get().MakeDirectory(*SnapshotRoot,true);
    const FString Stem=FPaths::Combine(SnapshotRoot,FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (!FFileHelper::SaveArrayToFile(Bytes,*(Stem+TEXT(".png")))) { LastSnapshot=TEXT("Thermal save failed"); return; }
    // Only the eventual file name is added after capture; scene data is frozen.
    Metadata->SetStringField(TEXT("image"),FPaths::GetCleanFilename(Stem+TEXT(".png")));
    FString Text; FJsonSerializer::Serialize(Metadata.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    if (!FFileHelper::SaveStringToFile(Text,*(Stem+TEXT(".json")))) { LastSnapshot=TEXT("Thermal PNG saved; metadata failed"); return; }
    LastSnapshot=Stem+TEXT(".png");
}
TSharedRef<FJsonObject> ADarkFactoryThermalRig::GetQARegions() const {
    FMinimalViewInfo Info; ThermalSensor->GetCameraView(0,Info);
    Info.AspectRatio=float(ThermalWidth)/ThermalHeight;
    FMatrix View,Projection,VP;
    UGameplayStatics::CalculateViewProjectionMatricesFromMinimalView(Info,TOptional<FMatrix>(),View,Projection,VP);
    auto Result=MakeShared<FJsonObject>();
    auto Region=[&](const TCHAR* Name,FVector Location,int32 HalfWidth,int32 HalfHeight) {
        FVector2D Pixel;
        if (!FSceneView::ProjectWorldToScreen(Location,FIntRect(0,0,ThermalWidth,ThermalHeight),VP,Pixel)) return;
        const int32 X=FMath::RoundToInt(Pixel.X),Y=FMath::RoundToInt(Pixel.Y);
        Result->SetArrayField(Name,{MakeShared<FJsonValueNumber>(FMath::Clamp(X-HalfWidth,0,ThermalWidth-1)),MakeShared<FJsonValueNumber>(FMath::Clamp(Y-HalfHeight,0,ThermalHeight-1)),MakeShared<FJsonValueNumber>(FMath::Clamp(X+HalfWidth,1,ThermalWidth)),MakeShared<FJsonValueNumber>(FMath::Clamp(Y+HalfHeight,1,ThermalHeight))});
    };
    if (TargetModule) {
        // Top face away from the raised ribs.
        const FVector Center=TargetModule->GetComponentLocation()+FVector(0,14,17.1f);
        Region(TEXT("target_module"),Center,3,3); Region(TEXT("occluder"),Center,3,3);
    }
    if (HeaterFaultStrip) Region(TEXT("heater"),HeaterFaultStrip->GetComponentLocation()+FVector(0,-3,0),2,5);
    if (GlueGapSegment) Region(TEXT("glue_gap"),GlueGapSegment->GetComponentLocation()+FVector(0,0,3.1f),2,2);
    if (GlueIntactSegment) Region(TEXT("glue_intact"),GlueIntactSegment->GetComponentLocation()+FVector(0,0,3.1f),2,2);
    return Result;
}

TSharedRef<FJsonObject> ADarkFactoryThermalRig::GetFieldQARegions() const {
    FMinimalViewInfo Info; ThermalSensor->GetCameraView(0,Info);
    Info.AspectRatio=float(ThermalWidth)/ThermalHeight;
    FMatrix View,Projection,VP;
    UGameplayStatics::CalculateViewProjectionMatricesFromMinimalView(Info,TOptional<FMatrix>(),View,Projection,VP);
    auto Result=MakeShared<FJsonObject>();
    auto Region=[&](const TCHAR* Name,const TCHAR* MeshName,FVector LocalPoint) {
        for (int32 Index=0;Index<Surfaces.Num();++Index) {
            const auto* Mesh=Surfaces[Index].Mesh.Get();
            if (!Mesh || Mesh->GetName()!=MeshName) continue;
            FVector2D Pixel;
            if (!FSceneView::ProjectWorldToScreen(Mesh->GetComponentTransform().TransformPosition(LocalPoint),FIntRect(0,0,ThermalWidth,ThermalHeight),VP,Pixel)) return;
            const int32 X=FMath::RoundToInt(Pixel.X),Y=FMath::RoundToInt(Pixel.Y);
            auto Value=MakeShared<FJsonObject>();
            Value->SetArrayField(TEXT("box"),{MakeShared<FJsonValueNumber>(FMath::Clamp(X-2,0,ThermalWidth-1)),MakeShared<FJsonValueNumber>(FMath::Clamp(Y-2,0,ThermalHeight-1)),MakeShared<FJsonValueNumber>(FMath::Clamp(X+2,1,ThermalWidth)),MakeShared<FJsonValueNumber>(FMath::Clamp(Y+2,1,ThermalHeight))});
            Value->SetNumberField(TEXT("stencil_id"),Index+1);
            Result->SetObjectField(Name,Value); return;
        }
    };
    Region(TEXT("body_core"),TEXT("BatteryModule1"),FVector(0,10.75,50.05));
    Region(TEXT("body_edge_near"),TEXT("BatteryModule1"),FVector(0,-42,50.05));
    Region(TEXT("body_edge_far"),TEXT("BatteryModule1"),FVector(0,42,50.05));
    Region(TEXT("thin_rib"),TEXT("ModuleRib1_1"),FVector(0,0,50.05));
    Region(TEXT("heater"),TEXT("WindowHeater1"),FVector(0,-50.05,0));
    return Result;
}
bool ADarkFactoryThermalRig::SaveFieldSmoke(const FString& Directory) {
    if (!TargetModule || !TargetModule->GetAttachParent()) return false;
    auto* Tray=TargetModule->GetAttachParent();
    const FTransform Original=Tray->GetRelativeTransform();
    const float OriginalAge=CoolingAgeSeconds;
    const bool OriginalFields=bSpatialFields;
    auto Report=MakeShared<FJsonObject>();
    auto Poses=MakeShared<FJsonObject>();
    bool Passed=true;
    SetSpatialFieldsEnabled(true);
    for (int32 Pose=0;Pose<3;++Pose) {
        FTransform Transform=Original;
        const TCHAR* Name=Pose==0 ? TEXT("reference"):Pose==1 ? TEXT("translated"):TEXT("rotated");
        if (Pose==1) Transform.AddToTranslation(FVector(30,-20,10));
        if (Pose==2) Transform.SetRotation(FQuat(FVector::UpVector,FMath::DegreesToRadians(15.f))*Original.GetRotation());
        Tray->SetRelativeTransform(Transform); NotifySceneChanged();
        const FString Stem=FPaths::Combine(Directory,FString(TEXT("field_"))+Name);
        Passed &= SaveRawForSmoke(Stem+TEXT(".png"));
        Poses->SetObjectField(Name,GetFieldQARegions());
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),1);
        Passed &= SaveRawForSmoke(Stem+TEXT("_ids.png"));
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),0);
    }
    Tray->SetRelativeTransform(Original); NotifySceneChanged();
    SetSpatialFieldsEnabled(false);
    Passed &= SaveRawForSmoke(FPaths::Combine(Directory,TEXT("field_uniform.png")));
    SetSpatialFieldsEnabled(true); SetCoolingAge(OriginalAge+30);
    Passed &= SaveRawForSmoke(FPaths::Combine(Directory,TEXT("field_cooled.png")));
    auto Cooling=MakeShared<FJsonObject>();
    Cooling->SetNumberField(TEXT("initial_age_seconds"),OriginalAge);
    Cooling->SetNumberField(TEXT("cooled_age_seconds"),CoolingAgeSeconds);
    Report->SetObjectField(TEXT("cooling"),Cooling);
    Report->SetObjectField(TEXT("poses"),Poses);
    Report->SetStringField(TEXT("scope"),TEXT("Offline spatial-field QA; ID masks never enter sensor exports or model inference"));
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    Passed &= FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,TEXT("field_regions.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    SetCoolingAge(OriginalAge); SetSpatialFieldsEnabled(OriginalFields);
    CaptureGPU();
    return Passed;
}
