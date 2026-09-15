#include "DarkFactoryProcessController.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryCellMechanisms.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace {
using Phase=EDarkFactoryProcessPhase;
struct FMotionCue { const TCHAR* Name; int32 Body; Phase Step; double At; int32 ObserverCloseup=0; int32 InspectionSpot=0; int32 ObserverMode=3; };
const FMotionCue MotionCues[]={
    {TEXT("missing_arrival"),1,Phase::Arriving,0.},
    {TEXT("rack_rest_clearance"),1,Phase::Arriving,0.,17},
    {TEXT("gripper_rest_closeup"),1,Phase::Arriving,0.,4},
    {TEXT("dispenser_rest_closeup"),1,Phase::Arriving,0.,5},
    {TEXT("welder_direct_mount_closeup"),1,Phase::Arriving,0.,10},
    {TEXT("inspection_rest_closeup"),1,Phase::Arriving,0.,11},
    {TEXT("iq9_base_1"),1,Phase::Arriving,0.,6},
    {TEXT("iq9_base_2"),1,Phase::Arriving,0.,7},
    {TEXT("iq9_base_3"),1,Phase::Arriving,0.,8},
    {TEXT("iq9_base_4"),1,Phase::Arriving,0.,9},
    {TEXT("missing_presence"),1,Phase::PanelPresence,1.1},
    {TEXT("permanent_frame_empty"),1,Phase::PanelPresence,1.1,20},
    {TEXT("clamps_left_base_clear"),1,Phase::PanelPresence,1.1,15},
    {TEXT("clamps_right_base_clear"),1,Phase::PanelPresence,1.1,16},
    {TEXT("clamp_platform_overview"),1,Phase::PanelPresence,1.1,18},
    {TEXT("platform_exit_support"),1,Phase::PanelPresence,1.1,19},
    {TEXT("camera_asset_visible"),1,Phase::PanelPresence,1.1,12},
    {TEXT("camera_asset_nir"),1,Phase::PanelPresence,1.1,12,0,1},
    {TEXT("camera_asset_thermal"),1,Phase::PanelPresence,1.1,12,0,2},
    {TEXT("nir_last_pose"),1,Phase::PanelPresence,2.0,11,2},
    {TEXT("nir_withdraw_early"),1,Phase::AdhesiveApplication,.2,11},
    {TEXT("nir_withdraw_via"),1,Phase::AdhesiveApplication,.4,11},
    {TEXT("nir_withdraw_late"),1,Phase::AdhesiveApplication,.6,11},
    {TEXT("sealant_right_start"),1,Phase::AdhesiveApplication,1.45},
    {TEXT("sealant_right_mid"),1,Phase::AdhesiveApplication,3.5,1},
    {TEXT("sealant_right_end"),1,Phase::AdhesiveApplication,6.00},
    {TEXT("sealant_left_mid"),1,Phase::AdhesiveApplication,9.1},
    {TEXT("sealant_left_end"),1,Phase::AdhesiveApplication,11.55,13},
    {TEXT("frame_with_sealant"),1,Phase::AdhesiveApplication,13.0,20},
    {TEXT("rack_pickup_contact"),1,Phase::PanelPlacement,1.4,17},
    {TEXT("stock_pickup"),1,Phase::PanelPlacement,1.7},
    {TEXT("rack_lift_clearance"),1,Phase::PanelPlacement,2.2,17},
    {TEXT("raised_transfer"),1,Phase::PanelPlacement,3.5},
    {TEXT("panel_above_frame"),1,Phase::PanelPlacement,4.75,21},
    {TEXT("panel_lowering"),1,Phase::PanelPlacement,5.35,21},
    {TEXT("panel_on_frame"),1,Phase::PanelPlacement,6.,20},
    {TEXT("held_panel"),1,Phase::PanelPlacement,6.,2},
    {TEXT("adhesive_inspection_hold"),1,Phase::Adhesive,4.},
    {TEXT("held_contact_thermal"),1,Phase::Adhesive,4.,13,0,2},
    {TEXT("pinch_open"),1,Phase::Welding,1.30,3},
    {TEXT("pinch_clamped"),1,Phase::Welding,1.45,3},
    {TEXT("weld_pulse_1"),1,Phase::Welding,1.70,3},
    {TEXT("rear_roof_clearance"),1,Phase::Welding,1.70,14},
    {TEXT("rear_roof_clearance_nir"),1,Phase::Welding,1.70,14,0,1},
    {TEXT("rear_roof_clearance_thermal"),1,Phase::Welding,1.70,14,0,2},
    {TEXT("pinch_released"),1,Phase::Welding,2.15,3},
    {TEXT("weld_pulse_2"),1,Phase::Welding,3.05},
    {TEXT("weld_pulse_3"),1,Phase::Welding,4.4},
    {TEXT("weld_pulse_4"),1,Phase::Welding,5.75},
    {TEXT("weld_pulse_5"),1,Phase::Welding,7.1},
    {TEXT("weld_pulse_6"),1,Phase::Welding,8.45,14},
    {TEXT("panel_seated"),1,Phase::PanelSeated,1.1},
    {TEXT("weld_cooling"),1,Phase::WeldCooling,8.},
    {TEXT("holder_retraction"),1,Phase::HolderRetract,.7},
    {TEXT("assembled_departure"),1,Phase::Departing,2.8},
    {TEXT("preassembled_arrival"),2,Phase::Arriving,2.8},
    {TEXT("rack_restock_clearance"),2,Phase::Arriving,2.8,17},
    {TEXT("preassembled_presence"),2,Phase::PanelPresence,1.1},
    {TEXT("preassembled_adhesive"),2,Phase::Adhesive,4.},
    {TEXT("contact_hot_thermal"),2,Phase::Adhesive,4.,13,0,2},
    {TEXT("contact_cool_thermal"),2,Phase::Adhesive,18.,13,0,2},
    {TEXT("preassembled_weld_cooling"),2,Phase::WeldCooling,1.1},
    {TEXT("preassembled_departure"),2,Phase::Departing,2.8},
};
bool FiniteMotionJson(const TSharedPtr<FJsonValue>& Value) {
    if (!Value.IsValid()) return false;
    if (Value->Type==EJson::Number) return FMath::IsFinite(Value->AsNumber());
    if (Value->Type==EJson::Array) { for (const auto& Item:Value->AsArray()) if (!FiniteMotionJson(Item)) return false; }
    if (Value->Type==EJson::Object) { for (const auto& Pair:Value->AsObject()->Values) if (!FiniteMotionJson(Pair.Value)) return false; }
    return true;
}
TArray<uint8> MotionPNG(UTextureRenderTarget2D* Target) {
    TArray<FColor> Pixels; if (!Target || Target->SizeX!=448 || Target->SizeY!=256) return {};
    FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags) || Pixels.Num()!=448*256) return {};
    for (auto& Pixel:Pixels) { const uint8 Gray=FMath::Clamp(FMath::RoundToInt(.2126*Pixel.R+.7152*Pixel.G+.0722*Pixel.B),0,255); Pixel=FColor(Gray,Gray,Gray,255); }
    TArray64<uint8> Bytes; FImageUtils::PNGCompressImageArray(448,256,Pixels,Bytes);
    TArray<uint8> Out; Out.Append(Bytes.GetData(),Bytes.Num()); return Out;
}
}
struct FDarkFactoryPanelMotionQA {
    double Started=FPlatformTime::Seconds(),LastMotionSample=-1.;
    int32 Cue=0,Warmup=0;
    bool Initialized=false,Frozen=false,AwaitScreenshot=false,Passed=true,Finished=false;
    FString Screenshot;
    TSharedPtr<FJsonObject> Pending;
    TArray<TSharedPtr<FJsonValue>> Samples,Motion;
};

void ADarkFactoryProcessController::PanelMotionQATick(float DeltaSeconds) {
    if (!PanelMotionQA) PanelMotionQA=MakeShared<FDarkFactoryPanelMotionQA>();
    auto& QA=*PanelMotionQA;
    if (QA.Finished) return;
    if (FPlatformTime::Seconds()-QA.Started>180.) {
        UE_LOG(LogTemp,Error,TEXT("DF_PANEL_MOTION_TIMEOUT")); QA.Finished=true; bReady=false; FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if (!QA.Initialized) {
        if (FPlatformTime::Seconds()-QA.Started<4.) return;
        if (IFileManager::Get().FileExists(*FPaths::Combine(OutputRoot,TEXT("panel_motion.json")))) {
            UE_LOG(LogTemp,Error,TEXT("DF_PANEL_MOTION_REQUIRES_FRESH_OUTPUT")); QA.Finished=true; bReady=false; FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        State.BodySequence=1; State.BodyId=TEXT("physical-qa-missing"); State.ClockSeconds=0; State.StageSeconds=0;
        State.Phase=Phase::Arriving; State.ActiveGate=-1; State.bPaused=false;
        State.Backend=TEXT("PHYSICAL QA / NO MODEL"); State.Message=TEXT("Authored motion fixture; no model requests or acceptance decisions");
        ProcessScene->BeginBody(1,0.,0); QA.Initialized=true;
    }
    if (QA.Frozen) { WeldingCell->SetAnimationEnabled(false); return; }
    const bool Done=QA.Cue>=UE_ARRAY_COUNT(MotionCues);
    const double Duration=ADarkFactoryProcessScene::GetPhaseDuration(State.Phase);
    if (Done && State.StageSeconds>=Duration) { QA.Finished=true; return; }
    if (!Done && (State.Phase!=MotionCues[QA.Cue].Step || State.BodySequence!=MotionCues[QA.Cue].Body)
        && (Duration<=0. || State.StageSeconds>=Duration)) {
        const auto& Cue=MotionCues[QA.Cue];
        if (State.BodySequence!=Cue.Body) {
            ProcessScene->EndBody(); State.BodySequence=Cue.Body; State.BodyId=TEXT("physical-qa-preassembled");
            ProcessScene->BeginBody(Cue.Body,State.ClockSeconds,1);
        }
        State.Phase=Cue.Step; State.StageSeconds=0;
        const TCHAR* Inspection=Cue.Step==Phase::PanelPresence ? TEXT("panel_presence"):Cue.Step==Phase::Adhesive ? TEXT("adhesive")
            :Cue.Step==Phase::PanelSeated ? TEXT("panel_seated"):Cue.Step==Phase::WeldCooling ? TEXT("weld"):nullptr;
        if (Inspection) ProcessScene->RequestInspectionView(Inspection,0,State.ClockSeconds);
    }
    const bool SameCue=!Done && State.Phase==MotionCues[QA.Cue].Step && State.BodySequence==MotionCues[QA.Cue].Body;
    if (SameCue && MotionCues[QA.Cue].InspectionSpot>0)
        ProcessScene->RequestInspectionView(TEXT("panel_presence"),MotionCues[QA.Cue].InspectionSpot,State.ClockSeconds);
    const double Stop=SameCue ? MotionCues[QA.Cue].At:ADarkFactoryProcessScene::GetPhaseDuration(State.Phase);
    const double Advance=FMath::Min(FMath::Clamp(double(DeltaSeconds),0.,.1),FMath::Max(0.,Stop-State.StageSeconds));
    State.StageSeconds+=Advance; State.ClockSeconds+=Advance;
    const double Move=FMath::SmoothStep(0.,1.,State.StageSeconds/ADarkFactoryProcessScene::CarriageTravelSeconds);
    const double Offset=State.Phase==Phase::Arriving ? FMath::Lerp(-ADarkFactoryProcessScene::CarriageTravelCm,0.,Move)
        :State.Phase==Phase::Departing ? FMath::Lerp(0.,ADarkFactoryProcessScene::CarriageTravelCm,Move):0.;
    WeldingCell->SetAnimationEnabled(true); ProcessScene->ApplyAssemblyFrame(State.Phase,State.StageSeconds,State.ClockSeconds,Offset,true);
    State.ActiveGate=-1; State.bBranchKnown=true; State.bPreassembled=State.BodySequence==2;
    for (int32 I=0;I<FDarkFactoryProcessStatus::StepCount;++I)
        State.Steps[I]=I==static_cast<int32>(State.Phase) ? EDarkFactoryStepState::Active:EDarkFactoryStepState::Pending;
    if (QA.LastMotionSample<0. || State.ClockSeconds-QA.LastMotionSample>=.1) {
        auto Motion=MakeShared<FJsonObject>(); Motion->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds);
        Motion->SetNumberField(TEXT("body_sequence"),State.BodySequence); Motion->SetNumberField(TEXT("phase"),static_cast<int32>(State.Phase));
        Motion->SetNumberField(TEXT("stage_seconds"),State.StageSeconds); Motion->SetNumberField(TEXT("carriage_offset_cm"),Offset);
        Motion->SetObjectField(TEXT("physical_arm_state"),WeldingCell->GetPanelAssemblyMetadata());
        QA.Passed &= FiniteMotionJson(MakeShared<FJsonValueObject>(Motion)); QA.Motion.Add(MakeShared<FJsonValueObject>(Motion));
        QA.LastMotionSample=State.ClockSeconds;
    }
    if (SameCue && State.StageSeconds>=Stop) { QA.Frozen=true; QA.Warmup=4; WeldingCell->SetAnimationEnabled(false); }
}

void ADarkFactoryProcessController::PanelMotionQACaptureTick() {
    if (!PanelMotionQA || !PanelMotionQA->Initialized) return;
    auto& QA=*PanelMotionQA;
    if (QA.Finished) {
        auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("schema"),TEXT("darkfactory-panel-motion-smoke-v1"));
        Report->SetStringField(TEXT("session_id"),SessionId); Report->SetStringField(TEXT("source"),TEXT("scripted_physical_scene"));
        Report->SetStringField(TEXT("scope"),TEXT("Rendered authored trajectories, fixed overview raw QA cameras and observer-only tool/mount closeups and four base EVK closeups; no model decisions, training samples or industrial collision certification"));
        Report->SetNumberField(TEXT("model_http_requests"),0); Report->SetBoolField(TEXT("model_accuracy_evidence"),false);
        Report->SetNumberField(TEXT("expected_checkpoints"),UE_ARRAY_COUNT(MotionCues)); Report->SetNumberField(TEXT("width"),448); Report->SetNumberField(TEXT("height"),256);
        Report->SetArrayField(TEXT("thermal_window_c"),{MakeShared<FJsonValueNumber>(20),MakeShared<FJsonValueNumber>(120)});
        Report->SetArrayField(TEXT("samples"),QA.Samples); Report->SetArrayField(TEXT("motion_samples"),QA.Motion);
        QA.Passed &= QA.Samples.Num()==UE_ARRAY_COUNT(MotionCues) && !Request.IsValid() && !bModelRequestsEnabled;
        Report->SetBoolField(TEXT("passed"),QA.Passed);
        FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<TCHAR,TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
        const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(OutputRoot,TEXT("panel_motion.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("DF_PANEL_MOTION_COMPLETE checkpoints=%d finite=%d saved=%d model_requests=0"),QA.Samples.Num(),QA.Passed,Saved);
        bReady=false; FPlatformMisc::RequestExitWithStatus(false,Saved && QA.Passed ? 0:1); return;
    }
    if (!QA.Frozen) return;
    if (QA.AwaitScreenshot) {
        if (FScreenshotRequest::IsScreenshotRequested()) return;
        QA.Passed &= IFileManager::Get().FileExists(*QA.Screenshot);
        QA.Samples.Add(MakeShared<FJsonValueObject>(QA.Pending)); QA.Pending.Reset();
        QA.AwaitScreenshot=false; QA.Frozen=false; ++QA.Cue;
        for (const auto& Light:NIRLights) Light->SetVisibility(false);
        return;
    }
    ADarkFactoryPawn* Observer=nullptr;
    for (TActorIterator<ADarkFactoryPawn> It(GetWorld());It;++It) { Observer=*It; break; }
    if (!Observer) { QA.Passed=false; QA.Finished=true; return; }
    // Observer-only closeups include both tools parked at the same physical time.
    // Raw NIR/thermal overview cameras and their lights retain this pose.
    const auto& Cue=MotionCues[QA.Cue];
    const FVector Target(0,-220,260),Eye(-850,-1040,720);
    const FRotator Rotation=(Target-Eye).Rotation();
    FVector ObserverEye=Eye,ObserverTarget=Target; float ObserverFOV=70.f;
    if (Cue.ObserverCloseup==1) { ObserverEye=FVector(-20,-430,405); ObserverTarget=FVector(45,-235,316); ObserverFOV=48.f; }
    if (Cue.ObserverCloseup==2) { ObserverEye=FVector(-110,-495,440); ObserverTarget=FVector(1.6,-285.1,341); ObserverFOV=55.f; }
    if (Cue.ObserverCloseup==3) { ObserverTarget=FVector(-80,-230,265); ObserverEye=ObserverTarget+FVector(100,60,20); ObserverFOV=53.f; }
    if (Cue.ObserverCloseup==13) { ObserverTarget=FVector(1.6,-285,268); ObserverEye=FVector(-30,-310,520); ObserverFOV=63.f; }
    if (Cue.ObserverCloseup==14) { ObserverTarget=FVector(-30,-210,300); ObserverEye=FVector(195,-470,410); ObserverFOV=58.f; }
    if (Cue.ObserverCloseup==20) { ObserverTarget=FVector(0,-285,270); ObserverEye=FVector(275,-455,480); ObserverFOV=57.f; }
    if (Cue.ObserverCloseup==21) { ObserverTarget=FVector(0,-285,315); ObserverEye=FVector(360,-640,450); ObserverFOV=58.f; }
    if (Cue.ObserverCloseup==15) { ObserverTarget=FVector(-205,0,147); ObserverEye=FVector(-445,270,310); ObserverFOV=64.f; }
    if (Cue.ObserverCloseup==16) { ObserverTarget=FVector(205,0,147); ObserverEye=FVector(445,270,310); ObserverFOV=64.f; }
    if (Cue.ObserverCloseup==17) { ObserverTarget=FVector(480,-445,230); ObserverEye=FVector(1000,-185,610); ObserverFOV=57.f; }
    if (Cue.ObserverCloseup==18) { ObserverTarget=FVector(0,410,110); ObserverEye=FVector(920,1050,780); ObserverFOV=65.f; }
    if (Cue.ObserverCloseup==19) { ObserverTarget=FVector(195,1050,117); ObserverEye=FVector(465,860,250); ObserverFOV=55.f; }
    if (Cue.ObserverCloseup==4) { ObserverEye=FVector(730,-180,390); ObserverTarget=FVector(505,-349,312); ObserverFOV=45.f; }
    if (Cue.ObserverCloseup==5) { ObserverEye=FVector(520,-275,315); ObserverTarget=FVector(345,-125,240); ObserverFOV=45.f; }
    if (Cue.ObserverCloseup>=6 && Cue.ObserverCloseup<=9) {
        bool Found=false;
        for (TActorIterator<ADarkFactoryIQ9Props> It(GetWorld());It;++It) {
            const auto Metadata=It->BuildMetadata();
            const auto& Mounts=Metadata->GetArrayField(TEXT("mounts"));
            const int32 Index=Cue.ObserverCloseup-6;
            if (!Mounts.IsValidIndex(Index)) break;
            const auto Mount=Mounts[Index]->AsObject();
            const auto& P=Mount->GetArrayField(TEXT("world_position_cm"));
            const auto& N=Mount->GetArrayField(TEXT("world_normal"));
            if (P.Num()!=3 || N.Num()!=3) break;
            ObserverTarget=FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());
            const FVector Normal(N[0]->AsNumber(),N[1]->AsNumber(),N[2]->AsNumber());
            // An oblique view reaches R3's cover between the stored panels; its
            // face rays are clear without hiding or moving the rack for this QA.
            ObserverEye=Index==2 ? ObserverTarget+Normal*25.
                +FVector::CrossProduct(FVector::UpVector,Normal)*40.+FVector(0,0,12)
                :ObserverTarget+Normal*105.+FVector(0,0,24);
            ObserverFOV=45.f; Found=true; break;
        }
        if (!Found) { QA.Passed=false; QA.Finished=true; return; }
    }
    if (Cue.ObserverCloseup==10 || Cue.ObserverCloseup==11) {
        const auto Arms=WeldingCell->GetPanelAssemblyMetadata()->GetArrayField(TEXT("robots"));
        const int32 Robot=Cue.ObserverCloseup==10 ? 1:3;
        bool Found=false;
        if (Arms.IsValidIndex(Robot)) for (const auto& J:Arms[Robot]->AsObject()->GetArrayField(TEXT("joints"))) {
            const auto Joint=J->AsObject();
            if (!Joint->GetStringField(TEXT("id")).EndsWith(TEXT("_tool_roll"))) continue;
            const auto& P=Joint->GetArrayField(TEXT("world_location_cm"));
            ObserverTarget=FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber())+FVector(0,0,-20);
            ObserverEye=ObserverTarget+FVector(-145,-95,55);
            ObserverFOV=45.f; Found=true; break;
        }
        if (!Found) { QA.Passed=false; QA.Finished=true; return; }
    }
    if (Cue.ObserverCloseup==12) {
        FVector Lens; FRotator LensRotation; float LensFOV;
        ProcessScene->GetInspectionCameraPose(TEXT("panel_presence"),0,Lens,LensRotation,LensFOV);
        ObserverTarget=Lens+LensRotation.RotateVector(FVector(-10,0,0));
        ObserverEye=Lens+LensRotation.RotateVector(FVector(68,-65,45)); ObserverFOV=45.f;
    }
    const bool Closeup=Cue.ObserverCloseup!=0;
    for (TActorIterator<AHUD> It(GetWorld());It;++It) It->bShowHUD=!Closeup;
    Observer->SetCaptureAutomation(true); Observer->SetObserverMode(Cue.ObserverMode);
    Observer->SetObserverHeadlightEnabled(false);
    Observer->SetActorLocationAndRotation(ObserverEye,(ObserverTarget-ObserverEye).Rotation());
    if (auto* Camera=Observer->FindComponentByClass<UCameraComponent>()) Camera->SetFieldOfView(ObserverFOV);
    Cameras[0]->TextureTarget=Targets[0]; Cameras[1]->TextureTarget=Targets[3];
    for (const auto& Camera:Cameras) { Camera->SetWorldLocationAndRotation(Eye,Rotation); Camera->FOVAngle=70.f; }
    for (const auto& Light:NIRLights) { Light->SetWorldLocationAndRotation(Eye,Rotation); Light->SetVisibility(true); }
    ThermalRig->PrepareInspectionMaterial(CameraMaterials[1]);
    Cameras[0]->CaptureScene(); Cameras[1]->CaptureScene();
    if (QA.Warmup>0) { --QA.Warmup; return; }
    const FString Name=Cue.Name;
    const FString NIR=Name+TEXT("_nir.png"),Thermal=Name+TEXT("_thermal.png"),Screen=Name+TEXT("_observer.png");
    const auto NIRBytes=MotionPNG(Targets[0]),ThermalBytes=MotionPNG(Targets[3]);
    QA.Passed &= !NIRBytes.IsEmpty() && !ThermalBytes.IsEmpty()
        && FFileHelper::SaveArrayToFile(NIRBytes,*FPaths::Combine(OutputRoot,NIR))
        && FFileHelper::SaveArrayToFile(ThermalBytes,*FPaths::Combine(OutputRoot,Thermal));
    QA.Pending=MakeShared<FJsonObject>(); QA.Pending->SetStringField(TEXT("name"),Name);
    QA.Pending->SetNumberField(TEXT("body_sequence"),State.BodySequence); QA.Pending->SetNumberField(TEXT("clock_seconds"),State.ClockSeconds);
    QA.Pending->SetNumberField(TEXT("stage_seconds"),State.StageSeconds); QA.Pending->SetNumberField(TEXT("phase"),static_cast<int32>(State.Phase));
    QA.Pending->SetStringField(TEXT("observer_screenshot"),Screen); QA.Pending->SetStringField(TEXT("nir_png"),NIR); QA.Pending->SetStringField(TEXT("thermal_png"),Thermal);
    QA.Pending->SetStringField(TEXT("camera_scope"),TEXT("fixed overview raw QA cameras, not R4 model inputs; closeup cues move only the observer"));
    QA.Pending->SetBoolField(TEXT("observer_closeup"),Closeup);
    QA.Pending->SetNumberField(TEXT("observer_mode"),Cue.ObserverMode);
    QA.Pending->SetNumberField(TEXT("observer_fov_degrees"),ObserverFOV);
    auto VectorJSON=[](const FVector& V) { return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}; };
    QA.Pending->SetArrayField(TEXT("observer_eye_cm"),VectorJSON(ObserverEye));
    QA.Pending->SetArrayField(TEXT("observer_target_cm"),VectorJSON(ObserverTarget));
    QA.Pending->SetObjectField(TEXT("scene"),ProcessScene->BuildMetadata()); QA.Pending->SetObjectField(TEXT("arm_state"),WeldingCell->GetPanelAssemblyMetadata());
    QA.Pending->SetObjectField(TEXT("tool_heat"),WeldingCell->BuildToolHeatMetadata());
    // Actual rendered bounds for rack/holder clearance review, confined to QA.
    TArray<UStaticMeshComponent*> CellMeshes; WeldingCell->GetComponents(CellMeshes);
    TArray<TSharedPtr<FJsonValue>> RackBounds;
    for (const auto* Mesh:CellMeshes) {
        const FString Id=Mesh->GetName();
        if (!Id.StartsWith(TEXT("SM_PanelSupply_")) && !Id.StartsWith(TEXT("SM_robot_px_ny_"))
            && !Id.StartsWith(TEXT("PanelGripper"))) continue;
        const FBox B=Mesh->Bounds.GetBox(); auto Entry=MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"),Id); Entry->SetBoolField(TEXT("visible"),Mesh->IsVisible());
        Entry->SetArrayField(TEXT("min_cm"),VectorJSON(B.Min)); Entry->SetArrayField(TEXT("max_cm"),VectorJSON(B.Max));
        RackBounds.Add(MakeShared<FJsonValueObject>(Entry));
    }
    QA.Pending->SetArrayField(TEXT("rack_holder_bounds"),RackBounds);
    bool ClampsVerified=false,PlatformsVerified=false;
    for (TActorIterator<ADarkFactoryCellMechanisms> It(GetWorld());It;++It) {
        const auto Mechanisms=It->BuildMetadata();
        QA.Pending->SetObjectField(TEXT("mechanisms"),Mechanisms);
        const auto& Fixtures=Mechanisms->GetArrayField(TEXT("fixtures"));
        ClampsVerified=Mechanisms->GetNumberField(TEXT("fixture_count"))==2 && Fixtures.Num()==2;
        for (const auto& Value:Fixtures) {
            const auto Fixture=Value->AsObject();
            const FString Id=Fixture->GetStringField(TEXT("id"));
            ClampsVerified &= (Id==TEXT("fixture_nx_py") || Id==TEXT("fixture_px_py"))
                && Fixture->GetBoolField(TEXT("surface_contact_bound"));
        }
        // Confirm actual imported deck triangles support every foot, including
        // the exit photoeyes. This catches platforms moved away from the clamps.
        TArray<UStaticMeshComponent*> MechanismMeshes; It->GetComponents(MechanismMeshes);
        TArray<TSharedPtr<FJsonValue>> Supports;
        PlatformsVerified=true;
        for (const TCHAR* FootId:{TEXT("fixture_nx_py_foot"),TEXT("fixture_px_py_foot"),TEXT("exit_photoeye_sensor_foot"),TEXT("exit_photoeye_reflector_foot")}) {
            const bool Right=FString(FootId).Contains(TEXT("px")) || FString(FootId).Contains(TEXT("sensor"));
            const FString DeckId=Right ? TEXT("SM_AssemblyFloorDeck_R1"):TEXT("SM_AssemblyFloorDeck_R2");
            auto* const* Foot=MechanismMeshes.FindByPredicate([&](const auto* M){return M->GetName()==FootId;});
            auto* const* Deck=CellMeshes.FindByPredicate([&](const auto* M){return M->GetName()==DeckId;});
            if (!Foot || !Deck) { PlatformsVerified=false; continue; }
            const FBox B=(*Foot)->Bounds.GetBox(); int32 Supported=0; double MaxGap=0.;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(ClampPlatformSupport),true);
            for (const double U:{0.,.5,1.}) for (const double V:{0.,.5,1.}) {
                const FVector P(FMath::Lerp(B.Min.X,B.Max.X,U),FMath::Lerp(B.Min.Y,B.Max.Y,V),B.Min.Z);
                FHitResult Hit;
                if ((*Deck)->LineTraceComponent(Hit,P+FVector(0,0,5),P-FVector(0,0,5),Params)) {
                    const double Gap=FMath::Abs(P.Z-Hit.ImpactPoint.Z); MaxGap=FMath::Max(MaxGap,Gap);
                    if (Gap<.05) ++Supported;
                }
            }
            auto Support=MakeShared<FJsonObject>(); Support->SetStringField(TEXT("foot"),FootId); Support->SetStringField(TEXT("deck"),DeckId);
            Support->SetNumberField(TEXT("supported_samples"),Supported); Support->SetNumberField(TEXT("expected_samples"),9);
            Support->SetNumberField(TEXT("maximum_vertical_gap_cm"),MaxGap); Support->SetArrayField(TEXT("foot_min_cm"),VectorJSON(B.Min));
            Support->SetArrayField(TEXT("foot_max_cm"),VectorJSON(B.Max)); Supports.Add(MakeShared<FJsonValueObject>(Support));
            PlatformsVerified &= Supported==9;
        }
        PlatformsVerified &= Supports.Num()==4;
        QA.Pending->SetArrayField(TEXT("platform_foot_support"),Supports);
        break;
    }
    QA.Pending->SetBoolField(TEXT("two_retained_clamps_verified"),ClampsVerified);
    QA.Passed &= ClampsVerified;
    QA.Pending->SetBoolField(TEXT("platform_support_verified"),PlatformsVerified); QA.Passed &= PlatformsVerified;
    QA.Passed &= FiniteMotionJson(MakeShared<FJsonValueObject>(QA.Pending));
    QA.Screenshot=FPaths::Combine(OutputRoot,Screen); FScreenshotRequest::RequestScreenshot(QA.Screenshot,!Closeup,false); QA.AwaitScreenshot=true;
    UE_LOG(LogTemp,Display,TEXT("DF_PANEL_MOTION_CHECKPOINT %s clock=%.3f"),*Name,State.ClockSeconds);
}
