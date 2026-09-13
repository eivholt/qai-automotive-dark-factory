#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryControlCabinet.generated.h"
class ADarkFactoryWeldingCell;
class UStaticMeshComponent;
class URectLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class FJsonObject;
/** Existing cabinet glass with GPU gauges; authored control display, not EVK telemetry. */
UCLASS()
class DARKFACTORY_API ADarkFactoryControlCabinet : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryControlCabinet();
    bool Initialize(ADarkFactoryWeldingCell* Cell);
    virtual void Tick(float DeltaSeconds) override;
    void SetEnabled(bool Enabled);
    bool IsEnabled() const { return bEnabled; }
    void SetPanelPower(bool Powered);
    bool IsPanelPowered() const { return bPowered; }
    void UpdateFromCell(bool Force=false);
    void GetInspectionView(FVector& Location,FRotator& Rotation) const;
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    struct FButton { FString Id,Color; FVector Center=FVector::ZeroVector; float Radius=1,Level=0; };
    TWeakObjectPtr<ADarkFactoryWeldingCell> SourceCell;
    TWeakObjectPtr<UStaticMeshComponent> Glass;
    UPROPERTY() TObjectPtr<UMaterialInterface> OriginalMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;
    UPROPERTY() TObjectPtr<URectLightComponent> ScreenLight;
    FVector Center=FVector::ZeroVector,Normal=FVector::ForwardVector,Right=FVector::RightVector,Up=FVector::UpVector;
    double Width=0,Height=0,AppliedSeconds=0,CycleSeconds=0,CycleProgress=0;
    int64 LastSample=-1;
    FTransform LastTransform=FTransform::Identity;
    TArray<FButton> Buttons;
    int32 Phases[4]={0,0,0,0};
    float Progress[4]={0,0,0,0};
    bool bInitialized=false,bEnabled=false,bPowered=true,bPaused=false;
    FString ParentName,SourceManifest;
};
