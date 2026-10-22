#pragma once
#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryRecordedAudio.generated.h"

class ADarkFactoryWeldingCell;
class FJsonObject;
struct FFactoryAudioBank;
struct FFactoryRecordedControls;
struct FFactoryRecordedRuntime;

/** Immutable PCM plus callback-owned playback; no UObject access on audio thread. */
UCLASS()
class DARKFACTORY_API UDarkFactoryRecordedVoice : public USynthComponent {
    GENERATED_BODY()
public:
    UDarkFactoryRecordedVoice(const FObjectInitializer& Initializer);
    void Configure(TSharedPtr<const FFactoryAudioBank,ESPMode::ThreadSafe> Bank,int32 Voice);
    void SetState(float Motor,float Sealant,float Bed,float Gate);
    bool Cue(int32 Clip,float Gain);
    void Invalidate();
    TSharedRef<FJsonObject> BuildMetadata() const;
protected:
    virtual bool Init(int32& SampleRate) override;
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params) override;
private:
    TSharedPtr<FFactoryRecordedControls,ESPMode::ThreadSafe> Controls;
};

/** Six fixed voices: four moving robot sources, carriage, and room/UI mix. */
UCLASS()
class DARKFACTORY_API ADarkFactoryRecordedAudio : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryRecordedAudio();
    bool Initialize(ADarkFactoryWeldingCell* Cell);
    void UpdateFromCell(bool Enabled,bool Muted);
    void NotifyObserverMode(int32 Mode);
    void NotifyInspectionCapture();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UDarkFactoryRecordedVoice>> Voices;
    TSharedPtr<FFactoryRecordedRuntime> Runtime;
    TWeakObjectPtr<ADarkFactoryWeldingCell> Cell;
    TArray<TWeakObjectPtr<USceneComponent>> Anchors;
    bool Queue(int32 Voice,int32 Clip,float Gain);
    void AudioSmokeTick();
};
