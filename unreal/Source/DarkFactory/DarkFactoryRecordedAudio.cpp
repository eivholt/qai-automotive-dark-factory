#include "DarkFactoryRecordedAudio.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessTypes.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include <atomic>

namespace {
enum EFactoryClip { Motor,Arrival,Departure,Clamp,Weld,Shutter,Pickup,Placement,Sealant,Cooling,Roof,NIR,Thermal,Room,Fan,Cabinet,Release,ClipCount };
constexpr int32 VoiceCount=6,QueueSize=8,MaxShots=4;
constexpr float PeakLimit=.11f;
const TCHAR* Names[]={TEXT("motor"),TEXT("arrival"),TEXT("departure"),TEXT("clamp"),TEXT("weld"),TEXT("shutter"),TEXT("pickup"),TEXT("placement"),TEXT("sealant"),TEXT("cooling"),TEXT("roof"),TEXT("nir"),TEXT("thermal"),TEXT("room"),TEXT("fan"),TEXT("cabinet"),TEXT("release")};
struct FRecordedCue { int32 Clip=-1; float Gain=0; uint32 Serial=0; };
}
struct FFactoryAudioBank { int32 Rate=48000; TArray<float> Clips[ClipCount]; };
struct FFactoryRecordedControls {
    TSharedPtr<const FFactoryAudioBank,ESPMode::ThreadSafe> Bank;
    int32 Voice=0;
    std::atomic<float> Motor{0},Sealant{0},Bed{0},Gate{0},RMS{0},Peak{0};
    std::atomic<uint32> Write{0},Read{0},Reset{0},Dropped{0};
    std::atomic<uint64> Samples{0};
    std::atomic<bool> Finite{true};
    std::atomic<uint32> Started[ClipCount]{};
    FRecordedCue Queue[QueueSize];
};
struct FFactoryRecordedRuntime {
    TSharedPtr<const FFactoryAudioBank,ESPMode::ThreadSafe> Bank;
    FVector Positions[5]{}; FQuat Rotations[4]{};
    double LastClock=-1,LastPhaseSeconds=-1;
    EDarkFactoryProcessPhase LastPhase=EDarkFactoryProcessPhase::Arriving;
    bool Allowed=false,HadFrame=false,CarMoving=false,WeldActive[4]{};
    bool AudioSmoke=false,SmokeRecording=false,SmokeRecordingStopped=false;
    float PreviousUnfocusedVolume=0;
    float OutputGainDb=10.f;
    double SmokeStart=0;
    int32 SmokeCue=0;
    bool SmokeWasAudible=false;
    TArray<int32> Events;
};
namespace {
TSharedPtr<const FFactoryAudioBank,ESPMode::ThreadSafe> LoadBank() {
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes,*FPaths::Combine(FPaths::ProjectDir(),TEXT("../assets/audio/runtime/bank.dfau")))) return nullptr;
    if (Bytes.Num()<16 || Bytes.Num()>32*1024*1024 || FMemory::Memcmp(Bytes.GetData(),"DFAU0001",8)!=0) return nullptr;
    int64 Offset=8;
    auto U32=[&]() -> uint32 {
        if (Offset+4>Bytes.Num()) return 0;
        const uint8* P=Bytes.GetData()+Offset; Offset+=4;
        return uint32(P[0])|(uint32(P[1])<<8)|(uint32(P[2])<<16)|(uint32(P[3])<<24);
    };
    auto B=MakeShared<FFactoryAudioBank,ESPMode::ThreadSafe>();
    B->Rate=U32(); const uint32 Count=U32();
    if (B->Rate!=48000 || Count!=ClipCount) return nullptr;
    for (uint32 I=0;I<Count;++I) {
        const uint32 Id=U32(),N=U32();
        if (Id!=I || N<100 || N>30*48000 || Offset+int64(N)*2>Bytes.Num()) return nullptr;
        auto& PCM=B->Clips[I]; PCM.SetNumUninitialized(N);
        for (uint32 J=0;J<N;++J) {
            const uint8* P=Bytes.GetData()+Offset; Offset+=2;
            PCM[J]=static_cast<int16>(uint16(P[0])|(uint16(P[1])<<8))/32768.f;
        }
    }
    if (Offset!=Bytes.Num()) return nullptr;
    return B;
}
class FRecordedGenerator final : public ISoundGenerator {
public:
    FRecordedGenerator(TSharedPtr<FFactoryRecordedControls,ESPMode::ThreadSafe> C,float Rate)
        : Controls(MoveTemp(C)),Step(Controls->Bank->Rate/FMath::Max(8000.f,Rate)) {
        Ramp=1.f-FMath::Exp(-1.f/(FMath::Max(8000.f,Rate)*.008f));
        for (int32 I=0;I<4;++I) Loop[I]=Controls->Voice*.137*Controls->Bank->Rate;
    }
    virtual int32 GetNumChannels() const override { return 1; }
    virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 512; }
    virtual int32 OnGenerateAudio(float* Out,int32 N) override {
        const float Target=Controls->Gate.load(std::memory_order_relaxed);
        const uint32 Reset=Controls->Reset.load(std::memory_order_acquire);
        if (Reset!=SeenReset) {
            // Let an existing shot release under the mute envelope. A resume or
            // seek clears it, and queued events from earlier epochs are rejected.
            if (Target>0) for (auto& S:Shots) S.Clip=-1;
            SeenReset=Reset;
        }
        uint32 Read=Controls->Read.load(std::memory_order_relaxed);
        const uint32 Write=Controls->Write.load(std::memory_order_acquire);
        while (Read!=Write) {
            const auto Cue=Controls->Queue[Read%QueueSize]; ++Read;
            if (Target>0 && Cue.Serial==Reset && Cue.Clip>=0 && Cue.Clip<ClipCount) {
                int32 Index=-1;
                for (int32 I=0;I<MaxShots;++I) if (Shots[I].Clip<0) { Index=I; break; }
                if (Index<0) { Controls->Dropped.fetch_add(1); continue; }
                Shots[Index]={Cue.Clip,Cue.Gain,0}; Controls->Started[Cue.Clip].fetch_add(1);
            }
        }
        Controls->Read.store(Read,std::memory_order_release);
        if (Target<=0 && Gate<.00001f) {
            Gate=0; for (auto& S:Shots) S.Clip=-1;
            FMemory::Memzero(Out,N*sizeof(float)); Controls->RMS.store(0); Controls->Samples.fetch_add(N); return N;
        }
        const float M=Controls->Motor.load(),S=Controls->Sealant.load(),B=Controls->Bed.load();
        double Energy=0; float Peak=0;
        for (int32 I=0;I<N;++I) {
            Gate+=(Target-Gate)*Ramp; MotorLevel+=(M-MotorLevel)*Ramp; SealantLevel+=(S-SealantLevel)*Ramp;
            float V=0;
            if (Controls->Voice<4) {
                V+=LoopSample(EFactoryClip::Motor,Loop[0])*MotorLevel*.22f;
                if (Controls->Voice==0) V+=LoopSample(EFactoryClip::Sealant,Loop[1])*SealantLevel*.10f;
            } else if (Controls->Voice==5) {
                V+=B*(LoopSample(Room,Loop[0])*.025f+LoopSample(Fan,Loop[1])*.065f+LoopSample(Cabinet,Loop[2])*.012f);
            }
            for (auto& Shot:Shots) if (Shot.Clip>=0) {
                const auto& PCM=Controls->Bank->Clips[Shot.Clip];
                if (Shot.Position>=PCM.Num()-1) { Shot.Clip=-1; continue; }
                const float MotionGate=Controls->Voice==4 && (Shot.Clip==Arrival || Shot.Clip==Departure) ? MotorLevel:1.f;
                V+=ReadSample(PCM,Shot.Position)*Shot.Gain*MotionGate; Shot.Position+=Step;
            }
            V*=Gate;
            // Leave ordinary levels linear; soften rare overlapping transients
            // instead of flattening pickup/contact peaks at a hard ceiling.
            const float Magnitude=FMath::Abs(V);
            if (Magnitude>.08f) {
                const float Excess=Magnitude-.08f;
                V=FMath::Sign(V)*(.08f+.03f*Excess/(.03f+Excess));
            }
            if (!FMath::IsFinite(V)) { V=0; Controls->Finite.store(false); }
            Out[I]=V; Energy+=V*V; Peak=FMath::Max(Peak,FMath::Abs(V));
        }
        Controls->Samples.fetch_add(N); Controls->RMS.store(N ? FMath::Sqrt(Energy/N):0);
        Controls->Peak.store(FMath::Max(Peak,Controls->Peak.load())); return N;
    }
private:
    struct FShot { int32 Clip=-1; float Gain=0; double Position=0; } Shots[MaxShots];
    TSharedPtr<FFactoryRecordedControls,ESPMode::ThreadSafe> Controls;
    double Step,Loop[4]{};
    float Ramp=0,Gate=0,MotorLevel=0,SealantLevel=0;
    uint32 SeenReset=0;
    float ReadSample(const TArray<float>& PCM,double Position) const {
        const int32 A=FMath::FloorToInt(Position),B=FMath::Min(A+1,PCM.Num()-1);
        return FMath::Lerp(PCM[A],PCM[B],static_cast<float>(Position-A));
    }
    float LoopSample(int32 Clip,double& Position) {
        const auto& PCM=Controls->Bank->Clips[Clip];
        while (Position>=PCM.Num()-1) Position-=PCM.Num()-1;
        const float V=ReadSample(PCM,Position); Position+=Step; return V;
    }
};
TSharedRef<FJsonObject> RecordedCallbackCheck(TSharedPtr<const FFactoryAudioBank,ESPMode::ThreadSafe> Bank) {
    auto C=MakeShared<FFactoryRecordedControls,ESPMode::ThreadSafe>(); C->Bank=MoveTemp(Bank);
    // A cue is queued before pause; no callback happens until after resume.
    // The old epoch must be rejected even when the audio device missed pause.
    C->Queue[0]={Shutter,.1f,0}; C->Write.store(1); C->Reset.store(1); C->Gate.store(1); C->Motor.store(.5f);
    FRecordedGenerator G(C,48000); TArray<float> Buffer; Buffer.SetNumZeroed(12000);
    G.OnGenerateAudio(Buffer.GetData(),512);
    const bool Stale=C->Started[Shutter].load()==0;
    C->Queue[1]={NIR,.1f,1}; C->Write.store(2); G.OnGenerateAudio(Buffer.GetData(),512);
    const bool Fresh=C->Started[NIR].load()==1 && C->RMS.load()>0;
    C->Gate.store(0); C->Reset.store(2); G.OnGenerateAudio(Buffer.GetData(),Buffer.Num()); G.OnGenerateAudio(Buffer.GetData(),512);
    bool Silent=true; for (int32 I=0;I<512;++I) Silent&=Buffer[I]==0;
    C->Reset.store(3); C->Gate.store(1);
    const uint32 Read=C->Read.load();
    for (uint32 I=0;I<QueueSize;++I) C->Queue[(Read+I)%QueueSize]={NIR,.1f,3};
    C->Write.store(Read+QueueSize); G.OnGenerateAudio(Buffer.GetData(),512);
    const bool Bounded=C->Dropped.load()==QueueSize-MaxShots && C->Finite.load() && C->Peak.load()<=PeakLimit;
    auto O=MakeShared<FJsonObject>();
    O->SetBoolField(TEXT("stale_cue_rejected_after_pause_resume"),Stale); O->SetBoolField(TEXT("fresh_cue_plays"),Fresh);
    O->SetBoolField(TEXT("settled_mute_exact_silence"),Silent); O->SetBoolField(TEXT("overload_bounded"),Bounded);
    O->SetBoolField(TEXT("passed"),Stale && Fresh && Silent && Bounded); return O;
}
}
UDarkFactoryRecordedVoice::UDarkFactoryRecordedVoice(const FObjectInitializer& O) : Super(O) {
    NumChannels=1; PreferredBufferLength=512; bAutoActivate=false; bAutoDestroy=false; bAlwaysPlay=true;
    bAllowSpatialization=true; bOverrideAttenuation=true;
    AttenuationOverrides.bAttenuate=true; AttenuationOverrides.bSpatialize=true;
    AttenuationOverrides.AttenuationShapeExtents=FVector(250,0,0); AttenuationOverrides.FalloffDistance=4500;
    AttenuationOverrides.bEnableOcclusion=false; AttenuationOverrides.bEnableReverbSend=false;
    bStopWhenOwnerDestroyed=true;
}
void UDarkFactoryRecordedVoice::Configure(TSharedPtr<const FFactoryAudioBank,ESPMode::ThreadSafe> Bank,int32 Voice) {
    check(!IsPlaying()); Controls=MakeShared<FFactoryRecordedControls,ESPMode::ThreadSafe>();
    Controls->Bank=MoveTemp(Bank); Controls->Voice=Voice;
    if (Voice==5) { bAllowSpatialization=false; AttenuationOverrides.bAttenuate=false; AttenuationOverrides.bSpatialize=false; }
}
bool UDarkFactoryRecordedVoice::Init(int32& Rate) { return Controls.IsValid() && Rate>=8000; }
ISoundGeneratorPtr UDarkFactoryRecordedVoice::CreateSoundGenerator(const FSoundGeneratorInitParams& P) { return MakeShared<FRecordedGenerator,ESPMode::ThreadSafe>(Controls,P.SampleRate); }
void UDarkFactoryRecordedVoice::SetState(float M,float S,float B,float G) {
    Controls->Motor.store(FMath::Clamp(M,0.f,1.f)); Controls->Sealant.store(FMath::Clamp(S,0.f,1.f));
    Controls->Bed.store(FMath::Clamp(B,0.f,1.f)); Controls->Gate.store(FMath::Clamp(G,0.f,1.f));
}
void UDarkFactoryRecordedVoice::Invalidate() { Controls->Reset.fetch_add(1,std::memory_order_release); }
bool UDarkFactoryRecordedVoice::Cue(int32 Clip,float Gain) {
    if (Clip<0 || Clip>=ClipCount || Controls->Gate.load()<=0) return false;
    const uint32 W=Controls->Write.load(),R=Controls->Read.load(std::memory_order_acquire);
    if (W-R>=QueueSize) { Controls->Dropped.fetch_add(1); return false; }
    Controls->Queue[W%QueueSize]={Clip,FMath::Clamp(Gain,0.f,.5f),Controls->Reset.load(std::memory_order_acquire)};
    Controls->Write.store(W+1,std::memory_order_release); return true;
}
TSharedRef<FJsonObject> UDarkFactoryRecordedVoice::BuildMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("voice"),Controls->Voice);
    O->SetNumberField(TEXT("generated_samples"),double(Controls->Samples.load())); O->SetNumberField(TEXT("rms"),Controls->RMS.load());
    O->SetNumberField(TEXT("peak"),Controls->Peak.load()); O->SetBoolField(TEXT("finite"),Controls->Finite.load());
    O->SetNumberField(TEXT("motor"),Controls->Motor.load()); O->SetNumberField(TEXT("gate"),Controls->Gate.load());
    O->SetNumberField(TEXT("sealant"),Controls->Sealant.load()); O->SetNumberField(TEXT("bed"),Controls->Bed.load());
    O->SetNumberField(TEXT("dropped"),Controls->Dropped.load()); O->SetBoolField(TEXT("spatialized"),Controls->Voice!=5);
    auto Counts=MakeShared<FJsonObject>(); for (int32 I=0;I<ClipCount;++I) Counts->SetNumberField(Names[I],Controls->Started[I].load());
    O->SetObjectField(TEXT("started"),Counts); return O;
}
ADarkFactoryRecordedAudio::ADarkFactoryRecordedAudio() {
    PrimaryActorTick.bCanEverTick=false; SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("RecordedAudioRoot")); SetRootComponent(SceneRoot);
}
bool ADarkFactoryRecordedAudio::Initialize(ADarkFactoryWeldingCell* InCell) {
    Runtime=MakeShared<FFactoryRecordedRuntime>(); Runtime->Bank=LoadBank();
    if (!Runtime->Bank || !InCell) return false;
    FParse::Value(FCommandLine::Get(),TEXT("DFAudioGainDb="),Runtime->OutputGainDb);
    Runtime->OutputGainDb=FMath::IsFinite(Runtime->OutputGainDb) ? FMath::Clamp(Runtime->OutputGainDb,-12.f,18.f):10.f;
    Cell=InCell; TArray<USceneComponent*> Components; InCell->GetComponents(Components);
    for (int32 I=0;I<4;++I) {
        const auto Timing=InCell->GetRobotTiming(I,InCell->GetMotionTime()); USceneComponent* Anchor=nullptr;
        for (auto* C:Components) if (C->GetName()==Timing.RobotId+TEXT("_tool_roll")) { Anchor=C; break; }
        if (!Anchor) return false; Anchors.Add(Anchor);
    }
    for (int32 I=0;I<VoiceCount;++I) {
        auto* V=NewObject<UDarkFactoryRecordedVoice>(this,*FString::Printf(TEXT("RecordedVoice%d"),I));
        V->SetupAttachment(SceneRoot); V->Configure(Runtime->Bank,I); V->RegisterComponent(); V->Initialize();
        V->SetVolumeMultiplier(FMath::Pow(10.f,Runtime->OutputGainDb/20.f)); V->Start(); Voices.Add(V);
    }
    Runtime->Events.SetNumZeroed(ClipCount);
    Runtime->AudioSmoke=FParse::Param(FCommandLine::Get(),TEXT("DFAudioSmoke"));
    UE_LOG(LogTemp,Display,TEXT("DF_RECORDED_AUDIO ready clips=%d voices=%d pcm_frames=%d"),ClipCount,VoiceCount,Runtime->Bank->Clips[Motor].Num());
    return true;
}
void ADarkFactoryRecordedAudio::EndPlay(const EEndPlayReason::Type R) {
    if (Runtime && Runtime->SmokeRecording) FApp::SetUnfocusedVolumeMultiplier(Runtime->PreviousUnfocusedVolume);
    for (const auto& V:Voices) if (V) { V->SetState(0,0,0,0); V->Stop(); } Super::EndPlay(R);
}
bool ADarkFactoryRecordedAudio::Queue(int32 Voice,int32 Clip,float Gain) {
    if (!Runtime->Allowed || !Voices.IsValidIndex(Voice)) return false;
    const bool Sent=Voices[Voice]->Cue(Clip,Gain); if (Sent) ++Runtime->Events[Clip]; return Sent;
}
void ADarkFactoryRecordedAudio::NotifyObserverMode(int32 Mode) {
    if (!Runtime) return;
    if (Mode==1) Queue(5,NIR,.11f); else if (Mode==2) Queue(5,Thermal,.11f); else if (Mode==3) Queue(5,Roof,.09f);
}
void ADarkFactoryRecordedAudio::NotifyInspectionCapture() { if (Runtime) Queue(3,Shutter,.11f); }
void ADarkFactoryRecordedAudio::UpdateFromCell(bool Enabled,bool Muted) {
    auto* C=Cell.Get(); if (!C || !Runtime || Voices.Num()!=VoiceCount) return;
    if (Runtime->AudioSmoke) { if (GetWorld()->HasBegunPlay()) AudioSmokeTick(); return; }
    const double Clock=C->GetEffectTime(),Dt=Clock-Runtime->LastClock;
    const bool WasAllowed=Runtime->Allowed;
    Runtime->Allowed=Enabled && !Muted && C->IsAnimating();
    if (WasAllowed!=Runtime->Allowed) for (const auto& V:Voices) V->Invalidate();
    const float Gate=Runtime->Allowed ? 1.f:0.f;
    const bool Continuous=Runtime->HadFrame && Dt>0 && Dt<.5;
    const auto Phase=C->GetPanelPhaseForAudio(); const double Time=C->GetPanelPhaseSecondsForAudio();
    const bool SamePhase=Runtime->HadFrame && Phase==Runtime->LastPhase && Time>=Runtime->LastPhaseSeconds;
    const double Previous=SamePhase ? Runtime->LastPhaseSeconds:-1.e-6;
    const bool Advance=Continuous && Runtime->Allowed;
    for (int32 I=0;I<4;++I) {
        const FTransform T=Anchors[I]->GetComponentTransform();
        const double Travel=Continuous ? FVector::Distance(T.GetLocation(),Runtime->Positions[I])/Dt:0;
        const double Turn=Continuous ? T.GetRotation().AngularDistance(Runtime->Rotations[I])/Dt:0;
        const float Movement=FMath::Clamp(static_cast<float>((Travel+Turn*65.)/450.),0.f,1.f);
        float Seal=0;
        if (I==0 && Phase==EDarkFactoryProcessPhase::AdhesiveApplication && Advance) for (int32 B=0;B<2;++B) {
            const double P=C->GetPanelAdhesiveProgress(B,Time); if (P>0 && P<1) Seal=1;
        }
        Voices[I]->SetState(Movement,Seal,0,Gate); Voices[I]->SetWorldLocation(T.GetLocation());
        Runtime->Positions[I]=T.GetLocation(); Runtime->Rotations[I]=T.GetRotation();
    }
    const FVector Car=C->GetCarriageRoot()->GetComponentLocation();
    const double CarSpeed=Continuous ? FVector::Distance(Car,Runtime->Positions[4])/Dt:0;
    const bool CarMoving=Advance && CarSpeed>20 && FVector::Distance(Car,Runtime->Positions[4])<500;
    Voices[4]->SetState(CarMoving ? FMath::Clamp(float(CarSpeed/400.),.3f,1.f):0,0,0,Gate); Voices[4]->SetWorldLocation(Car);
    Voices[5]->SetState(0,0,1,Gate);
    if (CarMoving && !Runtime->CarMoving) Queue(4,Phase==EDarkFactoryProcessPhase::Departing ? Departure:Arrival,.16f);
    auto Crossed=[&](double At) { return Advance && Previous<At && Time>=At; };
    if (C->IsProcessControlled()) {
        if (Phase==EDarkFactoryProcessPhase::PanelPlacement) {
            if (Crossed(1.25)) Queue(2,Pickup,.12f);
            if (Crossed(5.55)) Queue(2,Placement,.13f);
        }
        if (Phase==EDarkFactoryProcessPhase::HolderRetract && Crossed(.02)) Queue(2,Release,.12f);
        if (Phase==EDarkFactoryProcessPhase::Welding) for (int32 I=0;I<C->PanelWeldCount;++I) {
            if (Crossed(C->PanelWeldStartSeconds(I)-.15)) Queue(1,Clamp,.12f);
            if (I==C->PanelWeldCount-1 && Crossed(C->PanelWeldStartSeconds(I)+.5)) Queue(1,Cooling,.07f);
        }
    }
    bool Active[4]{};
    for (const auto& E:C->GetWeldEmitters()) if (E.EmitterIndex>=0 && E.EmitterIndex<4 && E.Intensity>0) Active[E.EmitterIndex]=true;
    for (int32 I=0;I<4;++I) {
        if (Advance && Active[I] && !Runtime->WeldActive[I]) Queue(I,Weld,.13f);
        Runtime->WeldActive[I]=Active[I];
    }
    if (Dt<0 || Dt>.5) for (const auto& V:Voices) V->Invalidate();
    Runtime->CarMoving=CarMoving; Runtime->Positions[4]=Car; Runtime->LastClock=Clock; Runtime->LastPhase=Phase;
    Runtime->LastPhaseSeconds=Time; Runtime->HadFrame=true;
}
TSharedRef<FJsonObject> ADarkFactoryRecordedAudio::BuildMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("model"),TEXT("approved-recorded-factory-audio-v1"));
    O->SetNumberField(TEXT("maximum_sources"),VoiceCount); O->SetNumberField(TEXT("maximum_shots_per_source"),MaxShots);
    O->SetNumberField(TEXT("queue_capacity_per_source"),QueueSize); O->SetNumberField(TEXT("peak_limit_per_source"),PeakLimit);
    O->SetBoolField(TEXT("callback_uobject_access"),false); O->SetNumberField(TEXT("sensor_readbacks"),0);
    O->SetNumberField(TEXT("sample_rate"),Runtime->Bank->Rate);
    O->SetNumberField(TEXT("output_gain_db"),Runtime->OutputGainDb);
    auto Events=MakeShared<FJsonObject>(); for (int32 I=0;I<ClipCount;++I) Events->SetNumberField(Names[I],Runtime->Events[I]);
    O->SetObjectField(TEXT("events"),Events);
    TArray<TSharedPtr<FJsonValue>> Rows; for (const auto& V:Voices) Rows.Add(MakeShared<FJsonValueObject>(V->BuildMetadata()));
    O->SetArrayField(TEXT("sources"),Rows); return O;
}
void ADarkFactoryRecordedAudio::AudioSmokeTick() {
    if (Runtime->SmokeStart<=0) Runtime->SmokeStart=FPlatformTime::Seconds();
    const double Age=FPlatformTime::Seconds()-Runtime->SmokeStart;
    FString Root=FPaths::Combine(FPaths::ProjectDir(),TEXT("../artifacts/audio-smoke")); FParse::Value(FCommandLine::Get(),TEXT("DFOutput="),Root);
    if (Age>.2 && !Runtime->SmokeRecording) {
        IFileManager::Get().MakeDirectory(*Root,true);
        Runtime->PreviousUnfocusedVolume=FApp::GetUnfocusedVolumeMultiplier(); FApp::SetUnfocusedVolumeMultiplier(1.f);
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this,12.f);
        Runtime->SmokeRecording=true;
    }
    const bool Audible=Age<10.; Runtime->Allowed=Audible;
    for (int32 I=0;I<VoiceCount;++I) Voices[I]->SetState(I<4 ? .5f:0.f,I==0 ? .5f:0.f,I==5 ? 1.f:0.f,Audible ? 1.f:0.f);
    if (Audible && Age>.5 && Runtime->SmokeCue<ClipCount && Age>.5+Runtime->SmokeCue*.45) {
        const int32 Clip=Runtime->SmokeCue++;
        Queue(Clip%VoiceCount,Clip,.1f);
    }
    if (Age>2. && Age<3.) for (const auto& V:Voices) Runtime->SmokeWasAudible|=V->BuildMetadata()->GetNumberField(TEXT("rms"))>1.e-5;
    if (Age>10.5 && !Runtime->SmokeRecordingStopped) {
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("engine_audio"),Root);
        Runtime->SmokeRecordingStopped=true;
    }
    if (Age<11.5) return;
    auto Report=BuildMetadata(); bool Passed=Runtime->SmokeWasAudible && Runtime->SmokeCue==ClipCount;
    const auto CallbackCheck=RecordedCallbackCheck(Runtime->Bank);
    Report->SetObjectField(TEXT("callback_self_check"),CallbackCheck); Passed&=CallbackCheck->GetBoolField(TEXT("passed"));
    int32 Started[ClipCount]{};
    for (const auto& V:Voices) {
        const auto M=V->BuildMetadata(); Passed&=M->GetBoolField(TEXT("finite")) && M->GetNumberField(TEXT("peak"))<=PeakLimit+1.e-6
            && M->GetNumberField(TEXT("rms"))==0 && M->GetNumberField(TEXT("generated_samples"))>0 && M->GetNumberField(TEXT("dropped"))==0;
        for (int32 I=0;I<ClipCount;++I) Started[I]+=M->GetObjectField(TEXT("started"))->GetIntegerField(Names[I]);
    }
    for (int32 I=0;I<ClipCount;++I) Passed&=Started[I]==1;
    const FString Recording=FPaths::Combine(Root,TEXT("engine_audio.wav"));
    const bool RecordingSaved=IFileManager::Get().FileSize(*Recording)>44;
    Passed&=RecordingSaved; Report->SetBoolField(TEXT("engine_mix_saved"),RecordingSaved);
    Report->SetBoolField(TEXT("audible_buffers_observed"),Runtime->SmokeWasAudible); Report->SetBoolField(TEXT("passed"),Passed);
    IFileManager::Get().MakeDirectory(*Root,true); FString JSON;
    FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));
    Passed&=FFileHelper::SaveStringToFile(JSON,*FPaths::Combine(Root,TEXT("audio.json")));
    UE_LOG(LogTemp,Display,TEXT("DF_AUDIO_SMOKE passed=%d"),Passed); FPlatformMisc::RequestExitWithStatus(false,Passed ? 0:1);
}
