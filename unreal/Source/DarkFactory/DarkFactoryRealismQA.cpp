#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryGameMode.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryEnvironmentDetails.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactorySoundscape.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryFactoryLights.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

void ADarkFactoryWorld::RealismSmokeTick(double Now) {
    if (Now<NextAutomation) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !ThermalRig || !CellMechanisms || !EnvironmentDetails || !Soundscape) {
        FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return;
    }
    auto Shot=[&](const FString& Name){FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false);};
    auto Pose=[&](double Seconds) {
        IQ9Props->SetLedTimeForQA(.1);
        WeldingCell->SetMotionTime(Seconds); CellMechanisms->UpdateFromCell(); Soundscape->UpdateFromCell();
    };
    auto Snapshot=[&]() {
        auto R=MakeShared<FJsonObject>();
        R->SetNumberField(TEXT("time"),WeldingCell->GetMotionTime());
        R->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata());
        R->SetObjectField(TEXT("environment"),EnvironmentDetails->BuildMetadata());
        R->SetObjectField(TEXT("mechanisms"),CellMechanisms->BuildMetadata());
        R->SetObjectField(TEXT("sound"),Soundscape->BuildMetadata());
        if (auto* HUD=Cast<ADarkFactoryHUD>(PC->GetHUD())) R->SetObjectField(TEXT("production_status"),HUD->BuildProductionStatusMetadata());
        return R;
    };
    auto Append=[&](const TCHAR* Key,const TSharedRef<FJsonObject>& Row) {
        auto Rows=RealismReport->GetArrayField(Key); Rows.Add(MakeShared<FJsonValueObject>(Row)); RealismReport->SetArrayField(Key,Rows);
    };
    auto Raw=[&](const FString& Stem,const TSharedRef<FJsonObject>& Row) {
        const FString NIR=Stem+TEXT("_raw_nir.png"),Thermal=Stem+TEXT("_raw_thermal.png");
        const bool Saved=SaveWeldingNIRQA(NIR,Stem) && ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,Thermal));
        Row->SetStringField(TEXT("raw_nir"),NIR); Row->SetStringField(TEXT("raw_thermal"),Thermal); Row->SetBoolField(TEXT("raw_saved"),Saved);
    };
    if (RealismStep==0) {
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        WeldingSensorStats=MakeShared<FJsonObject>(); RealismReport=MakeShared<FJsonObject>();
        if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences"))) {
            WeldingEffectsPreviousTemporalFreeze=Freeze->GetInt(); Freeze->Set(1,ECVF_SetByCode);
        }
        RealismReport->SetNumberField(TEXT("feature_count"),10);
        RealismReport->SetStringField(TEXT("scope"),TEXT("Authored factory detail, geometric fixture/presence staging, procedural audio, observer UI/bookmarks and cosmetic weld witness; no model inference or industrial validation"));
        for (const TCHAR* Key:{TEXT("stages"),TEXT("sensor_views"),TEXT("bookmarks"),TEXT("witness_views")}) RealismReport->SetArrayField(Key,{});
        SetRealismDetailsEnabled(true); WeldingCell->SetAnimationEnabled(false); WeldingCell->SetRobotTimingForQA(true);
        WeldingSparks->SetEffectsEnabled(true); WeldingSparks->ResetParticles(); WeldingSmoke->ResetParticles();
        Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3.); Pose(0);
    } else if (RealismStep<=24) {
        const int32 Index=(RealismStep-1)/2,Substep=(RealismStep-1)%2;
        const double Times[]={1.2,2.5,3.2,3.5,6.5,10.,15.6,16.,16.25,18.,20.6,21.1};
        const TCHAR* Names[]={TEXT("entry"),TEXT("arrived"),TEXT("clamping"),TEXT("secured"),TEXT("weld_stagger"),TEXT("welding"),TEXT("parked"),TEXT("unclamping"),TEXT("released"),TEXT("exit"),TEXT("empty"),TEXT("replacement")};
        const FString File=FString(TEXT("realism_"))+Names[Index]+TEXT(".png");
        if (Substep==0) {
            WeldingSparks->ResetParticles(); WeldingSmoke->ResetParticles();
            Pose(Times[Index]);
            if (Index==4 || Index==5) {
                double Time=Times[Index]-.6; Pose(Time); WeldingSparks->ResetParticles();
                while (Time<Times[Index]-1.e-8) {
                    const double Dt=FMath::Min(1./90.,Times[Index]-Time); Time+=Dt; Pose(Time);
                    WeldingSparks->AdvanceSimulation(Dt,true); WeldingSmoke->AdvanceSimulation(Dt,true);
                }
            }
            Observer->SetObserverBookmark(0,true); Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3.);
            auto R=Snapshot(); R->SetStringField(TEXT("name"),Names[Index]); R->SetStringField(TEXT("image"),File); Append(TEXT("stages"),R);
        } else Shot(File);
    } else if (RealismStep<=30) {
        const int32 Index=(RealismStep-25)/2,Substep=(RealismStep-25)%2;
        const TCHAR* Names[]={TEXT("visible"),TEXT("nir_moved"),TEXT("thermal")};
        const FString Stem=FString(TEXT("realism_sensor_"))+Names[Index];
        if (Substep==0) {
            Pose(10); WeldingSparks->SetEffectsEnabled(false); WeldingSmoke->ResetParticles();
            Observer->SetObserverBookmark(Index==1 ? 2:0,true); Observer->SetObserverMode(Index);
        } else {
            auto R=Snapshot(); R->SetStringField(TEXT("name"),Names[Index]); R->SetStringField(TEXT("image"),Stem+TEXT(".png"));
            Raw(Stem,R); Append(TEXT("sensor_views"),R); Shot(Stem+TEXT(".png"));
        }
    } else if (RealismStep<=40) {
        const int32 Index=(RealismStep-31)/2,Substep=(RealismStep-31)%2;
        const FString File=FString::Printf(TEXT("realism_bookmark_%d.png"),Index);
        if (Substep==0) { Pose(9); Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3.); Observer->SetObserverBookmark(Index,true); }
        else {
            auto R=MakeShared<FJsonObject>(); R->SetNumberField(TEXT("index"),Index); R->SetStringField(TEXT("name"),Observer->GetObserverBookmarkName()); R->SetStringField(TEXT("image"),File);
            Append(TEXT("bookmarks"),R); Shot(File);
        }
    } else if (RealismStep<=44) {
        const int32 Index=(RealismStep-41)/2,Substep=(RealismStep-41)%2;
        const FString Stem=Index==0 ? TEXT("realism_witness_on"):TEXT("realism_witness_off");
        if (Substep==0) {
            Pose(15.7); WeldingCell->SetWeldWitnessEnabled(Index==0); WeldingCell->SetBodyWeldHeatTimeForQA(20.5);
            const auto WitnessMetadata=WeldingCell->BuildBodyWeldHeatMetadata();
            const auto& Spots=WitnessMetadata->GetArrayField(TEXT("spots"));
            const auto Spot=Spots[0]->AsObject();
            auto V=[&](const TCHAR* Key) { const auto& A=Spot->GetArrayField(Key); return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()); };
            const FVector Center=V(TEXT("world_position_cm")),Normal=V(TEXT("world_normal"));
            Observer->SetActorLocation(Center+Normal*48+FVector(0,0,8)); Observer->SetActorRotation((Center-Observer->GetActorLocation()).Rotation());
            Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3.);
            auto* Camera=Observer->FindComponentByClass<UCameraComponent>(); Camera->FieldOfView=50; Camera->PostProcessSettings.AutoExposureBias=-1;
        } else {
            auto R=Snapshot(); R->SetStringField(TEXT("name"),Index==0 ? TEXT("on"):TEXT("off")); R->SetBoolField(TEXT("enabled"),Index==0); R->SetStringField(TEXT("image"),Stem+TEXT(".png"));
            Raw(Stem,R); Append(TEXT("witness_views"),R); Shot(Stem+TEXT(".png"));
        }
    } else if (RealismStep==45) {
        Observer->SetCaptureAutomation(false);
        Observer->SetObserverBookmark(0,true); const FVector Start=Observer->GetActorLocation();
        Observer->SetObserverBookmark(1,false); Observer->Tick(.825f);
        const FVector Middle=Observer->GetActorLocation(); const bool Moving=Observer->IsObserverBookmarkMoving();
        Observer->Tick(.825f); const FVector End=Observer->GetActorLocation(); const bool Finished=!Observer->IsObserverBookmarkMoving();
        Observer->SetObserverBookmark(1,true); const FVector Expected=Observer->GetActorLocation();
        const bool Valid=Moving && Finished && !Middle.ContainsNaN() && FVector::Distance(Start,Middle)>1. && FVector::Distance(End,Expected)<.001;
        auto V=[](const FVector& P) { return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)}; };
        auto R=MakeShared<FJsonObject>(); R->SetArrayField(TEXT("start"),V(Start)); R->SetArrayField(TEXT("middle"),V(Middle)); R->SetArrayField(TEXT("end"),V(End));
        R->SetBoolField(TEXT("moving_at_half"),Moving); R->SetBoolField(TEXT("finished"),Finished); R->SetBoolField(TEXT("passed"),Valid);
        RealismReport->SetObjectField(TEXT("bookmark_transition"),R); Observer->SetCaptureAutomation(true);
    } else {
        auto Same=[&](const FString& A,const FString& B) { TArray<uint8> X,Y; return FFileHelper::LoadFileToArray(X,*FPaths::Combine(OutputRoot,A)) && FFileHelper::LoadFileToArray(Y,*FPaths::Combine(OutputRoot,B)) && !X.IsEmpty() && X==Y; };
        bool Parity=true;
        for (const TCHAR* Mode:{TEXT("nir_moved"),TEXT("thermal")}) for (const TCHAR* Channel:{TEXT("nir"),TEXT("thermal")}) {
            Parity &= Same(FString(TEXT("realism_sensor_visible_raw_"))+Channel+TEXT(".png"),FString(TEXT("realism_sensor_"))+Mode+TEXT("_raw_")+Channel+TEXT(".png"));
        }
        const bool WitnessThermal=Same(TEXT("realism_witness_on_raw_thermal.png"),TEXT("realism_witness_off_raw_thermal.png"));
        Soundscape->SetMuted(true); const bool Mute=Soundscape->IsMuted(); Soundscape->SetMuted(false);
        const auto AudioReport=Soundscape->SaveOfflinePreviewForQA(FPaths::Combine(OutputRoot,TEXT("factory_sound_preview.wav")));
        const bool Audio=AudioReport->GetBoolField(TEXT("passed")) && AudioReport->GetObjectField(TEXT("dsp_self_check"))->GetBoolField(TEXT("passed"));
        RealismReport->SetObjectField(TEXT("audio_preview"),AudioReport);
        RealismReport->SetBoolField(TEXT("sensor_observer_parity"),Parity); RealismReport->SetBoolField(TEXT("witness_thermal_unchanged"),WitnessThermal);
        RealismReport->SetBoolField(TEXT("sound_mute_roundtrip"),Mute && !Soundscape->IsMuted()); RealismReport->SetBoolField(TEXT("audio_preview_saved"),Audio);
        RealismReport->SetObjectField(TEXT("sound_final"),Soundscape->BuildMetadata());
        RealismReport->SetObjectField(TEXT("nir_raw_statistics"),WeldingSensorStats.ToSharedRef());
        const bool Passed=Parity && WitnessThermal && Mute && Audio && RealismReport->GetObjectField(TEXT("bookmark_transition"))->GetBoolField(TEXT("passed"));
        RealismReport->SetBoolField(TEXT("passed"),Passed);
        FString JSON; FJsonSerializer::Serialize(RealismReport.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&JSON));
        const bool Saved=FFileHelper::SaveStringToFile(JSON,*FPaths::Combine(OutputRoot,TEXT("realism.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("DF_REALISM_SMOKE passed=%d sensor_parity=%d witness_thermal=%d audio=%d"),Passed && Saved,Parity,WitnessThermal,Audio);
        FPlatformMisc::RequestExitWithStatus(false,Passed && Saved ? 0:1); bWeldingSmoke=false; return;
    }
    ++RealismStep; NextAutomation=Now+.35;
}
