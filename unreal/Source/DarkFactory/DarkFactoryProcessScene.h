#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessTypes.h"
#include "DarkFactoryProcessScene.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class ADarkFactoryThermalRig;
class UMaterialInstanceDynamic;

/** Physical panel/adhesive state. Private labels never determine live gate decisions. */
UCLASS()
class DARKFACTORY_API ADarkFactoryProcessScene : public AActor {
    GENERATED_BODY()
public:
    static constexpr double CarriageTravelCm=2050.;
    static constexpr double CarriageTravelSeconds=3.2;
    static constexpr double TunnelLengthCm=1400.;
    static constexpr double InspectionDeploySeconds=1.;
    static constexpr double InspectionWithdrawSeconds=.8;
    static constexpr float AdhesiveCoolingTimeScale=.8f;
    static constexpr double WeldHaloCoolingSeconds=5.; // Leaves visible residual heat after the intervening NIR scan.
    static int32 AdhesiveBeadForView(int32 View) { return View==1 ? 1:0; }
    ADarkFactoryProcessScene();
    bool Initialize(ADarkFactoryWeldingCell* InCell,int32 Seed);
    // -1: seeded 1/5 preassembled; 0: missing; 1: preassembled. Offline override only.
    void BeginBody(int32 BodyIndex,double ClockSeconds,int32 PreassembledOverride=-1);
    void EndBody();
    void SetCaptureVariation(int32 Seed);
    void ApplyAssemblyFrame(EDarkFactoryProcessPhase Phase,double StageSeconds,double ClockSeconds,double Offset,bool Visible);
    static double GetPhaseDuration(EDarkFactoryProcessPhase Phase);
    bool PrepareCapture(const FString& Stage,int32 CaseIndex,int32 Seed,double& ClockOut);
    void RequestInspectionView(const FString& Stage,int32 View,double ClockSeconds,bool Snap=false);
    bool IsInspectionViewSettled(int32 View) const;
    void GetInspectionCameraPose(const FString& Stage,int32 View,FVector& Location,FRotator& Rotation,float& FOV) const;
    void GetInspectionFramingCorners(const FString& Stage,int32 View,TArray<FVector>& Corners) const;
    void GetSealantSurfaceSamples(int32 View,TArray<FVector>& Points) const;
    void GetInspectionObserverView(FVector& Location,FRotator& Rotation) const;
    void BindThermalRig(ADarkFactoryThermalRig* Rig);
    const TArray<FDarkFactoryImportedThermalSurface>& GetThermalSurfaces() const { return ThermalSurfaces; }
    TSharedRef<FJsonObject> BuildMetadata() const;
    float GetMaximumAdhesiveC() const;
    float GetMaximumWeldC() const;
    float GetMaximumSealantContactApparentC() const;
    float GetMaxAdhesiveTemperatureC() const { return GetMaximumAdhesiveC(); }
    float GetMaxWeldTemperatureC() const { return GetMaximumWeldC(); }
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<ADarkFactoryWeldingCell> Cell;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> PanelTemplate;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> IncomingPanel;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> HeldPanel;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> MountingFrame;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AdhesiveMeshes;
    UPROPERTY() TObjectPtr<USceneComponent> CameraMount;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CameraHousing;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CameraLens;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CaptureOccluder;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PanelMaterials;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> AdhesiveMaterials;
    TWeakObjectPtr<ADarkFactoryThermalRig> ThermalRig;
    TArray<FDarkFactoryImportedThermalSurface> ThermalSurfaces;
    mutable TArray<TSharedPtr<FJsonValue>> CaptureVisibilityRays;
    FTransform PanelTarget;
    TArray<FVector> SpotLocal,SpotNormals,AdhesiveLocal;
    TArray<float> AdhesiveInitialC,AdhesiveTau,AdhesiveC,ContactSurfaceC,WeldCoreC,WeldHaloC;
    TArray<double> AdhesiveDeposited,WeldDeposited,AdhesiveProgress,AdhesiveLengths;
    double ContactStarted=-1;
    double BodyBirthSeconds=0,Clock=0,LastClock=0,InspectionExtension=0,LastStageSeconds=0;
    int32 RandomSeed=42,BodySequence=0,SupplyRemaining=5,CaptureVariationSeed=0;
    EDarkFactoryProcessPhase CurrentPhase=EDarkFactoryProcessPhase::Arriving;
    FString InspectionStage=TEXT("panel_presence"),CaptureExpectedDecision;
    bool bInitialized=false,bBodyActive=false,bPreassembled=false,bPanelPicked=false,bPanelWelded=false;
    bool bBodyVisible=false,bCaptureVariation=false,bOfflinePrepared=false,bOccluded=false,bMisaligned=false;
    void UpdateTemperatureFields();
    void UpdatePanelVisibility();
};
