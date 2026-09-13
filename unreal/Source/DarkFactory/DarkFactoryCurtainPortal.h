#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryCurtainPortal.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UPhysicsConstraintComponent;
class UMaterialInstanceDynamic;
class UPhysicalMaterial;
class FJsonObject;

/** Native Chaos strip-curtain portal. Local X spans the opening, +Y points into
 * the opaque tunnel, and +Z is up. Place/rotate the actor before Initialize;
 * keep actor scale one and do not relocate the assembled simulated portal.
 * Rail gauge/profile follow the imported welding lane; the actor origin is its
 * ground-tile surface datum. Geometry, masses and hinge damping are illustrative.
 */
UCLASS()
class DARKFACTORY_API ADarkFactoryCurtainPortal : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryCurtainPortal();
    bool Initialize(float OpeningWidthCm=420.f, float OpeningHeightCm=390.f,
        float TunnelLengthCm=1000.f, int32 StripCount=14, int32 SegmentsPerStrip=3);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetSimulationEnabled(bool Enabled);
    void ResetSimulation(); // Restores initial segment poses and clears contact/peak telemetry.
    bool IsSimulationEnabled() const { return bSimulationEnabled; }
    int32 GetDynamicBodyCount() const { return StripMeshes.Num(); }
    int32 GetConstraintCount() const { return Constraints.Num(); }
    int32 GetCarriageContactCount() const { return CarriageContactCount; }
    float GetCurrentDeflectionDegrees() const;
    float GetPeakDeflectionDegrees() const { return PeakDeflectionDegrees; }
    float GetMaximumAnchorErrorCm() const;
    bool HasFinitePhysicsState() const { return bFinitePhysicsState; }
    const TArray<TObjectPtr<UStaticMeshComponent>>& GetStripMeshes() const { return StripMeshes; }
    const TArray<FDarkFactoryImportedThermalSurface>& GetThermalSurfaces() const { return ThermalSurfaces; }
    TSharedRef<FJsonObject> BuildPhysicsMetadata() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> StaticMeshes;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> StripMeshes;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> Constraints;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UPhysicalMaterial> StripContactMaterial;
    TArray<FDarkFactoryImportedThermalSurface> ThermalSurfaces;
    TArray<FTransform> RestTransforms;
    TArray<FVector> PausedLinearVelocities,PausedAngularVelocities;
    TArray<int32> ShortenedStripIndices;
    struct FJointFrames {
        TWeakObjectPtr<UStaticMeshComponent> Parent, Child;
        FTransform ParentFrame, ChildFrame;
    };
    TArray<FJointFrames> JointFrames;
    bool bInitialized=false, bSimulationEnabled=false, bFinitePhysicsState=true;
    bool bPendingInitialSimulation=false;
    float WidthCm=420, HeightCm=390, TunnelCm=1000;
    int32 Strips=14, Segments=3, CarriageContactCount=0, RailCutStrips=0;
    float PeakDeflectionDegrees=0, PeakAnchorErrorCm=0, PeakLinearSpeedCmS=0;
    FString PeakAnchorSegment;
    TMap<FName,int32> ContactCounts;
    UStaticMeshComponent* Box(const FString& Name, const FVector& Center,
        const FVector& Size, int32 MaterialIndex, bool Simulated);
    void RestoreConstraints();
    UFUNCTION()
    void OnStripHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
};
