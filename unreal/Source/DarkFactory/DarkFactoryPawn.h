#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DarkFactoryPawn.generated.h"
class UCameraComponent;
class USpotLightComponent;
class UMaterialInstanceDynamic;
class FDarkFactoryViewExtension;
class FJsonObject;
UCLASS()
class DARKFACTORY_API ADarkFactoryPawn : public APawn {
    GENERATED_BODY()
public:
    ADarkFactoryPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    bool bAssistedView = true;
    void ToggleView();
    void SetObserverMode(int32 Mode);
    void CycleView();
    int32 GetObserverMode() const { return ObserverMode; }
    bool IsFactoryLightsView() const { return ObserverMode==3; }
    void SetThermalPalette(int32 Palette);
    void SetObserverHeadlightEnabled(bool Enabled);
    // Retains the user's preference; the light emits only in dim observer mode0.
    bool IsObserverHeadlightEnabled() const { return bObserverHeadlightEnabled; }
    bool IsObserverHeadlightEmitting() const;
    bool IsObserverNIRIlluminatorEmitting() const;
    // Offline same-view light comparisons only; setting the mode restores normal behavior.
    void SetObserverNIRIlluminatorForQA(bool Enabled);
    const USpotLightComponent* GetObserverNIRIlluminator() const { return ObserverNIRIlluminator.Get(); }
    uint8 GetObserverViewLightingMask() const { return ObserverMode==1 ? 4u:1u; }
    bool IsThermalView() const { return bThermalView; }
    // Observer-only preferences. Infrared modes suspend these effects.
    void SetGraphicsOptions(bool DLSS,bool Lumen,bool HardwareRayTracing);
    bool IsObserverDLSSEnabled() const;
    bool IsObserverLumenEnabled() const;
    bool IsObserverHardwareRayTracingEnabled() const;
    FString GetGraphicsStatus() const;
    bool IsGraphicsLegacyFixture() const { return bGraphicsLegacyFixture; }
    TSharedRef<FJsonObject> BuildGraphicsMetadata() const;
    void RecordGraphicsView(const FString& Upscaler,bool RayTracingAllowed);
    bool HasThermalMaterial() const { return ThermalMaterial != nullptr; }
    void ApplyNightVision(UMaterialInstanceDynamic* Material) const;
    bool IsNightVisionQuality() const { return bNightVisionQuality; }
    bool HasNightVisionMaterial() const { return NightVisionMaterial != nullptr; }
    FString GetNightVisionStatus() const;
    static constexpr int32 ObserverBookmarkCount=5;
    bool SetObserverBookmark(int32 Index,bool Immediate=false);
    FString GetObserverBookmarkName() const;
    bool IsObserverBookmarkMoving() const { return bBookmarkMoving; }
    void SetCaptureAutomation(bool Active) { bAutomation=Active; Forward=Right=Up=Yaw=Pitch=0; }
    void SetNightVisionSmokeFrame(float Frame) { NightVisionFrameOverride = Frame; }
private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USpotLightComponent> ObserverHeadlight;
    // View channel 2 is observer-only; fixed NIR capture stays on channel 1.
    UPROPERTY() TObjectPtr<USpotLightComponent> ObserverNIRIlluminator;
    bool bObserverHeadlightEnabled = false;
    bool bGraphicsInitialized=false,bGraphicsLegacyFixture=false;
    bool bDLSSRequested=true,bLumenRequested=true,bHardwareRayTracingRequested=true;
    bool bDLSSAvailable=false,bHardwareRayTracingAvailable=false;
    float DLSSScreenPercentage=66.6667f;
    FString DLSSSupportStatus=TEXT("not initialized"),LastObserverUpscaler=TEXT("none");
    bool bLastViewRayTracing=false;
    uint64 ObserverGraphicsViews=0;
    void UpdateObserverGraphics();
    void ToggleDLSS(); void ToggleLumen(); void ToggleHardwareRayTracing();
    void ToggleObserverHeadlight();
    void UpdateObserverLighting();
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> NightVisionMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThermalMaterial;
    int32 ObserverMode = 1; // 0 dim, 1 NIR, 2 thermal, 3 bright factory.
    bool bThermalView = false;
    void ToggleHeatTransfer(); void ToggleThermalFields(); void CoolThermal();
    void ToggleThermal(); void CycleThermalPalette(); void NextThermalCase(); void SaveThermal();
    bool bNightVisionQuality = false;
    float NightVisionGain = 1.35f;
    float NightVisionGrain = .035f;
    float NightVisionVignette = .30f;
    float NightVisionHalo = .12f;
    float NightVisionSeed = 17;
    float NightVisionFrameOverride = -1;
    void GainDown(); void GainUp(); void GrainDown(); void GrainUp();
    TSharedPtr<FDarkFactoryViewExtension, ESPMode::ThreadSafe> ViewExtension;
    bool bAutomation = false;
    int32 ObserverBookmarkIndex=-1;
    bool bBookmarkMoving=false;
    float BookmarkElapsed=0;
    FVector BookmarkStart,BookmarkEnd;
    FQuat BookmarkRotationStart,BookmarkRotationEnd;
    void CycleObserverBookmark();
    void ControllerBookmark();
    void ToggleFactorySound();
    void ToggleColliderOverlay();
    void ToggleInspectionReplay();
    void PreviousInspectionReceipt();
    void NextInspectionReceipt();
    void CancelObserverBookmark();
    float Forward = 0, Right = 0, Up = 0, Yaw = 0, Pitch = 0;
    void MoveForward(float V) { Forward = V; }
    void MoveRight(float V) { Right = V; }
    void MoveUp(float V) { Up = V; }
    void StickYaw(float V) { Yaw = V; }
    void StickPitch(float V) { Pitch = V; }
    void MouseYaw(float V);
    void MousePitch(float V);
    void Normal(); void Missing(); void Misaligned(); void Occluded();
    void ToggleWeldingMotion();
    void QuitDemo();
    void Randomize(); void Next(); void Green(); void Capture(); void Inference(); void Home();
};
