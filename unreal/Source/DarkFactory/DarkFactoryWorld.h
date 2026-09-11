#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IHttpRequest.h"
#include "Async/Future.h"
#include "DarkFactorySensorReadback.h"
#include "DarkFactoryWorld.generated.h"

class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UTexture2D;
class UMaterialInterface;
class UPointLightComponent;
class URectLightComponent;
class FJsonObject;
class ADarkFactoryThermalRig;

UCLASS()
class DARKFACTORY_API ADarkFactoryWorld : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryWorld();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetScenario(int32 Variant, bool bNewSeed = true);
    void Randomize();
    void ToggleInference();
    void SaveSample();
    void RequestSample();
    bool bSensorActive = true;
    bool bGreenDisplay = true;
    bool bInferenceEnabled = true;
    int32 Scenario = 0;
    FString Decision = TEXT("A");
    FString Source = TEXT("DISCONNECTED");
    FString Status = TEXT("Waiting for bridge");
    FString EpisodeId;
    FString LastSaved;
    float LatencyMs = 0;
    int32 Seed = 42;
    UTextureRenderTarget2D* GetSensorTexture() const { return SensorTarget; }
    ADarkFactoryThermalRig* GetThermalRig() const { return ThermalRig; }
    void SetRoofLightIntensity(float Lumens);
    float GetRoofLightIntensity() const { return RoofLightLumens; }
    FString GetScenarioName() const;
    FString GetTruthLabel() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<USceneComponent> TrayRoot;
    UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Modules;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Occluder;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RejectArm;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Sensor;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> SensorTarget;
    UPROPERTY() TObjectPtr<UPointLightComponent> IRLight;
    UPROPERTY() TArray<TObjectPtr<URectLightComponent>> RoofWindowLights;
    float RoofLightLumens = 80;
    UPROPERTY() TObjectPtr<ADarkFactoryThermalRig> ThermalRig;
    bool bModalityScreenshots = false;
    bool bRoofLightSensorParity = true;
    void CaptureModalitiesTick(double Now);
    bool bThermalSmoke = false;
    bool bThermalSmokePassed = true;
    bool bThermalPreviewGpuOnly = false;
    float ThermalSmokeIntensity = 0;
    float ThermalSmokeRoofLightIntensity = 0;
    int32 NIRInferenceRequests = 0;
    int32 ThermalSmokeNIRRequests = 0;
    int32 ThermalSmokeReadbackBaseline = 0;
    bool bThermalStalePreviewRejected = false;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PendingRequest;
    TArray<uint8> LatestPng;
    FDarkFactorySensorReadback SensorReadback;
    TFuture<TArray<uint8>> EncodeFuture;
    uint64 ReadbackSerial = 0;
    int32 ReadbackEpoch = 0;
    int32 ReadbackSeed = 0;
    int32 ReadbackFrame = 0;
    float ReadbackGain = 1;
    float ReadbackNoise = 0;
    double ReadbackStarted = 0;
    double LatestPngCapturedAt = 0;
    double PreviewCapturedAt = 0;
    int32 PreviewEpoch = -1;
    bool bReadbackForInference = false;
    bool bSaveRequested = false;
    float PreviewHz = 15;
    int32 GPUPreviewCaptures = 0;
    int32 AsyncReadbacks = 0;
    int32 SyncReadbacks = 0;
    int32 StaleReadbacksDiscarded = 0;
    bool bReadbackSmoke = false;
    bool bReadbackParityPassed = false;
    bool bReadbackSmokeNoCpuPreview = false;
    double ReadbackSmokeSyncMs = 0;
    double ReadbackSmokeEnqueueMs = 0;
    double ReadbackSmokeDeadline = 0;
    FString Endpoint = TEXT("http://127.0.0.1:18190/inspect");
    FString ActiveRequestId;
    FString LeakageGroup;
    FString DatasetSplit;
    FString ContractVersion;
    FString PromptSha256;
    FString OutputRoot;
    TArray<TSharedPtr<FJsonObject>> PlanRows;
    TSharedPtr<FJsonObject> CurrentPlanRow;
    bool bLastSaveSucceeded = false;
    int32 TargetModule = 1;
    float OcclusionFraction = 1;
    float ExposureEV = 0;
    FVector CameraPositionJitter = FVector::ZeroVector;
    FVector CameraRotationJitter = FVector::ZeroVector;
    int32 Epoch = 0;
    int32 CaptureIndex = 0;
    int32 DatasetCount = 0;
    int32 DatasetIndex = 0;
    int32 SmokeStep = 0;
    int32 BaseSeed = 42;
    bool bSmoke = false;
    bool bNightVisionSmoke = false;
    TArray<uint8> NightVisionFrozenPng;
    int32 NightVisionCaptureIndex = 0;
    bool bBridgeSmoke = false;
    bool bBridgeSmokeFinished = false;
    bool bBridgeSmokePassed = false;
    FString BridgeExpected = TEXT("G");
    double BridgeSmokeDeadline = 0;
    bool bResolved = false;
    double NextCapture = 0;
    double NextRequest = 0;
    double NextAutomation = 0;
    double RequestStarted = 0;
    double ResultAt = 0;
    double MaxResultAge = 20;
    float Gain = 1;
    float NoiseStd = 0.005f;
    float Reflectance = 0.6f;
    float Roughness = 0.5f;
    float Misalignment = 24;
    FVector TrayOffset = FVector::ZeroVector;
    float Travel = 0;
    UStaticMeshComponent* Box(const TCHAR* Name, FVector Position, FVector Size, float Value, USceneComponent* Parent = nullptr);
    void BuildCell();
    void CaptureSensor(); // Deterministic offline capture/calibration only.
    void ToggleSensorForCalibration();
    bool BeginSensorReadback(bool bForInference);
    void PollSensorReadback();
    void Inspect();
    void OnInspect(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FString RequestedEpisode, int32 RequestedEpoch, double Started);
    void Hold(const FString& Why);
    void ResetEpisode();
    void PrepareDatasetSample();
    bool LoadPlan(const FString& Path);
    void ApplyPlanRow();
    void AutomationTick(double Now);
};


