#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryGameMode.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryFactoryLights.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace {
TArray<TSharedPtr<FJsonValue>> QAWorldVector(const FVector& V) {
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
FVector QAReadVector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key) {
    const auto& A=O->GetArrayField(Key);
    return A.Num()==3 ? FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()):FVector::ZeroVector;
}
FString QAExpansionJson(const TSharedRef<FJsonObject>& O) {
    FString S; FJsonSerializer::Serialize(O,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S;
}
// Identical local poses can round component world transforms differently at
// double precision. Keep structure/text exact and bound numerical roundoff.
bool QASeekEqual(const TSharedPtr<FJsonValue>& A,const TSharedPtr<FJsonValue>& B,double& MaxError) {
    if (!A.IsValid() || !B.IsValid()) return A.IsValid()==B.IsValid();
    if (A->Type!=B->Type) return false;
    switch (A->Type) {
    case EJson::Number: {
        const double X=A->AsNumber(),Y=B->AsNumber();
        if (!FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
        MaxError=FMath::Max(MaxError,FMath::Abs(X-Y));
        return FMath::Abs(X-Y)<=1.e-9;
    }
    case EJson::String: return A->AsString()==B->AsString();
    case EJson::Boolean: return A->AsBool()==B->AsBool();
    case EJson::Array: {
        const auto& X=A->AsArray(); const auto& Y=B->AsArray();
        if (X.Num()!=Y.Num()) return false;
        bool Same=true;
        for (int32 I=0;I<X.Num();++I) Same &= QASeekEqual(X[I],Y[I],MaxError);
        return Same;
    }
    case EJson::Object: {
        const auto X=A->AsObject(),Y=B->AsObject();
        if (X->Values.Num()!=Y->Values.Num()) return false;
        bool Same=true;
        for (const auto& Entry:X->Values) {
            const auto* Other=Y->Values.Find(Entry.Key);
            if (!Other) return false;
            Same &= QASeekEqual(Entry.Value,*Other,MaxError);
        }
        return Same;
    }
    default: return true;
    }
}
}

void ADarkFactoryWorld::RobotExpansionSmokeTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    auto* HUD=PC ? Cast<ADarkFactoryHUD>(PC->GetHUD()):nullptr;
    if (!Observer || !HUD || !IQ9Props || !WeldingCell || !WeldingSparks || !WeldingSmoke || !ThermalRig) {
        FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return;
    }
    auto Shot=[&](const FString& Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
    auto Pose=[&](double Time) { WeldingCell->SetMotionTime(Time); IQ9Props->SetLedTimeForQA(Time); };
    auto Same=[&](const FString& A,const FString& B) {
        TArray<uint8> X,Y; return FFileHelper::LoadFileToArray(X,*FPaths::Combine(OutputRoot,A)) &&
            FFileHelper::LoadFileToArray(Y,*FPaths::Combine(OutputRoot,B)) && !X.IsEmpty() && X==Y;
    };
    auto Snapshot=[&]() {
        auto O=MakeShared<FJsonObject>();
        O->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata());
        O->SetObjectField(TEXT("sparks"),WeldingSparks->BuildMetadata());
        O->SetObjectField(TEXT("iq9"),IQ9Props->BuildMetadata());
        return O;
    };
    if (RobotExpansionStep==0) {
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        if (WeldingEffectsPreviousTemporalFreeze<0) if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences"))) {
            WeldingEffectsPreviousTemporalFreeze=Freeze->GetInt(); Freeze->Set(1,ECVF_SetByCode);
        }
        if (!WeldingSensorStats) WeldingSensorStats=MakeShared<FJsonObject>();
        WeldingCell->SetAnimationEnabled(false);
        if (!WeldingCell->SetRobotTimingForQA(true)) { FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
        WeldingSparks->SetEffectsEnabled(true); WeldingSparks->ResetParticles(); WeldingSmoke->ResetParticles();
        RobotExpansionReport=MakeShared<FJsonObject>();
        RobotExpansionReport->SetStringField(TEXT("scope"),TEXT("Default randomized robot choreography, attached reference IQ9 props, observer-only NIR illumination and HUD frame rate. Earlier fixtures explicitly use canonical 18-second timing."));
        RobotExpansionReport->SetNumberField(TEXT("cycle_duration_seconds"),WeldingCell->GetCycleDurationSeconds());
        TArray<TSharedPtr<FJsonValue>> Bank,Probes;
        bool DelaysBounded=true,GateValid=true;
        for (int32 Cycle=0;Cycle<16;++Cycle) {
            Pose(Cycle*21.);
            Bank.Add(MakeShared<FJsonValueObject>(WeldingCell->BuildPreviewMetadata()->GetObjectField(TEXT("robot_timing"))));
            for (int32 I=0;I<4;++I) {
                const auto T=WeldingCell->GetRobotTiming(I,Cycle*21.);
                DelaysBounded &= T.RestDelaySeconds>=.2 && T.RestDelaySeconds<=.8 &&
                    T.PreWeldDelaySeconds>=.5 && T.PreWeldDelaySeconds<=2. &&
                    T.ApproachStartSeconds>=3.5 && T.ApproachEndSeconds<=T.WeldStartSeconds &&
                    T.WithdrawEndSeconds<=15.8+1.e-6;
            }
        }
        RobotExpansionReport->SetArrayField(TEXT("schedule_bank"),Bank);
        for (int32 Cycle=0;Cycle<2;++Cycle) {
            TArray<double> Times={0.,3.,16.3,17.5,19.8,20.9};
            for (int32 I=0;I<4;++I) {
                const auto T=WeldingCell->GetRobotTiming(I,Cycle*21.);
                for (double V:{T.ApproachStartSeconds-.02,T.ApproachStartSeconds+.02,T.ApproachEndSeconds+.02,
                    T.WeldStartSeconds-.02,T.WeldStartSeconds+.15,(T.WeldStartSeconds+T.WeldEndSeconds)*.5,
                    T.WeldEndSeconds-.02,T.WeldEndSeconds+.02,T.WithdrawEndSeconds+.02}) Times.AddUnique(V);
            }
            Times.Sort();
            for (double T:Times) {
                Pose(Cycle*21.+T); WeldingSparks->ResetParticles();
                for (const auto& E:WeldingCell->GetWeldEmitters()) {
                    if (E.EmitterIndex<0 || E.EmitterIndex>=4) { GateValid=false; continue; }
                    const auto R=WeldingCell->GetRobotTiming(E.EmitterIndex,WeldingCell->GetMotionTime());
                    GateValid &= T>=R.WeldStartSeconds && T<R.WeldEndSeconds && E.WeldAgeSeconds>=0 &&
                        E.WeldAgeSeconds<E.WeldDurationSeconds && !E.SurfacePosition.ContainsNaN();
                }
                auto Row=Snapshot(); Row->SetNumberField(TEXT("absolute_seconds"),Cycle*21.+T);
                Probes.Add(MakeShared<FJsonValueObject>(Row));
            }
        }
        RobotExpansionReport->SetArrayField(TEXT("probes"),Probes);
        RobotExpansionReport->SetBoolField(TEXT("delays_bounded"),DelaysBounded);
        RobotExpansionReport->SetBoolField(TEXT("emitters_gated_by_robot_clock"),GateValid);
        Pose(9.25); const auto SeekBefore=WeldingCell->BuildPreviewMetadata();
        Pose(211.); Pose(2.); Pose(9.25);
        const auto SeekAfter=WeldingCell->BuildPreviewMetadata();
        RobotExpansionReport->SetObjectField(TEXT("seek_reference"),SeekBefore);
        RobotExpansionReport->SetObjectField(TEXT("seek_restored"),SeekAfter);
        double SeekMaxError=0;
        const bool SeekStable=QASeekEqual(MakeShared<FJsonValueObject>(SeekBefore),MakeShared<FJsonValueObject>(SeekAfter),SeekMaxError);
        RobotExpansionReport->SetNumberField(TEXT("seek_numeric_tolerance"),1.e-9);
        RobotExpansionReport->SetNumberField(TEXT("seek_max_numeric_error"),SeekMaxError);
        RobotExpansionReport->SetBoolField(TEXT("seek_order_independent"),SeekStable);
        bRobotExpansionPassed &= DelaysBounded && GateValid && SeekStable;
        RobotExpansionReport->SetArrayField(TEXT("production_views"),{});
        RobotExpansionReport->SetArrayField(TEXT("nir_views"),{});
        RobotExpansionReport->SetArrayField(TEXT("iq9_closeups"),{});
        RobotExpansionFPSSamples=HUD->GetFrameRateSampleCount();
        Pose(0); Observer->SetObserverHeadlightEnabled(true);
    } else if (RobotExpansionStep<=12) {
        const int32 Index=(RobotExpansionStep-1)/2,Substep=(RobotExpansionStep-1)%2;
        const TCHAR* Names[]={TEXT("rest"),TEXT("approach_stagger"),TEXT("weld_stagger"),TEXT("all_welding"),TEXT("parked"),TEXT("departure")};
        const FString Name=Names[Index],File=TEXT("robot_expansion_")+Name+TEXT(".png");
        if (Substep==0) {
            double AMin=100,AMax=0,WMin=100,WMax=0,Park=0;
            for (int32 I=0;I<4;++I) {
                const auto R=WeldingCell->GetRobotTiming(I,0);
                AMin=FMath::Min(AMin,R.ApproachStartSeconds); AMax=FMath::Max(AMax,R.ApproachStartSeconds);
                WMin=FMath::Min(WMin,R.WeldStartSeconds); WMax=FMath::Max(WMax,R.WeldStartSeconds); Park=FMath::Max(Park,R.WithdrawEndSeconds);
            }
            const double Times[]={0.,(AMin+AMax)*.5,(WMin+WMax)*.5,WMax+.75,Park+.1,17.4};
            Pose(0); WeldingSparks->SetEffectsEnabled(true); WeldingSparks->ResetParticles();
            WeldingSmoke->SetEffectsEnabled(true); WeldingSmoke->SetRenderingEnabled(true); WeldingSmoke->ResetParticles();
            double Time=0;
            while (Time<Times[Index]-1.e-8) {
                const double Dt=FMath::Min(1./90.,Times[Index]-Time); Time+=Dt; Pose(Time);
                WeldingSparks->AdvanceSimulation(static_cast<float>(Dt),true); WeldingSmoke->AdvanceSimulation(static_cast<float>(Dt),true);
            }
            Observer->SetActorLocation(WeldingCell->GetObserverLocation()); Observer->SetActorRotation(WeldingCell->GetObserverRotation()); Observer->SetObserverMode(0);
            auto Row=Snapshot(); Row->SetStringField(TEXT("name"),Name); Row->SetStringField(TEXT("image"),File);
            auto Rows=RobotExpansionReport->GetArrayField(TEXT("production_views")); Rows.Add(MakeShared<FJsonValueObject>(Row)); RobotExpansionReport->SetArrayField(TEXT("production_views"),Rows);
        } else Shot(File);
    } else if (RobotExpansionStep<=20) {
        const int32 Index=(RobotExpansionStep-13)/2,Substep=(RobotExpansionStep-13)%2;
        const TCHAR* Names[]={TEXT("nir_home_on"),TEXT("nir_home_off"),TEXT("nir_moved_on"),TEXT("visible_home")};
        const FString Name=Names[Index],Stem=TEXT("robot_expansion_")+Name;
        if (Substep==0) {
            WeldingCell->SetMotionTime(9.); IQ9Props->SetLedTimeForQA(.1); WeldingSparks->SetEffectsEnabled(false); WeldingSmoke->ResetParticles();
            const FVector Home=WeldingCell->GetObserverLocation();
            Observer->SetActorLocation(Index==2 ? Home+FVector(260,120,90):Home);
            Observer->SetActorRotation(Index==2 ? (FVector(0,0,230)-Observer->GetActorLocation()).Rotation():WeldingCell->GetObserverRotation());
            Observer->SetObserverMode(Index==3 ? 0:1);
            if (Index==1) Observer->SetObserverNIRIlluminatorForQA(false);
        } else {
            bRobotExpansionPassed &= SaveWeldingNIRQA(Stem+TEXT("_raw_nir.png"),Stem);
            bRobotExpansionPassed &= SaveWeldingNIRQA(Stem+TEXT("_raw_nir_repeat.png"),Stem+TEXT("_repeat"));
            bRobotExpansionPassed &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,Stem+TEXT("_raw_thermal.png")));
            Shot(Stem+TEXT(".png"));
            const auto* Lamp=Observer->GetObserverNIRIlluminator();
            auto Row=Snapshot(); Row->SetStringField(TEXT("name"),Name); Row->SetStringField(TEXT("image"),Stem+TEXT(".png"));
            Row->SetStringField(TEXT("raw_nir"),Stem+TEXT("_raw_nir.png")); Row->SetStringField(TEXT("raw_thermal"),Stem+TEXT("_raw_thermal.png"));
            Row->SetNumberField(TEXT("observer_view_mask"),Observer->GetObserverViewLightingMask());
            Row->SetArrayField(TEXT("observer_position_cm"),QAWorldVector(Observer->GetActorLocation()));
            Row->SetArrayField(TEXT("observer_ir_position_cm"),QAWorldVector(Lamp->GetComponentLocation()));
            Row->SetArrayField(TEXT("observer_ir_relative_position_cm"),QAWorldVector(Lamp->GetRelativeLocation()));
            Row->SetBoolField(TEXT("observer_ir_emitting"),Observer->IsObserverNIRIlluminatorEmitting());
            Row->SetBoolField(TEXT("observer_ir_channel_2_only"),!Lamp->ViewLightingChannels.bViewChannel0 && !Lamp->ViewLightingChannels.bViewChannel1 && Lamp->ViewLightingChannels.bViewChannel2);
            Row->SetArrayField(TEXT("fixed_ir_position_cm"),QAWorldVector(IRLight->GetComponentLocation()));
            Row->SetBoolField(TEXT("fixed_ir_channel_1_only"),!IRLight->ViewLightingChannels.bViewChannel0 && IRLight->ViewLightingChannels.bViewChannel1 && !IRLight->ViewLightingChannels.bViewChannel2);
            Row->SetNumberField(TEXT("fps"),HUD->GetSmoothedFramesPerSecond()); Row->SetNumberField(TEXT("fps_samples"),static_cast<double>(HUD->GetFrameRateSampleCount()));
            auto Rows=RobotExpansionReport->GetArrayField(TEXT("nir_views")); Rows.Add(MakeShared<FJsonValueObject>(Row)); RobotExpansionReport->SetArrayField(TEXT("nir_views"),Rows);
        }
    } else if (RobotExpansionStep<=28) {
        const int32 Index=(RobotExpansionStep-21)/2,Substep=(RobotExpansionStep-21)%2;
        const FString File=FString::Printf(TEXT("robot_expansion_iq9_%d.png"),Index);
        if (Substep==0) {
            Pose(0); IQ9Props->SetLedTimeForQA(.1); Observer->SetObserverMode(3);
            FactoryLights->SetStartupTimeForQA(3.);
            // Preserve photographed PCB detail during close-up visual review.
            Observer->FindComponentByClass<UCameraComponent>()->PostProcessSettings.AutoExposureBias=-2;
            const auto Props=IQ9Props->BuildMetadata(); const auto& Mounts=Props->GetArrayField(TEXT("mounts"));
            if (!Mounts.IsValidIndex(Index)) { bRobotExpansionPassed=false; FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return; }
            const auto Mount=Mounts[Index]->AsObject(); const FVector Center=QAReadVector(Mount,TEXT("world_position_cm"));
            const FVector Normal=QAReadVector(Mount,TEXT("world_normal")),Up=QAReadVector(Mount,TEXT("world_up"));
            Observer->SetActorLocation(Center+Normal*60.+Up*12.); Observer->SetActorRotation((Center-Observer->GetActorLocation()).Rotation());
            auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("image"),File); Row->SetObjectField(TEXT("mount"),Mount);
            auto Rows=RobotExpansionReport->GetArrayField(TEXT("iq9_closeups")); Rows.Add(MakeShared<FJsonValueObject>(Row)); RobotExpansionReport->SetArrayField(TEXT("iq9_closeups"),Rows);
        } else Shot(File);
    } else {
        bool NIRSame=true,ThermalSame=true;
        for (const TCHAR* Name:{TEXT("nir_home_off"),TEXT("nir_moved_on"),TEXT("visible_home")}) {
            NIRSame &= Same(TEXT("robot_expansion_nir_home_on_raw_nir.png"),FString(TEXT("robot_expansion_"))+Name+TEXT("_raw_nir.png"));
            ThermalSame &= Same(TEXT("robot_expansion_nir_home_on_raw_thermal.png"),FString(TEXT("robot_expansion_"))+Name+TEXT("_raw_thermal.png"));
        }
        const bool FPSValid=FMath::IsFinite(HUD->GetSmoothedFramesPerSecond()) && HUD->GetSmoothedFramesPerSecond()>0 && HUD->GetFrameRateSampleCount()>RobotExpansionFPSSamples;
        RobotExpansionReport->SetBoolField(TEXT("fixed_nir_unchanged_by_observer"),NIRSame);
        RobotExpansionReport->SetBoolField(TEXT("fixed_thermal_unchanged_by_observer"),ThermalSame);
        RobotExpansionReport->SetBoolField(TEXT("fps_updates_while_scene_paused"),FPSValid);
        IQ9Props->SetLedTimeForQA(0.); RobotExpansionReport->SetObjectField(TEXT("iq9_led_on"),IQ9Props->BuildMetadata());
        IQ9Props->SetLedTimeForQA(.5); RobotExpansionReport->SetObjectField(TEXT("iq9_led_off"),IQ9Props->BuildMetadata());
        bRobotExpansionPassed &= NIRSame && ThermalSame && FPSValid;
        RobotExpansionReport->SetBoolField(TEXT("passed"),bRobotExpansionPassed);
        if (WeldingSensorStats) RobotExpansionReport->SetObjectField(TEXT("nir_raw_statistics"),WeldingSensorStats.ToSharedRef());
        if (!FFileHelper::SaveStringToFile(QAExpansionJson(RobotExpansionReport.ToSharedRef()),*FPaths::Combine(OutputRoot,TEXT("robot_expansion.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bRobotExpansionPassed=false;
        UE_LOG(LogTemp,Display,TEXT("DF_ROBOT_EXPANSION_SMOKE passed=%d fixed_nir=%d fixed_thermal=%d fps=%d"),bRobotExpansionPassed,NIRSame,ThermalSame,FPSValid);
        FPlatformMisc::RequestExitWithStatus(false,bRobotExpansionPassed ? 0:1); bWeldingSmoke=false; return;
    }
    ++RobotExpansionStep; NextAutomation=Now+.35;
}
