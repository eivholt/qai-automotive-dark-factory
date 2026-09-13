#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryFactoryLights.generated.h"

class FJsonObject;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class URectLightComponent;
class UStaticMeshComponent;

/** Observer-only fluorescent banks. Fixed scene captures never see their geometry or illumination. */
UCLASS()
class DARKFACTORY_API ADarkFactoryFactoryLights : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryFactoryLights();
    bool Initialize(bool ImportedScene);
    void SetEnabled(bool Enabled);
    bool IsEnabled() const { return bEnabled; }
    /** Nonnegative time applies and pins startup immediately; negative resumes from the held time. */
    void SetStartupTimeForQA(float Seconds);
    TSharedRef<FJsonObject> BuildMetadata() const;
    virtual void Tick(float DeltaSeconds) override;

private:
    enum { BankCount=6, TubesPerBank=3 };
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Tubes;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Housings;
    UPROPERTY() TArray<TObjectPtr<URectLightComponent>> Lights;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> TubeMaterials;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HousingMaterial;
    bool bInitialized=false, bImportedScene=false, bEnabled=false, bStartupTimePinned=false;
    float StartupElapsedSeconds=0, NominalBankLumens=0;
    double ActivationWorldSeconds=0;
    uint32 ActivationCount=0;
    float AppliedGains[BankCount]={};
    void ApplyState();
};