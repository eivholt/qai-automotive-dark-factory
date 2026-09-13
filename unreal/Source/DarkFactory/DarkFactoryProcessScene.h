#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessScene.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UBoxComponent;
class ADarkFactoryThermalRig;
class UMaterialInstanceDynamic;

/** Process-only authored inspection parts. No ground-truth value is a model input. */
UCLASS()
class DARKFACTORY_API ADarkFactoryProcessScene : public AActor {
    GENERATED_BODY()
public:
    // Process transport clears the full curtain sweep before replacing a body.
    static constexpr double CarriageTravelCm=2050.;
    static constexpr double CarriageTravelSeconds=3.2;
    static constexpr double TunnelLengthCm=1400.;
    ADarkFactoryProcessScene();
    bool Initialize(ADarkFactoryWeldingCell* InCell,int32 Seed);
    // -1: seeded 1/3 anomaly; 0 seated; 1 loose; 2 fallen; 3 missing.
    void BeginBody(int32 BodyIndex,double ClockSeconds,int32 FaultOverride=-1,int32 DoorOverride=-1);
    void EndBody();
    // Explicit offline episode variation; interactive defaults are unchanged.
    void SetCaptureVariation(int32 Seed);
    // Explicit snapshots default to the settled inspection pose; live control supplies extension.
    void ApplyFrame(double PoseSeconds,double ClockSeconds,double CarriageOffsetCm,bool BodyVisible,bool PermitWelding,double InspectionExtension=1.);
    void BindThermalRig(ADarkFactoryThermalRig* Rig);
    const TArray<FDarkFactoryImportedThermalSurface>& GetThermalSurfaces() const { return ThermalSurfaces; }
    void GetDoorSensorView(int32 Side,FVector& Location,FRotator& Rotation,float& FOV) const;
    // A gimbal aims the actual R4-mounted lens; root settles/render-captures afterward.
    void GetInspectionCameraPose(int32 Spot,FVector& Location,FRotator& Rotation,float& FOV);
    void GetInspectionObserverView(FVector& Location,FRotator& Rotation) const;
    TSharedRef<FJsonObject> BuildMetadata() const;
    float GetMaximumAdhesiveC() const;
    float GetMaximumWeldC() const;
    float GetMaxAdhesiveTemperatureC() const { return GetMaximumAdhesiveC(); }
    float GetMaxWeldTemperatureC() const { return GetMaximumWeldC(); }
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<ADarkFactoryWeldingCell> Cell;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> DoorMeshes;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> DoorColliders;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AdhesiveMeshes;
    UPROPERTY() TObjectPtr<USceneComponent> CameraGimbal;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CameraHousing;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CameraLens;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DoorMaterial;
    FVector CaptureCameraOffsets[2]={FVector::ZeroVector,FVector::ZeroVector};
    FVector CaptureTargetOffsets[2]={FVector::ZeroVector,FVector::ZeroVector};
    float CaptureFovOffsets[2]={0,0};
    float CaptureMaterialGain=1;
    int32 CaptureVariationSeed=0;
    bool bCaptureVariation=false;
    TWeakObjectPtr<ADarkFactoryThermalRig> ThermalRig;
    TArray<FDarkFactoryImportedThermalSurface> ThermalSurfaces;
    TArray<FTransform> DoorHome;
    TArray<FVector> AdhesiveLocal,SpotLocal,SpotNormals;
    TArray<float> AdhesiveInitialC,AdhesiveTau,AdhesiveC;
    FVector FallenVelocity=FVector::ZeroVector;
    FRotator FallenRotation=FRotator::ZeroRotator;
    double BodyBirthSeconds=0,Clock=0,LastClock=0;
    int32 RandomSeed=42,BodySequence=0,Fault=0,FaultDoor=-1;
    bool bInitialized=false,bBodyActive=false,bFallenDetached=false,bFallenResting=false;
    void ResetDoors();
    void UpdateDoors(double DeltaSeconds,bool Visible);
    void UpdateAdhesive();
};
