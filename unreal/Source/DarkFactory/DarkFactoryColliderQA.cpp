#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryGameMode.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactorySoundscape.h"
#include "DarkFactoryCurtainPortal.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryFactoryLights.h"
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

// Observer-only diagnostic evidence. Never enters the inference/dataset path.
void ADarkFactoryWorld::ColliderSmokeTick(double Now) {
    if (Now<NextAutomation) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    auto* HUD=PC ? Cast<ADarkFactoryHUD>(PC->GetHUD()):nullptr;
    if (!Observer || !HUD || !WeldingCell || !ThermalRig || !IQ9Props) {
        UE_LOG(LogTemp,Error,TEXT("DF_COLLIDER_SMOKE missing scene dependencies"));
        FPlatformMisc::RequestExitWithStatus(false,1); bWeldingSmoke=false; return;
    }
    auto Shot=[&](const FString& Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
    auto Pose=[&](double Seconds) {
        IQ9Props->SetLedTimeForQA(.1); WeldingCell->SetMotionTime(Seconds);
        if (CellMechanisms) CellMechanisms->UpdateFromCell();
        if (Soundscape) Soundscape->UpdateFromCell();
    };
    auto Snapshot=[&]() {
        auto R=MakeShared<FJsonObject>();
        R->SetNumberField(TEXT("time"),WeldingCell->GetMotionTime());
        R->SetObjectField(TEXT("pose"),WeldingCell->BuildPreviewMetadata());
        R->SetObjectField(TEXT("overlay"),HUD->BuildColliderOverlayMetadata());
        TArray<TSharedPtr<FJsonValue>> Portals;
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) if (Portal) Portals.Add(MakeShared<FJsonValueObject>(Portal->BuildPhysicsMetadata()));
        R->SetArrayField(TEXT("portal_physics"),Portals);
        return R;
    };
    auto Append=[&](const TCHAR* Key,const TSharedRef<FJsonObject>& Row) {
        auto Rows=ColliderReport->GetArrayField(Key); Rows.Add(MakeShared<FJsonValueObject>(Row)); ColliderReport->SetArrayField(Key,Rows);
    };
    auto Raw=[&](const FString& Stem,const TSharedRef<FJsonObject>& Row) {
        const FString NIR=Stem+TEXT("_raw_nir.png"),Thermal=Stem+TEXT("_raw_thermal.png");
        const bool N=SaveWeldingNIRQA(NIR,Stem);
        const bool T=ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,Thermal));
        Row->SetStringField(TEXT("raw_nir"),NIR); Row->SetStringField(TEXT("raw_thermal"),Thermal);
        Row->SetBoolField(TEXT("raw_saved"),N && T);
    };
    if (ColliderStep==0) {
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        WeldingSensorStats=MakeShared<FJsonObject>(); ColliderReport=MakeShared<FJsonObject>();
        if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences"))) {
            WeldingEffectsPreviousTemporalFreeze=Freeze->GetInt(); Freeze->Set(1,ECVF_SetByCode);
        }
        ColliderReport->SetStringField(TEXT("scope"),TEXT("Observer HUD collider diagnostic only; approximate bounds are not contact geometry, model inputs or collision certification"));
        ColliderReport->SetArrayField(TEXT("views"),{}); ColliderReport->SetArrayField(TEXT("motion_views"),{});
        bInferenceEnabled=false; SetRealismDetailsEnabled(true); WeldingCell->SetAnimationEnabled(false);
        WeldingCell->SetRobotTimingForQA(true); WeldingSparks->SetEffectsEnabled(false);
        WeldingSparks->ResetParticles(); WeldingSmoke->ResetParticles();
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        Observer->SetCaptureAutomation(true); Observer->SetObserverBookmark(0,true); HUD->SetColliderOverlayMode(0);
        Pose(10.); Observer->SetObserverMode(0);
    } else if (ColliderStep<=32) {
        const int32 Index=(ColliderStep-1)/2,Substep=(ColliderStep-1)%2;
        const int32 ObserverMode=Index/4,Sequence=Index%4;
        const int32 Modes[]={0,1,2,0};
        const TCHAR* Names[]={TEXT("off"),TEXT("physics"),TEXT("all"),TEXT("off_restored")};
        const FString Stem=FString::Printf(TEXT("collider_mode%d_%s"),ObserverMode,Names[Sequence]);
        if (Substep==0) {
            Observer->SetObserverMode(ObserverMode); FactoryLights->SetStartupTimeForQA(3.);
            HUD->SetColliderOverlayMode(Modes[Sequence]);
        } else {
            auto R=Snapshot(); R->SetNumberField(TEXT("observer_mode"),ObserverMode);
            R->SetStringField(TEXT("name"),Names[Sequence]); R->SetStringField(TEXT("image"),Stem+TEXT(".png"));
            Raw(Stem,R); Append(TEXT("views"),R); Shot(Stem+TEXT(".png"));
        }
    } else if (ColliderStep<=40) {
        const int32 Index=(ColliderStep-33)/2,Substep=(ColliderStep-33)%2;
        const double Times[]={18.,20.6,10.,10.};
        const TCHAR* Names[]={TEXT("moving"),TEXT("hidden"),TEXT("restored"),TEXT("near_plane")};
        const FString Stem=FString(TEXT("collider_"))+Names[Index];
        if (Substep==0) {
            Pose(Times[Index]); HUD->SetColliderOverlayMode(2); Observer->SetObserverMode(3);
            Observer->SetObserverBookmark(0,true); FactoryLights->SetStartupTimeForQA(3.);
            if (Index==3) { Observer->SetActorLocation(FVector(0,0,140)); Observer->SetActorRotation(FRotator(0,90,0)); }
        } else {
            auto R=Snapshot(); R->SetStringField(TEXT("name"),Names[Index]); R->SetStringField(TEXT("image"),Stem+TEXT(".png"));
            Append(TEXT("motion_views"),R); Shot(Stem+TEXT(".png"));
        }
    } else {
        auto SameFile=[&](const FString& A,const FString& B) {
            TArray<uint8> X,Y; return FFileHelper::LoadFileToArray(X,*FPaths::Combine(OutputRoot,A))
                && FFileHelper::LoadFileToArray(Y,*FPaths::Combine(OutputRoot,B)) && !X.IsEmpty() && X==Y;
        };
        auto Json=[](const TSharedRef<FJsonObject>& Value) {
            FString Text; FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
        };
        bool Parity=true,State=true,RawSaved=true,Filtering=true;
        const auto& Views=ColliderReport->GetArrayField(TEXT("views"));
        for (int32 M=0;M<4;++M) {
            const auto First=Views[M*4]->AsObject();
            for (int32 S=0;S<4;++S) {
                const auto R=Views[M*4+S]->AsObject(); RawSaved &= R->GetBoolField(TEXT("raw_saved"));
                for (const TCHAR* Channel:{TEXT("raw_nir"),TEXT("raw_thermal")}) Parity &= SameFile(First->GetStringField(Channel),R->GetStringField(Channel));
                State &= Json(First->GetObjectField(TEXT("pose")).ToSharedRef())==Json(R->GetObjectField(TEXT("pose")).ToSharedRef());
                const auto Overlay=R->GetObjectField(TEXT("overlay"));
                const int32 Mode=static_cast<int32>(Overlay->GetNumberField(TEXT("mode")));
                Filtering &= Mode==(S==3 ? 0:S) && !Overlay->GetBoolField(TEXT("truncated"));
                if (Mode==0) Filtering &= Overlay->GetNumberField(TEXT("last_drawn_lines"))==0;
                else Filtering &= Overlay->GetNumberField(TEXT("last_drawn_lines"))>0;
            }
        }
        ColliderReport->SetBoolField(TEXT("fixed_sensor_overlay_parity"),Parity);
        ColliderReport->SetBoolField(TEXT("cell_state_unchanged_by_overlay"),State);
        ColliderReport->SetBoolField(TEXT("raw_saved"),RawSaved); ColliderReport->SetBoolField(TEXT("overlay_modes_rendered"),Filtering);
        ColliderReport->SetObjectField(TEXT("nir_raw_statistics"),WeldingSensorStats.ToSharedRef());
        const bool Passed=Parity && State && RawSaved && Filtering;
        ColliderReport->SetBoolField(TEXT("passed"),Passed);
        FString Text=Json(ColliderReport.ToSharedRef());
        const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(OutputRoot,TEXT("collider_overlay.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("DF_COLLIDER_SMOKE passed=%d sensors=%d cell_unchanged=%d modes=%d"),Passed && Saved,Parity,State,Filtering);
        FPlatformMisc::RequestExitWithStatus(false,Passed && Saved ? 0:1); bWeldingSmoke=false; return;
    }
    ++ColliderStep; NextAutomation=Now+.35;
}
