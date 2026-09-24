#include "DarkFactoryProcessController.h"
#include "DarkFactoryAgentProtocol.h"
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
        FDarkFactoryInspectionReceipt Receipt; Receipt.Id=TEXT("receipt-policy"); Receipt.Body=TEXT("original-body");
        Receipt.Stage=TEXT("adhesive"); InspectionReceipts.Add(Receipt);
        State.Message=TEXT("Fresh evidence required");
        CompleteInspectionReceipt(Receipt.Id,true,TEXT("G"),80,.99,.95,false);
        Check(TEXT("replay_retains_actual_action_and_score"),InspectionReceipts[0].Action==State.Message && InspectionReceipts[0].Confidence==.99 && InspectionReceipts[0].Body==TEXT("original-body"));
        State.Message=TEXT("Next unrelated action");
        Check(TEXT("replay_is_not_live_state"),InspectionReceipts[0].Action!=State.Message);
        FDarkFactoryInspectionReceipt Pending; Pending.Id=TEXT("cancelled-policy"); InspectionReceipts.Add(Pending);
        ActiveRequestId=Pending.Id; CancelInspection();
        Check(TEXT("replay_cancel_cannot_claim_acceptance"),InspectionReceipts.Last().bComplete && !InspectionReceipts.Last().bValid && InspectionReceipts.Last().Decision==TEXT("A"));
        StepInspectionReplay(-1);
        Check(TEXT("replay_navigation_does_not_change_control"),GetInspectionReceipt()->Id==Receipt.Id && State.Message==TEXT("Next unrelated action"));
        InspectionReceipts.Reset(); ReplaySelection.Empty(); bInspectionReplay=false;
    }

    {
        DarkFactoryEnhancements::FCoolingRecovery Recovery;
        Check(TEXT("recovery_requires_two_rejects"),!Recovery.Observe(true,TEXT("R"),true) && Recovery.Observe(true,TEXT("R"),true));
        Check(TEXT("recovery_does_not_retrigger"),!Recovery.Observe(true,TEXT("R"),true));
        Recovery.Reset();
        Check(TEXT("recovery_error_breaks_consensus"),!Recovery.Observe(true,TEXT("R"),true) && !Recovery.Observe(false,TEXT("R"),true) && !Recovery.Observe(true,TEXT("R"),true));
        Check(TEXT("recovery_ambiguity_breaks_consensus"),!Recovery.Observe(true,TEXT("A"),true) && Recovery.ConsecutiveRejects==0);
        Check(TEXT("recovery_never_uses_other_stages"),!Recovery.Observe(true,TEXT("R"),false) && !Recovery.Observe(true,TEXT("R"),false));
        Check(TEXT("recovery_g_never_activates"),!Recovery.Observe(true,TEXT("G"),true) && Recovery.ConsecutiveRejects==0);
        Check(TEXT("motion_ease_retains_endpoints"),DarkFactoryEnhancements::MotionEase(0)==0. && DarkFactoryEnhancements::MotionEase(1)==1.);
        Check(TEXT("motion_ease_symmetric_midpoint"),FMath::IsNearlyEqual(DarkFactoryEnhancements::MotionEase(.5),.5));
        bool Monotonic=true; for (int32 I=0;I<100;++I) Monotonic &= DarkFactoryEnhancements::MotionEase(I/100.)<=DarkFactoryEnhancements::MotionEase((I+1)/100.);
        Check(TEXT("motion_ease_monotonic_no_overshoot"),Monotonic);
    }
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
    Decision(TEXT("A"));
    Check(TEXT("presence_ambiguity_holds_branch"),State.Phase==Phase::PanelPresence && !State.bBranchKnown);
    Decision(TEXT("G"),false);
    Check(TEXT("presence_transport_error_holds_branch"),State.Phase==Phase::PanelPresence && !State.bBranchKnown);
    Decision(TEXT("R"));
    Check(TEXT("one_missing_batch_selects_assembly"),State.Phase==Phase::AdhesiveApplication && State.bBranchKnown && !State.bPreassembled && State.Gates[0]==Gate::BranchChosen);
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
    Decision(TEXT("A"));
    Check(TEXT("adhesive_ambiguity_keeps_welding_held"),State.Phase==Phase::Adhesive && !State.bWeldAuthorized);
    Decision(TEXT("G")); Check(TEXT("one_adhesive_green_authorizes_only_welding"),State.Phase==Phase::Welding && State.bWeldAuthorized && State.Gates[1]==Gate::Passed && State.Gates[2]==Gate::Pending && State.Gates[3]==Gate::Pending);
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
    Decision(TEXT("A"));
    Check(TEXT("seating_ambiguity_does_not_release_holder"),State.Phase==Phase::PanelSeated && State.Gates[3]==Gate::Pending);
    Decision(TEXT("G")); Check(TEXT("one_seating_green_starts_weld_cooling"),State.Phase==Phase::WeldCooling && State.ActiveGate==3 && State.Gates[2]==Gate::Passed);
    Decision(TEXT("R")); Check(TEXT("hot_weld_red_holds"),State.Phase==Phase::WeldCooling && State.Gates[3]==Gate::Waiting);
    Check(TEXT("weld_cooling_never_auto_releases"),Hold(Phase::WeldCooling,300) && State.Steps[8]==Step::Pending);
    Decision(TEXT("G")); Check(TEXT("one_weld_green_authorizes_holder_retract_first"),State.Phase==Phase::HolderRetract && State.Gates[3]==Gate::Passed && State.Steps[9]==Step::Pending);
    const double RetractStart=State.ClockSeconds; CompleteMotion(Phase::HolderRetract);
    Check(TEXT("holder_clearance_precedes_departure"),State.Phase==Phase::Departing && State.ClockSeconds-RetractStart>=ADarkFactoryProcessScene::GetPhaseDuration(Phase::HolderRetract));
    const FString DepartedBody=State.BodyId; const int32 BeforeExit=State.PassedBodies;
    Tick(.1f); Check(TEXT("accepted_assembled_body_moves_forward"),WeldingCell->GetCarriageRoot()->GetRelativeLocation().Y>0);
    CompleteMotion(Phase::Departing);
    Check(TEXT("departure_counts_exit_and_changes_body_identity"),State.PassedBodies==BeforeExit+1 && State.BodyId!=DepartedBody && State.Phase==Phase::Arriving);
    Check(TEXT("new_body_resets_all_gate_consensus_and_branch_flags"),State.ConsecutivePasses==0 && State.ConsensusCode.IsEmpty() && !State.bBranchKnown && !State.bAssemblyPerformed && !State.bWeldAuthorized);

    ResetPresence(1);
    Check(TEXT("preassembled_body_still_waits_for_presence_model"),ProcessScene->BuildMetadata()->GetBoolField(TEXT("preassembled")) && Hold(Phase::PanelPresence,40) && !State.bBranchKnown);
    Decision(TEXT("A"));
    Check(TEXT("preassembled_uncertainty_does_not_skip_presence"),State.Phase==Phase::PanelPresence && !State.bBranchKnown);
    Decision(TEXT("G"));
    Check(TEXT("one_present_batch_selects_preassembled_thermal_path"),State.Phase==Phase::Adhesive && State.bPreassembled && State.bBranchKnown && State.Gates[0]==Gate::Passed);
    bool SkipStates=true; for (int32 I:{2,3,5,6,8}) SkipStates &= State.Steps[I]==Step::Skipped;
    Check(TEXT("preassembled_branch_explicitly_marks_five_skipped_actions"),SkipStates && State.Gates[2]==Gate::Skipped);
    Check(TEXT("preassembled_adhesive_hold_does_not_assemble_or_weld"),Hold(Phase::Adhesive,300) && !State.bAssemblyPerformed && !State.bWeldAuthorized);
    Decision(TEXT("G"));
    Check(TEXT("preassembled_adhesive_pass_skips_welding_and_seating"),State.Phase==Phase::WeldCooling && State.ActiveGate==3 && State.Gates[2]==Gate::Skipped && !State.bWeldAuthorized);
    Check(TEXT("preassembled_weld_cooling_remains_model_gated"),Hold(Phase::WeldCooling,100));
    Decision(TEXT("G"));
    Check(TEXT("preassembled_cool_pass_skips_holder_and_departs"),State.Phase==Phase::Departing && State.Steps[8]==Step::Skipped && !State.bAssemblyPerformed && !State.bWeldAuthorized);

    // Pause/cancel checks use an unsubmitted HTTP fixture and a late encoder.
    ResetPresence(); StartCapture();
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
    Check(TEXT("resume_accepts_one_fresh_presence_batch"),State.Phase==Phase::AdhesiveApplication && State.bBranchKnown);
    ResetPresence();
    CancelInspection();
    bModelRequestsEnabled=false; NextInspection=0; const uint64 DisabledSerial=CaptureSerial;
    for (int32 I=0;I<12;++I) { Tick(.1f); CaptureTick(); }
    Check(TEXT("no_inference_disables_gpu_capture_and_http"),CaptureInactive() && CaptureSerial==DisabledSerial && State.Phase==Phase::PanelPresence);
    bModelRequestsEnabled=true; DelayCapture();

    // Complete a third synthetic encoder after the batch deadline. The entry
    // guard must discard it before telemetry publication or HTTP submission.
    ResetPresence(); StartCapture();
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
    // The fixed policy requires a complete fresh batch. Scripted decisions
    // test state transitions only; the separate model smoke uses actual images.
    ResetPresence(); Decision(TEXT("A"));
    Check(TEXT("single_batch_uncertainty_holds_presence"),State.Phase==Phase::PanelPresence && State.ConsecutivePasses==0);
    Decision(TEXT("G"),false);
    Check(TEXT("single_batch_invalid_reply_cannot_choose_branch"),State.Phase==Phase::PanelPresence && State.Gates[0]==Gate::Error);
    StartCapture(); TogglePaused(); Decision(TEXT("G"));
    Check(TEXT("single_batch_pause_discards_partial_batch_and_ignores_reply"),State.bPaused && State.Phase==Phase::PanelPresence && BatchImages.IsEmpty() && ActiveRequestId.IsEmpty());
    TogglePaused(); DelayCapture(); StartCapture(); BatchStarted=FPlatformTime::Seconds()-MaxCaptureAgeSeconds-1.; SubmitBatch();
    Check(TEXT("single_batch_expired_capture_cannot_submit_or_choose_branch"),CaptureInactive() && ModelRequestsSubmitted==0 && State.Phase==Phase::PanelPresence && State.ConsecutivePasses==0);
    Decision(TEXT("R"));
    Check(TEXT("single_batch_missing_selects_assembly"),State.Phase==Phase::AdhesiveApplication && !State.bPreassembled);
    CompleteMotion(Phase::AdhesiveApplication); CompleteMotion(Phase::PanelPlacement);
    Decision(TEXT("R")); Decision(TEXT("A"));
    Check(TEXT("single_batch_thermal_hold_has_no_timer_override"),Hold(Phase::Adhesive,300) && !State.bWeldAuthorized);
    Decision(TEXT("G"));
    Check(TEXT("single_batch_cool_authorizes_welding"),State.Phase==Phase::Welding && State.bWeldAuthorized);
    CompleteMotion(Phase::Welding); Decision(TEXT("R"));
    Check(TEXT("single_batch_seating_rejection_holds"),State.Phase==Phase::PanelSeated && State.Gates[2]==Gate::Rejected && Hold(Phase::PanelSeated,10));
    Decision(TEXT("G")); Decision(TEXT("R"));
    Check(TEXT("single_batch_hot_weld_holds_holder"),State.Phase==Phase::WeldCooling && Hold(Phase::WeldCooling,10));
    Decision(TEXT("G"));
    Check(TEXT("single_batch_cool_weld_releases_holder"),State.Phase==Phase::HolderRetract);
    CompleteMotion(Phase::HolderRetract); CompleteMotion(Phase::Departing);
    Check(TEXT("single_batch_policy_survives_body_turnover"),State.RequiredConsensusBatches==1 && !State.bBranchKnown && State.ConsecutivePasses==0);
    ResetPresence(1); Decision(TEXT("G"));
    Check(TEXT("single_batch_preassembled_skips_assembly"),State.Phase==Phase::Adhesive && State.bPreassembled && State.Gates[2]==Gate::Skipped);
    Decision(TEXT("G"));
    Check(TEXT("single_batch_preassembled_skips_welding"),State.Phase==Phase::WeldCooling && !State.bWeldAuthorized);
    Decision(TEXT("G"));
    Check(TEXT("single_batch_preassembled_departs_after_both_thermal_gates"),State.Phase==Phase::Departing);
    {
        using namespace DarkFactoryAgent;
        auto Invalid=MakeShared<FJsonObject>(); Invalid->SetStringField(TEXT("status"),TEXT("invalid"));
        Invalid->SetStringField(TEXT("decision"),TEXT("A")); Invalid->SetBoolField(TEXT("valid"),false);
        Invalid->SetStringField(TEXT("reason"),TEXT("stale_reply")); Invalid->SetStringField(TEXT("stage"),TEXT("adhesive"));
        Invalid->SetNumberField(TEXT("capture_age_at_reply_seconds"),8.12512345); Invalid->SetNumberField(TEXT("images"),0);
        const auto Compact=CompactToolResult(Invalid);
        FString CompactText; FJsonSerializer::Serialize(Compact,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&CompactText));
        Check(TEXT("agent_compact_transport_preserves_invalid_evidence"),!Compact->GetBoolField(TEXT("valid")) && Compact->GetStringField(TEXT("decision"))==TEXT("A") &&
            Compact->GetStringField(TEXT("reason"))==TEXT("stale_reply") && Compact->GetStringField(TEXT("stage"))==TEXT("adhesive"));
        Check(TEXT("agent_compact_clock_is_numeric_bounded_precision_and_journal_unchanged"),CompactText.Contains(TEXT("\"capture_age_at_reply_seconds\":8.125")) &&
            Invalid->GetNumberField(TEXT("capture_age_at_reply_seconds"))==8.12512345 && Invalid->HasField(TEXT("images")));
        auto Parse=[&](const FString& Tool,const FString& Args,const FString& Finish=TEXT("tool_calls"),int32 Count=1,bool Bundled=false) {
            auto Fn=MakeShared<FJsonObject>(); Fn->SetStringField(TEXT("name"),Tool); Fn->SetStringField(TEXT("arguments"),Args);
            auto Call=MakeShared<FJsonObject>(); Call->SetStringField(TEXT("id"),TEXT("call1")); Call->SetStringField(TEXT("type"),TEXT("function")); Call->SetObjectField(TEXT("function"),Fn);
            TArray<TSharedPtr<FJsonValue>> Calls; for (int32 I=0;I<Count;++I) Calls.Add(MakeShared<FJsonValueObject>(Call));
            auto Message=MakeShared<FJsonObject>(); Message->SetArrayField(TEXT("tool_calls"),Calls);
            auto Choice=MakeShared<FJsonObject>(); Choice->SetObjectField(TEXT("message"),Message); Choice->SetStringField(TEXT("finish_reason"),Finish);
            auto Reply=MakeShared<FJsonObject>(); Reply->SetArrayField(TEXT("choices"),{MakeShared<FJsonValueObject>(Choice)});
            FCommand Command; FString Error; return ParseCommand(Reply,Command,Error,Bundled);
        };
        Check(TEXT("agent_simple_parser_accepts_named_macro"),Parse(TEXT("prepare_panel"),TEXT("{\"reason\":\"Prepare\"}"),TEXT("tool_calls"),1,true));
        Check(TEXT("agent_simple_parser_rejects_legacy_low_level_tool"),!Parse(TEXT("act"),TEXT("{\"action\":\"place_panel\",\"reason\":\"Place\"}"),TEXT("tool_calls"),1,true));
        Check(TEXT("agent_simple_parser_rejects_generated_parameters"),!Parse(TEXT("wait"),TEXT("{\"seconds\":15,\"reason\":\"Wait\"}"),TEXT("tool_calls"),1,true));
        const FString WeldArgs=TEXT("{\"action\":\"weld_panel\",\"reason\":\"Test arbitrary job\"}");
        Check(TEXT("agent_parser_accepts_weld_without_recipe_state"),Parse(TEXT("act"),WeldArgs));
        Check(TEXT("agent_parser_rejects_truncated_output"),!Parse(TEXT("act"),WeldArgs,TEXT("length")));
        Check(TEXT("agent_parser_rejects_parallel_jobs"),!Parse(TEXT("act"),WeldArgs,TEXT("tool_calls"),2));
        Check(TEXT("agent_parser_rejects_unknown_capability"),!Parse(TEXT("exec"),WeldArgs));
        Check(TEXT("agent_parser_rejects_extra_fields"),!Parse(TEXT("hold"),TEXT("{\"reason\":\"hold\",\"override\":true}")));
        Check(TEXT("agent_parser_rejects_unbounded_wait"),!Parse(TEXT("wait"),TEXT("{\"reason\":\"wait\",\"seconds\":1000}")));
        Check(TEXT("agent_reply_scope_matches"),SameScope(1,TEXT("a"),TEXT("r"),1,TEXT("a"),TEXT("r"),false));
        Check(TEXT("agent_reply_rejects_pause_old_body_old_epoch_and_consumed_id"),
            !SameScope(1,TEXT("a"),TEXT("r"),1,TEXT("a"),TEXT("r"),true) &&
            !SameScope(1,TEXT("a"),TEXT("r"),1,TEXT("b"),TEXT("r"),false) &&
            !SameScope(1,TEXT("a"),TEXT("r"),2,TEXT("a"),TEXT("r"),false) &&
            !SameScope(1,TEXT("a"),TEXT("r"),1,TEXT("a"),TEXT(""),false));
        FAudit Audit; Check(TEXT("agent_independent_audit_records_premature_weld"),Audit.ObserveAction(TEXT("weld_panel"),false,80.,20.).Contains(TEXT("welding_while_sealant_hot")));
        Audit.Evidence.Add(TEXT("adhesive"),{TEXT("G"),true,1.}); Audit.Complete(TEXT("apply_sealant"));
        Check(TEXT("agent_audit_invalidates_reheated_evidence"),!Audit.Evidence.Contains(TEXT("adhesive")));
        Audit=FAudit(); Audit.ObserveInspection(TEXT("panel_presence"),{TEXT("R"),true,1.});
        Audit.Complete(TEXT("apply_sealant")); Audit.Complete(TEXT("place_panel"));
        Audit.ObserveInspection(TEXT("panel_presence"),{TEXT("G"),true,20.});
        Check(TEXT("agent_later_presence_observation_is_not_incoming_truth"),!Audit.ObserveAction(TEXT("weld_panel"),false,20.,20.).Contains(TEXT("presence_branch_incorrect")));
        Agent=MakeShared<FRuntime>(); Agent->Started=FPlatformTime::Seconds(); State.bAgentControl=true; ProcessScene->SetAgentControl(true);
        ForcedPreassembled=0; ProcessScene->EndBody(); BeginBody(); CompleteMotion(Phase::Arriving);
        Check(TEXT("agent_arrival_has_no_automatic_inspection"),State.Phase==Phase::AgentIdle && State.ActiveGate==-1 && ModelRequestsSubmitted==0);
        Check(TEXT("agent_arrival_needs_no_artificial_decision_delay"),Agent->NextDecision<=State.ClockSeconds);
        FCommand Inspect; Inspect.Tool=TEXT("inspect"); Inspect.Argument=TEXT("panel_presence"); Inspect.Reason=TEXT("Policy input");
        DispatchAgentCommand(Inspect); DelayCapture();
        for (int32 I=0;I<12;++I) Tick(.1f);
        Decision(TEXT("R"));
        Check(TEXT("agent_presence_result_does_not_select_branch_or_start_job"),State.Phase==Phase::AgentIdle && !State.bBranchKnown && !Agent->Busy && Agent->Journal.Last()->AsObject()->GetStringField(TEXT("decision"))==TEXT("R"));
        FCommand Weld; Weld.Tool=TEXT("act"); Weld.Argument=TEXT("weld_panel"); Weld.Reason=TEXT("Deliberately wrong process choice");
        Agent->PendingCommand=Weld; AgentTick();
        Check(TEXT("agent_fast_reply_waits_for_actual_arm_withdrawal"),!ProcessScene->IsInspectionArmParked() && Agent->PendingCommand.IsSet() && State.Phase==Phase::AgentIdle && Agent->Failures==0);
        TogglePaused(); AgentTick();
        Check(TEXT("agent_pause_discards_queued_command"),!Agent->PendingCommand.IsSet() && State.Phase==Phase::AgentIdle && !Agent->Busy);
        TogglePaused(); Agent->PendingCommand=Weld;
        for (int32 I=0;I<4;++I) Tick(.1f);
        Check(TEXT("agent_queued_command_cannot_start_during_partial_withdrawal"),Agent->PendingCommand.IsSet() && State.Phase==Phase::AgentIdle);
        for (int32 I=0;I<6;++I) Tick(.1f);
        Check(TEXT("agent_dispatches_once_after_actual_withdrawal"),ProcessScene->IsInspectionArmParked() && !Agent->PendingCommand.IsSet() && Agent->Audits.Num()==1);
        Check(TEXT("agent_executes_recipe_violation_and_audits_without_veto"),State.Phase==Phase::Welding && Agent->Failures>0 && Agent->Rejected==0);
        const FString OriginalJob=Agent->Job; DispatchAgentCommand(Inspect);
        Check(TEXT("agent_executor_prevents_overlapping_jobs"),Agent->Job==OriginalJob && State.Phase==Phase::Welding);
        CompleteMotion(Phase::Welding);
        Check(TEXT("agent_completed_weld_does_not_start_seating_inspection"),State.Phase==Phase::AgentIdle && State.ActiveGate==-1 && Agent->Audit.Completed.FindRef(TEXT("weld_panel"))==1);
        FCommand Wait; Wait.Tool=TEXT("wait"); Wait.Seconds=2.; Wait.Reason=TEXT("Wait without inspection");
        DispatchAgentCommand(Wait); const double Before=State.ClockSeconds;
        for (int32 I=0;I<25;++I) Tick(.1);
        Check(TEXT("agent_wait_advances_physics_without_hidden_inference"),State.ClockSeconds>Before+2 && !Agent->Busy && State.Phase==Phase::AgentIdle && ModelRequestsSubmitted==0);
        DispatchAgentCommand(Inspect); StartCapture(); const uint64 BeforeEpoch=Agent->Epoch;
        TogglePaused(); Check(TEXT("agent_pause_cancels_inspection_and_epoch"),Agent->Epoch>BeforeEpoch && !Agent->Busy && CaptureInactive() && State.bPaused);
        TogglePaused();
        FCommand Depart; Depart.Tool=TEXT("act"); Depart.Argument=TEXT("send_forward"); Depart.Reason=TEXT("Deliberately missing all acceptance inspections");
        DispatchAgentCommand(Depart);
        Check(TEXT("agent_can_depart_with_bad_recipe_audit_only"),State.Phase==Phase::Departing && Agent->Failures>0);
        Agent->PendingCommand=Weld; // Synthetic old-body response awaiting dispatch.
        CompleteMotion(Phase::Departing);
        Check(TEXT("agent_body_turnover_clears_observations_and_jobs"),State.Phase==Phase::Arriving && Agent->Journal.IsEmpty() && Agent->Audit.Completed.IsEmpty() && !Agent->HolderEngaged && !Agent->PendingCommand.IsSet());
        {
            const FString SavedEndpoint=Endpoint,SavedSource=ExpectedModelSource;
            ModelBackends={
                {TEXT("evk_hexagon"),TEXT("http://127.0.0.1:18198/v1/inspect"),TEXT("http://127.0.0.1:18196/v1/chat/completions")},
                {TEXT("host_gpu"),TEXT("http://127.0.0.1:18191/v1/inspect"),TEXT("http://127.0.0.1:18194/v1/chat/completions")}
            };
            bBackendSwitchEnabled=true;
            SelectModelBackend(TEXT("evk_hexagon"));
            CompleteMotion(Phase::Arriving); DispatchAgentCommand(Inspect); StartCapture();
            const FString SwitchBody=State.BodyId; const uint64 SwitchEpoch=Agent->Epoch;
            const int32 SwitchTurns=Agent->Turns,SwitchAudit=Agent->Failures;
            Agent->PendingCommand=Weld;
            ActiveRequestId=TEXT("old-backend-request"); Request=FHttpModule::Get().CreateRequest();
            Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr,FHttpResponsePtr,bool) {});
            const auto OldRequest=Request;
            Agent->Request=FHttpModule::Get().CreateRequest();
            Agent->Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr,FHttpResponsePtr,bool) {});
            const auto OldAgentRequest=Agent->Request;
            Agent->RequestId=TEXT("old-agent-request");
            TPromise<TArray<uint8>> LateEncoder; EncodeFuture=LateEncoder.GetFuture();
            Check(TEXT("backend_switch_selects_both_endpoints"),SelectModelBackend(TEXT("host_gpu"))
                && Endpoint==ModelBackends[1].VisionEndpoint && Agent->Endpoint==ModelBackends[1].AgentEndpoint);
            Check(TEXT("backend_switch_cancels_old_requests_and_queued_command"),!Request && !Agent->Request
                && !OldRequest->OnProcessRequestComplete().IsBound() && !OldAgentRequest->OnProcessRequestComplete().IsBound()
                && ActiveRequestId.IsEmpty() && Agent->RequestId.IsEmpty() && Agent->Epoch>SwitchEpoch && !Agent->PendingCommand.IsSet());
            Check(TEXT("backend_switch_closes_interrupted_inspection_without_acceptance"),!Agent->Busy && State.Phase==Phase::AgentIdle
                && Agent->Journal.Last()->AsObject()->GetStringField(TEXT("status"))==TEXT("cancelled_by_backend_switch")
                && !Agent->Journal.Last()->AsObject()->HasField(TEXT("decision")));
            LateEncoder.SetValue(TArray<uint8>{1,2,3}); CaptureTick();
            Check(TEXT("backend_switch_discards_late_encoder_and_keeps_actual_history"),CaptureInactive() && BatchImages.IsEmpty()
                && State.BodyId==SwitchBody && Agent->Turns==SwitchTurns && Agent->Failures==SwitchAudit && !Agent->Messages.IsEmpty());
            DispatchAgentCommand(Wait); State.bPaused=true;
            const int32 MessagesBefore=Agent->Messages.Num(); const double WaitBefore=Agent->WaitUntil;
            SelectModelBackend(TEXT("evk_hexagon"));
            Check(TEXT("backend_switch_preserves_paused_physical_job_and_conversation"),State.bPaused && Agent->Busy && Agent->Job==TEXT("wait")
                && Agent->WaitUntil==WaitBefore && Agent->Messages.Num()==MessagesBefore && State.BodyId==SwitchBody);
            Agent->Held=true; State.bAgentHeld=true; Agent->Error.Empty();
            SelectModelBackend(TEXT("host_gpu"));
            Check(TEXT("backend_switch_preserves_explicit_model_hold"),Agent->Held && State.bAgentHeld);
            Agent->Error=TEXT("Agent endpoint unavailable"); Agent->Turns=41;
            SelectModelBackend(TEXT("evk_hexagon"));
            Check(TEXT("backend_switch_cannot_reset_exhausted_decision_budget"),Agent->Held && Agent->Turns==41);
            const uint64 UnchangedEpoch=Agent->Epoch;
            Check(TEXT("backend_switch_rejects_unknown_profile_without_mutation"),!SelectModelBackend(TEXT("unknown")) && Agent->Epoch==UnchangedEpoch);
            bBackendSwitchEnabled=false;
            Check(TEXT("backend_switch_disabled_preserves_selected_endpoints"),!SelectModelBackend(TEXT("host_gpu")) && Agent->Epoch==UnchangedEpoch);
            Endpoint=SavedEndpoint; ExpectedModelSource=SavedSource; ModelBackends.Reset(); BackendSwitches=0; State.bPaused=false;
        }
        // V2 macros contain authored motion/measurement, never acceptance or
        // branch repair. These synthetic results verify executor semantics only.
        Agent->Bundled=true; ProcessScene->EndBody(); BeginBody(); CompleteMotion(Phase::Arriving);
        FCommand Prepare; Prepare.Tool=TEXT("prepare_panel"); Prepare.Reason=TEXT("Macro fixture");
        DispatchAgentCommand(Prepare); CompleteMotion(Phase::AdhesiveApplication);
        Check(TEXT("agent_prepare_continues_to_placement_without_extra_llm_choice"),Agent->Busy && Agent->Job==TEXT("place_panel") && Agent->CompletedSteps.Num()==1);
        const double PlacingAge=State.StageSeconds;
        TogglePaused(); Tick(.1f);
        Check(TEXT("agent_prepare_pause_preserves_completed_sealant_and_placement"),Agent->Busy && Agent->CompletedSteps.Num()==1 && State.StageSeconds==PlacingAge);
        TogglePaused(); CompleteMotion(Phase::PanelPlacement);
        Check(TEXT("agent_prepare_reports_actual_steps_and_holds_without_inspecting"),!Agent->Busy && Agent->HolderEngaged && State.ActiveGate==-1 &&
            Agent->Journal.Last()->AsObject()->GetArrayField(TEXT("completed_steps")).Num()==2);
        const int32 BeforeRepeat=Agent->Audit.Completed.FindRef(TEXT("apply_sealant"));
        DispatchAgentCommand(Prepare);
        Check(TEXT("agent_prepare_duplicate_pickup_fails_before_applying_more_sealant"),!Agent->Busy && Agent->Audit.Completed.FindRef(TEXT("apply_sealant"))==BeforeRepeat &&
            Agent->Journal.Last()->AsObject()->GetArrayField(TEXT("completed_steps")).IsEmpty());
        FCommand Temperatures; Temperatures.Tool=TEXT("inspect_temperatures"); Temperatures.Reason=TEXT("Read both temperatures");
        DispatchAgentCommand(Temperatures); DelayCapture(); Decision(TEXT("R")); DelayCapture();
        Check(TEXT("agent_thermal_bundle_continues_after_hot_first_result"),Agent->Busy && StageName()==TEXT("weld") && Agent->ThermalResults.Num()==1);
        Decision(TEXT("A"),false);
        auto Thermal=Agent->Journal.Last()->AsObject();
        Check(TEXT("agent_thermal_bundle_preserves_hot_and_invalid_results_without_retry"),!Agent->Busy && Thermal->GetArrayField(TEXT("results")).Num()==2 &&
            Thermal->GetArrayField(TEXT("results"))[0]->AsObject()->GetStringField(TEXT("decision"))==TEXT("R") &&
            !Thermal->GetArrayField(TEXT("results"))[1]->AsObject()->GetBoolField(TEXT("valid")) && ModelRequestsSubmitted==0);
        DispatchAgentCommand(Temperatures); DelayCapture(); Decision(TEXT("G")); DelayCapture(); TogglePaused();
        Check(TEXT("agent_thermal_bundle_cancellation_keeps_only_actual_first_result"),!Agent->Busy &&
            Agent->Journal.Last()->AsObject()->GetStringField(TEXT("status"))==TEXT("cancelled_by_pause") &&
            Agent->Journal.Last()->AsObject()->GetArrayField(TEXT("results")).Num()==1);
        TogglePaused(); DispatchAgentCommand(Temperatures); DelayCapture();
        Check(TEXT("agent_thermal_bundle_retry_starts_fresh_first_stage"),StageName()==TEXT("adhesive") && Agent->ThermalResults.IsEmpty());
        TogglePaused(); TogglePaused();
        FCommand Finish; Finish.Tool=TEXT("finish_body"); Finish.Reason=TEXT("Deliberately unchecked finish");
        DispatchAgentCommand(Finish); CompleteMotion(Phase::HolderRetract);
        Check(TEXT("agent_finish_retracts_then_advances_without_quality_veto"),!Agent->HolderEngaged && Agent->Busy && State.Phase==Phase::Departing && Agent->Failures>0);
        CompleteMotion(Phase::Departing); CompleteMotion(Phase::Arriving);
        DispatchAgentCommand(Finish);
        Check(TEXT("agent_finish_without_holder_only_advances"),State.Phase==Phase::Departing && Agent->CompletedSteps.IsEmpty());
        CompleteMotion(Phase::Departing);
        Agent->Remote=true;
        auto Arrival=MakeShared<FJsonObject>(); Arrival->SetStringField(TEXT("status"),TEXT("completed"));
        QueueAgentHarnessEvent(TEXT("arrival"),Arrival);
        auto Packet=AgentHarnessPacket();
        Check(TEXT("edge_harness_packet_excludes_prompts_history_and_source_truth"),Packet->Values.Num()==10 &&
            !Packet->HasField(TEXT("messages")) && !Packet->HasField(TEXT("system_prompt")) && !Packet->HasField(TEXT("tools")) &&
            !Packet->HasField(TEXT("preassembled")) && Packet->GetStringField(TEXT("body_id"))==State.BodyId);
        Check(TEXT("edge_harness_event_identity_and_sequence_are_application_owned"),Agent->HarnessSerial==1 &&
            Packet->GetArrayField(TEXT("events"))[0]->AsObject()->GetNumberField(TEXT("seq"))==1 &&
            Packet->GetArrayField(TEXT("events"))[0]->AsObject()->GetStringField(TEXT("call_id")).IsEmpty());
        Agent->HarnessRequest=FHttpModule::Get().CreateRequest();
        Agent->HarnessRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr,FHttpResponsePtr,bool) {});
        const auto PendingHarness=Agent->HarnessRequest; const uint64 HarnessEpoch=Agent->Epoch;
        CancelAgentDecision();
        Check(TEXT("edge_cancel_preserves_unacknowledged_events_but_invalidates_http_scope"),!Agent->HarnessRequest &&
            !PendingHarness->OnProcessRequestComplete().IsBound() && Agent->Epoch>HarnessEpoch && Agent->HarnessEvents.Num()==1);
        State.bPaused=true;
        Check(TEXT("edge_harness_pause_is_explicit_even_when_no_camera_is_running"),AgentHarnessPacket()->GetBoolField(TEXT("paused")));
        State.bPaused=false;
        Check(TEXT("edge_harness_close_includes_unacknowledged_final_events"),AgentHarnessPacket(true)->GetBoolField(TEXT("closed")) &&
            AgentHarnessPacket(true)->GetArrayField(TEXT("events")).Num()==1);
        Agent->Remote=false;
        CancelAgentDecision(); Agent.Reset(); State.bAgentControl=false; ProcessScene->SetAgentControl(false);
    }
    CancelInspection(); bReady=false; bModelRequestsEnabled=OriginalRequests; WeldingCell->SetAnimationEnabled(false);
    auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("schema"),TEXT("darkfactory-process-policy-smoke-v1"));
    Report->SetStringField(TEXT("contract_version"),ContractVersion); Report->SetStringField(TEXT("source"),TEXT("policy_test")); Report->SetStringField(TEXT("session_id"),SessionId);
    auto Execution=MakeShared<FJsonObject>(); Execution->SetNumberField(TEXT("required_consensus_batches"),State.RequiredConsensusBatches);
    Report->SetObjectField(TEXT("execution_policy"),Execution);
    Report->SetStringField(TEXT("scope"),TEXT("Native panel-assembly policy, physical holds and scan poses with scripted decisions/synthetic encoder mailboxes; no model accuracy or rendered-image validation"));
    Report->SetBoolField(TEXT("model_accuracy_evidence"),false); Report->SetNumberField(TEXT("http_requests_submitted"),static_cast<double>(ModelRequestsSubmitted));
    Report->SetNumberField(TEXT("checks_count"),Checks.Num()); Report->SetArrayField(TEXT("checks"),Checks); Report->SetArrayField(TEXT("inspection_scan_poses"),InspectionPolicyPoses); Report->SetBoolField(TEXT("passed"),Passed);
    FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<TCHAR,TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
    const bool Saved=FFileHelper::SaveStringToFile(Text,*FPaths::Combine(OutputRoot,TEXT("process_policy.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_POLICY_SMOKE checks=%d passed=%d saved=%d model_requests=0"),Checks.Num(),Passed,Saved);
    return Passed && Saved;
}
