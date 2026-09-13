#include "DarkFactoryWeldingSparks.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace {
constexpr float FountainRatePerEmitter=240.f, FalloutRatePerEmitter=100.f;
constexpr int32 BurstParticlesPerEvent=64;
constexpr double PulsePeriod=1.05, PulseStagger=.11;
constexpr float BurstEnd=.10f, FountainEnd=.42f, FalloutEnd=.82f;
// A fixed, bounded auxiliary light budget; more fragments do not multiply it.
constexpr float ResidualLightLumensPerWeight=.6f, MaximumResidualLumens=180.f;
constexpr float Temperatures[]={500,300,180,130,100,80,65,52,42,34,27,22};
constexpr const TCHAR* ModeNames[]={TEXT("burst"),TEXT("fountain"),TEXT("fallout")};
float SparkSmooth01(float X) { const float S=FMath::Clamp(X,0.f,1.f); return S*S*(3.f-2.f*S); }
const TCHAR* PulsePhase(const FDarkFactoryWeldPulseEmitter& E) {
    if (E.PulseIndex<0) return TEXT("stagger_wait");
    if (E.PulseAgeSeconds<BurstEnd) return ModeNames[0];
    if (E.PulseAgeSeconds<FountainEnd) return ModeNames[1];
    if (E.PulseAgeSeconds<FalloutEnd) return ModeNames[2];
    return TEXT("gap");
}
TArray<TSharedPtr<FJsonValue>> SparkVectorJson(const FVector& P) {
    return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
}
// Stable within a native run and independent of rendering/counter changes. Hash
// explicit scalar bytes, never padding or UObject addresses.
void HashBytes(uint64& H,const void* Data,SIZE_T Bytes) {
    const auto* P=static_cast<const uint8*>(Data);
    for (SIZE_T I=0;I<Bytes;++I) { H^=P[I]; H*=1099511628211ull; }
}
FTransform Hidden() { return FTransform(FQuat::Identity,FVector(0,0,-10000),FVector(.00001)); }
}
ADarkFactoryWeldingSparks::ADarkFactoryWeldingSparks() {
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("SparkRoot")); SetRootComponent(SceneRoot);
}
bool ADarkFactoryWeldingSparks::Initialize(ADarkFactoryWeldingCell* Cell) {
    if (!Cell || SourceCell.IsValid()) return false;
    auto* ParticleMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* ArcMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* ParticleMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingParticle.M_WeldingParticle"));
    auto* ArcMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingArc.M_WeldingArc"));
    if (!ParticleMesh || !ArcMesh || !ParticleMaterial || !ArcMaterial) { UE_LOG(LogTemp,Error,TEXT("DF_SPARKS_MISSING_CONTENT run setup_welding_effects.py")); return false; }
    SourceCell=Cell; AddTickPrerequisiteActor(Cell);
    for (int32 B=0;B<BinCount;++B) {
        auto* Instances=NewObject<UInstancedStaticMeshComponent>(this,*FString::Printf(TEXT("SparkTemperature%d"),B));
        Instances->SetupAttachment(SceneRoot); Instances->SetMobility(EComponentMobility::Movable);
        Instances->SetStaticMesh(ParticleMesh); Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Instances->SetCanEverAffectNavigation(false); Instances->SetCastShadow(false);
        Instances->SetLightingChannels(true,true,false); Instances->SetNumCustomDataFloats(1);
        auto* Material=UMaterialInstanceDynamic::Create(ParticleMaterial,this);
        const float Cold=FMath::Clamp(B/9.f,0.f,1.f);
        Material->SetVectorParameterValue(TEXT("SparkColor"),FLinearColor(1.f,FMath::Lerp(.8f,.10f,Cold),FMath::Lerp(.4f,.006f,Cold)));
        Material->SetScalarParameterValue(TEXT("EmissiveStrength"),90.f);
        Instances->SetMaterial(0,Material); Instances->RegisterComponent(); Bins.Add(Instances);
        FDarkFactoryImportedThermalSurface Surface; Surface.Mesh=Instances; Surface.TemperatureC=Temperatures[B]; Surface.Emissivity=.95f;
        ThermalSurfaces.Add(Surface);
    }
    for (int32 I=0;I<EmitterCount;++I) {
        auto* Arc=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("WeldingArc%d"),I));
        Arc->SetupAttachment(SceneRoot); Arc->SetMobility(EComponentMobility::Movable); Arc->SetStaticMesh(ArcMesh);
        Arc->SetCollisionEnabled(ECollisionEnabled::NoCollision); Arc->SetCastShadow(false); Arc->SetLightingChannels(true,true,false);
        Arc->SetMaterial(0,ArcMaterial); Arc->SetRelativeScale3D(FVector(.035)); Arc->SetVisibility(false); Arc->RegisterComponent(); Arcs.Add(Arc);
        FDarkFactoryImportedThermalSurface Surface; Surface.Mesh=Arc; Surface.TemperatureC=500; Surface.Emissivity=.98f; ThermalSurfaces.Add(Surface);
        auto* Light=NewObject<UPointLightComponent>(this,*FString::Printf(TEXT("WeldFlash%d"),I));
        Light->SetupAttachment(SceneRoot); Light->SetMobility(EComponentMobility::Movable); Light->SetIntensityUnits(ELightUnits::Lumens);
        Light->ViewLightingChannels.bViewChannel0=true; Light->ViewLightingChannels.bViewChannel1=true; Light->ViewLightingChannels.bViewChannel2=true;
        Light->SetLightColor(FLinearColor(.68f,.82f,1.f)); Light->SetAttenuationRadius(230); Light->SetSourceRadius(3);
        Light->SetCastShadows(false); Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
        Light->SetAffectGlobalIllumination(false); Light->SetAffectReflection(false); Light->SetIntensity(0); Light->RegisterComponent(); Lights.Add(Light);
    }
    Particles.SetNum(MaxParticles); ResetParticles();
    return true;
}
TArray<FDarkFactoryWeldPulseEmitter> ADarkFactoryWeldingSparks::GetPulseEmitters() const {
    TArray<FDarkFactoryWeldPulseEmitter> Result;
    if (!bEffectsEnabled || !SourceCell.IsValid()) return Result;
    const auto Emitters=SourceCell->GetWeldEmitters();
    for (const auto& Source:Emitters) {
        const int32 I=Source.EmitterIndex;
        if (I<0 || I>=EmitterCount) continue;
        FDarkFactoryWeldPulseEmitter E;
        E.Id=Source.Id; E.EmitterIndex=I; E.Position=Source.SurfacePosition;
        E.Normal=Source.SurfaceNormal.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector); E.SourceTool=Source.SourceTool;
        E.SourceIntensity=FMath::IsFinite(Source.Intensity) ? FMath::Clamp(Source.Intensity,0.f,1.f):0.f;
        // Real robots start on their independent seeded clock. Retain the old
        // phase offset only for the explicitly selected canonical QA fixture.
        const double Elapsed=Source.WeldAgeSeconds-(SourceCell->IsRobotTimingRandomized() ? 0.:PulseStagger*I);
        if (Elapsed>=0) {
            E.PulseIndex=FMath::FloorToInt(Elapsed/PulsePeriod);
            E.PulseAgeSeconds=static_cast<float>(Elapsed-E.PulseIndex*PulsePeriod);
            const float Age=E.PulseAgeSeconds;
            if (Age<BurstEnd) E.BurstWeight=1.f-SparkSmooth01(Age/BurstEnd);
            else if (Age<FountainEnd) E.FountainWeight=FMath::Sin(PI*(Age-BurstEnd)/(FountainEnd-BurstEnd));
            else if (Age<FalloutEnd) E.FalloutWeight=1.f-SparkSmooth01((Age-FountainEnd)/(FalloutEnd-FountainEnd));
            E.SmokeWeight=E.SourceIntensity*(.9f*E.BurstWeight+.55f*E.FountainWeight+.22f*E.FalloutWeight+.08f*FMath::Exp(-Age/.5f));
            E.ArcIntensity=E.SourceIntensity*(.1f+.9f*FMath::Max(E.BurstWeight,.8f*E.FountainWeight));
        }
        Result.Add(E);
    }
    return Result;
}
void ADarkFactoryWeldingSparks::ResetParticles() {
    for (auto& P:Particles) P=FSpark();
    FreeParticles.Reset(MaxParticles);
    for (int32 I=Particles.Num()-1;I>=0;--I) FreeParticles.Add(I);
    for (float& A:SpawnAccumulator) A=0;
    for (int32& Pulse:LastBurstPulse) Pulse=-1;
    for (auto& Stats:ModeStats) Stats=FModeStats();
    BurstEvents=PeakSpawnAttemptsPerStep=0;
    Random.Initialize(73129); CollisionCount=RestCount=SpawnedCount=DroppedCount=PenetratingContacts=0; FixedRemainder=RestAgeTotal=0;
    PeakAlive=PeakAirborne=PeakSweepsPerStep=PeakStepsPerAdvance=PeakLiveStepsPerTick=0;
    PhysicsSteps=CollisionSweeps=LiveTimingSamples=0;
    LivePhysicsCpuMsTotal=LiveRenderingCpuMsTotal=LiveTotalCpuMsTotal=0;
    LivePhysicsCpuMsMax=LiveRenderingCpuMsMax=LiveTotalCpuMsMax=0;
    LastCycle=SourceCell.IsValid() ? SourceCell->GetCycleIndex():-1; UpdateRendering();
}
void ADarkFactoryWeldingSparks::Spawn(const FDarkFactoryWeldPulseEmitter& Emitter,ESparkMode Mode) {
    if (FreeParticles.IsEmpty()) { ++DroppedCount; return; }
    const int32 Slot=FreeParticles.Pop(EAllowShrinking::No);
    FSpark& P=Particles[Slot]; P=FSpark(); P.Alive=true; P.Emitter=Emitter.EmitterIndex; P.Mode=Mode;
    P.Radius=Random.FRandRange(.12f,.2f); P.GlowRadius=Random.FRandRange(.8f,1.3f);
    P.SourceTool=Emitter.SourceTool.Get();
    const FVector Normal=Emitter.Normal;
    P.Position=Emitter.Position+Normal*(P.Radius+.25f); P.Normal=Normal;
    const float Azimuth=Random.FRand()*2*PI;
    const FVector Radial(FMath::Cos(Azimuth),FMath::Sin(Azimuth),0);
    float Speed=0; FVector Direction;
    switch (Mode) {
    case ESparkMode::Burst:
        Speed=Random.FRandRange(900,1800);
        Direction=Radial*Random.FRandRange(.55f,1.f)+FVector::UpVector*Random.FRandRange(.45f,1.f);
        P.Lifetime=Random.FRandRange(2.8f,3.7f); P.Drag=.30f; P.CoolingTau=.60f; P.InitialTemperatureRise=1400.f;
        P.Brightness=1.4f; P.MaxStreakLength=24.f; P.FadeRate=2.1f;
        break;
    case ESparkMode::Fountain:
        Speed=Random.FRandRange(450,1000);
        Direction=Radial*Random.FRandRange(.25f,.6f)+FVector::UpVector*Random.FRandRange(.8f,1.1f);
        P.Lifetime=Random.FRandRange(3.3f,4.4f); P.Drag=.50f; P.CoolingTau=.80f; P.InitialTemperatureRise=950.f;
        P.Brightness=1.f; P.MaxStreakLength=18.f; P.FadeRate=1.9f;
        break;
    default:
        Speed=Random.FRandRange(100,400);
        Direction=Radial*Random.FRandRange(.3f,.8f)-FVector::UpVector*Random.FRandRange(.4f,1.f);
        P.Lifetime=Random.FRandRange(3.8f,4.8f); P.Drag=.80f; P.CoolingTau=1.05f; P.InitialTemperatureRise=650.f;
        P.Brightness=.8f; P.MaxStreakLength=12.f; P.FadeRate=1.6f;
        break;
    }
    // Keep the authored jet in the contacted surface's outward hemisphere. Its
    // final speed remains in the declared range even after this projection.
    const double NormalPart=FVector::DotProduct(Direction,Normal);
    if (NormalPart<.12) Direction+=Normal*(.12-NormalPart);
    P.Velocity=Direction.GetSafeNormal(UE_SMALL_NUMBER,Normal)*Speed;
    auto& Stats=ModeStats[static_cast<int32>(Mode)]; ++Stats.Spawned;
    Stats.InitialSpeedTotal+=Speed; Stats.InitialUpwardTotal+=P.Velocity.Z;
    Stats.InitialOutwardTotal+=FVector::DotProduct(P.Velocity,Normal);
    Stats.MinimumInitialSpeed=FMath::Min(Stats.MinimumInitialSpeed,Speed); Stats.MaximumInitialSpeed=FMath::Max(Stats.MaximumInitialSpeed,Speed);
    ++SpawnedCount;
}
void ADarkFactoryWeldingSparks::Step(float Dt,bool Emit) {
    const int32 AttemptsBefore=SpawnedCount+DroppedCount;
    if (Emit && bEffectsEnabled) for (const auto& E:GetPulseEmitters()) {
        const int32 I=E.EmitterIndex;
        if (E.PulseIndex<0 || E.SourceIntensity<=.01f) continue;
        if (E.PulseAgeSeconds<BurstEnd && LastBurstPulse[I]!=E.PulseIndex) {
            LastBurstPulse[I]=E.PulseIndex; ++BurstEvents;
            for (int32 Grain=0;Grain<BurstParticlesPerEvent;++Grain) Spawn(E,ESparkMode::Burst);
        }
        const float Rate=FountainRatePerEmitter*E.FountainWeight+FalloutRatePerEmitter*E.FalloutWeight;
        SpawnAccumulator[I]+=Dt*Rate*E.SourceIntensity;
        while (SpawnAccumulator[I]>=1.f) {
            Spawn(E,E.FountainWeight>0 ? ESparkMode::Fountain:ESparkMode::Fallout);
            SpawnAccumulator[I]-=1.f;
        }
    }
    PeakSpawnAttemptsPerStep=FMath::Max(PeakSpawnAttemptsPerStep,SpawnedCount+DroppedCount-AttemptsBefore);
    int32 SweepsThisStep=0;
    ++PhysicsSteps;
    for (int32 Slot=0;Slot<Particles.Num();++Slot) {
        auto& P=Particles[Slot]; if (!P.Alive) continue;
        P.Age+=Dt; if (P.Age>=P.Lifetime) { P.Alive=false; FreeParticles.Add(Slot); continue; }
        if (P.Resting) {
            if (auto* Surface=P.RestSurface.Get()) P.Position=Surface->GetComponentTransform().TransformPosition(P.RestLocal);
            continue;
        }
        P.Velocity.Z-=980.f*Dt; P.Velocity*=FMath::Exp(-P.Drag*Dt);
        const FVector End=P.Position+P.Velocity*Dt;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WeldingSparkCollision),true,this);
        // The fragment is born inside the electrode/contact envelope. Ignore only
        // its emitting tool for 100 ms; all car and surrounding surfaces still collide.
        if (P.Age<.1f && P.SourceTool.IsValid()) Query.AddIgnoredComponent(P.SourceTool.Get());
        ++SweepsThisStep;
        if (GetWorld()->SweepSingleByChannel(Hit,P.Position,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(P.Radius),Query)) {
            ++CollisionCount; ++P.Bounces; PenetratingContacts+=Hit.bStartPenetrating;
            const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
            P.Position=Hit.ImpactPoint+Normal*(P.Radius+.15f); P.Normal=Normal;
            const float NormalSpeed=FVector::DotProduct(P.Velocity,Normal);
            const FVector Tangential=P.Velocity-Normal*NormalSpeed;
            P.Velocity=Tangential*.56f-Normal*NormalSpeed*.33f;
            if (Normal.Z>.35f && (P.Velocity.Size()<75.f || P.Bounces>=3)) {
                P.Resting=true; P.Velocity=FVector::ZeroVector; P.RestSurface=Hit.GetComponent(); ++RestCount; RestAgeTotal+=P.Age;
                if (auto* Surface=P.RestSurface.Get()) P.RestLocal=Surface->GetComponentTransform().InverseTransformPosition(P.Position);
            }
        } else P.Position=End;
        if (P.Position.Z < -150) { P.Alive=false; FreeParticles.Add(Slot); }
    }
    CollisionSweeps+=SweepsThisStep;
    PeakSweepsPerStep=FMath::Max(PeakSweepsPerStep,SweepsThisStep);
    int32 Alive=0, Airborne=0;
    for (const auto& P:Particles) { Alive+=P.Alive; Airborne+=P.Alive && !P.Resting; }
    PeakAlive=FMath::Max(PeakAlive,Alive); PeakAirborne=FMath::Max(PeakAirborne,Airborne);
}
void ADarkFactoryWeldingSparks::AdvanceSimulation(float Seconds,bool Emit) {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_WeldingParticles);
    if (!SourceCell.IsValid() || !FMath::IsFinite(Seconds) || Seconds<0) return;
    const double PhysicsStarted=bRecordLiveTiming ? FPlatformTime::Seconds():0;
    FixedRemainder+=FMath::Min(Seconds,5.f);
    constexpr double StepSeconds=1./PhysicsHz;
    int32 AdvanceSteps=0;
    while (FixedRemainder>=StepSeconds) { Step(static_cast<float>(StepSeconds),Emit); FixedRemainder-=StepSeconds; ++AdvanceSteps; }
    PeakStepsPerAdvance=FMath::Max(PeakStepsPerAdvance,AdvanceSteps);
    const double RenderingStarted=bRecordLiveTiming ? FPlatformTime::Seconds():0;
    UpdateRendering();
    if (bRecordLiveTiming) {
        const double Finished=FPlatformTime::Seconds();
        const double PhysicsMs=(RenderingStarted-PhysicsStarted)*1000;
        const double RenderingMs=(Finished-RenderingStarted)*1000;
        const double TotalMs=(Finished-PhysicsStarted)*1000;
        ++LiveTimingSamples;
        LivePhysicsCpuMsTotal+=PhysicsMs; LiveRenderingCpuMsTotal+=RenderingMs; LiveTotalCpuMsTotal+=TotalMs;
        LivePhysicsCpuMsMax=FMath::Max(LivePhysicsCpuMsMax,PhysicsMs);
        LiveRenderingCpuMsMax=FMath::Max(LiveRenderingCpuMsMax,RenderingMs);
        LiveTotalCpuMsMax=FMath::Max(LiveTotalCpuMsMax,TotalMs);
        PeakLiveStepsPerTick=FMath::Max(PeakLiveStepsPerTick,AdvanceSteps);
    }
}
void ADarkFactoryWeldingSparks::UpdateRendering() {
    if (Bins.Num()!=BinCount) return;
    TArray<FTransform> Transforms[BinCount]; TArray<float> Fades[BinCount];
    FVector GlowPositions[EmitterCount]={}; float GlowWeights[EmitterCount]={};
    for (const auto& P:Particles) if (P.Alive && bEffectsEnabled) {
        const float Temp=20.f+FMath::Min(480.f,P.InitialTemperatureRise*FMath::Exp(-P.Age/P.CoolingTau)); int32 Bin=BinCount-1;
        for (int32 B=0;B<BinCount-1;++B) if (Temp>=(Temperatures[B]+Temperatures[B+1])*.5f) { Bin=B; break; }
        const float Fade=P.Brightness*FMath::Exp(-FMath::Max(0.f,P.Age-.5f)*P.FadeRate)*FMath::Clamp((P.Lifetime-P.Age)*3.f,0.f,1.f);
        const float Length=P.Resting ? P.GlowRadius*2: FMath::Clamp(static_cast<float>(P.Velocity.Size())*.02f,P.GlowRadius*2,P.MaxStreakLength);
        const FQuat Rotation=P.Resting ? FQuat::Identity:P.Velocity.GetSafeNormal().Rotation().Quaternion();
        Transforms[Bin].Add(FTransform(Rotation,P.Position,FVector(Length,P.GlowRadius*2,P.GlowRadius*2)/100.f)); Fades[Bin].Add(Fade);
        GlowPositions[P.Emitter]+=P.Position*Fade; GlowWeights[P.Emitter]+=Fade;
    }
    for (int32 B=0;B<BinCount;++B) {
        auto* Mesh=Bins[B].Get(); const int32 VisibleCount=Transforms[B].Num();
        while (Mesh->GetInstanceCount()<VisibleCount) Mesh->AddInstance(Hidden(),true);
        while (Transforms[B].Num()<Mesh->GetInstanceCount()) { Transforms[B].Add(Hidden()); Fades[B].Add(0); }
        for (int32 I=0;I<Fades[B].Num();++I) Mesh->SetCustomDataValue(I,0,Fades[B][I],false);
        if (!Transforms[B].IsEmpty()) Mesh->BatchUpdateInstancesTransforms(0,Transforms[B],true,true,true);
    }
    const auto Emitters=GetPulseEmitters();
    const FDarkFactoryWeldPulseEmitter* ByRobot[EmitterCount]={};
    for (const auto& E:Emitters) if (E.EmitterIndex>=0 && E.EmitterIndex<EmitterCount) ByRobot[E.EmitterIndex]=&E;
    for (int32 I=0;I<EmitterCount;++I) {
        const auto* E=ByRobot[I];
        const bool Active=bEffectsEnabled && E && E->ArcIntensity>.01f;
        Arcs[I]->SetVisibility(Active);
        if (Active) {
            Arcs[I]->SetWorldLocation(E->Position+E->Normal); Arcs[I]->SetWorldScale3D(FVector(.025f+.04f*E->ArcIntensity));
            Lights[I]->SetWorldLocation(E->Position+E->Normal*5); Lights[I]->SetIntensity(1250.f*E->ArcIntensity);
        } else if (GlowWeights[I]>.02f) { Lights[I]->SetWorldLocation(GlowPositions[I]/GlowWeights[I]+FVector(0,0,10)); Lights[I]->SetIntensity(FMath::Min(MaximumResidualLumens,GlowWeights[I]*ResidualLightLumensPerWeight)); }
        else Lights[I]->SetIntensity(0);
    }
}
void ADarkFactoryWeldingSparks::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!SourceCell.IsValid()) return;
    if (LastCycle!=SourceCell->GetCycleIndex()) ResetParticles();
    if (bEffectsEnabled && SourceCell->IsAnimating()) {
        // Measure only the live tick: frozen QA advances (including five-second aging)
        // must not be presented as interactive CPU frame costs.
        bRecordLiveTiming=true;
        AdvanceSimulation(FMath::Clamp(DeltaSeconds,0.f,.1f));
        bRecordLiveTiming=false;
    }
}
int32 ADarkFactoryWeldingSparks::GetAliveCount() const { int32 Count=0; for (const auto& P:Particles) Count+=P.Alive; return Count; }
TSharedRef<FJsonObject> ADarkFactoryWeldingSparks::BuildMetadata() const {
    auto Meta=MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("model"),TEXT("bounded-swept-world-particles-v3"));
    Meta->SetNumberField(TEXT("alive"),GetAliveCount()); Meta->SetNumberField(TEXT("capacity"),MaxParticles);
    Meta->SetNumberField(TEXT("spawned"),SpawnedCount); Meta->SetNumberField(TEXT("capacity_drops"),DroppedCount);
    Meta->SetNumberField(TEXT("spawn_attempts"),static_cast<double>(SpawnedCount)+DroppedCount);
    Meta->SetNumberField(TEXT("emitter_count"),EmitterCount);
    Meta->SetStringField(TEXT("profile"),TEXT("cinematic-expulsion-v1"));
    Meta->SetBoolField(TEXT("effects_enabled"),bEffectsEnabled);
    Meta->SetNumberField(TEXT("maximum_continuous_emission_rate_per_emitter_per_second"),FountainRatePerEmitter);
    Meta->SetNumberField(TEXT("fountain_rate_per_emitter_per_second"),FountainRatePerEmitter);
    Meta->SetNumberField(TEXT("fallout_rate_per_emitter_per_second"),FalloutRatePerEmitter);
    Meta->SetNumberField(TEXT("burst_particles_per_emitter_event"),BurstParticlesPerEvent);
    Meta->SetNumberField(TEXT("burst_events"),BurstEvents);
    Meta->SetNumberField(TEXT("pulse_period_seconds"),PulsePeriod); Meta->SetNumberField(TEXT("pulse_stagger_seconds"),SourceCell.IsValid() && SourceCell->IsRobotTimingRandomized() ? 0.:PulseStagger);
    Meta->SetNumberField(TEXT("burst_duration_seconds"),BurstEnd); Meta->SetNumberField(TEXT("fountain_end_seconds"),FountainEnd);
    Meta->SetNumberField(TEXT("fallout_end_seconds"),FalloutEnd); Meta->SetNumberField(TEXT("maximum_lifetime_seconds"),4.8);
    Meta->SetStringField(TEXT("clock"),TEXT("independent robot weld age; fixed pulse offset only in canonical QA; pause freezes live advancement; explicit offline advances age grains against a fixed envelope"));
    Meta->SetStringField(TEXT("scope"),TEXT("authored cinematic expulsion, not a normal-weld quality label or calibrated resistance-welding material model"));
    TArray<TSharedPtr<FJsonValue>> Pulses;
    for (const auto& E:GetPulseEmitters()) {
        auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"),E.Id); O->SetNumberField(TEXT("emitter_index"),E.EmitterIndex);
        O->SetNumberField(TEXT("pulse_index"),E.PulseIndex); O->SetNumberField(TEXT("pulse_age_seconds"),E.PulseAgeSeconds);
        O->SetStringField(TEXT("phase"),PulsePhase(E)); O->SetNumberField(TEXT("burst_weight"),E.BurstWeight);
        O->SetNumberField(TEXT("fountain_weight"),E.FountainWeight); O->SetNumberField(TEXT("fallout_weight"),E.FalloutWeight);
        O->SetNumberField(TEXT("smoke_weight"),E.SmokeWeight); O->SetNumberField(TEXT("arc_intensity"),E.ArcIntensity);
        O->SetArrayField(TEXT("position_cm"),SparkVectorJson(E.Position)); O->SetArrayField(TEXT("normal"),SparkVectorJson(E.Normal));
        Pulses.Add(MakeShared<FJsonValueObject>(O));
    }
    Meta->SetArrayField(TEXT("pulse_emitters"),Pulses);
    Meta->SetNumberField(TEXT("peak_alive"),PeakAlive); Meta->SetNumberField(TEXT("peak_airborne"),PeakAirborne);
    Meta->SetNumberField(TEXT("collisions"),CollisionCount); Meta->SetNumberField(TEXT("rest_events"),RestCount);
    int32 Airborne=0; for (const auto& P:Particles) Airborne+=P.Alive && !P.Resting;
    Meta->SetNumberField(TEXT("airborne"),Airborne);
    Meta->SetNumberField(TEXT("mean_rest_age_seconds"),RestCount ? RestAgeTotal/RestCount:0);
    Meta->SetNumberField(TEXT("initial_overlap_contacts"),PenetratingContacts);
    auto Budget=MakeShared<FJsonObject>();
    Budget->SetNumberField(TEXT("fixed_step_hz"),PhysicsHz);
    Budget->SetNumberField(TEXT("maximum_sweeps_per_step"),MaxParticles);
    // At most one burst plus ceil(240/90) accumulated continuous grains per emitter.
    Budget->SetNumberField(TEXT("maximum_spawn_per_step"),EmitterCount*(BurstParticlesPerEvent+3));
    Budget->SetNumberField(TEXT("peak_spawn_attempts_per_step"),PeakSpawnAttemptsPerStep);
    Budget->SetNumberField(TEXT("maximum_sweeps_per_simulated_second"),MaxParticles*PhysicsHz);
    Budget->SetNumberField(TEXT("steps"),static_cast<double>(PhysicsSteps));
    Budget->SetNumberField(TEXT("collision_sweeps"),static_cast<double>(CollisionSweeps));
    Budget->SetNumberField(TEXT("peak_sweeps_per_step"),PeakSweepsPerStep);
    Budget->SetNumberField(TEXT("peak_steps_per_advance_including_offline_qa"),PeakStepsPerAdvance);
    Budget->SetNumberField(TEXT("peak_steps_per_live_tick"),PeakLiveStepsPerTick);
    int32 RenderSlots=0; for (const auto& Bin:Bins) if (Bin) RenderSlots+=Bin->GetInstanceCount();
    Budget->SetNumberField(TEXT("render_instance_slots"),RenderSlots);
    Budget->SetNumberField(TEXT("maximum_render_instance_slots"),BinCount*MaxParticles);
    Budget->SetStringField(TEXT("scope"),TEXT("90 Hz sphere sweeps for airborne grains; resting grains follow their contact surface; counters include offline QA advances"));
    Meta->SetObjectField(TEXT("physical_budget"),Budget);
    auto Timing=MakeShared<FJsonObject>();
    Timing->SetNumberField(TEXT("samples"),static_cast<double>(LiveTimingSamples));
    Timing->SetNumberField(TEXT("physics_mean_ms"),LiveTimingSamples ? LivePhysicsCpuMsTotal/LiveTimingSamples:0);
    Timing->SetNumberField(TEXT("physics_max_ms"),LivePhysicsCpuMsMax);
    Timing->SetNumberField(TEXT("update_rendering_mean_ms"),LiveTimingSamples ? LiveRenderingCpuMsTotal/LiveTimingSamples:0);
    Timing->SetNumberField(TEXT("update_rendering_max_ms"),LiveRenderingCpuMsMax);
    Timing->SetNumberField(TEXT("advance_total_mean_ms"),LiveTimingSamples ? LiveTotalCpuMsTotal/LiveTimingSamples:0);
    Timing->SetNumberField(TEXT("advance_total_max_ms"),LiveTotalCpuMsMax);
    Timing->SetStringField(TEXT("scope"),TEXT("FPlatformTime CPU elapsed time for live Tick AdvanceSimulation only; includes UpdateRendering; excludes reset, direct QA advances, GPU completion, and other actors; reset each cycle"));
    Meta->SetObjectField(TEXT("cpu_live_tick"),Timing);
    Meta->SetStringField(TEXT("collision_size"),TEXT("1.2-2 mm grain radius; larger GPU emissive glow for readability; source tool excluded for first 100 ms"));
    Meta->SetNumberField(TEXT("thermal_bins"),BinCount); Meta->SetNumberField(TEXT("cpu_image_readbacks"),0);
    Meta->SetStringField(TEXT("thermal"),TEXT("mode-specific authored exponential cooling shared by all views in 12 surface-profile buckets, clamped 500C maximum; display/model window may saturate"));
    TArray<TSharedPtr<FJsonValue>> Phases;
    int32 ModeAlive[ModeCount]={}, ModeResting[ModeCount]={};
    bool Finite=true; uint64 Signature=14695981039346656037ull;
    FBox Bounds(ForceInit);
    for (int32 Slot=0;Slot<Particles.Num();++Slot) {
        const auto& P=Particles[Slot]; if (!P.Alive) continue;
        const int32 M=static_cast<int32>(P.Mode); ++ModeAlive[M]; ModeResting[M]+=P.Resting;
        Finite&=!P.Position.ContainsNaN() && !P.Velocity.ContainsNaN() && FMath::IsFinite(P.Age) && FMath::IsFinite(P.Lifetime);
        Bounds+=P.Position;
        HashBytes(Signature,&Slot,sizeof(Slot));
        for (int32 Axis=0;Axis<3;++Axis) { const double V=P.Position[Axis], W=P.Velocity[Axis]; HashBytes(Signature,&V,sizeof(V)); HashBytes(Signature,&W,sizeof(W)); }
        HashBytes(Signature,&P.Age,sizeof(P.Age)); HashBytes(Signature,&P.Lifetime,sizeof(P.Lifetime));
        const uint8 Rest=P.Resting ? 1:0, Mode=static_cast<uint8>(P.Mode);
        HashBytes(Signature,&Rest,sizeof(Rest)); HashBytes(Signature,&Mode,sizeof(Mode));
    }
    for (int32 M=0;M<ModeCount;++M) {
        const auto& Stats=ModeStats[M]; auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("name"),ModeNames[M]);
        O->SetNumberField(TEXT("spawned"),Stats.Spawned); O->SetNumberField(TEXT("alive"),ModeAlive[M]); O->SetNumberField(TEXT("resting"),ModeResting[M]);
        O->SetNumberField(TEXT("mean_initial_speed_cm_s"),Stats.Spawned ? Stats.InitialSpeedTotal/Stats.Spawned:0);
        O->SetNumberField(TEXT("min_initial_speed_cm_s"),Stats.Spawned ? Stats.MinimumInitialSpeed:0);
        O->SetNumberField(TEXT("max_initial_speed_cm_s"),Stats.MaximumInitialSpeed);
        O->SetNumberField(TEXT("mean_initial_upward_speed_cm_s"),Stats.Spawned ? Stats.InitialUpwardTotal/Stats.Spawned:0);
        O->SetNumberField(TEXT("mean_initial_outward_speed_cm_s"),Stats.Spawned ? Stats.InitialOutwardTotal/Stats.Spawned:0);
        Phases.Add(MakeShared<FJsonValueObject>(O));
    }
    Meta->SetArrayField(TEXT("phase_counts"),Phases); Meta->SetBoolField(TEXT("finite_particle_state"),Finite);
    Meta->SetStringField(TEXT("particle_state_signature"),FString::Printf(TEXT("%016llx"),static_cast<unsigned long long>(Signature)));
    auto LiveBounds=MakeShared<FJsonObject>(); LiveBounds->SetBoolField(TEXT("valid"),Bounds.IsValid!=0);
    LiveBounds->SetArrayField(TEXT("minimum"),SparkVectorJson(Bounds.IsValid ? Bounds.Min:FVector::ZeroVector));
    LiveBounds->SetArrayField(TEXT("maximum"),SparkVectorJson(Bounds.IsValid ? Bounds.Max:FVector::ZeroVector));
    Meta->SetObjectField(TEXT("live_bounds_cm"),LiveBounds);
    return Meta;
}
