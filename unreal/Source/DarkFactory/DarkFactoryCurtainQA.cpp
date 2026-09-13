#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryWeldingSparks.h"
#include "DarkFactoryWeldingSmoke.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactorySoundscape.h"
#include "DarkFactoryFactoryLights.h"
#include "DarkFactoryCurtainPortal.h"
#include "DarkFactoryThermalRig.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace {
constexpr double TravelCm=ADarkFactoryProcessScene::CarriageTravelCm;
constexpr double TravelSeconds=ADarkFactoryProcessScene::CarriageTravelSeconds,SettleSeconds=10.,PauseSeconds=.75;
const TCHAR* PassNames[]={TEXT("entry_forward"),TEXT("entry_return"),TEXT("entry_forward_repeat"),TEXT("exit_forward")};
const double Starts[]={-TravelCm,0.,-TravelCm,0.},Ends[]={0.,-TravelCm,0.,TravelCm};
const int32 PortalIndices[]={0,0,0,1};
bool PortalSafe(const TSharedRef<FJsonObject>& Metadata,bool Settled=false) {
    const auto Contacts=Metadata->GetObjectField(TEXT("contact_events_by_component"));
    double Ceiling=0; Contacts->TryGetNumberField(TEXT("TunnelCeiling"),Ceiling);
    return Metadata->GetBoolField(TEXT("finite_physics_state")) &&
        Metadata->GetNumberField(TEXT("live_constraints"))==42. &&
        !Metadata->GetBoolField(TEXT("initial_binding_pending")) &&
        Metadata->GetNumberField(TEXT("peak_anchor_error_cm"))<10. &&
        Metadata->GetNumberField(TEXT("peak_deflection_degrees"))<120. && Ceiling==0 &&
        (!Settled || Metadata->GetNumberField(TEXT("current_deflection_degrees"))<10.);
}
}

bool ADarkFactoryWorld::InitializeCurtainSmoke() {
    if (!ProcessScene || ProcessController || !WeldingCell || CurtainPortals.Num()!=2) return false;
    // Only this separate fixture drives the carriage before Chaos. At the start
    // of each tick we sample the previous completed physics step, then submit
    // the next kinematic pose. Production and legacy QA retain their tick order.
    SetTickGroup(TG_PrePhysics);
    auto Isolate=[&](AActor* Actor) { if (Actor) { RemoveTickPrerequisiteActor(Actor); Actor->SetActorTickEnabled(false); } };
    Isolate(WeldingCell); Isolate(WeldingSparks); Isolate(WeldingSmoke); Isolate(IQ9Props);
    Isolate(CellMechanisms); Isolate(ControlCabinet); Isolate(Soundscape); Isolate(FactoryLights);
    Isolate(ThermalRig);
    for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) RemoveTickPrerequisiteActor(Portal);
    bInferenceEnabled=false; Source=TEXT("CHAOS PHYSICS FIXTURE"); Decision=TEXT("A");
    Status=TEXT("Curtain physics only; no model or classification");
    SetRealismDetailsEnabled(false);
    WeldingSparks->SetEffectsEnabled(false); WeldingSparks->ResetParticles(); WeldingSmoke->ResetParticles();
    WeldingCell->SetAnimationEnabled(false);
    ProcessScene->BeginBody(1,0.,1,0); // Explicit loose front door: geometry fixture, never a prediction.
    ProcessScene->ApplyFrame(3.4,0.,-TravelCm,true,false,0.);
    // Match legacy fixture startup: do not rebuild constraints in BeginPlay,
    // before Chaos has completed its first scene registration/physics step.
    for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) Portal->SetSimulationEnabled(false);
    CurtainReport=MakeShared<FJsonObject>(); CurtainPause=MakeShared<FJsonObject>();
    FString RunId=FGuid::NewGuid().ToString(EGuidFormats::Digits),Requested;
    if (FParse::Value(FCommandLine::Get(),TEXT("DFCurtainRunId="),Requested)) {
        FGuid Parsed; if (!FGuid::Parse(Requested,Parsed)) return false; RunId=Requested;
    }
    CurtainReport->SetStringField(TEXT("schema"),TEXT("darkfactory-curtain-smoke-v1"));
    CurtainReport->SetStringField(TEXT("source"),TEXT("chaos_physics_fixture"));
    CurtainReport->SetStringField(TEXT("session_id"),RunId);
    CurtainReport->SetStringField(TEXT("scope"),TEXT("Real Chaos strips and authored carriage/door colliders at process speed; no model inference or accuracy evidence"));
    CurtainReport->SetStringField(TEXT("sample_order"),TEXT("Previous completed Chaos step sampled before next pre-physics carriage pose"));
    CurtainReport->SetNumberField(TEXT("model_http_requests"),NIRInferenceRequests);
    CurtainReport->SetNumberField(TEXT("initial_resets_per_portal"),1);
    CurtainReport->SetNumberField(TEXT("measurement_resets"),0);
    CurtainReport->SetNumberField(TEXT("travel_distance_cm"),TravelCm);
    CurtainReport->SetNumberField(TEXT("tunnel_length_cm"),ADarkFactoryProcessScene::TunnelLengthCm);
    CurtainReport->SetNumberField(TEXT("travel_seconds"),TravelSeconds);
    CurtainReport->SetNumberField(TEXT("settle_seconds"),SettleSeconds);
    CurtainReport->SetStringField(TEXT("travel_curve"),TEXT("smoothstep; identical to live process transport"));
    CurtainReport->SetStringField(TEXT("first_body_geometry"),TEXT("loose door 0, returned through entry"));
    CurtainReport->SetStringField(TEXT("second_body_geometry"),TEXT("all doors seated, forward exit"));
    CurtainReport->SetStringField(TEXT("screenshot_directory"),FPaths::Combine(TEXT("screenshots"),RunId));
    IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputRoot,TEXT("screenshots"),RunId),true);
    CurtainPause->SetBoolField(TEXT("tested"),false);
    for (const TCHAR* Key:{TEXT("peak_pose_drift_cm"),TEXT("peak_rotation_drift_degrees"),TEXT("linear_velocity_restore_error_cm_s"),TEXT("angular_velocity_restore_error_deg_s")}) CurtainPause->SetNumberField(Key,0.);
    if (auto* PC=GetWorld()->GetFirstPlayerController()) {
        if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=false;
        if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) {
            Observer->SetCaptureAutomation(true); Observer->SetObserverMode(0);
            Observer->SetObserverHeadlightEnabled(true); SetRoofLightIntensity(80);
            if (auto* Camera=Observer->FindComponentByClass<UCameraComponent>()) Camera->FieldOfView=55;
        }
    }
    CurtainWallStarted=FPlatformTime::Seconds();
    UE_LOG(LogTemp,Display,TEXT("DF_CURTAIN_SMOKE_READY real_chaos=1 model_http=0 seconds_about=59"));
    return true;
}

void ADarkFactoryWorld::CurtainSmokeTick(float DeltaSeconds,double Now) {
    if (bCurtainFinished || !CurtainReport) return;
    if (Now-CurtainWallStarted>110.) { FinishCurtainSmoke(false,TEXT("110 second wall timeout")); return; }
    if (Now<NextAutomation) return;
    bool ShotRequested=false;
    auto Shot=[&](const FString& Name) {
        ShotRequested=true;
        const FString Relative=FPaths::Combine(CurtainReport->GetStringField(TEXT("screenshot_directory")),Name+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Relative),false,false);
        return Relative;
    };
    auto BeginPass=[&]() {
        // The prior return-settled screenshot must render before the next body
        // appears. BeginPass is entered on the following game frame.
        if (CurtainPassIndex==2) {
            ProcessScene->BeginBody(2,CurtainClock,0);
            ProcessScene->ApplyFrame(3.4,CurtainClock,-TravelCm,true,false,0.);
        }
        CurtainPassTime=0; CurtainClearClock=-1; CurtainMovingSamples=0;
        CurtainCarriageOffset=Starts[CurtainPassIndex];
        const int32 PortalIndex=PortalIndices[CurtainPassIndex];
        auto* Portal=CurtainPortals[PortalIndex].Get();
        CurtainContactBaseline=Portal->GetCarriageContactCount();
        CurtainPass=MakeShared<FJsonObject>();
        CurtainPass->SetStringField(TEXT("name"),PassNames[CurtainPassIndex]);
        CurtainPass->SetNumberField(TEXT("portal_index"),PortalIndex);
        CurtainPass->SetNumberField(TEXT("start_offset_cm"),Starts[CurtainPassIndex]);
        CurtainPass->SetNumberField(TEXT("end_offset_cm"),Ends[CurtainPassIndex]);
        CurtainPass->SetNumberField(TEXT("move_start_clock_seconds"),CurtainClock);
        const FTransform Pose=Portal->GetActorTransform();
        const FVector Position=Pose.TransformPosition(FVector(650,-680,370));
        const FVector Target=Pose.TransformPosition(FVector(0,0,180));
        if (auto* PC=GetWorld()->GetFirstPlayerController()) if (auto* Observer=Cast<ADarkFactoryPawn>(PC->GetPawn())) {
            Observer->SetActorLocation(Position); Observer->SetActorRotation((Target-Position).Rotation());
        }
        UE_LOG(LogTemp,Display,TEXT("DF_CURTAIN_PASS %s"),PassNames[CurtainPassIndex]);
    };
    if (!bCurtainStarted) {
        // This is the only strip reset. Subsequent passages, pause and settling
        // retain the actual shared Chaos state without scripted pose correction.
        for (ADarkFactoryCurtainPortal* P:CurtainPortals) { P->ResetSimulation(); P->SetSimulationEnabled(true); }
        bCurtainStarted=true; BeginPass();
    }
    if (CurtainPassIndex>=4) { FinishCurtainSmoke(true); return; }
    if (!CurtainPass) BeginPass();
    auto* Portal=CurtainPortals[PortalIndices[CurtainPassIndex]].Get();
    auto Sample=MakeShared<FJsonObject>();
    Sample->SetNumberField(TEXT("clock_seconds"),CurtainClock);
    Sample->SetStringField(TEXT("pass"),PassNames[CurtainPassIndex]);
    Sample->SetNumberField(TEXT("offset_cm"),CurtainCarriageOffset);
    Sample->SetBoolField(TEXT("paused"),bCurtainPaused);
    TArray<TSharedPtr<FJsonValue>> Finite,Anchors,Deflections;
    for (ADarkFactoryCurtainPortal* P:CurtainPortals) {
        bool Valid=P->HasFinitePhysicsState();
        for (UStaticMeshComponent* Mesh:P->GetStripMeshes()) Valid &= !Mesh->GetComponentTransform().ContainsNaN() && !Mesh->GetPhysicsLinearVelocity().ContainsNaN() && !Mesh->GetPhysicsAngularVelocityInDegrees().ContainsNaN();
        Finite.Add(MakeShared<FJsonValueBoolean>(Valid));
        Anchors.Add(MakeShared<FJsonValueNumber>(P->GetMaximumAnchorErrorCm()));
        Deflections.Add(MakeShared<FJsonValueNumber>(P->GetCurrentDeflectionDegrees()));
    }
    Sample->SetArrayField(TEXT("finite_physics_state"),Finite);
    Sample->SetArrayField(TEXT("anchor_error_cm"),Anchors); Sample->SetArrayField(TEXT("deflection_degrees"),Deflections);
    CurtainSamples.Add(MakeShared<FJsonValueObject>(Sample));
    if (CurtainSamples.Num()>7000) { FinishCurtainSmoke(false,TEXT("sample budget exceeded; use t.MaxFPS 60")); return; }
    if (!bCurtainPaused && CurtainPassTime>0 && CurtainPassTime<TravelSeconds) ++CurtainMovingSamples;
    if (!CurtainPass->HasField(TEXT("during_screenshot")) && Portal->GetCarriageContactCount()>CurtainContactBaseline && Portal->GetCurrentDeflectionDegrees()>10.)
        CurtainPass->SetStringField(TEXT("during_screenshot"),Shot(FString(PassNames[CurtainPassIndex])+TEXT("_during")));
    double ActiveLinear=0,ActiveAngular=0;
    if (!bCurtainPauseDone && CurtainPassIndex==0 && !bCurtainPaused) for (UStaticMeshComponent* Mesh:Portal->GetStripMeshes()) {
        ActiveLinear=FMath::Max(ActiveLinear,Mesh->GetPhysicsLinearVelocity().Size());
        ActiveAngular=FMath::Max(ActiveAngular,Mesh->GetPhysicsAngularVelocityInDegrees().Size());
    }
    if (!ShotRequested && (ActiveLinear>1. || ActiveAngular>1.) && Portal->GetCarriageContactCount()>CurtainContactBaseline && !bCurtainPauseDone && CurtainPassIndex==0 && !bCurtainPaused && Portal->GetCurrentDeflectionDegrees()>5. && CurtainPassTime<2.) {
        double Linear=0,Angular=0;
        for (ADarkFactoryCurtainPortal* P:CurtainPortals) for (UStaticMeshComponent* Mesh:P->GetStripMeshes()) {
            CurtainPauseTransforms.Add(Mesh->GetComponentTransform());
            CurtainPauseLinear.Add(Mesh->GetPhysicsLinearVelocity());
            CurtainPauseAngular.Add(Mesh->GetPhysicsAngularVelocityInDegrees());
            Linear=FMath::Max(Linear,Mesh->GetPhysicsLinearVelocity().Size());
            Angular=FMath::Max(Angular,Mesh->GetPhysicsAngularVelocityInDegrees().Size());
        }
        CurtainPause->SetNumberField(TEXT("linear_velocity_before_cm_s"),Linear);
        CurtainPause->SetNumberField(TEXT("angular_velocity_before_deg_s"),Angular);
        CurtainPause->SetNumberField(TEXT("deflection_before_degrees"),Portal->GetCurrentDeflectionDegrees());
        CurtainPause->SetStringField(TEXT("before_screenshot"),Shot(TEXT("entry_pause_before")));
        for (ADarkFactoryCurtainPortal* P:CurtainPortals) P->SetSimulationEnabled(false);
        bCurtainPaused=true; CurtainPauseStarted=Now;
        return; // No carriage clock or pose update during the pause.
    }
    if (bCurtainPaused) {
        int32 Index=0;
        double Position=CurtainPause->GetNumberField(TEXT("peak_pose_drift_cm"));
        double Rotation=CurtainPause->GetNumberField(TEXT("peak_rotation_drift_degrees"));
        for (ADarkFactoryCurtainPortal* P:CurtainPortals) for (UStaticMeshComponent* Mesh:P->GetStripMeshes()) {
            Position=FMath::Max(Position,FVector::Distance(Mesh->GetComponentLocation(),CurtainPauseTransforms[Index].GetLocation()));
            Rotation=FMath::Max(Rotation,FMath::RadiansToDegrees(Mesh->GetComponentQuat().AngularDistance(CurtainPauseTransforms[Index].GetRotation()))); ++Index;
        }
        CurtainPause->SetNumberField(TEXT("peak_pose_drift_cm"),Position);
        CurtainPause->SetNumberField(TEXT("peak_rotation_drift_degrees"),Rotation);
        if (Now-CurtainPauseStarted<PauseSeconds) return;
        CurtainPause->SetStringField(TEXT("after_screenshot"),Shot(TEXT("entry_pause_after")));
        for (ADarkFactoryCurtainPortal* P:CurtainPortals) P->SetSimulationEnabled(true);
        Index=0; double Linear=0,Angular=0;
        for (ADarkFactoryCurtainPortal* P:CurtainPortals) for (UStaticMeshComponent* Mesh:P->GetStripMeshes()) {
            Linear=FMath::Max(Linear,FVector::Distance(Mesh->GetPhysicsLinearVelocity(),CurtainPauseLinear[Index]));
            Angular=FMath::Max(Angular,FVector::Distance(Mesh->GetPhysicsAngularVelocityInDegrees(),CurtainPauseAngular[Index])); ++Index;
        }
        CurtainPause->SetNumberField(TEXT("linear_velocity_restore_error_cm_s"),Linear);
        CurtainPause->SetNumberField(TEXT("angular_velocity_restore_error_deg_s"),Angular);
        CurtainPause->SetNumberField(TEXT("duration_seconds"),Now-CurtainPauseStarted);
        CurtainPause->SetBoolField(TEXT("tested"),true);
        bCurtainPaused=false; bCurtainPauseDone=true;
    }
    if (!ShotRequested && !CurtainPass->HasField(TEXT("action_screenshot")) && Portal->GetCarriageContactCount()>CurtainContactBaseline && Portal->GetCurrentDeflectionDegrees()>45.) {
        CurtainPass->SetStringField(TEXT("action_screenshot"),Shot(FString(PassNames[CurtainPassIndex])+TEXT("_action")));
        CurtainPass->SetNumberField(TEXT("action_deflection_degrees"),Portal->GetCurrentDeflectionDegrees());
        CurtainPass->SetNumberField(TEXT("action_clock_seconds"),CurtainClock);
    }
    // Measurements above describe the last completed physics frame. Advance the
    // exact production smoothstep below, with real game-frame delta and no seeks.
    CurtainMaxFrameDelta=FMath::Max(CurtainMaxFrameDelta,double(DeltaSeconds));
    if (CurtainPassTime>=TravelSeconds) {
        if (CurtainClearClock<0) {
            CurtainClearClock=CurtainClock;
            // Production removes a returned/departed body after the offscreen
            // journey. Leaving it parked here would prop up a four-metre strip
            // within reach even though it is outside the observer view.
            const bool RemoveBody=CurtainPassIndex==1 || CurtainPassIndex==3;
            if (RemoveBody) {
                ProcessScene->EndBody();
                ProcessScene->ApplyFrame(3.4,CurtainClock,CurtainCarriageOffset,false,false,0.);
            }
            CurtainPass->SetBoolField(TEXT("body_removed_before_settling"),RemoveBody);
            int32 ActiveColliders=0;
            for (UBoxComponent* Collider:WeldingCell->GetCarriageColliders()) ActiveColliders+=Collider->GetCollisionEnabled()!=ECollisionEnabled::NoCollision ? 1:0;
            CurtainPass->SetNumberField(TEXT("carriage_colliders_during_settle"),ActiveColliders);
            CurtainPass->SetNumberField(TEXT("travel_duration_seconds"),CurtainPassTime);
            CurtainPass->SetNumberField(TEXT("move_end_clock_seconds"),CurtainClock);
            CurtainPass->SetNumberField(TEXT("clear_clock_seconds"),CurtainClearClock);
        }
        if (CurtainClock-CurtainClearClock>=SettleSeconds) {
            CurtainPass->SetNumberField(TEXT("settled_clock_seconds"),CurtainClock);
            CurtainPass->SetNumberField(TEXT("settled_after_clear_seconds"),CurtainClock-CurtainClearClock);
            CurtainPass->SetNumberField(TEXT("moving_samples"),CurtainMovingSamples);
            CurtainPass->SetNumberField(TEXT("contact_events_delta"),Portal->GetCarriageContactCount()-CurtainContactBaseline);
            CurtainPass->SetObjectField(TEXT("settled_metadata"),Portal->BuildPhysicsMetadata());
            CurtainPass->SetStringField(TEXT("settled_screenshot"),Shot(FString(PassNames[CurtainPassIndex])+TEXT("_settled")));
            const FBox Bounds=WeldingCell->GetCarriageBounds(); const double Y=Portal->GetActorLocation().Y;
            CurtainPass->SetNumberField(TEXT("carriage_clearance_cm"),Y<Bounds.Min.Y ? Bounds.Min.Y-Y:Y>Bounds.Max.Y ? Y-Bounds.Max.Y:0.);
            CurtainPasses.Add(MakeShared<FJsonValueObject>(CurtainPass));
            ++CurtainPassIndex;
            if (CurtainPassIndex>=4) return; // Final PNG is saved at end of this frame, before next tick verdict.
            CurtainPass.Reset();
            return; // Let this portal screenshot render before moving the observer for the next pass.
        }
    }
    CurtainClock+=DeltaSeconds; CurtainPassTime+=DeltaSeconds;
    const double Alpha=FMath::SmoothStep(0.f,1.f,float(CurtainPassTime/TravelSeconds));
    CurtainCarriageOffset=FMath::Lerp(Starts[CurtainPassIndex],Ends[CurtainPassIndex],Alpha);
    const bool BodyVisible=CurtainClearClock<0 || (CurtainPassIndex!=1 && CurtainPassIndex!=3);
    ProcessScene->ApplyFrame(3.4,CurtainClock,CurtainCarriageOffset,BodyVisible,false,0.);
}

void ADarkFactoryWorld::FinishCurtainSmoke(bool Completed,const FString& Error) {
    bCurtainFinished=true;
    if (!CurtainReport) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    bool Passed=Completed && CurtainPasses.Num()==4 && bCurtainPauseDone && ProcessController==nullptr && NIRInferenceRequests==0;
    TArray<TSharedPtr<FJsonValue>> FinalPortals;
    for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { auto Metadata=Portal->BuildPhysicsMetadata(); Passed &= PortalSafe(Metadata,true); FinalPortals.Add(MakeShared<FJsonValueObject>(Metadata)); }
    for (const auto& Value:CurtainPasses) {
        const auto Pass=Value->AsObject();
        Passed &= Pass->GetNumberField(TEXT("contact_events_delta"))>0 && Pass->GetNumberField(TEXT("moving_samples"))>20 &&
            Pass->GetNumberField(TEXT("settled_after_clear_seconds"))>=SettleSeconds &&
            Pass->GetNumberField(TEXT("carriage_clearance_cm"))>10. && PortalSafe(Pass->GetObjectField(TEXT("settled_metadata")).ToSharedRef(),true);
        if (Pass->GetNumberField(TEXT("end_offset_cm"))!=0.) Passed &= Pass->GetBoolField(TEXT("body_removed_before_settling")) && Pass->GetNumberField(TEXT("carriage_colliders_during_settle"))==0;
        for (const TCHAR* Key:{TEXT("during_screenshot"),TEXT("settled_screenshot")}) {
            FString Relative; Passed &= Pass->TryGetStringField(Key,Relative) && IFileManager::Get().FileSize(*FPaths::Combine(OutputRoot,Relative))>100;
        }
    }
    for (const auto& SampleValue:CurtainSamples) {
        const auto Sample=SampleValue->AsObject();
        for (const auto& V:Sample->GetArrayField(TEXT("finite_physics_state"))) Passed &= V->AsBool();
        for (const auto& V:Sample->GetArrayField(TEXT("anchor_error_cm"))) Passed &= V->AsNumber()<10.;
        for (const auto& V:Sample->GetArrayField(TEXT("deflection_degrees"))) Passed &= V->AsNumber()<120.;
    }
    if (bCurtainPauseDone) {
        Passed &= CurtainPause->GetBoolField(TEXT("tested")) && CurtainPause->GetNumberField(TEXT("deflection_before_degrees"))>5 &&
            (CurtainPause->GetNumberField(TEXT("linear_velocity_before_cm_s"))>1 || CurtainPause->GetNumberField(TEXT("angular_velocity_before_deg_s"))>1);
        for (const TCHAR* Key:{TEXT("peak_pose_drift_cm"),TEXT("peak_rotation_drift_degrees"),TEXT("linear_velocity_restore_error_cm_s"),TEXT("angular_velocity_restore_error_deg_s")}) Passed &= CurtainPause->GetNumberField(Key)<.1;
    }
    CurtainReport->SetArrayField(TEXT("passes"),CurtainPasses); CurtainReport->SetArrayField(TEXT("samples"),CurtainSamples);
    CurtainReport->SetArrayField(TEXT("final_portals"),FinalPortals); CurtainReport->SetObjectField(TEXT("pause"),CurtainPause);
    CurtainReport->SetNumberField(TEXT("max_frame_delta_seconds"),CurtainMaxFrameDelta);
    CurtainReport->SetNumberField(TEXT("elapsed_simulation_seconds"),CurtainClock);
    CurtainReport->SetNumberField(TEXT("elapsed_wall_seconds"),FPlatformTime::Seconds()-CurtainWallStarted);
    CurtainReport->SetBoolField(TEXT("passed"),Passed); CurtainReport->SetStringField(TEXT("error"),Error);
    FString Text; FJsonSerializer::Serialize(CurtainReport.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(OutputRoot,TEXT("curtains.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("DF_CURTAIN_SMOKE completed=%d passed=%d saved=%d samples=%d"),Completed,Passed,Saved,CurtainSamples.Num());
    FPlatformMisc::RequestExitWithStatus(false,Passed && Saved ? 0:1);
}
