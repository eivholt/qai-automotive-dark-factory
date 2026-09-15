#include "DarkFactoryFactoryLights.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace {
constexpr float StartupDurationSeconds=1.5f;
constexpr float RoofOutputScale=.9f;
constexpr float NominalTubeEmission=12.f*RoofOutputScale;
constexpr float StartDelays[]={0.f,.12f,.24f,.06f,.18f,.30f};
const FLinearColor TubeColor(.82f,.91f,1.f);

float BankGain(float Elapsed,int32 Bank) {
    const float T=Elapsed-StartDelays[Bank];
    // Authored cold-start strikes, not a simulation of mains frequency or a ballast.
    // The final bank reaches full output at 1.5 seconds. No ongoing flicker cost.
    if (T<.055f) return 0.f;
    if (T<.105f) return .18f;
    if (T<.175f) return 0.f;
    if (T<.285f) return .85f;
    if (T<.36f) return .04f;
    if (T<.47f) return 1.f;
    if (T<.56f) return .12f;
    if (T<.67f) return .92f;
    if (T<.75f) return .30f;
    const float S=FMath::Clamp((T-.75f)/.45f,0.f,1.f);
    return .72f+.28f*S*S*(3.f-2.f*S);
}

void IsolateFixture(UPrimitiveComponent* Mesh) {
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);
    Mesh->SetCastHiddenShadow(false);
    Mesh->SetLightingChannels(true,false,false);
    Mesh->SetHiddenInSceneCapture(true);
    Mesh->SetRenderCustomDepth(false);
    Mesh->SetCustomDepthStencilValue(0);
    Mesh->SetVisibleInRayTracing(false);
    Mesh->bVisibleInReflectionCaptures=false;
    Mesh->bVisibleInRealTimeSkyCaptures=false;
    Mesh->bVisibleInReflections=false;
    Mesh->SetAffectDynamicIndirectLighting(false);
    Mesh->SetAffectDistanceFieldLighting(false);
    Mesh->SetEmissiveLightSource(false);
    Mesh->SetVisibility(false);
}
TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& V) {
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
}

ADarkFactoryFactoryLights::ADarkFactoryFactoryLights() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("FactoryLightRoot"));
    SetRootComponent(SceneRoot);
}

bool ADarkFactoryFactoryLights::Initialize(bool ImportedScene) {
    if (bInitialized) return bImportedScene==ImportedScene;
    if (!GetWorld()) return false;
    auto* TubeMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* HousingMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    // The existing particle material already has the instanced-static-mesh permutation.
    // Instance float zero stays one; this bank's MID controls uniform tube emission.
    auto* TubeBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingParticle.M_WeldingParticle"));
    auto* HousingBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!TubeMesh || !HousingMesh || !TubeBase || !HousingBase) {
        UE_LOG(LogTemp,Error,TEXT("DF_FACTORY_LIGHTS_MISSING_CONTENT run setup_content.py"));
        return false;
    }
    bImportedScene=ImportedScene;
    NominalBankLumens=(ImportedScene ? 2000.f:800.f)*RoofOutputScale;
    HousingMaterial=UMaterialInstanceDynamic::Create(HousingBase,this);
    HousingMaterial->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.38f,.41f,.44f));
    HousingMaterial->SetScalarParameterValue(TEXT("Roughness"),.5f);
    const float X=ImportedScene ? 420.f:230.f, Y=ImportedScene ? 720.f:280.f;
    const float Z=ImportedScene ? 720.f:480.f;
    const float TubeLength=ImportedScene ? 260.f:140.f;
    const float TubeSpacing=ImportedScene ? 30.f:24.f;
    const float TubeDiameter=ImportedScene ? 4.5f:4.f;
    for (int32 I=0;I<BankCount;++I) {
        const FVector Position(I<3 ? -X:X,((I%3)-1)*Y,Z);
        auto* Tube=NewObject<UInstancedStaticMeshComponent>(this,*FString::Printf(TEXT("FluorescentTubes%d"),I));
        Tube->SetupAttachment(SceneRoot.Get());
        IsolateFixture(Tube);
        Tube->SetStaticMesh(TubeMesh);
        Tube->SetRelativeLocation(Position+FVector(0,0,-10));
        Tube->SetNumCustomDataFloats(1);
        auto* Material=UMaterialInstanceDynamic::Create(TubeBase,this);
        Material->SetVectorParameterValue(TEXT("SparkColor"),TubeColor);
        Material->SetScalarParameterValue(TEXT("EmissiveStrength"),0.f);
        Tube->SetMaterial(0,Material);
        for (int32 J=0;J<TubesPerBank;++J) {
            const FTransform Transform(FRotator(90,0,0),FVector(0,(J-1)*TubeSpacing,0),
                FVector(TubeDiameter/100.f,TubeDiameter/100.f,TubeLength/100.f));
            const int32 Instance=Tube->AddInstance(Transform);
            Tube->SetCustomDataValue(Instance,0,1.f,false);
        }
        AddInstanceComponent(Tube);
        Tube->RegisterComponent();
        Tubes.Add(Tube);
        TubeMaterials.Add(Material);

        auto* Housing=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("FluorescentHousing%d"),I));
        Housing->SetupAttachment(SceneRoot.Get());
        IsolateFixture(Housing);
        Housing->SetStaticMesh(HousingMesh);
        Housing->SetRelativeLocation(Position);
        Housing->SetRelativeScale3D(FVector((TubeLength+20.f)/100.f,(TubeSpacing*2.f+40.f)/100.f,.12f));
        Housing->SetMaterial(0,HousingMaterial);
        AddInstanceComponent(Housing);
        Housing->RegisterComponent();
        Housings.Add(Housing);

        auto* Light=NewObject<URectLightComponent>(this,*FString::Printf(TEXT("FluorescentLight%d"),I));
        Light->SetupAttachment(SceneRoot.Get());
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetRelativeLocation(Position+FVector(0,0,-16));
        Light->SetRelativeRotation(FRotator(-90,0,0));
        Light->SetIntensityUnits(ELightUnits::Lumens);
        Light->SetLightColor(TubeColor);
        Light->SetSourceWidth(TubeSpacing*2.f+20.f);
        Light->SetSourceHeight(TubeLength);
        Light->SetAttenuationRadius(ImportedScene ? 1800.f:1000.f);
        Light->ViewLightingChannels.bViewChannel0=true;
        Light->ViewLightingChannels.bViewChannel1=false;
        Light->ViewLightingChannels.bViewChannel2=false;
        // Six broad unshadowed direct lights keep the added GPU work bounded.
        // Lighting-channel isolation does not cover shared GI, so disable it explicitly.
        Light->SetCastShadows(false);
        Light->SetIndirectLightingIntensity(0.f);
        Light->SetVolumetricScatteringIntensity(0.f);
        Light->SetAffectGlobalIllumination(false);
        Light->SetAffectReflection(false);
        Light->SetAffectTranslucentLighting(false);
        Light->SetIntensity(0.f);
        Light->SetVisibility(false);
        AddInstanceComponent(Light);
        Light->RegisterComponent();
        Lights.Add(Light);
    }
    bInitialized=true;
    SetActorTickEnabled(false);
    ApplyState();
    return true;
}

void ADarkFactoryFactoryLights::SetEnabled(bool Enabled) {
    if (!bInitialized || bEnabled==Enabled) return;
    bEnabled=Enabled;
    StartupElapsedSeconds=0;
    bStartupTimePinned=false;
    ActivationWorldSeconds=GetWorld() ? GetWorld()->GetTimeSeconds():0;
    if (Enabled) ++ActivationCount;
    SetActorTickEnabled(Enabled);
    ApplyState();
}

void ADarkFactoryFactoryLights::SetStartupTimeForQA(float Seconds) {
    if (!bInitialized || !FMath::IsFinite(Seconds)) return;
    bStartupTimePinned=Seconds>=0;
    if (bStartupTimePinned) StartupElapsedSeconds=Seconds;
    else ActivationWorldSeconds=(GetWorld() ? GetWorld()->GetTimeSeconds():0)-StartupElapsedSeconds;
    SetActorTickEnabled(bEnabled && !bStartupTimePinned && StartupElapsedSeconds<StartupDurationSeconds);
    ApplyState();
}

void ADarkFactoryFactoryLights::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!bInitialized || !bEnabled || bStartupTimePinned) return;
    StartupElapsedSeconds=FMath::Clamp(static_cast<float>(GetWorld()->GetTimeSeconds()-ActivationWorldSeconds),0.f,StartupDurationSeconds);
    ApplyState();
    if (StartupElapsedSeconds>=StartupDurationSeconds) SetActorTickEnabled(false);
}

void ADarkFactoryFactoryLights::ApplyState() {
    if (!bInitialized) return;
    for (int32 I=0;I<BankCount;++I) {
        const float Gain=bEnabled ? BankGain(StartupElapsedSeconds,I):0.f;
        Tubes[I]->SetVisibility(bEnabled);
        Housings[I]->SetVisibility(bEnabled);
        Lights[I]->SetVisibility(bEnabled && Gain>0.f);
        if (!FMath::IsNearlyEqual(Gain,AppliedGains[I],1.e-6f)) {
            Lights[I]->SetIntensity(NominalBankLumens*Gain);
            TubeMaterials[I]->SetScalarParameterValue(TEXT("EmissiveStrength"),NominalTubeEmission*Gain);
            AppliedGains[I]=Gain;
        }
    }
}

TSharedRef<FJsonObject> ADarkFactoryFactoryLights::BuildMetadata() const {
    auto Meta=MakeShared<FJsonObject>();
    Meta->SetBoolField(TEXT("initialized"),bInitialized);
    Meta->SetBoolField(TEXT("enabled"),bEnabled);
    Meta->SetBoolField(TEXT("imported_scene"),bImportedScene);
    Meta->SetNumberField(TEXT("startup_elapsed_seconds"),StartupElapsedSeconds);
    Meta->SetNumberField(TEXT("startup_duration_seconds"),StartupDurationSeconds);
    Meta->SetBoolField(TEXT("startup_time_pinned"),bStartupTimePinned);
    Meta->SetNumberField(TEXT("activation_count"),ActivationCount);
    Meta->SetNumberField(TEXT("bank_count"),Lights.Num());
    Meta->SetNumberField(TEXT("light_count"),Lights.Num());
    Meta->SetNumberField(TEXT("tube_count"),Tubes.Num()*TubesPerBank);
    Meta->SetNumberField(TEXT("mesh_component_count"),Tubes.Num()+Housings.Num());
    Meta->SetBoolField(TEXT("tick_enabled"),IsActorTickEnabled());
    Meta->SetStringField(TEXT("startup_model"),TEXT("Deterministic staggered cold-start strikes; authored display effect, not electrical simulation"));
    TArray<TSharedPtr<FJsonValue>> BankRows;
    bool Channel0=true,Channel1=false,Channel2=false,Shadows=false,GI=false,Reflection=false,Translucent=false;
    float MaxIndirect=0,MaxVolumetric=0;
    bool HiddenInCapture=true,CustomDepth=false,Collision=false,GeometryShadows=false,GeometryGI=false,GeometryReflections=false;
    for (int32 I=0;I<Lights.Num();++I) {
        const auto* Light=Lights[I].Get();
        auto Bank=MakeShared<FJsonObject>();
        Bank->SetNumberField(TEXT("index"),I);
        Bank->SetArrayField(TEXT("position_cm"),VectorJson(Housings[I]->GetComponentLocation()));
        Bank->SetArrayField(TEXT("light_position_cm"),VectorJson(Light->GetComponentLocation()));
        Bank->SetNumberField(TEXT("start_delay_seconds"),StartDelays[I]);
        Bank->SetNumberField(TEXT("gain"),AppliedGains[I]);
        Bank->SetNumberField(TEXT("intensity_lumens"),Light->Intensity);
        Bank->SetNumberField(TEXT("nominal_lumens"),NominalBankLumens);
        Bank->SetNumberField(TEXT("tube_emissive_strength"),NominalTubeEmission*AppliedGains[I]);
        Bank->SetNumberField(TEXT("nominal_tube_emissive_strength"),NominalTubeEmission);
        Bank->SetNumberField(TEXT("tube_count"),Tubes[I]->GetInstanceCount());
        Bank->SetBoolField(TEXT("fixture_visible"),Tubes[I]->IsVisible() && Housings[I]->IsVisible());
        Bank->SetBoolField(TEXT("light_visible"),Light->IsVisible());
        BankRows.Add(MakeShared<FJsonValueObject>(Bank));
        Channel0&=Light->ViewLightingChannels.bViewChannel0;
        Channel1|=Light->ViewLightingChannels.bViewChannel1;
        Channel2|=Light->ViewLightingChannels.bViewChannel2;
        Shadows|=Light->CastShadows;
        GI|=Light->bAffectGlobalIllumination;
        Reflection|=Light->bAffectReflection;
        Translucent|=Light->bAffectTranslucentLighting;
        MaxIndirect=FMath::Max(MaxIndirect,Light->IndirectLightingIntensity);
        MaxVolumetric=FMath::Max(MaxVolumetric,Light->VolumetricScatteringIntensity);
        const UPrimitiveComponent* Geometry[]={Tubes[I].Get(),Housings[I].Get()};
        for (const auto* Mesh:Geometry) {
            HiddenInCapture&=Mesh->bHiddenInSceneCapture;
            CustomDepth|=Mesh->bRenderCustomDepth;
            Collision|=Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision;
            GeometryShadows|=Mesh->CastShadow || Mesh->bCastHiddenShadow;
            GeometryGI|=Mesh->bAffectDynamicIndirectLighting || Mesh->bAffectDistanceFieldLighting || Mesh->bEmissiveLightSource;
            GeometryReflections|=Mesh->bVisibleInReflectionCaptures || Mesh->bVisibleInRealTimeSkyCaptures || Mesh->bVisibleInRayTracing || Mesh->bVisibleInReflections;
        }
    }
    Meta->SetArrayField(TEXT("banks"),BankRows);
    auto Isolation=MakeShared<FJsonObject>();
    Isolation->SetBoolField(TEXT("view_channel0"),Channel0);
    Isolation->SetBoolField(TEXT("view_channel1"),Channel1);
    Isolation->SetBoolField(TEXT("view_channel2"),Channel2);
    Isolation->SetBoolField(TEXT("cast_shadows"),Shadows);
    Isolation->SetNumberField(TEXT("indirect_lighting_intensity"),MaxIndirect);
    Isolation->SetNumberField(TEXT("volumetric_scattering_intensity"),MaxVolumetric);
    Isolation->SetBoolField(TEXT("affect_global_illumination"),GI);
    Isolation->SetBoolField(TEXT("affect_reflection"),Reflection);
    Isolation->SetBoolField(TEXT("affect_translucent_lighting"),Translucent);
    Isolation->SetBoolField(TEXT("geometry_hidden_in_scene_capture"),HiddenInCapture);
    Isolation->SetBoolField(TEXT("geometry_custom_depth"),CustomDepth);
    Isolation->SetBoolField(TEXT("geometry_collision"),Collision);
    Isolation->SetBoolField(TEXT("geometry_cast_shadows"),GeometryShadows);
    Isolation->SetBoolField(TEXT("geometry_affect_indirect"),GeometryGI);
    Isolation->SetBoolField(TEXT("geometry_visible_in_reflections"),GeometryReflections);
    Meta->SetObjectField(TEXT("isolation"),Isolation);
    return Meta;
}