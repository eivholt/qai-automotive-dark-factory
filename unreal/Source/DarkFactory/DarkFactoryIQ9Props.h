#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryIQ9Props.generated.h"

class FJsonObject;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Prior local IQ9 EVK appearance reused as four attached visual props, not live EVK telemetry. */
UCLASS()
class DARKFACTORY_API ADarkFactoryIQ9Props : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryIQ9Props();
    bool Initialize(ADarkFactoryWeldingCell* Cell,const FString& ManifestPath=FString());
    virtual void Tick(float DeltaSeconds) override;
    void SetEnabled(bool Enabled);
    bool IsEnabled() const { return bEnabled; }
    /** Nonnegative pins only decorative LED time; negative releases to the cell clock. */
    void SetLedTimeForQA(double Seconds);
    const TArray<FDarkFactoryImportedThermalSurface>& GetThermalSurfaces() const { return ThermalSurfaces; }
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    struct FMount {
        FString Id,ParentName;
        TWeakObjectPtr<UStaticMeshComponent> Parent;
        TWeakObjectPtr<USceneComponent> Anchor;
        FVector Contact=FVector::ZeroVector,Normal=FVector::UpVector;
        int32 FirstLed=0;
    };
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Anchors;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LedMeshes;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> LedMaterials;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> LedLights;
    TWeakObjectPtr<ADarkFactoryWeldingCell> SourceCell;
    TArray<FMount> Mounts;
    TArray<FDarkFactoryImportedThermalSurface> ThermalSurfaces;
    TArray<bool> LedStates;
    bool bInitialized=false,bEnabled=true;
    double LedTimeOverride=-1,AppliedLedSeconds=0;
    FString SourceManifest;
    UStaticMeshComponent* AddMesh(const FString& Name,USceneComponent* Parent,UStaticMesh* Mesh,
        UMaterialInterface* Material,const FVector& Position,const FRotator& Rotation,const FVector& Scale,bool Shadow);
    void UpdateLeds();
};
