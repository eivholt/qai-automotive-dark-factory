#pragma once
#include "DarkFactoryEnhancements.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryProcessTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Async/Future.h"
#include "DarkFactorySensorReadback.h"
#include "DarkFactoryInspectionReceipt.h"
#include "DarkFactoryProcessController.generated.h"
class ADarkFactoryWorld;
class ADarkFactoryWeldingCell;
class ADarkFactoryProcessScene;
class ADarkFactoryThermalRig;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class USpotLightComponent;
class FJsonObject;
class FJsonValue;
struct FDarkFactoryPanelMotionQA;
namespace DarkFactoryAgent { struct FRuntime; struct FCommand; }

/** Four camera-only Reason2 gates with bounded async capture and fail-hold transport. */
UCLASS()
class DARKFACTORY_API ADarkFactoryProcessController : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryProcessController();
    bool Initialize(ADarkFactoryWorld* World,ADarkFactoryWeldingCell* Cell,ADarkFactoryProcessScene* Scene,ADarkFactoryThermalRig* Thermal);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void CaptureTick(); // Called by World after physics and thermal LUT updates.
    void TogglePaused();
    const FDarkFactoryProcessStatus& GetStatus() const { return State; }
    UTextureRenderTarget2D* GetPreviewTexture(int32 Slot) const;
    void ToggleInspectionReplay() { bInspectionReplay=!bInspectionReplay; ReplaySelection.Empty(); }
    void StepInspectionReplay(int32 Direction);
    bool IsInspectionReplayVisible() const { return bInspectionReplay; }
    const FDarkFactoryInspectionReceipt* GetInspectionReceipt() const;
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    FDarkFactoryProcessStatus State;
    TWeakObjectPtr<ADarkFactoryWorld> WorldOwner;
    TWeakObjectPtr<ADarkFactoryWeldingCell> WeldingCell;
    TWeakObjectPtr<ADarkFactoryProcessScene> ProcessScene;
    TWeakObjectPtr<ADarkFactoryThermalRig> ThermalRig;
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> Cameras;
    UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> CameraMaterials;
    UPROPERTY() TArray<TObjectPtr<USpotLightComponent>> NIRLights;
    FDarkFactorySensorReadback Readback;
    TFuture<TArray<uint8>> EncodeFuture;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> Request;
    FString SessionId,Endpoint,ExpectedModelSource,ContractVersion,ActiveRequestId,CaptureBodyId,CaptureStage,OutputRoot,LastModel,LastAdapter,LastScoringMethod,LastBackendAlias;
    TMap<FString,FString> PromptHashes;
    struct FPreviewStamp { FString Body,Stage; double At=0; };
    TArray<FPreviewStamp> PreviewStamps;
    TArray<TArray<uint8>> BatchImages;
    UPROPERTY() TArray<FDarkFactoryInspectionReceipt> InspectionReceipts;
    bool bInspectionReplay=false;
    FString ReplaySelection;
    void RetainInspectionReceipt();
    void CompleteInspectionReceipt(const FString& Id,bool Valid,const FString& Code,float Latency,double Confidence,double Mass,bool Bound);
    TArray<TSharedPtr<FJsonValue>> BatchInspectionViews,InspectionCaptures;
    TSet<FString> SavedInspectionScreenshots;
    TSharedPtr<FJsonObject> PendingInspectionView;
    bool bCapturePoseReady=false,bSensorResourcesReady=false;
    DarkFactoryEnhancements::FCoolingRecovery CoolingRecovery;
    double SensorAssetsIdleSince=0,SensorResourcesReadyAt=0;
    TArray<TSharedPtr<FJsonValue>> Events;
    TArray<TSharedPtr<FJsonValue>> ModelSmokeOutcomes,ModelSmokeThermalChecks,ModelSmokeDecisions;
    bool bModelSmokeOutcomesMatch=true,bModelSmokeThermalLimitsMatch=true;
    uint64 CaptureSerial=0,BatchSerial=0,ModelRequestsSubmitted=0;
    int32 CaptureSlot=-1,SettleFrames=0,SensorWarmupCaptures=0,ForcedPreassembled=-1,CompletedBodies=0,CaptureCase=-1,ExpectedImages=0;
    int32 QAStartCase=0,QAEndCase=40,QAEpisodes=0,QASeed=701;
    bool bCasePrepared=false,bPhaseScreenshotTaken=false,bModelRequestsEnabled=true;
    FString CaptureSampleId;
    double PhaseStarted=0,NextInspection=0,BatchStarted=0,RequestStarted=0;
    double CapturedAtMs=0,ResponseDeadlineSeconds=20,MaxCaptureAgeSeconds=8,SessionStarted=0;
    double MinimumLabelMass=.5,MinimumConditionalProbability=.8;
    float CaptureNIRPowerScale=1.f;
    bool bCaptureActive=false,bReady=false,bCaptureQA=false,bModelQA=false,bPolicyQA=false,bPanelMotionQA=false;
    bool bSealantCoverageCapture=false;
    TSharedPtr<FDarkFactoryPanelMotionQA> PanelMotionQA;
    void PanelMotionQATick(float DeltaSeconds);
    void PanelMotionQACaptureTick();
    void BeginBody();
    void EnterPhase(EDarkFactoryProcessPhase Phase);
    void StartCapture();
    void CaptureNext();
    void RecordInspectionCapture(USceneCaptureComponent2D* Camera);
    void SubmitBatch();
    void ApplyDecision(const FString& Decision,bool Valid,const FString& Reason,float Latency,const FString& Backend);
    void CancelInspection();
    void RecordEvent(const FString& Name);
    bool WriteReport() const;
    FString StageName() const;
    void CaptureQATick();
    bool RunPolicySmoke();
    TSharedPtr<DarkFactoryAgent::FRuntime> Agent;
    bool InitializeAgent();
    void BeginAgentBody();
    void AgentTick();
    void RequestAgentDecision();
    void DispatchAgentCommand(const DarkFactoryAgent::FCommand& Command);
    void CompleteAgentTool(const TSharedRef<FJsonObject>& Result);
    void AgentInspectionResult(const FString& Code,bool Valid,const FString& Reason,float Latency);
    void CancelAgentDecision();
    void AuditAgentAction(const FString& Action);
    void RecordAgentTrace(const FString& Event,const TSharedRef<FJsonObject>& Detail);
    void AgentTransportHold(const FString& Error);
    void AppendAgentMetadata(const TSharedRef<FJsonObject>& Report) const;
    static bool HasGrayscaleSensorPixels(const TArray<FColor>& Pixels);
    static bool AdvanceSensorIdleWindow(double Now,bool bCompiling,double& IdleSince);
    bool SensorResourcesReady();
    static bool IsModelSmokeOutcomeCorrect(int32 BodySequence,bool bDeparted,bool bPreassembled,bool bAssembled,bool bWelded);
    bool HasPassedModelSmoke() const;
    void RecordModelSmokeOutcome();
    void RecordModelSmokeThermalGate();
};
