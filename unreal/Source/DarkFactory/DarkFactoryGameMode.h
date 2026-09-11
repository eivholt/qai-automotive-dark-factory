#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "DarkFactoryGameMode.generated.h"
class UMaterialInstanceDynamic;
UCLASS()
class DARKFACTORY_API ADarkFactoryGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    ADarkFactoryGameMode();
    virtual void StartPlay() override;
};
UCLASS()
class DARKFACTORY_API ADarkFactoryHUD : public AHUD {
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    bool HasNightVisionMaterial() const { return SensorDisplayMaterial != nullptr; }
private:
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SensorDisplayMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThermalDisplayMaterial;
    bool bTriedNightVisionMaterial = false;
};
