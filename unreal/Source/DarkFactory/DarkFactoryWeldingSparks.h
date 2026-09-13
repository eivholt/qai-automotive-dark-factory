#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.generated.h"
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UPrimitiveComponent;

/** Canonical weld-clock envelope shared by fragments, smoke and local arc lights. */
struct FDarkFactoryWeldPulseEmitter {
    FString Id;
    int32 EmitterIndex=0, PulseIndex=-1;
    FVector Position=FVector::ZeroVector, Normal=FVector::UpVector;
    TWeakObjectPtr<UStaticMeshComponent> SourceTool;
    float PulseAgeSeconds=0, BurstWeight=0, FountainWeight=0, FalloutWeight=0;
    float SmokeWeight=0, ArcIntensity=0, SourceIntensity=0;
};

/** Bounded physical particles, GPU-instanced rendering, one shared world simulation for all views. */
UCLASS()
class DARKFACTORY_API ADarkFactoryWeldingSparks : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryWeldingSparks();
    bool Initialize(ADarkFactoryWeldingCell* Cell);
    virtual void Tick(float DeltaSeconds) override;
    void ResetParticles();
    void SetEffectsEnabled(bool Enabled) { bEffectsEnabled=Enabled; UpdateRendering(); }
    bool AreEffectsEnabled() const { return bEffectsEnabled; }
    // Samples the source cell clock, including while paused; never advances simulation.
    TArray<FDarkFactoryWeldPulseEmitter> GetPulseEmitters() const;
    void AdvanceSimulation(float Seconds, bool Emit=true);
    const TArray<FDarkFactoryImportedThermalSurface>& GetThermalSurfaces() const { return ThermalSurfaces; }
    TSharedRef<FJsonObject> BuildMetadata() const;
    int32 GetAliveCount() const;
    int32 GetCollisionCount() const { return CollisionCount; }
    int32 GetRestCount() const { return RestCount; }
private:
    enum { MaxParticles=4096, BinCount=12, EmitterCount=4, PhysicsHz=90, ModeCount=3 };
    enum class ESparkMode : uint8 { Burst, Fountain, Fallout };
    struct FSpark {
        FVector Position=FVector::ZeroVector, Velocity=FVector::ZeroVector;
        FVector RestLocal=FVector::ZeroVector, Normal=FVector::UpVector;
        TWeakObjectPtr<UPrimitiveComponent> RestSurface, SourceTool;
        float Age=0, Lifetime=3.2f, Radius=.16f, GlowRadius=1.f;
        float Drag=.3f, CoolingTau=.65f, InitialTemperatureRise=1200.f;
        float Brightness=1.f, MaxStreakLength=18.f, FadeRate=2.f;
        int32 Bounces=0, Emitter=0;
        ESparkMode Mode=ESparkMode::Burst;
        bool Alive=false, Resting=false;
    };
    struct FModeStats {
        int32 Spawned=0;
        double InitialSpeedTotal=0, InitialUpwardTotal=0, InitialOutwardTotal=0;
        float MinimumInitialSpeed=MAX_flt, MaximumInitialSpeed=0;
    };
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Bins;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Arcs;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
    TWeakObjectPtr<ADarkFactoryWeldingCell> SourceCell;
    TArray<FDarkFactoryImportedThermalSurface> ThermalSurfaces;
    TArray<FSpark> Particles;
    TArray<int32> FreeParticles;
    FRandomStream Random;
    float SpawnAccumulator[EmitterCount]={};
    int32 LastBurstPulse[EmitterCount]={-1,-1,-1,-1};
    FModeStats ModeStats[ModeCount];
    int32 BurstEvents=0, PeakSpawnAttemptsPerStep=0;
    int32 CollisionCount=0, RestCount=0, SpawnedCount=0, DroppedCount=0, LastCycle=-1;
    bool bEffectsEnabled=true;
    double FixedRemainder=0, RestAgeTotal=0;
    int32 PenetratingContacts=0;
    int32 PeakAlive=0, PeakAirborne=0, PeakSweepsPerStep=0, PeakStepsPerAdvance=0, PeakLiveStepsPerTick=0;
    uint64 PhysicsSteps=0, CollisionSweeps=0, LiveTimingSamples=0;
    bool bRecordLiveTiming=false;
    double LivePhysicsCpuMsTotal=0, LiveRenderingCpuMsTotal=0, LiveTotalCpuMsTotal=0;
    double LivePhysicsCpuMsMax=0, LiveRenderingCpuMsMax=0, LiveTotalCpuMsMax=0;
    void Step(float Dt,bool Emit);
    void UpdateRendering();
    void Spawn(const FDarkFactoryWeldPulseEmitter& Emitter,ESparkMode Mode);
};
