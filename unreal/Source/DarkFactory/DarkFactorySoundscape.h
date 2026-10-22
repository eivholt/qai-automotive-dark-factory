#pragma once
#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "GameFramework/Actor.h"
#include "DarkFactorySoundscape.generated.h"

class ADarkFactoryWeldingCell;
class ADarkFactoryRecordedAudio;
class FJsonObject;
struct FDarkFactoryAudioControls;

/** One mono source; the generator owns DSP state, never dereferences a UObject. */
UCLASS()
class DARKFACTORY_API UDarkFactoryMachineSynth : public USynthComponent {
    GENERATED_BODY()
public:
    UDarkFactoryMachineSynth(const FObjectInitializer& ObjectInitializer);
    void ConfigureVoice(int32 Index);
    void SetLevels(float Motor, float Weld, float Gate);
    TSharedRef<FJsonObject> BuildMetadata() const;
protected:
    virtual bool Init(int32& SampleRate) override;
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params) override;
private:
    TSharedPtr<FDarkFactoryAudioControls,ESPMode::ThreadSafe> Controls;
};

/** Six bounded spatial sources, driven by the imported cell's actual local clocks. */
UCLASS()
class DARKFACTORY_API ADarkFactorySoundscape : public AActor {
    GENERATED_BODY()
public:
    ADarkFactorySoundscape();
    bool Initialize(ADarkFactoryWeldingCell* Cell,bool PreferRecorded=false);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void UpdateFromCell();
    void SetEnabled(bool Enabled);
    void SetMuted(bool Muted);
    void NotifyObserverMode(int32 Mode);
    void NotifyInspectionCapture();
    bool IsMuted() const { return bMuted; }
    bool IsEnabled() const { return bEnabled; }
    TSharedRef<FJsonObject> BuildMetadata() const;
    // Offline DSP audition only: a mono first-cycle mix, not the live spatial mixer.
    TSharedRef<FJsonObject> SaveOfflinePreviewForQA(const FString& WavPath) const;
    static TSharedRef<FJsonObject> BuildDSPSelfCheck();
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UDarkFactoryMachineSynth>> Sources;
    UPROPERTY() TObjectPtr<ADarkFactoryRecordedAudio> Recorded;
    TWeakObjectPtr<ADarkFactoryWeldingCell> SourceCell;
    TArray<TWeakObjectPtr<USceneComponent>> RobotAnchors;
    bool bInitialized=false, bEnabled=true, bMuted=false;
    double LastSourceSeconds=0;
};
