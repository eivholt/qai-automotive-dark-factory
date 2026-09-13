#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingSmoke.generated.h"

class ADarkFactoryWeldingCell;
class ADarkFactoryWeldingSparks;
class UInstancedStaticMeshComponent;
class FJsonObject;
struct FDarkFactoryWeldPulseEmitter;

/** Bounded optical smoke shared by visible/NIR views; no thermal gas or volume simulation. */
UCLASS()
class DARKFACTORY_API ADarkFactoryWeldingSmoke : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryWeldingSmoke();
    bool Initialize(ADarkFactoryWeldingCell* Cell,ADarkFactoryWeldingSparks* Sparks);
    virtual void Tick(float DeltaSeconds) override;
    void SetEffectsEnabled(bool Enabled);
    bool AreEffectsEnabled() const;
    void ResetParticles();
    void AdvanceSimulation(float Seconds,bool Emit=true);
    /** Draw-only QA toggle. Particle state, age and random sequence are unchanged. */
    void SetRenderingEnabled(bool Enabled);
    bool IsRenderingEnabled() const { return bRenderingEnabled; }
    int32 GetAliveCount() const;
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    enum { MaxParticles=128, EmitterCount=4, SimulationHz=60 };
    struct FPuff {
        FVector Origin=FVector::ZeroVector,InitialDrift=FVector::ZeroVector;
        float Age=0,Lifetime=3,InitialRadius=5,Growth=7,RiseSpeed=28,CurlRadius=9;
        float Seed=0,Density=1;
        int32 Id=0,EmitterIndex=0,PulseIndex=0;
        bool Alive=false;
    };
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Instances;
    TWeakObjectPtr<ADarkFactoryWeldingCell> SourceCell;
    TWeakObjectPtr<ADarkFactoryWeldingSparks> SourceSparks;
    TArray<FPuff> Puffs;
    FRandomStream Random;
    float SpawnAccumulator[EmitterCount]={};
    double LastSourceSeconds=0,SimulationSeconds=0,FixedRemainder=0;
    int32 Spawned=0,Expired=0,Dropped=0,Cleared=0,PeakAlive=0;
    bool bInitialized=false,bEffectsEnabled=true,bRenderingEnabled=true;
    bool bLastSourceEnabled=true;
    void Spawn(const FDarkFactoryWeldPulseEmitter& Emitter);
    void ClearParticles();
    void UpdateRendering();
    FVector Position(const FPuff& Puff) const;
    float Radius(const FPuff& Puff) const;
    float Opacity(const FPuff& Puff) const;
};
