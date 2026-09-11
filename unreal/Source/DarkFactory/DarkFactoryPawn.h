#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DarkFactoryPawn.generated.h"
class UCameraComponent;
class UMaterialInstanceDynamic;
class FDarkFactoryViewExtension;
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
    void SetThermalPalette(int32 Palette);
    bool IsThermalView() const { return bThermalView; }
    bool HasThermalMaterial() const { return ThermalMaterial != nullptr; }
    void ApplyNightVision(UMaterialInstanceDynamic* Material) const;
    bool IsNightVisionQuality() const { return bNightVisionQuality; }
    bool HasNightVisionMaterial() const { return NightVisionMaterial != nullptr; }
    FString GetNightVisionStatus() const;
    void SetCaptureAutomation(bool Active) { bAutomation=Active; Forward=Right=Up=Yaw=Pitch=0; }
    void SetNightVisionSmokeFrame(float Frame) { NightVisionFrameOverride = Frame; }
private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> NightVisionMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThermalMaterial;
    bool bThermalView = false;
    void ToggleHeatTransfer(); void ToggleThermalFields(); void CoolThermal();
    void CycleView(); void ToggleThermal(); void CycleThermalPalette(); void NextThermalCase(); void SaveThermal();
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
    float Forward = 0, Right = 0, Up = 0, Yaw = 0, Pitch = 0;
    void MoveForward(float V) { Forward = V; }
    void MoveRight(float V) { Right = V; }
    void MoveUp(float V) { Up = V; }
    void StickYaw(float V) { Yaw = V; }
    void StickPitch(float V) { Pitch = V; }
    void MouseYaw(float V);
    void MousePitch(float V);
    void Normal(); void Missing(); void Misaligned(); void Occluded();
    void Randomize(); void Next(); void Green(); void Capture(); void Inference(); void Home();
};
