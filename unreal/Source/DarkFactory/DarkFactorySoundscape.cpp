#include "DarkFactorySoundscape.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryRecordedAudio.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <atomic>

struct FDarkFactoryAudioControls {
    explicit FDarkFactoryAudioControls(int32 InVoice) : Voice(InVoice) {}
    const int32 Voice;
    std::atomic<float> Motor{0}, Weld{0}, Gate{0};
    std::atomic<uint64> GeneratedSamples{0};
    std::atomic<float> LastRMS{0}, Peak{0};
    std::atomic<bool> Finite{true};
};
namespace {
constexpr int32 SourceCount=6, SampleRateQA=48000;
constexpr float SourcePeakLimit=.11f;
const TCHAR* SourceNames[SourceCount]={TEXT("Robot 1"),TEXT("Robot 2"),TEXT("Robot 3"),TEXT("Robot 4"),TEXT("Carriage drive"),TEXT("Extractor")};
float Smooth01(double X) { const float T=static_cast<float>(FMath::Clamp(X,0.,1.)); return T*T*T*(10.f+T*(-15.f+6.f*T)); }
float MovementEnvelope(double X) {
    // Same knots/easing as the authored joint curve; quiet at each held pose.
    constexpr double Knots[]={0,.20,.56,.80,1};
    if (X<=0 || X>=1) return 0;
    for (int32 I=0;I<4;++I) if (X<=Knots[I+1]) {
        const double U=(X-Knots[I])/(Knots[I+1]-Knots[I]);
        return static_cast<float>(16.*U*U*(1.-U)*(1.-U));
    }
    return 0;
}
struct FAudioFrame { float Motor[SourceCount]={}, Weld[SourceCount]={}; };
FAudioFrame EvaluateFrame(const ADarkFactoryWeldingCell* Cell,double Seconds) {
    FAudioFrame F;
    if (!Cell) return F;
    const double T=FMath::Fmod(FMath::Max(0.,Seconds),Cell->GetCycleDurationSeconds());
    for (int32 I=0;I<4;++I) {
        const auto R=Cell->GetRobotTiming(I,Seconds);
        if (!R.Valid) continue;
        if (T>=R.ApproachStartSeconds && T<R.ApproachEndSeconds) F.Motor[I]=MovementEnvelope((T-R.ApproachStartSeconds)/(R.ApproachEndSeconds-R.ApproachStartSeconds));
        else if (T>=R.WeldEndSeconds && T<R.WithdrawEndSeconds) F.Motor[I]=MovementEnvelope(1.-(T-R.WeldEndSeconds)/(R.WithdrawEndSeconds-R.WeldEndSeconds));
        else if (T>=R.WeldStartSeconds && T<R.WeldEndSeconds) {
            const double Age=T-R.WeldStartSeconds;
            F.Motor[I]=.065f; // Quiet precision tracking, never a fast-move cue.
            const double Elapsed=Age-(Cell->IsRobotTimingRandomized() ? 0.:I*.11);
            if (Elapsed>=0) {
                const double P=FMath::Fmod(Elapsed,1.05);
                const float Burst=P<.10 ? 1.f-Smooth01(P/.10):0.f;
                const float Fountain=P>=.10 && P<.42 ? FMath::Sin(PI*(P-.10)/.32):0.f;
                const float Fallout=P>=.42 && P<.82 ? 1.f-Smooth01((P-.42)/.40):0.f;
                F.Weld[I]=Smooth01(Age/.18)*Smooth01((R.WeldEndSeconds-T)/.18)*(.9f*Burst+.58f*Fountain+.24f*Fallout);
            }
        }
    }
    const double Departure=Cell->GetCarriageDepartureStartSeconds();
    const double U=T<3. ? T/3. : T>=Departure && T<Cell->GetCarriageDepartureEndSeconds() ? (T-Departure)/3.5:-1.;
    if (U>=0 && U<=1) F.Motor[4]=static_cast<float>(16.*U*U*(1.-U)*(1.-U));
    F.Motor[5]=.42f; // Extractor ambience while the cell is running, including dwell.
    return F;
}
class FFactorySoundGenerator final : public ISoundGenerator {
public:
    FFactorySoundGenerator(TSharedPtr<FDarkFactoryAudioControls,ESPMode::ThreadSafe> InControls,float InRate)
        : Controls(MoveTemp(InControls)), Rate(FMath::Max(8000.f,InRate)), Seed(0x923AC1u+7919u*Controls->Voice) {
        Ramp=1.f-FMath::Exp(-1.f/(Rate*.008f));
    }
    virtual int32 GetNumChannels() const override { return 1; }
    virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 512; }
    virtual int32 OnGenerateAudio(float* OutAudio,int32 NumSamples) override {
        const float M=Controls->Motor.load(std::memory_order_relaxed);
        const float W=Controls->Weld.load(std::memory_order_relaxed);
        const float G=Controls->Gate.load(std::memory_order_relaxed);
        if (G<=0 && Gain<.00001f) {
            Gain=0; FMemory::Memzero(OutAudio,sizeof(float)*NumSamples);
            Controls->LastRMS.store(0,std::memory_order_relaxed);
            Controls->GeneratedSamples.fetch_add(NumSamples,std::memory_order_relaxed);
            return NumSamples;
        }
        double Energy=0; float Peak=0; bool Finite=true;
        for (int32 I=0;I<NumSamples;++I) {
            Gain+=(G-Gain)*Ramp; Motor+=(M-Motor)*Ramp; Weld+=(W-Weld)*Ramp;
            Seed^=Seed<<13; Seed^=Seed>>17; Seed^=Seed<<5;
            const float Noise=(Seed&0xffffff)/8388607.5f-1.f;
            LowNoise+=.022f*(Noise-LowNoise);
            Crackle*=.962f; if ((Seed>>24)>253) Crackle=1.f;
            const int32 Voice=Controls->Voice;
            const double Frequency=Voice<4 ? 155.+Voice*19.+Motor*385. : Voice==4 ? 66.+Motor*72.:94.;
            Phase+=2.*PI*Frequency/Rate; if (Phase>=2.*PI) Phase-=2.*PI;
            ModPhase+=2.*PI*(Voice==5 ? 19.:7.)/Rate; if (ModPhase>=2.*PI) ModPhase-=2.*PI;
            float V=0;
            if (Voice<4) {
                V=Motor*(.046f*FMath::Sin(Phase+.17*FMath::Sin(ModPhase))+.014f*FMath::Sin(2.*Phase)+.009f*Noise)
                    +Weld*(.058f*(Noise-LowNoise)+.041f*Crackle*Noise);
            } else if (Voice==4) {
                V=Motor*(.048f*FMath::Sin(Phase)+.022f*FMath::Sin(3.*Phase)+.055f*LowNoise)*( .82f+.18f*FMath::Sin(ModPhase));
            } else {
                V=Motor*(.027f*FMath::Sin(Phase)+.014f*FMath::Sin(2.*Phase)+.14f*LowNoise)*( .91f+.09f*FMath::Sin(ModPhase));
            }
            V=FMath::Clamp(V*Gain,-SourcePeakLimit,SourcePeakLimit);
            if (!FMath::IsFinite(V)) { V=0; Finite=false; }
            OutAudio[I]=V; Peak=FMath::Max(Peak,FMath::Abs(V)); Energy+=V*V;
        }
        Controls->GeneratedSamples.fetch_add(NumSamples,std::memory_order_relaxed);
        Controls->LastRMS.store(NumSamples>0 ? static_cast<float>(FMath::Sqrt(Energy/NumSamples)):0,std::memory_order_relaxed);
        Controls->Peak.store(FMath::Max(Controls->Peak.load(std::memory_order_relaxed),Peak),std::memory_order_relaxed);
        if (!Finite) Controls->Finite.store(false,std::memory_order_relaxed);
        return NumSamples;
    }
private:
    TSharedPtr<FDarkFactoryAudioControls,ESPMode::ThreadSafe> Controls;
    float Rate, Ramp=0, Gain=0, Motor=0, Weld=0, LowNoise=0, Crackle=0;
    uint32 Seed;
    double Phase=0, ModPhase=0;
};
TArray<TSharedPtr<FJsonValue>> VectorJSON(FVector V) {
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
void Put16(TArray<uint8>& B,uint16 V) { B.Add(static_cast<uint8>(V)); B.Add(static_cast<uint8>(V>>8)); }
void Put32(TArray<uint8>& B,uint32 V) { Put16(B,static_cast<uint16>(V)); Put16(B,static_cast<uint16>(V>>16)); }
void Tag(TArray<uint8>& B,const ANSICHAR* S) { for (int32 I=0;I<4;++I) B.Add(S[I]); }
}

UDarkFactoryMachineSynth::UDarkFactoryMachineSynth(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    NumChannels=1; PreferredBufferLength=512; bAutoActivate=false;
    bAllowSpatialization=true; bOverrideAttenuation=true;
    AttenuationOverrides.bAttenuate=true; AttenuationOverrides.bSpatialize=true;
    AttenuationOverrides.AttenuationShape=EAttenuationShape::Sphere;
    AttenuationOverrides.AttenuationShapeExtents=FVector(250,0,0);
    AttenuationOverrides.FalloffDistance=4500;
    AttenuationOverrides.bEnableOcclusion=false; AttenuationOverrides.bEnableReverbSend=false;
    AttenuationOverrides.bEnableListenerFocus=false;
    bStopWhenOwnerDestroyed=true; bAutoDestroy=false; bIsUISound=false;
    Controls=MakeShared<FDarkFactoryAudioControls,ESPMode::ThreadSafe>(0);
}
void UDarkFactoryMachineSynth::ConfigureVoice(int32 Index) {
    check(!IsPlaying());
    Controls=MakeShared<FDarkFactoryAudioControls,ESPMode::ThreadSafe>(FMath::Clamp(Index,0,SourceCount-1));
}
bool UDarkFactoryMachineSynth::Init(int32& SampleRate) { return SampleRate>=8000; }
ISoundGeneratorPtr UDarkFactoryMachineSynth::CreateSoundGenerator(const FSoundGeneratorInitParams& Params) {
    return MakeShared<FFactorySoundGenerator,ESPMode::ThreadSafe>(Controls,Params.SampleRate);
}
void UDarkFactoryMachineSynth::SetLevels(float Motor,float Weld,float Gate) {
    Controls->Motor.store(FMath::Clamp(Motor,0.f,1.f),std::memory_order_relaxed);
    Controls->Weld.store(FMath::Clamp(Weld,0.f,1.f),std::memory_order_relaxed);
    Controls->Gate.store(FMath::Clamp(Gate,0.f,1.f),std::memory_order_relaxed);
}
TSharedRef<FJsonObject> UDarkFactoryMachineSynth::BuildMetadata() const {
    auto O=MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("index"),Controls->Voice); O->SetStringField(TEXT("name"),SourceNames[Controls->Voice]);
    O->SetArrayField(TEXT("position_cm"),VectorJSON(GetComponentLocation()));
    O->SetNumberField(TEXT("motor_envelope"),Controls->Motor.load()); O->SetNumberField(TEXT("weld_envelope"),Controls->Weld.load());
    O->SetNumberField(TEXT("output_gate"),Controls->Gate.load()); O->SetBoolField(TEXT("playing"),IsPlaying());
    O->SetNumberField(TEXT("generated_samples"),static_cast<double>(Controls->GeneratedSamples.load()));
    O->SetNumberField(TEXT("last_buffer_rms"),Controls->LastRMS.load()); O->SetNumberField(TEXT("peak_absolute_sample"),Controls->Peak.load());
    O->SetBoolField(TEXT("finite_audio"),Controls->Finite.load()); O->SetBoolField(TEXT("spatialized"),bAllowSpatialization && AttenuationOverrides.bSpatialize);
    return O;
}
ADarkFactorySoundscape::ADarkFactorySoundscape() {
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("SoundscapeRoot")); SetRootComponent(SceneRoot);
}
bool ADarkFactorySoundscape::Initialize(ADarkFactoryWeldingCell* Cell,bool PreferRecorded) {
    if (bInitialized) return Cell==SourceCell.Get();
    if (!IsValid(Cell)) return false;
    if (PreferRecorded) {
        auto* Candidate=GetWorld()->SpawnActor<ADarkFactoryRecordedAudio>();
        if (Candidate && Candidate->Initialize(Cell)) {
            Recorded=Candidate; SourceCell=Cell; bInitialized=true; AddTickPrerequisiteActor(Cell);
            UpdateFromCell(); return true;
        }
        if (Candidate) Candidate->Destroy();
        UE_LOG(LogTemp,Warning,TEXT("DF_RECORDED_AUDIO unavailable; using procedural fallback. Run scripts/audio/prepare_runtime_audio.py for the private bank."));
    }
    TArray<USceneComponent*> Components; Cell->GetComponents(Components);
    for (int32 I=0;I<4;++I) {
        const auto Timing=Cell->GetRobotTiming(I,Cell->GetMotionTime());
        USceneComponent* Anchor=nullptr;
        for (auto* C:Components) if (C->GetName()==Timing.RobotId+TEXT("_tool_roll")) { Anchor=C; break; }
        if (!Timing.Valid || !Anchor) { RobotAnchors.Reset(); return false; }
        RobotAnchors.Add(Anchor);
    }
    SourceCell=Cell; AddTickPrerequisiteActor(Cell);
    for (int32 I=0;I<SourceCount;++I) {
        auto* Source=NewObject<UDarkFactoryMachineSynth>(this,*FString::Printf(TEXT("FactorySound%d"),I));
        Source->SetupAttachment(SceneRoot); Source->ConfigureVoice(I); Source->RegisterComponent();
        Source->Initialize(); Sources.Add(Source); Source->Start();
    }
    bInitialized=true; UpdateFromCell(); return true;
}
void ADarkFactorySoundscape::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); UpdateFromCell(); }
void ADarkFactorySoundscape::EndPlay(const EEndPlayReason::Type Reason) {
    if (Recorded) { Recorded->Destroy(); Recorded=nullptr; }
    for (const auto& S:Sources) if (S) { S->SetLevels(0,0,0); S->Stop(); }
    Super::EndPlay(Reason);
}
void ADarkFactorySoundscape::SetEnabled(bool Enabled) { bEnabled=Enabled; UpdateFromCell(); }
void ADarkFactorySoundscape::SetMuted(bool Muted) { bMuted=Muted; UpdateFromCell(); }
void ADarkFactorySoundscape::NotifyObserverMode(int32 Mode) { if (Recorded) Recorded->NotifyObserverMode(Mode); }
void ADarkFactorySoundscape::NotifyInspectionCapture() { if (Recorded) Recorded->NotifyInspectionCapture(); }
void ADarkFactorySoundscape::UpdateFromCell() {
    if (Recorded) { Recorded->UpdateFromCell(bEnabled,bMuted); return; }
    const auto* Cell=SourceCell.Get();
    const FAudioFrame F=EvaluateFrame(Cell,Cell ? Cell->GetMotionTime():0);
    const float Gate=Cell && bInitialized && bEnabled && !bMuted && Cell->IsAnimating() ? 1.f:0.f;
    if (Cell) LastSourceSeconds=Cell->GetMotionTime();
    for (int32 I=0;I<Sources.Num();++I) {
        auto* S=Sources[I].Get(); S->SetLevels(F.Motor[I],F.Weld[I],Gate);
        if (I<4 && RobotAnchors[I].IsValid()) S->SetWorldLocation(RobotAnchors[I]->GetComponentLocation());
        else if (I==4 && Cell && Cell->GetCarriageBounds().IsValid) {
            const FBox Bounds=Cell->GetCarriageBounds(); const FVector C=Bounds.GetCenter();
            S->SetWorldLocation(FVector(C.X,C.Y,Bounds.Min.Z-25.));
        } else if (I==5 && Cell) S->SetWorldLocation(Cell->GetActorTransform().TransformPosition(FVector(0,0,630)));
    }
}
TSharedRef<FJsonObject> ADarkFactorySoundscape::BuildMetadata() const {
    if (Recorded) {
        auto O=Recorded->BuildMetadata(); O->SetBoolField(TEXT("initialized"),bInitialized);
        O->SetBoolField(TEXT("enabled"),bEnabled); O->SetBoolField(TEXT("muted"),bMuted); return O;
    }
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("model"),TEXT("phase-synced-procedural-factory-audio-v1"));
    O->SetStringField(TEXT("scope"),TEXT("Authored spatial ambience; no measured machinery acoustics, alarms or model telemetry"));
    O->SetBoolField(TEXT("initialized"),bInitialized); O->SetBoolField(TEXT("enabled"),bEnabled); O->SetBoolField(TEXT("muted"),bMuted);
    O->SetBoolField(TEXT("scene_animating"),SourceCell.IsValid() && SourceCell->IsAnimating());
    O->SetNumberField(TEXT("source_motion_seconds"),LastSourceSeconds); O->SetNumberField(TEXT("maximum_sources"),SourceCount);
    O->SetNumberField(TEXT("peak_limit_per_source"),SourcePeakLimit); O->SetNumberField(TEXT("gain_smoothing_seconds"),.008);
    O->SetNumberField(TEXT("collision_queries"),0); O->SetNumberField(TEXT("sensor_readbacks"),0);
    O->SetNumberField(TEXT("spatial_inner_radius_cm"),250); O->SetNumberField(TEXT("spatial_falloff_cm"),4500);
    TArray<TSharedPtr<FJsonValue>> Values; for (const auto& S:Sources) Values.Add(MakeShared<FJsonValueObject>(S->BuildMetadata()));
    O->SetArrayField(TEXT("sources"),Values); return O;
}
TSharedRef<FJsonObject> ADarkFactorySoundscape::BuildDSPSelfCheck() {
    auto O=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Rows; bool Passed=true;
    for (int32 I=0;I<SourceCount;++I) {
        auto C=MakeShared<FDarkFactoryAudioControls,ESPMode::ThreadSafe>(I); C->Motor.store(1); C->Weld.store(I<4 ? .8f:0.f); C->Gate.store(1);
        FFactorySoundGenerator G(C,SampleRateQA); TArray<float> Buffer; Buffer.SetNumUninitialized(SampleRateQA/4);
        G.OnGenerateAudio(Buffer.GetData(),Buffer.Num()); const float RMS=C->LastRMS.load(),Peak=C->Peak.load();
        auto Repeated=MakeShared<FDarkFactoryAudioControls,ESPMode::ThreadSafe>(I);
        Repeated->Motor.store(1); Repeated->Weld.store(I<4 ? .8f:0.f); Repeated->Gate.store(1);
        FFactorySoundGenerator Reference(Repeated,SampleRateQA); TArray<float> ReferenceBuffer; ReferenceBuffer.SetNumUninitialized(Buffer.Num());
        Reference.OnGenerateAudio(ReferenceBuffer.GetData(),ReferenceBuffer.Num());
        const bool Deterministic=FMemory::Memcmp(Buffer.GetData(),ReferenceBuffer.GetData(),Buffer.Num()*sizeof(float))==0;
        C->Gate.store(0); G.OnGenerateAudio(Buffer.GetData(),Buffer.Num()); G.OnGenerateAudio(Buffer.GetData(),Buffer.Num());
        float SilentPeak=0; for (float X:Buffer) SilentPeak=FMath::Max(SilentPeak,FMath::Abs(X));
        const bool Valid=C->Finite.load() && RMS>1.e-4f && Peak<=SourcePeakLimit && SilentPeak==0 && Deterministic;
        auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("name"),SourceNames[I]); R->SetBoolField(TEXT("passed"),Valid);
        R->SetNumberField(TEXT("active_rms"),RMS); R->SetNumberField(TEXT("active_peak"),Peak); R->SetNumberField(TEXT("settled_muted_peak"),SilentPeak); R->SetBoolField(TEXT("deterministic_pcm"),Deterministic);
        Rows.Add(MakeShared<FJsonValueObject>(R)); Passed&=Valid;
    }
    O->SetBoolField(TEXT("passed"),Passed); O->SetStringField(TEXT("scope"),TEXT("Native DSP kernels; not device output or live spatialization"));
    O->SetArrayField(TEXT("sources"),Rows); return O;
}
TSharedRef<FJsonObject> ADarkFactorySoundscape::SaveOfflinePreviewForQA(const FString& WavPath) const {
    auto O=MakeShared<FJsonObject>(); O->SetBoolField(TEXT("passed"),false);
    const auto* Cell=SourceCell.Get(); if (!Cell) return O;
    const double Duration=Cell->GetCycleDurationSeconds();
    constexpr int32 Block=800; // 60 Hz phase controls, same kernels as audio-device rendering.
    const int32 Blocks=FMath::RoundToInt(Duration*SampleRateQA/Block),Count=Blocks*Block;
    TArray<uint8> Wav; Wav.Reserve(44+Count*2);
    Tag(Wav,"RIFF"); Put32(Wav,36+Count*2); Tag(Wav,"WAVE"); Tag(Wav,"fmt "); Put32(Wav,16);
    Put16(Wav,1); Put16(Wav,1); Put32(Wav,SampleRateQA); Put32(Wav,SampleRateQA*2); Put16(Wav,2); Put16(Wav,16);
    Tag(Wav,"data"); Put32(Wav,Count*2);
    TArray<TSharedPtr<FDarkFactoryAudioControls,ESPMode::ThreadSafe>> C;
    TArray<TUniquePtr<FFactorySoundGenerator>> G;
    for (int32 I=0;I<SourceCount;++I) { auto P=MakeShared<FDarkFactoryAudioControls,ESPMode::ThreadSafe>(I); P->Gate.store(1); C.Add(P); G.Add(MakeUnique<FFactorySoundGenerator>(P,SampleRateQA)); }
    float Scratch[Block],Mix[Block]; double Energy=0; float Peak=0;
    for (int32 B=0;B<Blocks;++B) {
        FMemory::Memzero(Mix,sizeof(Mix)); const auto Frame=EvaluateFrame(Cell,static_cast<double>(B*Block)/SampleRateQA);
        for (int32 I=0;I<SourceCount;++I) {
            C[I]->Motor.store(Frame.Motor[I]); C[I]->Weld.store(Frame.Weld[I]); G[I]->OnGenerateAudio(Scratch,Block);
            for (int32 J=0;J<Block;++J) Mix[J]+=Scratch[J];
        }
        for (float Sample:Mix) { Peak=FMath::Max(Peak,FMath::Abs(Sample)); Energy+=Sample*Sample; const int16 PCM=static_cast<int16>(FMath::RoundToInt(FMath::Clamp(Sample,-1.f,1.f)*32767)); Put16(Wav,static_cast<uint16>(PCM)); }
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(WavPath),true);
    const bool Saved=FFileHelper::SaveArrayToFile(Wav,*WavPath);
    O->SetBoolField(TEXT("passed"),Saved && Peak>0 && Peak<1); O->SetStringField(TEXT("file"),WavPath);
    O->SetStringField(TEXT("scope"),TEXT("Deterministic mono first-cycle audition of the actual DSP and schedule envelopes; excludes engine spatial mixer and listener attenuation"));
    O->SetNumberField(TEXT("duration_seconds"),Duration); O->SetNumberField(TEXT("sample_rate_hz"),SampleRateQA); O->SetNumberField(TEXT("channels"),1);
    O->SetNumberField(TEXT("peak_absolute_sample"),Peak); O->SetNumberField(TEXT("rms"),FMath::Sqrt(Energy/Count));
    O->SetObjectField(TEXT("dsp_self_check"),BuildDSPSelfCheck()); return O;
}
