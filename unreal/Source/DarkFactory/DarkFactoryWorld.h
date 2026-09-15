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
class UMaterialInstanceDynamic;
class UPointLightComponent;
class URectLightComponent;
class FJsonObject;
class FJsonValue;
class ADarkFactoryThermalRig;
class ADarkFactoryWeldingCell;
class ADarkFactoryWeldingSparks;
class ADarkFactoryWeldingSmoke;
class ADarkFactoryIQ9Props;
class ADarkFactoryEnvironmentDetails;
class ADarkFactoryCellMechanisms;
class ADarkFactorySoundscape;
class ADarkFactoryControlCabinet;
class ADarkFactoryCurtainPortal;
class ADarkFactoryFactoryLights;
class ADarkFactoryProcessController;
class ADarkFactoryProcessScene;
struct FDarkFactoryGraphicsQA;

UCLASS()
class DARKFACTORY_API ADarkFactoryWorld : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryWorld();
    // Set by the current map's game mode before BeginPlay; launcher flags win.
    bool bPanelMapDefaults=false;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetScenario(int32 Variant, bool bNewSeed = true);
    void Randomize();
    void ToggleInference();
    void ToggleWeldingAnimation();
    bool IsImportedScene() const { return WeldingCell != nullptr; }
    ADarkFactoryWeldingCell* GetWeldingCell() const { return WeldingCell; }
    bool GetObserverHome(FVector& Location, FRotator& Rotation) const;
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
    ADarkFactorySoundscape* GetSoundscape() const { return Soundscape; }
    ADarkFactoryControlCabinet* GetControlCabinet() const { return ControlCabinet; }
    ADarkFactoryProcessController* GetProcessController() const { return ProcessController; }
    ADarkFactoryCellMechanisms* GetCellMechanisms() const { return CellMechanisms; }
    bool AreRealismDetailsEnabled() const { return bRealismDetailsEnabled; }
    void SetRealismDetailsEnabled(bool Enabled);
    void SetFactoryLightsEnabled(bool Enabled);
    static constexpr float DimVisibleLightScale=.7f;
    void SetRoofLightIntensity(float Lumens);
    void SetObserverIndirectLightingEnabled(bool Enabled);
    float GetRoofLightIntensity() const { return RoofLightLumens; }
    float GetActiveRoofLightIntensity() const { return RoofLightLumens*RoofLightOutputScale; }
    FString GetScenarioName() const;
    FString GetTruthLabel() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<USceneComponent> TrayRoot;
    UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WeldingNIRMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Modules;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Occluder;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RejectArm;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Sensor;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> SensorTarget;
    UPROPERTY() TObjectPtr<UPointLightComponent> IRLight;
    UPROPERTY() TArray<TObjectPtr<URectLightComponent>> RoofWindowLights;
    float RoofLightLumens = 80;
    float RoofLightOutputScale = DimVisibleLightScale;
    UPROPERTY() TObjectPtr<ADarkFactoryFactoryLights> FactoryLights;
    UPROPERTY() TObjectPtr<ADarkFactoryThermalRig> ThermalRig;
    UPROPERTY() TObjectPtr<ADarkFactoryWeldingCell> WeldingCell;
    UPROPERTY() TObjectPtr<ADarkFactoryWeldingSparks> WeldingSparks;
    UPROPERTY() TObjectPtr<ADarkFactoryWeldingSmoke> WeldingSmoke;
    UPROPERTY() TObjectPtr<ADarkFactoryIQ9Props> IQ9Props;
    UPROPERTY() TObjectPtr<ADarkFactoryEnvironmentDetails> EnvironmentDetails;
    UPROPERTY() TObjectPtr<ADarkFactoryCellMechanisms> CellMechanisms;
    UPROPERTY() TObjectPtr<ADarkFactorySoundscape> Soundscape;
    UPROPERTY() TObjectPtr<ADarkFactoryControlCabinet> ControlCabinet;
    UPROPERTY() TObjectPtr<ADarkFactoryProcessScene> ProcessScene;
    UPROPERTY() TObjectPtr<ADarkFactoryProcessController> ProcessController;
    bool bProcessEnabled=false;
    bool bGraphicsQA=false;
    TSharedPtr<FDarkFactoryGraphicsQA> GraphicsQA;
    void GraphicsSmokeTick(double Now);
    // Dedicated native Chaos fixture. No controller, inference or training path.
    bool bCurtainQA=false,bCurtainStarted=false,bCurtainFinished=false;
    bool bCurtainPaused=false,bCurtainPauseDone=false;
    int32 CurtainPassIndex=0,CurtainMovingSamples=0,CurtainContactBaseline=0;
    double CurtainClock=0,CurtainPassTime=0,CurtainClearClock=-1,CurtainWallStarted=0;
    double CurtainPauseStarted=0,CurtainCarriageOffset=-1800,CurtainMaxFrameDelta=0;
    TSharedPtr<FJsonObject> CurtainReport,CurtainPass,CurtainPause;
    TArray<TSharedPtr<FJsonValue>> CurtainSamples,CurtainPasses;
    TArray<FTransform> CurtainPauseTransforms;
    TArray<FVector> CurtainPauseLinear,CurtainPauseAngular;
    bool InitializeCurtainSmoke();
    void CurtainSmokeTick(float DeltaSeconds,double Now);
    void FinishCurtainSmoke(bool Completed,const FString& Error=TEXT(""));
    bool bCabinetQA=false;
    int32 CabinetStep=0;
    TSharedPtr<FJsonObject> CabinetReport;
    void CabinetSmokeTick(double Now);
    bool bRealismDetailsEnabled=false,bRealismQA=false;
    int32 RealismStep=0;
    TSharedPtr<FJsonObject> RealismReport;
    void RealismSmokeTick(double Now);
    bool bColliderQA=false;
    int32 ColliderStep=0;
    TSharedPtr<FJsonObject> ColliderReport;
    void ColliderSmokeTick(double Now);
    UPROPERTY() TArray<TObjectPtr<ADarkFactoryCurtainPortal>> CurtainPortals;
    TSharedPtr<FJsonObject> WeldingProductionQA;
    bool bPhysicsOverrideAcquired = false;
    bool bWeldingSmoke = false;
    TSharedPtr<FJsonObject> WeldingRestMetadata, WeldingMotionMetadata;
    TSharedPtr<FJsonObject> WeldingFrozenScene, WeldingAfterGuards, WeldingSensorStats;
    bool bWeldingSensorQA = true, bWeldingGuardsPassed = false;
    int32 WeldingQAReadbacks = 0, WeldingThermalReadbackBaseline = 0;
    float WeldingRoofLumens = 0;
    bool SaveWeldingNIRQA(const FString& Name, const FString& StatsName);
    TSharedRef<FJsonObject> WeldingSceneSnapshot() const;
    bool TryBuildImportedCell(const FString& Manifest, FString& Error);
    void WeldingSmokeTick(double Now);
    void ToolCoolingSmokeTick(double Now);
    void BodyWeldHeatSmokeTick(double Now);
    void ImportedFieldsSmokeTick(double Now);
    void FactoryLightingSmokeTick(double Now);
    void RobotChoreographySmokeTick(double Now);
    void MotorHeatSmokeTick(double Now);
    void WeldingEffectsSmokeTick(double Now);
    void RobotExpansionSmokeTick(double Now);
    bool bRobotExpansionQA=false,bRobotExpansionPassed=true;
    int32 RobotExpansionStep=0;
    uint64 RobotExpansionFPSSamples=0;
    TSharedPtr<FJsonObject> RobotExpansionReport;
    bool bWeldingEffectsQA=false,bWeldingEffectsPassed=true;
    int32 WeldingEffectsStep=0,WeldingEffectsPreviousTemporalFreeze=-1;
    TSharedPtr<FJsonObject> WeldingEffectsReport;
    FString WeldingEffectsStateSignature;
    bool bMotorHeatQA=false, bMotorHeatPassed=true;
    int32 MotorHeatStep=0;
    TSharedPtr<FJsonObject> MotorHeatReport;
    FString MotorHeatGeometrySignature, MotorHeatStageSignature, MotorHeatHotSignature;
    bool bRobotChoreographyQA=false, bRobotChoreographyPassed=true;
    int32 RobotChoreographyStep=0;
    TSharedPtr<FJsonObject> RobotChoreographyReport;
    bool bFactoryLightingQA=false, bFactoryLightingPassed=true;
    int32 FactoryLightingStep=0;
    TSharedPtr<FJsonObject> FactoryLightingReport;
    FString FactoryLightingGeometrySignature;
    bool bImportedFieldsQA=false, bImportedFieldsPassed=true;
    int32 ImportedFieldsStep=0;
    TSharedPtr<FJsonObject> ImportedFieldsReport;
    FString ImportedFieldsGeometrySignature;
    bool bBodyWeldHeatQA=false, bBodyWeldHeatPassed=true;
    int32 BodyWeldHeatStep=0;
    TSharedPtr<FJsonObject> BodyWeldHeatReport;
    FString BodyWeldGeometrySignature, BodyWeldHeatSignature, BodyWeldLightingSignature;
    bool bToolCoolingQA=false, bToolCoolingPassed=true;
    int32 ToolCoolingStep=0;
    TSharedPtr<FJsonObject> ToolCoolingReport;
    FString ToolCoolingGeometrySignature, ToolCoolingHeatSignature;
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
