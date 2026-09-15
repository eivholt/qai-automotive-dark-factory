#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "DarkFactoryColliderOverlay.h"
#include "DarkFactoryGameMode.generated.h"
class UMaterialInstanceDynamic;
class UTexture2D;
class ADarkFactoryThermalRig;
class ADarkFactoryWeldingCell;
class ADarkFactoryProcessController;
class ADarkFactoryWorld;
class FJsonObject;
UCLASS()
class DARKFACTORY_API ADarkFactoryGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    ADarkFactoryGameMode();
    virtual void StartPlay() override;
protected:
    bool bPanelMapDefaults=false;
};
/** Current editor map; explicit launcher/fixture selections still take precedence. */
UCLASS()
class DARKFACTORY_API ADarkFactoryPanelGameMode : public ADarkFactoryGameMode {
    GENERATED_BODY()
public:
    ADarkFactoryPanelGameMode();
};
UCLASS()
class DARKFACTORY_API ADarkFactoryHUD : public AHUD {
    GENERATED_BODY()
public:
    ADarkFactoryHUD();
    virtual void Tick(float DeltaSeconds) override;
    virtual void DrawHUD() override;
    double GetSmoothedFramesPerSecond() const { return SmoothedFrameSeconds>0 ? 1./SmoothedFrameSeconds:0.; }
    uint64 GetFrameRateSampleCount() const { return FrameRateSamples; }
    TSharedRef<FJsonObject> BuildProductionStatusMetadata() const;
    void SetColliderOverlayMode(int32 Mode);
    int32 GetColliderOverlayMode() const { return ColliderOverlayMode; }
    void CycleColliderOverlay() { SetColliderOverlayMode((ColliderOverlayMode+1)%3); }
    TSharedRef<FJsonObject> BuildColliderOverlayMetadata();
    bool HasNightVisionMaterial() const { return SensorDisplayMaterial != nullptr; }
    // Actual procedural HUD draw bounds, excluding labels, scales and borders.
    TSharedRef<FJsonObject> GetThermalQARegions() const;
private:
    FIntRect ThermalPreviewQARegion=FIntRect(0,0,0,0);
    FIntRect ThermalObserverQARegion=FIntRect(0,0,0,0);
    int32 ColliderOverlayMode=0;
    double ColliderCacheTime=-1;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> ColliderComponents;
    TArray<FDarkFactoryColliderBox> ColliderBoxes;
    int32 ColliderDrawnLines=0,ColliderVisibleBoxes=0;
    bool bColliderTruncated=false;
    FBox2D ColliderScreenBounds=FBox2D(ForceInit);
    void GatherColliderBoxes();
    void DrawColliderOverlay();
    void DrawColliderLegend(float RightPanelX,bool Imported);
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SensorDisplayMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThermalDisplayMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThermalMonochromeDisplayMaterial;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ProcessThermalDisplayMaterials;
    UPROPERTY() TObjectPtr<UTexture2D> ThermalScaleTexture;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ThermalScaleMaterials;
    void DrawProductionStatus(const ADarkFactoryWeldingCell* Cell,float RightPanelX);
    void DrawProcessStatus(const ADarkFactoryProcessController* Process,float RightPanelX);
    void DrawProcessCaptures(const ADarkFactoryProcessController* Process,const ADarkFactoryWorld* World,float RightPanelX,float PanelWidth);
    void InitializeThermalScales(UMaterialInterface* Parent);
    void DrawThermalScale(int32 Palette,float MinC,float MaxC,float X,float Y,float Width,float Scale=1.f);
    void DrawObserverThermalScale(const ADarkFactoryThermalRig* Thermal,float X,float Y,float Width,float Scale=1.f);
    bool bTriedNightVisionMaterial = false;
    // Wall-clock game-frame intervals, independent of scene motion/heat clocks.
    double LastFrameTimeSeconds = 0;
    double SmoothedFrameSeconds = 0;
    uint64 LastFrameNumber = MAX_uint64;
    uint64 FrameRateSamples = 0;
};
