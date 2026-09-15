#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryGameMode.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactorySoundscape.h"
#include "DarkFactoryCurtainPortal.h"
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

// Private rendered evidence for the existing cabinet glass. Never a model input.
void ADarkFactoryWorld::CabinetSmokeTick(double Now) {
    if (Now<NextAutomation) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    auto* HUD=PC ? Cast<ADarkFactoryHUD>(PC->GetHUD()):nullptr;
    if (!Observer || !WeldingCell || !ControlCabinet || !ThermalRig || !IQ9Props || !FactoryLights) {
        UE_LOG(LogTemp,Error,TEXT("DF_CABINET_SMOKE missing scene dependencies"));
        FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return;
    }
    auto Json=[](const TSharedRef<FJsonObject>& Value) {
        FString Text; FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
    };
    auto Pose=[&](double Seconds) {
        IQ9Props->SetLedTimeForQA(.1); WeldingCell->SetMotionTime(Seconds);
        if (CellMechanisms) CellMechanisms->UpdateFromCell();
        if (Soundscape) Soundscape->UpdateFromCell();
        ControlCabinet->UpdateFromCell(true);
    };
    auto Vector=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key) {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if (!O || !O->TryGetArrayField(Key,A) || A->Num()!=3) return FVector::ZeroVector;
        return FVector((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber());
    };
    auto Snapshot=[&]() {
        auto R=MakeShared<FJsonObject>();
        R->SetNumberField(TEXT("time"),WeldingCell->GetMotionTime());
        R->SetNumberField(TEXT("observer_mode"),Observer->GetObserverMode());
        R->SetNumberField(TEXT("thermal_profile_count"),ThermalRig->GetProfileCount());
        R->SetNumberField(TEXT("imported_mesh_count"),WeldingCell->GetMeshCount());
        R->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata());
        const auto Panel=ControlCabinet->BuildMetadata(); R->SetObjectField(TEXT("panel"),Panel);
        const TSharedPtr<FJsonObject>* ScreenPtr=nullptr;
        if (Panel->TryGetObjectField(TEXT("screen"),ScreenPtr)) {
            const auto Screen=*ScreenPtr;
            const FVector Center=Vector(Screen,TEXT("center_world_cm")),Right=Vector(Screen,TEXT("right_world")),Up=Vector(Screen,TEXT("up_world"));
            auto Project=[&](const FVector& C,double HalfWidth,double HalfHeight) {
                auto Q=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Corners;
                bool Valid=true; FVector2D Low(DBL_MAX,DBL_MAX),High(-DBL_MAX,-DBL_MAX);
                for (int32 I=0;I<4;++I) {
                    const FVector P=C+Right*(I&1 ? HalfWidth:-HalfWidth)+Up*(I&2 ? HalfHeight:-HalfHeight);
                    FVector2D Pixel; Valid &= PC->ProjectWorldLocationToScreen(P,Pixel);
                    Valid &= FMath::IsFinite(Pixel.X) && FMath::IsFinite(Pixel.Y);
                    Low.X=FMath::Min(Low.X,Pixel.X); Low.Y=FMath::Min(Low.Y,Pixel.Y);
                    High.X=FMath::Max(High.X,Pixel.X); High.Y=FMath::Max(High.Y,Pixel.Y);
                    Corners.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Pixel.X),MakeShared<FJsonValueNumber>(Pixel.Y)}));
                }
                Q->SetBoolField(TEXT("valid"),Valid); Q->SetArrayField(TEXT("corners_px"),Corners);
                Q->SetArrayField(TEXT("bounds_px"),{MakeShared<FJsonValueNumber>(Low.X),MakeShared<FJsonValueNumber>(Low.Y),MakeShared<FJsonValueNumber>(High.X),MakeShared<FJsonValueNumber>(High.Y)});
                return Q;
            };
            R->SetObjectField(TEXT("screen_projection"),Project(Center,Screen->GetNumberField(TEXT("width_cm"))*.5,Screen->GetNumberField(TEXT("height_cm"))*.5));
            TArray<TSharedPtr<FJsonValue>> Buttons;
            for (const auto& ButtonValue:Panel->GetArrayField(TEXT("buttons"))) {
                const auto Button=ButtonValue->AsObject(); const double Radius=Button->GetNumberField(TEXT("radius_cm"));
                auto Q=Project(Vector(Button,TEXT("center_world_cm")),Radius,Radius);
                Q->SetNumberField(TEXT("index"),Button->GetNumberField(TEXT("index")));
                Q->SetNumberField(TEXT("level"),Button->GetNumberField(TEXT("level")));
                Buttons.Add(MakeShared<FJsonValueObject>(Q));
            }
            R->SetArrayField(TEXT("button_projections"),Buttons);
        }
        return R;
    };
    if (CabinetStep==0) {
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        CabinetReport=MakeShared<FJsonObject>(); WeldingSensorStats=MakeShared<FJsonObject>();
        CabinetReport->SetArrayField(TEXT("views"),{});
        CabinetReport->SetStringField(TEXT("scope"),TEXT("Authored cabinet LCD/LED appearance with simulated cycle telemetry; no live robot or model decision data"));
        if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences"))) {
            WeldingEffectsPreviousTemporalFreeze=Freeze->GetInt(); Freeze->Set(1,ECVF_SetByCode);
        }
        bInferenceEnabled=false; SetRealismDetailsEnabled(true); ControlCabinet->SetEnabled(true);
        WeldingCell->SetAnimationEnabled(false); WeldingCell->SetRobotTimingForQA(true);
        WeldingSparks->SetEffectsEnabled(false); WeldingSparks->ResetParticles(); WeldingSmoke->ResetParticles();
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetCaptureAutomation(true); Observer->SetNightVisionSmokeFrame(3);
        Observer->SetObserverHeadlightEnabled(false); Observer->SetObserverMode(0); SetRoofLightIntensity(0);
        if (HUD) HUD->SetColliderOverlayMode(0);
        if (auto* Camera=Observer->FindComponentByClass<UCameraComponent>()) { Camera->FieldOfView=50; Camera->PostProcessSettings.AutoExposureBias=0; }
        Pose(10.);
    } else if (CabinetStep<=26) {
        const int32 Index=(CabinetStep-1)/2,Substep=(CabinetStep-1)%2;
        const TCHAR* Names[]={TEXT("dark_off"),TEXT("dark_on"),TEXT("mode0"),TEXT("mode1"),TEXT("mode2"),TEXT("mode3"),TEXT("phase_idle"),TEXT("phase_stagger"),TEXT("phase_welding"),TEXT("phase_parked"),TEXT("phase_restored"),TEXT("pause_before"),TEXT("pause_after")};
        const double Times[]={10.,10.,10.,10.,10.,10.,0.,6.5,10.,16.,10.,10.,10.};
        const FString Stem=FString(TEXT("cabinet_"))+Names[Index];
        if (Substep==0) {
            if (Index!=12) Pose(Times[Index]);
            ControlCabinet->SetPanelPower(Index!=0); ControlCabinet->UpdateFromCell(Index!=12);
            FVector Location; FRotator Rotation; ControlCabinet->GetInspectionView(Location,Rotation);
            Observer->SetActorLocation(Location); Observer->SetActorRotation(Rotation);
            const int32 Mode=(Index>=2 && Index<=5) ? Index-2:0;
            Observer->SetObserverMode(Mode); Observer->SetObserverHeadlightEnabled(false);
            SetRoofLightIntensity(Index>=2 && Index<=5 ? 80:0);
            FactoryLights->SetStartupTimeForQA(3.);
        } else {
            auto R=Snapshot(); R->SetStringField(TEXT("name"),Names[Index]); R->SetStringField(TEXT("image"),Stem+TEXT(".png"));
            if (Index<=5) {
                const FString NIR=Stem+TEXT("_raw_nir.png"),Thermal=Stem+TEXT("_raw_thermal.png");
                const bool N=SaveWeldingNIRQA(NIR,Stem),T=ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,Thermal));
                R->SetStringField(TEXT("raw_nir"),NIR); R->SetStringField(TEXT("raw_thermal"),Thermal); R->SetBoolField(TEXT("raw_saved"),N && T);
            }
            auto Views=CabinetReport->GetArrayField(TEXT("views")); Views.Add(MakeShared<FJsonValueObject>(R)); CabinetReport->SetArrayField(TEXT("views"),Views);
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Stem+TEXT(".png")),false,false);
        }
    } else {
        const auto& Views=CabinetReport->GetArrayField(TEXT("views"));
        auto SameFile=[&](const FString& A,const FString& B) {
            TArray<uint8> X,Y; return FFileHelper::LoadFileToArray(X,*FPaths::Combine(OutputRoot,A)) && FFileHelper::LoadFileToArray(Y,*FPaths::Combine(OutputRoot,B)) && !X.IsEmpty() && X==Y;
        };
        bool RawSaved=Views.Num()==13,Parity=RawSaved,Thermal=RawSaved,Pause=RawSaved,Restored=RawSaved,Counts=RawSaved;
        if (Views.Num()==13) {
            for (int32 I=0;I<6;++I) RawSaved &= Views[I]->AsObject()->GetBoolField(TEXT("raw_saved"));
            for (int32 I=3;I<6;++I) for (const TCHAR* Channel:{TEXT("raw_nir"),TEXT("raw_thermal")}) Parity &= SameFile(Views[2]->AsObject()->GetStringField(Channel),Views[I]->AsObject()->GetStringField(Channel));
            Thermal &= SameFile(Views[0]->AsObject()->GetStringField(TEXT("raw_thermal")),Views[1]->AsObject()->GetStringField(TEXT("raw_thermal")));
            auto UI=[&](int32 I) {
                auto P=Views[I]->AsObject()->GetObjectField(TEXT("panel")); auto Signature=MakeShared<FJsonObject>();
                for (const TCHAR* Key:{TEXT("clock_seconds"),TEXT("cycle_progress"),TEXT("cycle_seconds")}) Signature->SetNumberField(Key,P->GetNumberField(Key));
                Signature->SetArrayField(TEXT("robots"),P->GetArrayField(TEXT("robots"))); Signature->SetArrayField(TEXT("buttons"),P->GetArrayField(TEXT("buttons")));
                return Json(Signature);
            };
            Pause &= UI(11)==UI(12); Restored &= UI(8)==UI(10);
            for (const auto& Value:Views) {
                const auto R=Value->AsObject(),P=R->GetObjectField(TEXT("panel"));
                Counts &= R->GetNumberField(TEXT("thermal_profile_count"))==240 && P->GetNumberField(TEXT("mesh_count_added"))==0 && P->GetNumberField(TEXT("thermal_profiles_added"))==0 && P->GetNumberField(TEXT("light_count"))<=1;
            }
        }
        CabinetReport->SetBoolField(TEXT("fixed_sensor_observer_parity"),Parity); CabinetReport->SetBoolField(TEXT("power_thermal_unchanged"),Thermal);
        CabinetReport->SetBoolField(TEXT("pause_holds_panel_state"),Pause); CabinetReport->SetBoolField(TEXT("seek_restores_panel_state"),Restored);
        CabinetReport->SetBoolField(TEXT("bounded_resources"),Counts); CabinetReport->SetBoolField(TEXT("raw_saved"),RawSaved);
        CabinetReport->SetObjectField(TEXT("nir_raw_statistics"),WeldingSensorStats.ToSharedRef());
        const bool Passed=RawSaved && Parity && Thermal && Pause && Restored && Counts; CabinetReport->SetBoolField(TEXT("passed"),Passed);
        const FString Text=Json(CabinetReport.ToSharedRef());
        const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(OutputRoot,TEXT("control_cabinet.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("DF_CABINET_SMOKE passed=%d sensors=%d thermal=%d pause=%d seek=%d resources=%d"),Passed && Saved,Parity,Thermal,Pause,Restored,Counts);
        FPlatformMisc::RequestExitWithStatus(false,Passed && Saved ? 0:1); bWeldingSmoke=false; return;
    }
    ++CabinetStep; NextAutomation=Now+.45;
}
