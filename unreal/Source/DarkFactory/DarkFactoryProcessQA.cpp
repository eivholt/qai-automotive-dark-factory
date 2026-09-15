#include "DarkFactoryProcessController.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryWeldingCell.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Explicit native policy inputs and synthetic encoder mailboxes; never model accuracy.
// No ProcessRequest or complete synthetic image batch is submitted by this fixture.
bool ADarkFactoryProcessController::RunPolicySmoke() {
    if (!FParse::Param(FCommandLine::Get(),TEXT("DFProcessPolicySmoke")) || !bReady || !ProcessScene.IsValid()
        || !WeldingCell.IsValid() || State.bRequestPending || Request.IsValid() || Readback.IsBusy() || EncodeFuture.IsValid()) return false;
    using Phase=EDarkFactoryProcessPhase; using Gate=EDarkFactoryGateState; using Step=EDarkFactoryStepState;
    TArray<TSharedPtr<FJsonValue>> Checks,InspectionPolicyPoses;
    bool Passed=true; const bool OriginalRequests=bModelRequestsEnabled;
    bModelRequestsEnabled=true;
    auto Check=[&](const FString& Name,bool Good,const FString& Detail=FString()) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Name); Row->SetBoolField(TEXT("passed"),Good);
        Row->SetStringField(TEXT("detail"),Detail); Checks.Add(MakeShared<FJsonValueObject>(Row)); Passed &= Good;
        if (!Good) UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_POLICY_CHECK_FAILED %s %s"),*Name,*Detail);
    };
    auto DelayCapture=[&]() { NextInspection=FPlatformTime::Seconds()+3600.; };
    auto Decision=[&](const TCHAR* Code,bool Valid=true) {
        ApplyDecision(Code,Valid,TEXT("POLICY TEST: scripted control input"),0,TEXT("POLICY TEST")); DelayCapture();
    };
    auto AtFixture=[&]() { return WeldingCell->GetCarriageRoot() && FMath::Abs(WeldingCell->GetCarriageRoot()->GetRelativeLocation().Y)<.001; };
    auto CompleteMotion=[&](Phase Expected) {
        const double Duration=ADarkFactoryProcessScene::GetPhaseDuration(Expected);
        for (int32 I=0;I<FMath::CeilToInt(Duration/.1)+3 && State.Phase==Expected;++I) Tick(.1f);
        DelayCapture();
    };
    auto ResetPresence=[&](int32 Preassembled=0) {
        CancelInspection(); State.bPaused=false; bCaptureQA=false; bModelQA=false; ForcedPreassembled=Preassembled;
        State.Backend=TEXT("POLICY TEST"); ProcessScene->EndBody(); BeginBody(); CompleteMotion(Phase::Arriving); Tick(0.f);
        DelayCapture();
    };
    auto Hold=[&](Phase Expected,int32 Count) {
        bool Safe=true;
        for (int32 I=0;I<Count;++I) {
            Tick(.1f); Safe &= State.Phase==Expected && AtFixture() && !WeldingCell->IsWelding() && WeldingCell->GetWeldEmitters().IsEmpty();
        }
        return Safe;
    };
    auto CaptureInactive=[&]() { return !bCaptureActive && !State.bRequestPending && !Request.IsValid() && !Readback.IsBusy() && !EncodeFuture.IsValid(); };
    auto FinishSyntheticView=[&](uint8 Byte) {
        TPromise<TArray<uint8>> Encoder; EncodeFuture=Encoder.GetFuture(); Encoder.SetValue(TArray<uint8>{Byte,2,3}); CaptureTick();
    };

    {
        TArray<FColor> Source; Source.Init(FColor(80,80,80,255),448*256);
        Check(TEXT("sensor_accepts_actual_grayscale_source"),HasGrayscaleSensorPixels(Source));
        Source[0]=FColor(80,81,80,255);
        Check(TEXT("sensor_allows_one_code_roundoff"),HasGrayscaleSensorPixels(Source));
        Source.Last()=FColor(100,80,74,255);
        Check(TEXT("sensor_rejects_cold_sepia_before_cpu_conversion"),!HasGrayscaleSensorPixels(Source));
        Source.Reset();
        Check(TEXT("sensor_rejects_empty_source"),!HasGrayscaleSensorPixels(Source));
    }
    {
        double IdleSince=0;
        Check(TEXT("sensor_startup_holds_while_compiling"),!AdvanceSensorIdleWindow(10.,true,IdleSince) && IdleSince==0.);
        Check(TEXT("sensor_startup_requires_continuous_idle_window"),!AdvanceSensorIdleWindow(11.,false,IdleSince)
            && !AdvanceSensorIdleWindow(15.99,false,IdleSince));
        Check(TEXT("new_compilation_restarts_sensor_idle_window"),!AdvanceSensorIdleWindow(16.,true,IdleSince)
            && !AdvanceSensorIdleWindow(17.,false,IdleSince) && !AdvanceSensorIdleWindow(21.99,false,IdleSince));
        Check(TEXT("sensor_startup_accepts_five_stable_seconds"),AdvanceSensorIdleWindow(22.,false,IdleSince));
    }
    Check(TEXT("four_stage_contract_and_six_preview_slots"),ContractVersion==TEXT("automotive-panel-process-v3")
        && PromptHashes.Num()==4 && Targets.Num()==6 && Cameras.Num()==2 && CameraMaterials.Num()==2);
    Check(TEXT("expected_smoke_branches_all_exit"),IsModelSmokeOutcomeCorrect(1,true,false,true,true)
        && IsModelSmokeOutcomeCorrect(2,true,true,false,false) && IsModelSmokeOutcomeCorrect(3,true,false,true,true));
    Check(TEXT("smoke_rejects_reversed_branches_despite_three_exits"),!IsModelSmokeOutcomeCorrect(1,true,true,false,false)
        && !IsModelSmokeOutcomeCorrect(2,true,false,true,true));
    Check(TEXT("smoke_rejects_unperformed_assembly_or_unwanted_weld"),!IsModelSmokeOutcomeCorrect(1,true,false,false,true)
        && !IsModelSmokeOutcomeCorrect(1,true,false,true,false) && !IsModelSmokeOutcomeCorrect(2,true,true,false,true));
    Check(TEXT("smoke_rejects_invalid_sequence_or_held_body"),!IsModelSmokeOutcomeCorrect(0,true,false,true,true)
        && !IsModelSmokeOutcomeCorrect(4,true,false,true,true) && !IsModelSmokeOutcomeCorrect(2,false,true,false,false));
    {
        const auto SavedState=State; const bool SavedQA=bModelQA,SavedOutcomes=bModelSmokeOutcomesMatch,SavedThermal=bModelSmokeThermalLimitsMatch;
        const auto SavedRows=ModelSmokeOutcomes,SavedThermalRows=ModelSmokeThermalChecks; const int32 SavedCompleted=CompletedBodies;
        bModelQA=true; bModelSmokeOutcomesMatch=true; bModelSmokeThermalLimitsMatch=true; ModelSmokeOutcomes.Reset(); ModelSmokeThermalChecks.Reset();
        for (int32 I=1;I<=3;++I) {
            State.BodySequence=I; State.BodyId=FString::Printf(TEXT("policy-body-%d"),I); State.Phase=Phase::Departing;
            State.bBranchKnown=true; State.bPreassembled=I==2; State.bAssemblyPerformed=I!=2; State.bWeldAuthorized=I!=2;
            State.Gates[1]=State.Gates[3]=Gate::Passed; State.Gates[2]=I==2 ? Gate::Skipped:Gate::Passed; RecordModelSmokeOutcome();
        }
        State.PassedBodies=3; CompletedBodies=3;
        Check(TEXT("smoke_requires_six_thermal_audits"),!HasPassedModelSmoke());
        for (int32 I=0;I<6;++I) ModelSmokeThermalChecks.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
        Check(TEXT("smoke_accepts_correct_three_branch_outcomes"),HasPassedModelSmoke());
        bModelSmokeThermalLimitsMatch=false; Check(TEXT("smoke_rejects_any_failed_acceptance_audit"),!HasPassedModelSmoke());
        State=SavedState; bModelQA=SavedQA; bModelSmokeOutcomesMatch=SavedOutcomes; bModelSmokeThermalLimitsMatch=SavedThermal;
        ModelSmokeOutcomes=SavedRows; ModelSmokeThermalChecks=SavedThermalRows; CompletedBodies=SavedCompleted;
    }

    ResetPresence();
    Check(TEXT("arrival_stops_at_panel_presence"),State.Phase==Phase::PanelPresence && State.ActiveGate==0 && AtFixture() && !State.bBranchKnown);
    Check(TEXT("missing_panel_does_not_choose_branch_without_model"),!ProcessScene->BuildMetadata()->GetBoolField(TEXT("preassembled")) && Hold(Phase::PanelPresence,300) && !State.bBranchKnown);
    Decision(TEXT("G")); Check(TEXT("one_presence_green_does_not_choose_branch"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==1 && State.ConsensusCode==TEXT("G"));
    Decision(TEXT("A")); Decision(TEXT("G"));
    Check(TEXT("green_amber_green_resets_presence_streak"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==1);
    Decision(TEXT("G"),false); Decision(TEXT("G"));
    Check(TEXT("green_error_green_resets_presence_streak"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==1);
    Decision(TEXT("R")); Check(TEXT("changing_presence_code_starts_new_consensus"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==1 && State.ConsensusCode==TEXT("R"));
    Decision(TEXT("R"));
    Check(TEXT("two_missing_replies_choose_expected_assembly_branch"),State.Phase==Phase::AdhesiveApplication && State.bBranchKnown && !State.bPreassembled && State.Gates[0]==Gate::BranchChosen);
    Check(TEXT("assembly_starts_before_panel_placement_without_welding"),!State.bAssemblyPerformed && !State.bWeldAuthorized && State.Gates[1]==Gate::Pending && !WeldingCell->IsWelding());
    const double ApplyStart=State.ClockSeconds; CompleteMotion(Phase::AdhesiveApplication);
    Check(TEXT("adhesive_application_obeys_authored_duration"),State.Phase==Phase::PanelPlacement && State.ClockSeconds-ApplyStart>=ADarkFactoryProcessScene::GetPhaseDuration(Phase::AdhesiveApplication));
    Check(TEXT("adhesive_application_cannot_skip_placement"),State.ActiveGate==-1 && State.Steps[2]==Step::Complete && !State.bAssemblyPerformed);
    CompleteMotion(Phase::PanelPlacement); Tick(0.f);
    Check(TEXT("placement_finishes_before_adhesive_gate"),State.Phase==Phase::Adhesive && State.ActiveGate==1 && State.bAssemblyPerformed && !State.bWeldAuthorized);
    Check(TEXT("held_panel_is_physical_before_thermal_gate"),ProcessScene->BuildMetadata()->GetBoolField(TEXT("panel_picked")) && AtFixture());
    const float InitialAdhesive=ProcessScene->GetMaximumAdhesiveC(); const double HoldClock=State.ClockSeconds;
    Decision(TEXT("R")); Check(TEXT("thermal_red_is_wait_not_exception"),State.Phase==Phase::Adhesive && State.Gates[1]==Gate::Waiting && State.Steps[4]==Step::Hold);
    Check(TEXT("adhesive_hold_outlasts_legacy_cycle"),Hold(Phase::Adhesive,300));
    Check(TEXT("adhesive_clock_cools_without_auto_acceptance"),State.ClockSeconds>HoldClock+29.9 && ProcessScene->GetMaximumAdhesiveC()<InitialAdhesive && !State.bWeldAuthorized && State.Gates[1]!=Gate::Passed);
    Decision(TEXT("G")); Decision(TEXT("A")); Decision(TEXT("G"));
    Check(TEXT("adhesive_amber_resets_green_consensus"),State.Phase==Phase::Adhesive && State.ConsecutivePasses==1);
    Decision(TEXT("G")); Check(TEXT("two_adhesive_greens_authorize_only_welding_step"),State.Phase==Phase::Welding && State.bWeldAuthorized && State.Gates[1]==Gate::Passed && State.Gates[2]==Gate::Pending && State.Gates[3]==Gate::Pending);
    bool WeldSeen=false,OnlyArmC=true,CarFixed=true; float PeakWeld=0;
    const double WeldStart=State.ClockSeconds;
    for (int32 I=0;I<FMath::CeilToInt(ADarkFactoryProcessScene::GetPhaseDuration(Phase::Welding)/.1)+3 && State.Phase==Phase::Welding;++I) {
        Tick(.1f); CarFixed &= AtFixture(); PeakWeld=FMath::Max(PeakWeld,ProcessScene->GetMaximumWeldC());
        for (const auto& E:WeldingCell->GetWeldEmitters()) { WeldSeen=true; OnlyArmC &= E.EmitterIndex==ADarkFactoryWeldingCell::PanelWelderRobot; }
    }
    DelayCapture();
    Check(TEXT("only_arm_c_emits_welding_after_thermal_authorization"),WeldSeen && OnlyArmC);
    Check(TEXT("welding_body_remains_fixed"),CarFixed);
    Check(TEXT("welding_retains_localized_hot_core"),PeakWeld>1500.f && PeakWeld<=1600.001f,FString::Printf(TEXT("peak=%.3f"),PeakWeld));
    Check(TEXT("weld_motion_finishes_before_nir_seating_gate"),State.Phase==Phase::PanelSeated && State.ActiveGate==2 && State.ClockSeconds-WeldStart>=ADarkFactoryProcessScene::GetPhaseDuration(Phase::Welding));
    Decision(TEXT("R"));
    Check(TEXT("seating_red_holds_as_exception"),State.Phase==Phase::PanelSeated && State.Gates[2]==Gate::Rejected && State.Steps[6]==Step::Exception);
    Check(TEXT("seating_exception_never_releases_or_departs"),Hold(Phase::PanelSeated,300) && State.Steps[8]==Step::Pending && State.Gates[3]==Gate::Pending);
    Decision(TEXT("G")); Decision(TEXT("R")); Decision(TEXT("G"));
    Check(TEXT("seating_red_resets_green_consensus"),State.Phase==Phase::PanelSeated && State.ConsecutivePasses==1);
    Decision(TEXT("G")); Check(TEXT("two_seating_greens_start_weld_cooling_gate"),State.Phase==Phase::WeldCooling && State.ActiveGate==3 && State.Gates[2]==Gate::Passed);
    Decision(TEXT("R")); Check(TEXT("hot_weld_red_holds"),State.Phase==Phase::WeldCooling && State.Gates[3]==Gate::Waiting);
    Check(TEXT("weld_cooling_never_auto_releases"),Hold(Phase::WeldCooling,300) && State.Steps[8]==Step::Pending);
    Decision(TEXT("G")); Check(TEXT("one_weld_green_keeps_holder_engaged"),State.Phase==Phase::WeldCooling && State.ConsecutivePasses==1);
    Decision(TEXT("G")); Check(TEXT("two_weld_greens_authorize_holder_retract_first"),State.Phase==Phase::HolderRetract && State.Gates[3]==Gate::Passed && State.Steps[9]==Step::Pending);
    const double RetractStart=State.ClockSeconds; CompleteMotion(Phase::HolderRetract);
    Check(TEXT("holder_clearance_precedes_departure"),State.Phase==Phase::Departing && State.ClockSeconds-RetractStart>=ADarkFactoryProcessScene::GetPhaseDuration(Phase::HolderRetract));
    const FString DepartedBody=State.BodyId; const int32 BeforeExit=State.PassedBodies;
    Tick(.1f); Check(TEXT("accepted_assembled_body_moves_forward"),WeldingCell->GetCarriageRoot()->GetRelativeLocation().Y>0);
    CompleteMotion(Phase::Departing);
    Check(TEXT("departure_counts_exit_and_changes_body_identity"),State.PassedBodies==BeforeExit+1 && State.BodyId!=DepartedBody && State.Phase==Phase::Arriving);
    Check(TEXT("new_body_resets_all_gate_consensus_and_branch_flags"),State.ConsecutivePasses==0 && State.ConsensusCode.IsEmpty() && !State.bBranchKnown && !State.bAssemblyPerformed && !State.bWeldAuthorized);

    ResetPresence(1);
    Check(TEXT("preassembled_body_still_waits_for_presence_model"),ProcessScene->BuildMetadata()->GetBoolField(TEXT("preassembled")) && Hold(Phase::PanelPresence,40) && !State.bBranchKnown);
    Decision(TEXT("R")); Decision(TEXT("A")); Decision(TEXT("R"));
    Check(TEXT("missing_branch_also_requires_uninterrupted_consensus"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==1);
    Decision(TEXT("G")); Decision(TEXT("G"));
    Check(TEXT("present_consensus_selects_preassembled_thermal_path"),State.Phase==Phase::Adhesive && State.bPreassembled && State.bBranchKnown && State.Gates[0]==Gate::Passed);
    bool SkipStates=true; for (int32 I:{2,3,5,6,8}) SkipStates &= State.Steps[I]==Step::Skipped;
    Check(TEXT("preassembled_branch_explicitly_marks_five_skipped_actions"),SkipStates && State.Gates[2]==Gate::Skipped);
    Check(TEXT("preassembled_adhesive_hold_does_not_assemble_or_weld"),Hold(Phase::Adhesive,300) && !State.bAssemblyPerformed && !State.bWeldAuthorized);
    Decision(TEXT("G")); Decision(TEXT("G"));
    Check(TEXT("preassembled_adhesive_pass_skips_welding_and_seating"),State.Phase==Phase::WeldCooling && State.ActiveGate==3 && State.Gates[2]==Gate::Skipped && !State.bWeldAuthorized);
    Check(TEXT("preassembled_weld_cooling_remains_model_gated"),Hold(Phase::WeldCooling,100));
    Decision(TEXT("G")); Decision(TEXT("G"));
    Check(TEXT("preassembled_cool_pass_skips_holder_and_departs"),State.Phase==Phase::Departing && State.Steps[8]==Step::Skipped && !State.bAssemblyPerformed && !State.bWeldAuthorized);

    // Pause/cancel checks use an unsubmitted HTTP fixture and a late encoder.
    ResetPresence(); Decision(TEXT("R")); StartCapture();
    Check(TEXT("nir_capture_uses_one_bounded_three_view_batch"),bCaptureActive && State.bRequestPending && CaptureSlot==0 && ExpectedImages==3);
    FinishSyntheticView(11);
    Check(TEXT("encoder_completion_consumed_once"),!EncodeFuture.IsValid() && BatchImages.Num()==1 && CaptureSlot==1 && !Request.IsValid());
    BatchInspectionViews.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>())); PendingInspectionView=MakeShared<FJsonObject>();
    TPromise<TArray<uint8>> Late; EncodeFuture=Late.GetFuture();
    Request=FHttpModule::Get().CreateRequest(); ActiveRequestId=TEXT("policy-unsubmitted-cancel"); bool CallbackRan=false;
    Request->OnProcessRequestComplete().BindLambda([&CallbackRan](FHttpRequestPtr,FHttpResponsePtr,bool) { CallbackRan=true; });
    const auto PendingRequest=Request; const double PausedClock=State.ClockSeconds;
    TogglePaused();
    Check(TEXT("pause_cancels_capture_http_identity_and_partial_audits"),State.bPaused && !bCaptureActive && !State.bRequestPending && !Request.IsValid()
        && ActiveRequestId.IsEmpty() && BatchImages.IsEmpty() && BatchInspectionViews.IsEmpty() && !PendingInspectionView.IsValid() && CaptureSlot==-1);
    Check(TEXT("pause_unbinds_http_callback"),!PendingRequest->OnProcessRequestComplete().IsBound() && !CallbackRan);
    Check(TEXT("pause_erases_presence_branch_consensus"),State.ConsecutivePasses==0 && State.ConsensusCode.IsEmpty());
    Late.SetValue(TArray<uint8>{4,5,6}); CaptureTick();
    Check(TEXT("late_encoder_is_drained_without_republishing"),!EncodeFuture.IsValid() && BatchImages.IsEmpty() && !State.bRequestPending && CaptureSlot==-1);
    Decision(TEXT("R")); for (int32 I=0;I<10;++I) Tick(.1f);
    Check(TEXT("paused_input_and_clock_cannot_choose_branch"),State.Phase==Phase::PanelPresence && State.ClockSeconds==PausedClock && State.ConsecutivePasses==0 && !WeldingCell->IsAnimating());
    TogglePaused(); DelayCapture(); Decision(TEXT("R"));
    Check(TEXT("resume_needs_two_new_matching_presence_decisions"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==1);
    CancelInspection();
    bModelRequestsEnabled=false; NextInspection=0; const uint64 DisabledSerial=CaptureSerial;
    for (int32 I=0;I<12;++I) { Tick(.1f); CaptureTick(); }
    Check(TEXT("no_inference_disables_gpu_capture_and_http"),CaptureInactive() && CaptureSerial==DisabledSerial && State.Phase==Phase::PanelPresence);
    bModelRequestsEnabled=true; DelayCapture();

    // Complete a third synthetic encoder after the batch deadline. The entry
    // guard must discard it before telemetry publication or HTTP submission.
    ResetPresence(); Decision(TEXT("G")); StartCapture();
    const uint64 RequestsBeforeStale=ModelRequestsSubmitted;
    const int32 AuditsBeforeStale=InspectionCaptures.Num();
    CaptureSlot=2; BatchImages={TArray<uint8>{1,2,3},TArray<uint8>{4,5,6}};
    BatchInspectionViews={MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()),MakeShared<FJsonValueObject>(MakeShared<FJsonObject>())};
    PendingInspectionView=MakeShared<FJsonObject>();
    BatchStarted=FPlatformTime::Seconds()-MaxCaptureAgeSeconds-1.;
    TPromise<TArray<uint8>> StaleEncoder; EncodeFuture=StaleEncoder.GetFuture(); StaleEncoder.SetValue(TArray<uint8>{7,8,9});
    CaptureTick();
    Check(TEXT("expired_final_encoder_never_submits_http_or_publishes_batch"),ModelRequestsSubmitted==RequestsBeforeStale
        && InspectionCaptures.Num()==AuditsBeforeStale && CaptureInactive() && BatchImages.IsEmpty()
        && BatchInspectionViews.IsEmpty() && !PendingInspectionView.IsValid());
    Check(TEXT("expired_encoder_invalidates_consensus_and_holds"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==0
        && State.ConsensusCode.IsEmpty() && State.Gates[0]==Gate::Error && !EncodeFuture.IsValid());
    // The final submission boundary independently rejects expiry during frame
    // processing, before any complete batch is added to the public audit list.
    StartCapture(); BatchStarted=FPlatformTime::Seconds()-MaxCaptureAgeSeconds-1.;
    SubmitBatch();
    Check(TEXT("publication_boundary_rechecks_batch_age"),CaptureInactive() && ModelRequestsSubmitted==RequestsBeforeStale
        && InspectionCaptures.Num()==AuditsBeforeStale && State.Phase==Phase::PanelPresence);

    // Both modalities must follow the same physical lens and ordered waypoints.
    // Only two synthetic mailboxes are completed, so no batch can reach HTTP.
    const FTransform FixedMount=WeldingCell->GetInspectionCameraMountTransform();
    const FTransform OpticalRelative=FTransform(FQuat::Identity,FVector(21,0,0))*FixedMount;
    for (const Phase GatePhase:{Phase::PanelPresence,Phase::Adhesive,Phase::PanelSeated,Phase::WeldCooling}) {
        ResetPresence(); EnterPhase(GatePhase); DelayCapture();
        const FString Stage=StageName(); const bool NIR=GatePhase==Phase::PanelPresence || GatePhase==Phase::PanelSeated;
        const int32 CameraIndex=NIR ? 0:1; const uint64 BeforeSerial=CaptureSerial;
        StartCapture(); bool Ordered=true,Mounted=true,Framed=true,MovingHeld=true; TArray<FVector> LensLocations;
        for (int32 View=0;View<3;++View) {
            if (View>0) FinishSyntheticView(20+View);
            Ordered &= CaptureSlot==View && ExpectedImages==3 && BatchImages.Num()==View;
            Tick(.1f); CaptureTick();
            if (!ProcessScene->IsInspectionViewSettled(View)) MovingHeld &= !bCapturePoseReady && SettleFrames==3 && CaptureSerial==BeforeSerial;
            for (int32 I=0;I<20 && !ProcessScene->IsInspectionViewSettled(View);++I) Tick(.1f);
            CaptureTick(); // At most two settle frames so far; never enqueue readback.
            FVector Location; FRotator Rotation; float FOV=0;
            ProcessScene->GetInspectionCameraPose(Stage,View,Location,Rotation,FOV); LensLocations.Add(Location);
            auto* Tool=WeldingCell->GetInspectionJoint();
            Mounted &= Tool && FTransform(Rotation,Location).GetRelativeTransform(Tool->GetComponentTransform()).Equals(OpticalRelative,.001)
                && WeldingCell->GetInspectionCameraMountTransform().Equals(FixedMount,.00001)
                && Cameras[CameraIndex]->GetComponentLocation().Equals(Location,.001)
                && Cameras[CameraIndex]->GetComponentRotation().Equals(Rotation,.001);
            Ordered &= ProcessScene->IsInspectionViewSettled(View) && bCapturePoseReady && Cameras[CameraIndex]->TextureTarget==Targets[View+(NIR ? 0:3)]
                && SensorWarmupCaptures>0 && SensorWarmupCaptures+SettleFrames==3;
            TArray<FVector> Corners; ProcessScene->GetInspectionFramingCorners(Stage,View,Corners);
            const double HalfTan=FMath::Tan(FMath::DegreesToRadians(double(FOV)*.5));
            bool ViewFramed=Corners.Num()==16 && HalfTan>0.;
            for (const auto& Corner:Corners) {
                const FVector V=Rotation.UnrotateVector(Corner-Location);
                ViewFramed &= !V.ContainsNaN() && V.X>1 && FMath::Abs(V.Y)<=V.X*HalfTan && FMath::Abs(V.Z)*448./256.<=V.X*HalfTan;
            }
            Framed &= ViewFramed;
            auto Pose=MakeShared<FJsonObject>(); Pose->SetStringField(TEXT("stage"),Stage); Pose->SetNumberField(TEXT("spot_index"),View);
            Pose->SetBoolField(TEXT("all_target_corners_in_frustum"),ViewFramed); Pose->SetObjectField(TEXT("scan"),WeldingCell->GetInspectionScanMetadata());
            InspectionPolicyPoses.Add(MakeShared<FJsonValueObject>(Pose));
        }
        Check(Stage+TEXT("_ordered_physical_three_view_capture"),Ordered && BatchImages.Num()==2 && !Request.IsValid());
        Check(Stage+TEXT("_readback_waits_for_arm_travel"),MovingHeld && CaptureSerial==BeforeSerial && !Readback.IsBusy());
        Check(Stage+TEXT("_lens_is_rigidly_fixed_to_tool"),Mounted);
        Check(Stage+TEXT("_all_authored_target_corners_are_in_frustum"),Framed);
        Check(Stage+TEXT("_scan_positions_translate_distinctly"),LensLocations.Num()==3
            && FVector::Distance(LensLocations[0],LensLocations[1])>30 && FVector::Distance(LensLocations[1],LensLocations[2])>30 && FVector::Distance(LensLocations[0],LensLocations[2])>30);
        CancelInspection();
    }

    // The acceptance audit observes physical temperature without changing policy.
    ResetPresence(); EnterPhase(Phase::Adhesive); DelayCapture();
    ModelSmokeThermalChecks.Reset(); bModelSmokeThermalLimitsMatch=true;
    double AuditClock=0; const bool HotPrepared=ProcessScene->PrepareCapture(TEXT("adhesive"),0,1234,AuditClock); State.ClockSeconds=AuditClock;
    RecordModelSmokeThermalGate();
    Check(TEXT("acceptance_audit_rejects_hot_adhesive"),HotPrepared && !ModelSmokeThermalChecks.Last()->AsObject()->GetBoolField(TEXT("ready_at_acceptance")) && !bModelSmokeThermalLimitsMatch && State.Phase==Phase::Adhesive);
    const bool CoolPrepared=ProcessScene->PrepareCapture(TEXT("adhesive"),2,1234,AuditClock); State.ClockSeconds=AuditClock;
    RecordModelSmokeThermalGate();
    Check(TEXT("cool_audit_does_not_erase_prior_failure"),CoolPrepared && ModelSmokeThermalChecks.Last()->AsObject()->GetBoolField(TEXT("ready_at_acceptance")) && !bModelSmokeThermalLimitsMatch && State.Phase==Phase::Adhesive);
    CancelInspection(); bReady=false; bModelRequestsEnabled=OriginalRequests; WeldingCell->SetAnimationEnabled(false);
    auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("schema"),TEXT("darkfactory-process-policy-smoke-v1"));
    Report->SetStringField(TEXT("contract_version"),ContractVersion); Report->SetStringField(TEXT("source"),TEXT("policy_test")); Report->SetStringField(TEXT("session_id"),SessionId);
    Report->SetStringField(TEXT("scope"),TEXT("Native panel-assembly policy, physical holds and scan poses with scripted decisions/synthetic encoder mailboxes; no model accuracy or rendered-image validation"));
    Report->SetBoolField(TEXT("model_accuracy_evidence"),false); Report->SetNumberField(TEXT("http_requests_submitted"),static_cast<double>(ModelRequestsSubmitted));
    Report->SetNumberField(TEXT("checks_count"),Checks.Num()); Report->SetArrayField(TEXT("checks"),Checks); Report->SetArrayField(TEXT("inspection_scan_poses"),InspectionPolicyPoses); Report->SetBoolField(TEXT("passed"),Passed);
    FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<TCHAR,TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
    const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(OutputRoot,TEXT("process_policy.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_POLICY_SMOKE checks=%d passed=%d saved=%d model_requests=0"),Checks.Num(),Passed,Saved);
    return Passed && Saved;
}
