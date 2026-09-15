#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryWeldingCell.generated.h"

class UStaticMeshComponent;
class USplineMeshComponent;
class UBoxComponent;
class FJsonObject;
class ADarkFactoryThermalRig;
class UMaterialInstanceDynamic;
enum class EDarkFactoryProcessPhase : uint8;

/** Authored visual temperature only; never a calibrated material or thermal body. */
struct FDarkFactoryImportedThermalSurface {
    UStaticMeshComponent* Mesh = nullptr;
    float TemperatureC = 24;
    float Emissivity = .9f;
    float CoreEdgeDropC = 0;
};
struct FDarkFactoryWeldEmitter {
    FString Id;
    UStaticMeshComponent* SourceTool = nullptr;
    FVector Position = FVector::ZeroVector;
    FVector Direction = -FVector::UpVector;
    FVector SurfacePosition = FVector::ZeroVector;
    FVector SurfaceNormal = FVector::UpVector;
    float Intensity = 0;
    int32 EmitterIndex = 0; // Stable even when only a subset of robots is welding.
    double WeldAgeSeconds = 0, WeldDurationSeconds = 7.5;
};

/** Cycle-local schedule derived from seed, robot identity and a bounded cycle bank. */
struct FDarkFactoryRobotTiming {
    bool Valid=false, Randomized=true;
    FString RobotId;
    int32 RobotIndex=-1, CycleIndex=0, ScheduleIndex=0;
    double CycleSeconds=0, CycleDurationSeconds=21;
    double RestDelaySeconds=0, PreWeldDelaySeconds=0;
    double ApproachStartSeconds=3.5, ApproachEndSeconds=4.5;
    double WeldStartSeconds=4.5, WeldEndSeconds=12, WithdrawEndSeconds=13;
};

/** Deterministic authored cell cycle; rigid visual robot links, no inspection decisions. */
UCLASS()
class DARKFACTORY_API ADarkFactoryWeldingCell : public AActor {
    GENERATED_BODY()
public:
    static constexpr double CycleDurationSeconds = 21.;
    static constexpr double LegacyCycleDurationSeconds = 18.;
    static constexpr int32 RobotTimingBankCycles = 16;
    static constexpr double ProcessApproachSpeedScale=.8;
    static constexpr double ProcessWeldPoseStartSeconds=3.4;
    static constexpr double ProcessWeldPoseEndSeconds=16.3+(1./ProcessApproachSpeedScale-1.);
    static constexpr double ProcessWeldPoseDurationSeconds=ProcessWeldPoseEndSeconds-ProcessWeldPoseStartSeconds;
    static constexpr double ProcessWeldCorePeakC=1600.;
    static constexpr double ProcessWeldHaloPeakC=420.;
    static constexpr float ProcessWeldCoreRadiusCm=2.f;
    static constexpr double ProcessWeldCoreCoolingTauSeconds=.65;
    double GetRobotApproachDurationSeconds() const { return bProcessControlled ? 1./ProcessApproachSpeedScale:1.; }
    ADarkFactoryWeldingCell();
    bool LoadScene(const FString& Path, FString& Error);
    virtual void Tick(float DeltaSeconds) override;
    void SetAnimationEnabled(bool Enabled) { bAnimating = Enabled; }
    bool IsAnimating() const { return bAnimating; }
    void SetMotionTime(double Seconds);
    // Explicit model-gated process mode; legacy preview/QA retains its original cycle.
    void SetProcessControlled(bool Enabled);
    void SetProcessMotorCaptureVariation(int32 Seed);
    bool IsProcessControlled() const { return bProcessControlled; }
    void BeginProcessBody(int32 BodyIndex,double ClockSeconds);
    void SetProcessFrame(double PoseSeconds,double ClockSeconds,double CarriageOffsetCm,bool Visible,bool PermitWelding,double InspectionExtension=1.);
    double GetInspectionExtension() const { return ProcessInspectionExtension; }
    // Process-only six-joint motion; the camera mounting transform stays fixed.
    void RequestInspectionSpot(int32 Spot,double ClockSeconds,bool Snap=false,bool NIR=false,bool Adhesive=false);
    bool IsInspectionSpotSettled(int32 Spot) const;
    FTransform GetInspectionCameraMountTransform() const;
    TSharedRef<FJsonObject> GetInspectionScanMetadata() const;
    USceneComponent* GetCarriageRoot() const { return CarriageRoot; }
    USceneComponent* GetInspectionJoint() const;
    UStaticMeshComponent* FindSceneMesh(const FString& Name) const;
    bool GetWeldSite(int32 Index,FVector& LocalPosition,FVector& LocalNormal) const;
    float GetMaximumProcessWeldC() const;
    // Imported trunk-panel assembly, explicitly enabled for the new process only.
    static constexpr int32 PanelWelderRobot=1;
    static constexpr int32 PanelAdhesiveRobot=0;
    static constexpr int32 PanelHolderRobot=2;
    static constexpr double PanelAdhesiveSeconds=13.5;
    static constexpr double PanelPlacementSeconds=6.;
    static constexpr double PanelPickupSeconds=1.5;
    static constexpr int32 PanelWeldCount=6;
    static constexpr double PanelWeldingSeconds=11.5;
    static double PanelWeldStartSeconds(int32 Index) { return 1.45+1.35*Index; }
    static constexpr double PanelRetractSeconds=2.5;
    bool InitializePanelAssembly();
    UStaticMeshComponent* GetPanelBodyMesh() const;
    FTransform GetPanelSupplyTransform(int32 Slot=4) const;
    FTransform GetPanelHeldTransform() const;
    static constexpr double PanelSeatingLiftCm=.5;
    bool GetPanelContact(int32 Index,FVector& LocalPosition,FVector& LocalNormal) const;
    bool GetPanelAdhesivePath(int32 Bead,TArray<FVector>& LocalPoints,TArray<FVector>& LocalNormals) const;
    static double GetPanelAdhesiveStartSeconds(int32 Bead);
    static double GetPanelAdhesiveProgress(int32 Bead,double PhaseSeconds);
    void SetPanelSupplyCount(int32 Count);
    void SetPanelMotionState(EDarkFactoryProcessPhase Phase,double PhaseSeconds,bool HoldingPanel=false);
    TSharedRef<FJsonObject> GetPanelAssemblyMetadata() const;
    FString GetPanelFramePath() const;
    double GetArrivalOffsetCm() const { return ArrivalOffset; }
    double GetDepartureOffsetCm() const { return DepartureOffset; }

    double GetMotionTime() const { return MotionSeconds; }
    // Effect aging continues while process gates hold the authored robot pose.
    double GetEffectTime() const { return bProcessControlled ? ProcessClock:MotionSeconds; }
    double GetCycleSeconds() const;
    double GetCycleDurationSeconds() const { return bRandomizedRobotTiming ? CycleDurationSeconds:LegacyCycleDurationSeconds; }
    double GetCarriageDepartureStartSeconds() const { return (bRandomizedRobotTiming ? 16.3:13.5)+(bProcessControlled ? 1./ProcessApproachSpeedScale-1.:0.); }
    double GetCarriageDepartureEndSeconds() const { return GetCarriageDepartureStartSeconds()+3.5; }
    bool IsRobotTimingRandomized() const { return bRandomizedRobotTiming; }
    // Explicit old-fixture timing only. Interactive/default mode is randomized.
    bool SetRobotTimingForQA(bool Randomized);
    FDarkFactoryRobotTiming GetRobotTiming(int32 RobotIndex,double AbsoluteSeconds) const;
    TSharedRef<FJsonObject> BuildRobotTimingMetadata(double AbsoluteSeconds) const;
    int32 GetCycleIndex() const;
    FString GetCyclePhaseName() const;
    bool IsWelding() const;
    TArray<FDarkFactoryWeldEmitter> GetWeldEmitters() const;
    FBox GetCarriageBounds() const;
    const TArray<TObjectPtr<UBoxComponent>>& GetCarriageColliders() const { return CarriageColliders; }
    int32 GetMeshCount() const { return MeshComponents.Num(); }
    int32 GetJointCount() const { return JointMotion.Num(); }
    int32 GetHolderCount() const { return HolderComponents.Num(); }
    const TArray<FDarkFactoryImportedThermalSurface>& GetThermalSurfaces() const { return ThermalSurfaces; }
    FVector GetObserverLocation() const { return ObserverLocation; }
    FRotator GetObserverRotation() const { return (ObserverLookAt-ObserverLocation).Rotation(); }
    FVector GetSensorLocation() const { return SensorLocation; }
    FVector GetSensorLookAt() const { return SensorLookAt; }
    FRotator GetSensorRotation() const { return (SensorLookAt-SensorLocation).Rotation(); }
    float GetSensorFOV() const { return SensorFOV; }
    TSharedRef<FJsonObject> BuildPreviewMetadata() const;
    void BindThermalRig(ADarkFactoryThermalRig* Rig);
    void SetWeldWitnessEnabled(bool Enabled);
    bool HasWeldWitness() const { return bWeldWitnessEnabled; }
    void SetToolHeatTimeForQA(double Seconds); // Offline same-pose cooling evidence only.
    TSharedRef<FJsonObject> BuildToolHeatMetadata() const;
    void SetBodyWeldHeatTimeForQA(double Seconds); // Vary heat only in a frozen diagnostic pose.
    TSharedRef<FJsonObject> BuildBodyWeldHeatMetadata() const;
    void SetMotorHeatTimeForQA(double Seconds,double IdleSeconds=0); // Heat-only deterministic seek.
    TSharedRef<FJsonObject> BuildMotorHeatMetadata() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<USceneComponent> CarriageRoot;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> JointComponents;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MeshComponents;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyComponents;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HolderComponents;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> CarriageColliders;
    UPROPERTY() TArray<TObjectPtr<USplineMeshComponent>> HoseComponents;
    struct FJointMotion {
        TWeakObjectPtr<USceneComponent> Component;
        FString Id;
        int32 RobotIndex=INDEX_NONE;
        FVector Axis = FVector::UpVector;
        FQuat RestRotation = FQuat::Identity;
        double Minimum = 0, Maximum = 0, RestAngle = 0, WeldAmplitude = 0;
        double Angle = 0;
        TArray<double> WorkAngles;
        TArray<double> ApproachAngles; // Three authored poses between rest and solved work.
    };
    struct FMotorHeatRig {
        TWeakObjectPtr<UStaticMeshComponent> Mesh;
        FString Role;
        TArray<int32> Drivers;
        TArray<double> DriverWeights;
        double MaximumC=80, HeatingRate=40, CoolingTau=25, ReferenceSpeed=120;
        float CoreEdgeDropC=0, TemperatureC=20, Activity=0;
        TArray<double> PrefixDecay,PrefixRise;
        TArray<float> Duty;
    };
    double EvaluateJointAngle(const FJointMotion& Joint,double Seconds) const;
    double RobotCanonicalPhase(int32 RobotIndex,double Seconds) const;
    double GetTimingBankSeconds() const { return GetCycleDurationSeconds()*(bRandomizedRobotTiming ? RobotTimingBankCycles:1); }
    int32 MotorDutySteps=2160;
    int32 RobotTimingSeed=42;
    bool bRandomizedRobotTiming=true, bToolHeatContinuous=true;
    bool InitializeMotorHeat();
    void UpdateMotorHeat(double Seconds,double IdleSeconds=0);
    void UpdateProcessMotorHeat();
    double EvaluateMotorTemperature(const FMotorHeatRig& Rig,double Seconds,double IdleSeconds=0) const;
    bool CheckMotorHeatModel();
    TArray<FMotorHeatRig> MotorHeatRigs;
    TSharedPtr<FJsonObject> MotorHeatSelfCheck;
    double MotorHeatSeconds=0,MotorIdleSeconds=0;
    struct FToolHeatCurve {
        TArray<double> PrefixRise;
        double Target=20, HeatingTau=1, CoolingTau=1, CycleDecay=0, BankDecay=0;
    };
    struct FEmitterRig {
        FString Id;
        TWeakObjectPtr<USceneComponent> Joint;
        TWeakObjectPtr<UStaticMeshComponent> SourceTool;
        FVector Position = FVector::ZeroVector;
        FVector Direction = -FVector::UpVector;
        TArray<FVector> SurfacePositions, SurfaceNormals;
        FVector HeatTipLocal=FVector::ZeroVector;
        TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> HeatMaterials;
        float BodyC=20, TipC=20, Glow=0;
        FToolHeatCurve BodyCurve,TipCurve;
    };
    void BuildRobotHeatCurves();
    double EvaluateToolTemperature(int32 RobotIndex,const FToolHeatCurve& Curve,double Seconds) const;
    struct FHoseAnchor { TWeakObjectPtr<USceneComponent> Joint; FVector LocalPosition = FVector::ZeroVector; };
    struct FHoseRig { TArray<FHoseAnchor> Anchors; TArray<TWeakObjectPtr<USplineMeshComponent>> Sections; };
    struct FBodyWeldSpot {
        FString Id;
        TWeakObjectPtr<UStaticMeshComponent> Mesh;
        FVector LocalPosition = FVector::ZeroVector;
        FVector LocalNormal = FVector::UpVector;
        float WitnessStrength = 0;
        float RadiusCm = 12, TemperatureC = 20, HaloTemperatureC = 20, Glow = 0, ContactErrorCm = 0;
    };
    struct FBodyWeldMaterial {
        TWeakObjectPtr<UStaticMeshComponent> Mesh;
        TArray<int32> SpotIndices;
        TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
    };
    bool InitializeBodyWeldHeat();
    void UpdateBodyWeldHeat(double Seconds);
    double BodyWeldHeatSeconds = 0;
    bool bWeldWitnessEnabled=false;
    double EvaluateBodyWeldTemperature(int32 RobotIndex,double Seconds,bool Halo=false) const;
    TArray<FBodyWeldSpot> BodyWeldSpots;
    TArray<FBodyWeldMaterial> BodyWeldMaterials;
    bool InitializeToolHeat();
    void UpdateToolHeat(double Seconds);
    TWeakObjectPtr<ADarkFactoryThermalRig> ToolThermalRig;
    double ToolHeatSeconds=0;
    void BuildCarriageCollision();
    void BuildHoses(const TMap<FName,USceneComponent*>& Parents);
    void UpdateHoses();
    TArray<FJointMotion> JointMotion;
    TArray<FEmitterRig> EmitterRigs;
    TArray<FHoseRig> HoseRigs;
    TArray<FDarkFactoryImportedThermalSurface> ThermalSurfaces;
    FBox WorkingBodyBounds = FBox(ForceInit);
    FVector ObserverLocation, ObserverLookAt, SensorLocation, SensorLookAt;
    float SensorFOV = 58;
    double MotionSeconds = 0, ArrivalOffset = -1500, DepartureOffset = 1500;
    bool bLoaded = false, bAnimating = true, bCarVisible = false;
    bool bDramaticApproach = false;
    bool bProcessControlled=false,bProcessVisible=true,bProcessPermitWelding=false,bProcessWeldStarted=false;
    int32 ProcessBodyIndex=0;
    double ProcessClock=0,ProcessBirthClock=0,ProcessWeldEpoch=-1,ProcessCarriageOffset=0;
    double ProcessInspectionExtension=0;
    static constexpr double InspectionScanDurationSeconds=.8;
    TArray<int32> InspectionJointIndices;
    TArray<double> InspectionScanFromAngles;
    int32 InspectionRequestedSpot=0;
    double InspectionScanStartClock=0;
    bool bInspectionScanConfigured=false;
    void InitializeInspectionScan();
    double InspectionScanAngle(int32 JointIndex,double ClockSeconds) const;
    FTransform InspectionEndpointTransform(int32 Spot) const;
    TArray<float> ProcessInitialBodyC,ProcessInitialTipC;
    TArray<double> ProcessPreviousJointAngles;
    double ProcessMotorLastClock=-1,ProcessMotorSampleSeconds=0;
    bool bProcessMotorCaptureVariation=false;
    int32 ProcessMotorCaptureSeed=0;
    bool bPanelAssembly=false;
    EDarkFactoryProcessPhase PanelPhase;
    double PanelPhaseSeconds=0;
    int32 PanelSupplyCount=5,PanelPickupSlot=4;
    bool bPanelHolderEngaged=false;
    double PanelToolHeatClock=-1;
    FQuat PanelSupplyRotation=FQuat::Identity;
    TArray<FVector> PanelContacts,PanelContactNormals;
    struct FPanelProfileShare { TWeakObjectPtr<UStaticMeshComponent> Target,Source; };
    TArray<FPanelProfileShare> PanelProfileShares;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> PinchParts;
    FVector PinchPivots[2],PinchTips[2],PinchAxis=FVector::ForwardVector;
    FTransform PinchMount;
    double PinchClampAngle=0,PinchOpenAngle=14,PinchCurrentAngle=14;
    void UpdatePinchGun();
    void BindPanelSharedProfiles();
    double InspectionTargetAngle(int32 Spot,int32 Joint) const;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> PanelSupplyMeshes;
    FTransform PanelHeldOffset,PanelInspectionMount;
    TArray<TArray<double>> PanelAssemblyPoses;
    TArray<TArray<double>> PanelWelderWaypoints,PanelAdhesiveWaypoints,PanelHolderWaypoints,PanelWeldCrossPoses,PanelWeldPoses;
    TArray<TArray<double>> PanelInspectionPoses,PanelNIRInspectionPoses,PanelAdhesiveInspectionPoses;
    TArray<TArray<double>> PanelAdhesiveStrokePoses,PanelAdhesivePathProgress;
    TArray<TArray<FVector>> PanelAdhesivePathPoints,PanelAdhesivePathNormals;
    bool bPanelInspectionNIR=false,bPanelInspectionAdhesive=false;
    double PanelJointAngle(const FJointMotion& Joint) const;
    FString ManifestPath,PanelAuthoringSHA256,PanelMotionRecipe,PanelLayoutRecipe;
};
