#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "DarkFactorySensorReadback.h"
#include "DarkFactoryHeatExchange.h"
#include "DarkFactoryThermalRig.generated.h"

class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UTexture2D;
class UMaterialInstanceDynamic;
class ADarkFactoryWorld;
class FJsonObject;

struct FDarkFactoryThermalSurface {
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    float BaseC = 24;
    float CurrentC = 24;
    float Emissivity = .9f;
    float ApparentC = 24; // Original uniform-profile value, retained for comparison.
    float CoreSurfaceC = 24;
    float EdgeSurfaceC = 24;
    float EdgeFraction = .22f;
    float EdgeFalloff = 2.4f;
    float CharacteristicLengthCm = 1;
    float CoolingRetention = 1;
    bool bPowered = false;
    float FaceDeltaC[6] = {};
    FBox LocalBounds;
    FTransform LastTransform;
    bool bTransformCached = false;
};

/** Temperature/emissivity driven LWIR proxy; entirely separate from NIR inference. */
UCLASS()
class DARKFACTORY_API ADarkFactoryThermalRig : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryThermalRig();
    void Initialize(ADarkFactoryWorld* Cell, USceneCaptureComponent2D* NIRCamera);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    int32 RegisterSurface(UStaticMeshComponent* Mesh, float TemperatureC, float Emissivity);
    void SetCase(int32 Case);
    void SetSpatialFieldsEnabled(bool Enabled);
    bool HasSpatialFields() const { return bSpatialFields; }
    void AdvanceCooling() { SetCoolingAge(CoolingAgeSeconds>=1800 ? 0:CoolingAgeSeconds+15); }
    void SetHeatTransferEnabled(bool Enabled);
    bool HasHeatTransfer() const { return bHeatTransferEnabled; }
    bool SaveTransferSmoke(const FString& Directory);
    void SetCoolingAge(float Seconds);
    float GetCoolingAge() const { return CoolingAgeSeconds; }
    bool SaveFieldSmoke(const FString& Directory);
    void NextCase() { SetCase(CaseIndex==3 ? 5:CaseIndex>=4 ? 0:CaseIndex+1); }
    FString GetCaseName() const;
    bool IsTransferCase() const { return CaseIndex==5; }
    void ApplyMaterial(UMaterialInstanceDynamic* Material, int32 Palette) const;
    void CaptureGPU();
    bool SaveRawForSmoke(const FString& Path);
    void RequestSnapshot();
    void NotifySceneChanged(); // Invalidates pending frames after shared geometry resets.
    void SetFrozen(bool Frozen) { bFrozen=Frozen; }
    bool IsReady() const { return ThermalMaterial && ThermalLUT && ThermalTarget; }
    UTextureRenderTarget2D* GetTarget() const { return CaptureRevision==Revision ? ThermalTarget.Get():nullptr; }
    int32 GetGPUCaptures() const { return GPUCaptures; }
    int32 GetReadbacks() const { return Readbacks; }
    int32 GetStaleSnapshotDiscards() const { return StaleSnapshotDiscards; }
    int32 GetProfileCount() const { return Surfaces.Num(); }
    TSharedRef<FJsonObject> GetEncodingMetadata() const;
    TSharedRef<FJsonObject> GetQARegions() const;
    float AmbientC = 24;
    float MinC = 20;
    float MaxC = 120;
    int32 DisplayPalette = 0;
    FString LastSnapshot;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> ThermalSensor;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> ThermalTarget;
    UPROPERTY() TObjectPtr<UTexture2D> ThermalLUT;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThermalMaterial;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> DepthTestCover;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> HeaterFaultStrip;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> GlueGapSegment;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> GlueIntactSegment;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> TargetModule;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> TransferSource;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> TransferReceiver;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> TransferBlocker;
    TArray<FDarkFactoryThermalSurface> Surfaces;
    FDarkFactoryHeatExchange HeatExchange;
    bool bHeatTransferEnabled = true;
    void ResetExchange();
    TArray<FLinearColor> LUTEntries;
    bool bSpatialFields = true;
    float CoolingAgeSeconds = 15;
    int32 LUTUploads = 0;
    TWeakObjectPtr<ADarkFactoryWorld> CellOwner;
    TSharedPtr<FJsonObject> PendingSnapshotMetadata; // Frozen on the game thread at capture.
    FDarkFactorySensorReadback Readback;
    TFuture<TArray<uint8>> EncodeFuture;
    int32 CaseIndex = 0;
    uint64 Revision = 0;
    uint64 CaptureRevision = 0;
    uint64 PendingRevision = 0;
    bool bSnapshotRequested = false;
    bool bFrozen = false;
    float CaptureHz = 10;
    double NextCapture = 0;
    int32 GPUCaptures = 0;
    int32 Readbacks = 0;
    int32 StaleSnapshotDiscards = 0;
    FString SnapshotRoot;
    void UpdateLUT();
    void UpdateTransforms(bool Force=false);
    void UpdateProfile(int32 Index);
    void UploadLUT();
    TSharedRef<FJsonObject> GetFieldQARegions() const;
    TSharedRef<FJsonObject> BuildSnapshotMetadata() const;
    void SaveEncodedSnapshot(TArray<uint8> Bytes, TSharedPtr<FJsonObject> Metadata);
    UStaticMeshComponent* Fixture(const TCHAR* Name, FVector Position, FVector Size, float TempC, float Emissivity, bool Registered=true);
};
