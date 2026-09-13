#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace {
// 28 * pulse weight, not a continuous 28 births: conservative maximum-lifetime
// replay at 30-144Hz input peaks at117/128, even with source intensity fixed1.
constexpr float BirthRatePerEmitter=28.f;
constexpr float MaterialOpacity=.52f;
constexpr float MinimumLifetime=3.f,MaximumLifetime=4.4f;
FTransform HiddenTransform() { return FTransform(FQuat::Identity,FVector(0,0,-10000),FVector(.00001)); }
TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& V) {
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
float Smooth01(float X) { X=FMath::Clamp(X,0.f,1.f); return X*X*(3.f-2.f*X); }
}

ADarkFactoryWeldingSmoke::ADarkFactoryWeldingSmoke() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("WeldingSmokeRoot"));
    SetRootComponent(SceneRoot);
}
bool ADarkFactoryWeldingSmoke::Initialize(ADarkFactoryWeldingCell* Cell,ADarkFactoryWeldingSparks* Sparks) {
    if (bInitialized || !IsValid(Cell) || !IsValid(Sparks)) return false;
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingSmoke.M_WeldingSmoke"));
    if (!Mesh || !Material) {
        UE_LOG(LogTemp,Error,TEXT("DF_WELDING_SMOKE_MISSING_CONTENT run setup_welding_smoke.py"));
        return false;
    }
    SourceCell=Cell; SourceSparks=Sparks;
    AddTickPrerequisiteActor(Cell); AddTickPrerequisiteActor(Sparks);
    Instances=NewObject<UInstancedStaticMeshComponent>(this,TEXT("OpticalWeldSmoke"));
    Instances->SetupAttachment(SceneRoot.Get());
    Instances->SetMobility(EComponentMobility::Movable);
    Instances->SetStaticMesh(Mesh);
    Instances->SetMaterial(0,Material);
    Instances->SetNumCustomDataFloats(3);
    Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Instances->SetGenerateOverlapEvents(false);
    Instances->SetCanEverAffectNavigation(false);
    Instances->SetCastShadow(false);
    Instances->SetCastHiddenShadow(false);
    Instances->SetReceivesDecals(false);
    Instances->SetLightingChannels(true,true,false);
    Instances->SetRenderCustomDepth(false);
    Instances->SetCustomDepthStencilValue(0);
    Instances->SetAffectDynamicIndirectLighting(false);
    Instances->SetAffectDistanceFieldLighting(false);
    Instances->SetEmissiveLightSource(false);
    Instances->SetVisibleInRayTracing(false);
    Instances->bVisibleInReflectionCaptures=false;
    Instances->bVisibleInRealTimeSkyCaptures=false;
    Instances->bVisibleInReflections=false;
    // Both observer and NIR see the same translucent geometry. The after-tonemap
    // thermal material ignores this before-DOF color; no fake opaque hot gas.
    Instances->SetHiddenInSceneCapture(false);
    for (int32 I=0;I<MaxParticles;++I) Instances->AddInstance(HiddenTransform());
    AddInstanceComponent(Instances.Get());
    Instances->RegisterComponent();
    Puffs.SetNum(MaxParticles);
    bInitialized=true;
    ResetParticles();
    return true;
}
bool ADarkFactoryWeldingSmoke::AreEffectsEnabled() const {
    const auto* Sparks=SourceSparks.Get();
    return bInitialized && bEffectsEnabled && Sparks && Sparks->AreEffectsEnabled();
}
void ADarkFactoryWeldingSmoke::ClearParticles() {
    for (auto& Puff:Puffs) if (Puff.Alive) { Puff.Alive=false; ++Cleared; }
    for (float& Value:SpawnAccumulator) Value=0;
    FixedRemainder=0;
}
void ADarkFactoryWeldingSmoke::ResetParticles() {
    if (!bInitialized) return;
    for (auto& Puff:Puffs) Puff=FPuff();
    for (float& Value:SpawnAccumulator) Value=0;
    Random.Initialize(41873);
    Spawned=Expired=Dropped=Cleared=PeakAlive=0;
    SimulationSeconds=FixedRemainder=0;
    LastSourceSeconds=SourceCell.IsValid() ? SourceCell->GetMotionTime():0;
    bLastSourceEnabled=SourceSparks.IsValid() && SourceSparks->AreEffectsEnabled();
    UpdateRendering();
}
void ADarkFactoryWeldingSmoke::SetEffectsEnabled(bool Enabled) {
    if (bEffectsEnabled==Enabled) return;
    bEffectsEnabled=Enabled;
    if (!Enabled) ClearParticles();
    UpdateRendering();
}
void ADarkFactoryWeldingSmoke::SetRenderingEnabled(bool Enabled) {
    if (bRenderingEnabled==Enabled) return;
    bRenderingEnabled=Enabled;
    if (Instances) Instances->SetVisibility(AreEffectsEnabled() && bRenderingEnabled && GetAliveCount()>0);
}
int32 ADarkFactoryWeldingSmoke::GetAliveCount() const {
    int32 Count=0;
    for (const auto& Puff:Puffs) Count+=Puff.Alive ? 1:0;
    return Count;
}
void ADarkFactoryWeldingSmoke::Spawn(const FDarkFactoryWeldPulseEmitter& Emitter) {
    if (Emitter.EmitterIndex<0 || Emitter.EmitterIndex>=EmitterCount ||
        Emitter.Position.ContainsNaN() || Emitter.Normal.ContainsNaN()) return;
    FPuff* Target=nullptr;
    for (auto& Puff:Puffs) if (!Puff.Alive) { Target=&Puff; break; }
    if (!Target) { ++Dropped; return; }
    *Target=FPuff();
    Target->Alive=true;
    Target->Id=++Spawned;
    Target->EmitterIndex=Emitter.EmitterIndex;
    Target->PulseIndex=Emitter.PulseIndex;
    const FVector Normal=Emitter.Normal.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    Target->Origin=Emitter.Position+Normal*Random.FRandRange(4.f,7.f)
        +FVector(Random.FRandRange(-2.f,2.f),Random.FRandRange(-2.f,2.f),0);
    Target->InitialDrift=FVector(Random.FRandRange(-7.f,7.f),Random.FRandRange(-7.f,7.f),0);
    Target->Lifetime=Random.FRandRange(MinimumLifetime,MaximumLifetime);
    Target->InitialRadius=Random.FRandRange(12.f,17.f);
    Target->Growth=Random.FRandRange(11.f,17.f);
    Target->RiseSpeed=Random.FRandRange(33.f,46.f);
    Target->CurlRadius=Random.FRandRange(10.f,22.f);
    Target->Seed=Random.FRand();
    Target->Density=Random.FRandRange(.65f,1.f);
}
FVector ADarkFactoryWeldingSmoke::Position(const FPuff& Puff) const {
    const float A=Puff.Age,Phase=Puff.Seed*2.f*PI;
    const float Curl=Puff.CurlRadius*(1.f-FMath::Exp(-A*1.3f));
    const FVector Swirl(Curl*(FMath::Sin(Phase+A*1.7f)-FMath::Sin(Phase)),
        Curl*(FMath::Cos(Phase+A*1.3f)-FMath::Cos(Phase)),0);
    return Puff.Origin+Puff.InitialDrift*(.8f*(1.f-FMath::Exp(-A/.8f)))
        +FVector(4.5f*A,-2.f*A,Puff.RiseSpeed*A+2.2f*A*A)+Swirl;
}
float ADarkFactoryWeldingSmoke::Radius(const FPuff& Puff) const {
    return Puff.InitialRadius+Puff.Growth*Puff.Age;
}
float ADarkFactoryWeldingSmoke::Opacity(const FPuff& Puff) const {
    const float Remaining=FMath::Clamp(1.f-Puff.Age/Puff.Lifetime,0.f,1.f);
    return MaterialOpacity*Puff.Density*Smooth01(Puff.Age/.12f)*Remaining*Remaining;
}
void ADarkFactoryWeldingSmoke::AdvanceSimulation(float Seconds,bool Emit) {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_WeldingSmoke);
    if (!bInitialized || !FMath::IsFinite(Seconds) || Seconds<=0.f || Seconds>30.f) return;
    if (SourceCell.IsValid()) LastSourceSeconds=SourceCell->GetMotionTime();
    if (!AreEffectsEnabled()) { ClearParticles(); UpdateRendering(); return; }
    const auto Emitters=Emit ? SourceSparks->GetPulseEmitters():TArray<FDarkFactoryWeldPulseEmitter>();
    FixedRemainder+=Seconds;
    constexpr double StepSeconds=1.0/SimulationHz;
    while (FixedRemainder+1.e-9>=StepSeconds) {
        for (auto& Puff:Puffs) if (Puff.Alive) {
            Puff.Age+=static_cast<float>(StepSeconds);
            if (Puff.Age>=Puff.Lifetime) { Puff.Alive=false; ++Expired; }
        }
        for (const auto& Emitter:Emitters) {
            if (Emitter.EmitterIndex<0 || Emitter.EmitterIndex>=EmitterCount || !FMath::IsFinite(Emitter.SmokeWeight)) continue;
            auto& Accumulator=SpawnAccumulator[Emitter.EmitterIndex];
            Accumulator+=BirthRatePerEmitter*static_cast<float>(StepSeconds)*FMath::Clamp(Emitter.SmokeWeight,0.f,1.f);
            while (Accumulator>=1.f) { Spawn(Emitter); Accumulator-=1.f; }
        }
        SimulationSeconds+=StepSeconds;
        FixedRemainder=FMath::Max(0.,FixedRemainder-StepSeconds);
        PeakAlive=FMath::Max(PeakAlive,GetAliveCount());
    }
    UpdateRendering();
}
void ADarkFactoryWeldingSmoke::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!bInitialized) return;
    const auto* Cell=SourceCell.Get();
    const bool SourceEnabled=SourceSparks.IsValid() && SourceSparks->AreEffectsEnabled();
    if (!SourceEnabled || !bEffectsEnabled || !Cell) {
        if (GetAliveCount()>0) ClearParticles();
        if (Instances) Instances->SetVisibility(false);
        bLastSourceEnabled=SourceEnabled;
        if (Cell) LastSourceSeconds=Cell->GetMotionTime();
        return;
    }
    const double Now=Cell->GetMotionTime();
    if (!bLastSourceEnabled) { LastSourceSeconds=Now; bLastSourceEnabled=true; }
    // Offline QA explicitly advances the shared state while the cell is paused.
    // A paused pose change must not clear/age plumes or be consumed twice.
    if (!Cell->IsAnimating()) { LastSourceSeconds=Now; return; }
    const double Elapsed=Now-LastSourceSeconds;
    if (Elapsed<-.000001 || Elapsed>.25) {
        ResetParticles(); // An interactive seek starts a fresh bounded optical history.
        LastSourceSeconds=Now;
        return;
    }
    if (Elapsed>0) AdvanceSimulation(static_cast<float>(Elapsed),true);
}
void ADarkFactoryWeldingSmoke::UpdateRendering() {
    if (!Instances) return;
    TArray<FTransform> Transforms;
    Transforms.Reserve(MaxParticles);
    for (int32 I=0;I<Puffs.Num();++I) {
        const auto& Puff=Puffs[I];
        const float Data[]={Puff.Alive ? Puff.Age:0.f,Puff.Seed,Puff.Alive ? Opacity(Puff)/MaterialOpacity:0.f};
        Instances->SetCustomData(I,MakeArrayView(Data),false);
        if (!Puff.Alive) { Transforms.Add(HiddenTransform()); continue; }
        const float R=Radius(Puff)/50.f;
        Transforms.Add(FTransform(FRotator(0,Puff.Seed*360.f+Puff.Age*11.f,0),
            Position(Puff),FVector(R,R,R*1.25f)));
    }
    Instances->BatchUpdateInstancesTransforms(0,Transforms,true,true,true);
    Instances->SetVisibility(AreEffectsEnabled() && bRenderingEnabled && GetAliveCount()>0);
}
TSharedRef<FJsonObject> ADarkFactoryWeldingSmoke::BuildMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),TEXT("shared-pulse-optical-wisps-v2"));
    Result->SetStringField(TEXT("scope"),TEXT("Analytic expanding buoyant wisps, procedural translucent shading; no fluid, collision, measured scattering or LWIR smoke/gas simulation"));
    Result->SetBoolField(TEXT("initialized"),bInitialized);
    Result->SetBoolField(TEXT("effects_enabled"),AreEffectsEnabled());
    Result->SetBoolField(TEXT("requested_effects_enabled"),bEffectsEnabled);
    Result->SetBoolField(TEXT("rendering_enabled"),bRenderingEnabled);
    Result->SetBoolField(TEXT("visible"),Instances && Instances->IsVisible());
    Result->SetNumberField(TEXT("max_particles"),MaxParticles);
    Result->SetNumberField(TEXT("alive_particles"),GetAliveCount());
    Result->SetNumberField(TEXT("peak_alive"),PeakAlive);
    Result->SetNumberField(TEXT("spawned_particles"),Spawned);
    Result->SetNumberField(TEXT("expired_particles"),Expired);
    Result->SetNumberField(TEXT("dropped_particles"),Dropped);
    Result->SetNumberField(TEXT("cleared_particles"),Cleared);
    Result->SetNumberField(TEXT("birth_rate_per_emitter"),BirthRatePerEmitter);
    Result->SetNumberField(TEXT("material_opacity"),MaterialOpacity);
    Result->SetStringField(TEXT("radius_cm"),TEXT("initial12-17; growth11-17 cm/s; soft vertically stretched envelope"));
    Result->SetStringField(TEXT("rise_speed_cm_s"),TEXT("initial33-46; analytic buoyant acceleration4.4 cm/s2"));
    Result->SetNumberField(TEXT("simulation_seconds"),SimulationSeconds);
    Result->SetNumberField(TEXT("source_motion_seconds"),SourceCell.IsValid() ? SourceCell->GetMotionTime():0.);
    Result->SetNumberField(TEXT("minimum_lifetime_seconds"),MinimumLifetime);
    Result->SetNumberField(TEXT("maximum_lifetime_seconds"),MaximumLifetime);
    TArray<TSharedPtr<FJsonValue>> Rows;
    FBox Bounds(ForceInit);
    double TotalOpacity=0,MeanZ=0;
    float Oldest=0,Youngest=MaximumLifetime;
    bool Finite=true;
    for (const auto& Puff:Puffs) if (Puff.Alive) {
        const FVector P=Position(Puff);
        const float R=Radius(Puff),Alpha=Opacity(Puff);
        Finite &= !P.ContainsNaN() && !Puff.Origin.ContainsNaN() && FMath::IsFinite(R) &&
            FMath::IsFinite(Alpha) && FMath::IsFinite(Puff.Age) && FMath::IsFinite(Puff.Seed) &&
            Puff.Age>=0 && Puff.Lifetime>=MinimumLifetime && Puff.Lifetime<=MaximumLifetime &&
            R>0 && Alpha>=0 && Alpha<=MaterialOpacity;
        auto Row=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("id"),Puff.Id);
        Row->SetNumberField(TEXT("emitter_index"),Puff.EmitterIndex);
        Row->SetNumberField(TEXT("pulse_index"),Puff.PulseIndex);
        Row->SetArrayField(TEXT("position_cm"),VectorJson(P));
        Row->SetArrayField(TEXT("origin_cm"),VectorJson(Puff.Origin));
        Row->SetNumberField(TEXT("age_seconds"),Puff.Age);
        Row->SetNumberField(TEXT("lifetime_seconds"),Puff.Lifetime);
        Row->SetNumberField(TEXT("radius_cm"),R);
        Row->SetNumberField(TEXT("opacity"),Alpha);
        Row->SetNumberField(TEXT("seed"),Puff.Seed);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        Bounds+=P-FVector(R,R,R*1.25f); Bounds+=P+FVector(R,R,R*1.25f);
        TotalOpacity+=Alpha; MeanZ+=P.Z;
        Oldest=FMath::Max(Oldest,Puff.Age); Youngest=FMath::Min(Youngest,Puff.Age);
    }
    auto ParticleState=MakeShared<FJsonObject>();
    ParticleState->SetArrayField(TEXT("puffs"),Rows);
    FString StateText;
    FJsonSerializer::Serialize(ParticleState,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&StateText));
    Result->SetStringField(TEXT("particle_state_signature"),FMD5::HashAnsiString(*StateText));
    Result->SetBoolField(TEXT("finite_particle_state"),Finite);
    Result->SetNumberField(TEXT("total_opacity"),TotalOpacity);
    Result->SetNumberField(TEXT("oldest_age_seconds"),Oldest);
    Result->SetNumberField(TEXT("youngest_age_seconds"),Rows.IsEmpty() ? 0.f:Youngest);
    Result->SetNumberField(TEXT("mean_z_cm"),Rows.IsEmpty() ? 0.:MeanZ/Rows.Num());
    Result->SetArrayField(TEXT("bounds_min_cm"),VectorJson(Bounds.IsValid ? Bounds.Min:FVector::ZeroVector));
    Result->SetArrayField(TEXT("bounds_max_cm"),VectorJson(Bounds.IsValid ? Bounds.Max:FVector::ZeroVector));
    Result->SetArrayField(TEXT("puffs"),Rows);
    auto Budget=MakeShared<FJsonObject>();
    Budget->SetNumberField(TEXT("draw_components"),1);
    Budget->SetNumberField(TEXT("instance_capacity"),MaxParticles);
    Budget->SetNumberField(TEXT("custom_data_floats"),3);
    Budget->SetNumberField(TEXT("simulation_hz"),SimulationHz);
    Budget->SetNumberField(TEXT("collision_queries"),0);
    Budget->SetBoolField(TEXT("cpu_volume_solver"),false);
    Result->SetObjectField(TEXT("budget"),Budget);
    auto Modality=MakeShared<FJsonObject>();
    Modality->SetBoolField(TEXT("visible"),true);
    Modality->SetBoolField(TEXT("nir"),true);
    Modality->SetBoolField(TEXT("thermal"),false);
    Modality->SetBoolField(TEXT("custom_depth"),Instances && Instances->bRenderCustomDepth);
    Modality->SetBoolField(TEXT("translucency_before_dof"),true);
    Modality->SetBoolField(TEXT("shadows"),Instances && Instances->CastShadow);
    Modality->SetBoolField(TEXT("indirect_lighting"),false);
    Result->SetObjectField(TEXT("modality"),Modality);
    return Result;
}
