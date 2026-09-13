#include "DarkFactoryProcessController.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryWeldingCell.h"
#include "Components/SceneComponent.h"
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

// Deliberate policy-level inputs exercise the native state machine, not Reason2.
// No HTTP ProcessRequest, camera evidence export or model accuracy claim occurs.
bool ADarkFactoryProcessController::RunPolicySmoke() {
    if (!FParse::Param(FCommandLine::Get(),TEXT("DFProcessPolicySmoke")) || !bReady
        || !ProcessScene.IsValid() || !WeldingCell.IsValid() || State.bRequestPending
        || Request.IsValid() || Readback.IsBusy() || EncodeFuture.IsValid()) {
        UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_POLICY_SMOKE requires a fresh initialized standalone policy fixture"));
        return false;
    }
    using EPhase=EDarkFactoryProcessPhase;
    using EGate=EDarkFactoryGateState;
    TArray<TSharedPtr<FJsonValue>> Checks;
    bool Passed=true;
    const bool OriginalModelRequestsEnabled=bModelRequestsEnabled;
    bModelRequestsEnabled=true; // Scripted policy fixture; HTTP submission remains suppressed.
    auto Check=[&](const TCHAR* Name,bool Good,const FString& Detail=FString()) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Name);
        Row->SetBoolField(TEXT("passed"),Good); Row->SetStringField(TEXT("detail"),Detail);
        Checks.Add(MakeShared<FJsonValueObject>(Row)); Passed &= Good;
        if (!Good) UE_LOG(LogTemp,Error,TEXT("DF_PROCESS_POLICY_CHECK_FAILED %s %s"),Name,*Detail);
    };
    auto Decision=[&](const TCHAR* Code,bool Valid=true) {
        ApplyDecision(Code,Valid,TEXT("POLICY TEST: scripted native control input, not model inference"),0,TEXT("POLICY TEST"));
        // A policy test controls when captures begin; elapsed simulated time must
        // never cause it to submit a request to the user's real model service.
        NextInspection=FPlatformTime::Seconds()+3600.;
    };
    const FString R4Prefix=WeldingCell->GetRobotTiming(3,WeldingCell->GetMotionTime()).RobotId+TEXT("_");
    TArray<double> R4Rest,R4Active;
    bool R4ReferencesValid=true;
    const auto R4ReferenceMetadata=WeldingCell->BuildPreviewMetadata();
    for (const auto& Value:R4ReferenceMetadata->GetArrayField(TEXT("joints"))) {
        const auto Joint=Value->AsObject();
        if (!Joint->GetStringField(TEXT("id")).StartsWith(R4Prefix)) continue;
        R4Rest.Add(Joint->GetNumberField(TEXT("rest_angle_deg")));
        const auto& Approach=Joint->GetArrayField(TEXT("approach_angles_deg"));
        R4ReferencesValid &= Approach.Num()==3;
        if (Approach.Num()==3) R4Active.Add(Approach[2]->AsNumber());
    }
    auto R4Angles=[&]() {
        TArray<double> Angles;
        const auto Metadata=WeldingCell->BuildPreviewMetadata();
        for (const auto& Value:Metadata->GetArrayField(TEXT("joints"))) {
            const auto Joint=Value->AsObject();
            if (Joint->GetStringField(TEXT("id")).StartsWith(R4Prefix)) Angles.Add(Joint->GetNumberField(TEXT("angle_deg")));
        }
        return Angles;
    };
    auto SameSixAngles=[](const TArray<double>& Actual,const TArray<double>& Expected) {
        if (Actual.Num()!=6 || Expected.Num()!=6) return false;
        for (int32 I=0;I<6;++I) if (!FMath::IsNearlyEqual(Actual[I],Expected[I],.001)) return false;
        return true;
    };
    auto R4AtRest=[&]() { return WeldingCell->GetInspectionExtension()==0. && SameSixAngles(R4Angles(),R4Rest); };
    auto R4AtActive=[&]() { return WeldingCell->GetInspectionExtension()==1. && SameSixAngles(R4Angles(),R4Active); };
    auto CaptureInactive=[&]() {
        return !bCaptureActive && !State.bRequestPending && !Request.IsValid() && !Readback.IsBusy() && !EncodeFuture.IsValid();
    };
    Check(TEXT("inspection_robot_has_six_authored_rest_and_active_joints"),R4ReferencesValid && R4Rest.Num()==6
        && R4Active.Num()==6 && !SameSixAngles(R4Rest,R4Active));
    bool AllSettledArrivalsAtRest=true,SawSettledArrival=false;
    auto ResetToDoors=[&](int32 Fault=0) {
        CancelInspection(); State.bPaused=false; bCaptureQA=false; bModelQA=false;
        State.Backend=TEXT("POLICY TEST"); ForcedFault=Fault;
        ProcessScene->EndBody(); BeginBody();
        for (int32 I=0;I<40 && State.Phase==EPhase::Arriving;++I) {
            Tick(.1f);
            if (State.Phase==EPhase::Arriving && State.StageSeconds>=.9) {
                SawSettledArrival=true; AllSettledArrivalsAtRest &= R4AtRest();
            }
        }
        Tick(0.f); NextInspection=FPlatformTime::Seconds()+3600.;
    };
    auto AtFixture=[&]() {
        return WeldingCell->GetCarriageRoot()
            && FMath::Abs(WeldingCell->GetCarriageRoot()->GetRelativeLocation().Y)<.001;
    };
    auto AdvanceHold=[&](EPhase Phase,int32 Count) {
        bool Safe=true;
        for (int32 I=0;I<Count;++I) {
            Tick(.1f);
            Safe &= State.Phase==Phase && !WeldingCell->IsWelding()
                && WeldingCell->GetWeldEmitters().IsEmpty() && AtFixture();
        }
        return Safe;
    };

    Check(TEXT("model_smoke_fixture_expected_destinations"),IsModelSmokeOutcomeCorrect(1,true)
        && IsModelSmokeOutcomeCorrect(2,false) && IsModelSmokeOutcomeCorrect(3,true));
    Check(TEXT("model_smoke_fixture_wrong_destinations_rejected"),!IsModelSmokeOutcomeCorrect(1,false)
        && !IsModelSmokeOutcomeCorrect(2,true) && !IsModelSmokeOutcomeCorrect(3,false));
    Check(TEXT("model_smoke_fixture_invalid_sequences_rejected"),!IsModelSmokeOutcomeCorrect(0,true)
        && !IsModelSmokeOutcomeCorrect(0,false) && !IsModelSmokeOutcomeCorrect(4,true) && !IsModelSmokeOutcomeCorrect(4,false));
    {
        // Exercise the real outcome recorder and final predicate with synthetic
        // fixture results. No tick, model request or physical action occurs here.
        const auto SavedState=State;
        const bool SavedModelQA=bModelQA,SavedMatch=bModelSmokeOutcomesMatch;
        const int32 SavedCompleted=CompletedBodies;
        const auto SavedOutcomes=ModelSmokeOutcomes;
        bModelQA=true; CompletedBodies=3; State.PassedBodies=2; State.ReturnedBodies=1;
        auto Outcome=[&](int32 Sequence,bool Departed) {
            State.BodySequence=Sequence; State.BodyId=FString::Printf(TEXT("policy-outcome-%d"),Sequence);
            State.Phase=Departed ? EPhase::Departing:EPhase::Returning;
            RecordModelSmokeOutcome();
        };
        ModelSmokeOutcomes.Reset(); bModelSmokeOutcomesMatch=true;
        Outcome(1,true); Outcome(2,false); Outcome(3,true);
        Check(TEXT("model_smoke_correct_individual_results_pass"),HasPassedModelSmoke()
            && ModelSmokeOutcomes.Num()==3 && bModelSmokeOutcomesMatch);
        const bool SavedThermalLimits=bModelSmokeThermalLimitsMatch;
        bModelSmokeThermalLimitsMatch=false;
        Check(TEXT("model_smoke_thermal_failure_blocks_otherwise_correct_outcomes"),!HasPassedModelSmoke());
        bModelSmokeThermalLimitsMatch=SavedThermalLimits;
        bModelQA=false;
        Check(TEXT("ordinary_process_cannot_claim_model_smoke_pass"),!HasPassedModelSmoke());
        bModelQA=true; ModelSmokeOutcomes.Pop();
        Check(TEXT("model_smoke_missing_body_evidence_cannot_pass"),!HasPassedModelSmoke());
        ModelSmokeOutcomes.Reset(); bModelSmokeOutcomesMatch=true;
        Outcome(1,false); Outcome(2,true); Outcome(3,true);
        Check(TEXT("model_smoke_swapped_results_fail_despite_correct_totals"),!HasPassedModelSmoke()
            && !bModelSmokeOutcomesMatch && State.PassedBodies==2 && State.ReturnedBodies==1 && ModelSmokeOutcomes.Num()==3);
        State=SavedState; bModelQA=SavedModelQA; CompletedBodies=SavedCompleted;
        ModelSmokeOutcomes=SavedOutcomes; bModelSmokeOutcomesMatch=SavedMatch;
    }

    ResetToDoors();
    Check(TEXT("arrival_stops_at_door_gate"),State.Phase==EPhase::Doors && State.ActiveGate==0 && AtFixture());
    Check(TEXT("new_body_gates_pending_after_doors"),State.Gates[1]==EGate::Pending && State.Gates[2]==EGate::Pending);
    const double DoorClock=State.ClockSeconds;
    Check(TEXT("no_reply_cannot_start_weld_or_move_body"),AdvanceHold(EPhase::Doors,240));
    Check(TEXT("door_hold_continues_simulation_clock"),State.ClockSeconds>DoorClock+23.9);
    Check(TEXT("inspection_robot_rests_through_door_hold"),R4AtRest());

    Decision(TEXT("G"));
    Check(TEXT("first_door_green_holds"),State.Phase==EPhase::Doors && State.ConsecutivePasses==1);
    Decision(TEXT("A"));
    Check(TEXT("ambiguous_reply_resets_green_streak"),State.Phase==EPhase::Doors && State.ConsecutivePasses==0 && State.Gates[0]==EGate::Waiting);
    Decision(TEXT("G"));
    Check(TEXT("green_ambiguous_green_does_not_pass"),State.Phase==EPhase::Doors && State.ConsecutivePasses==1);
    Decision(TEXT("G"),false);
    Check(TEXT("invalid_green_is_hold_and_error"),State.Phase==EPhase::Doors && State.Decision==TEXT("A") && State.ConsecutivePasses==0 && State.Gates[0]==EGate::Error);
    Decision(TEXT("G"));
    Check(TEXT("green_error_green_does_not_pass"),State.Phase==EPhase::Doors && State.ConsecutivePasses==1);
    Decision(TEXT("G"));
    Check(TEXT("two_fresh_door_greens_enable_only_adhesive"),State.Phase==EPhase::Adhesive && State.Gates[0]==EGate::Passed
        && State.ActiveGate==1 && State.Gates[2]==EGate::Pending && State.ConsecutivePasses==0);

    // Exercise the real thermal readiness guard without CaptureTick/rendering:
    // NullRHI policy fixtures must never export pixels or submit HTTP requests.
    const uint64 BeforeApproachCapture=CaptureSerial;
    StartCapture();
    Check(TEXT("thermal_capture_refused_before_inspection_approach"),R4AtRest() && CaptureInactive() && CaptureSerial==BeforeApproachCapture);
    for (int32 I=0;I<3;++I) Tick(.1f);
    const double MidApproachExtension=WeldingCell->GetInspectionExtension(),MidApproachClock=State.ClockSeconds;
    const auto MidApproachAngles=R4Angles();
    Check(TEXT("inspection_robot_moves_between_authored_endpoints"),MidApproachExtension>0. && MidApproachExtension<1.
        && MidApproachAngles.Num()==6 && !SameSixAngles(MidApproachAngles,R4Rest) && !SameSixAngles(MidApproachAngles,R4Active));
    StartCapture();
    Check(TEXT("thermal_capture_refused_while_inspection_arm_travels"),CaptureInactive() && CaptureSerial==BeforeApproachCapture);
    TogglePaused();
    for (int32 I=0;I<10;++I) Tick(.1f);
    Check(TEXT("pause_freezes_inspection_arm_mid_approach"),State.bPaused && State.ClockSeconds==MidApproachClock
        && WeldingCell->GetInspectionExtension()==MidApproachExtension && SameSixAngles(R4Angles(),MidApproachAngles)
        && !WeldingCell->IsAnimating() && CaptureInactive());
    TogglePaused(); NextInspection=FPlatformTime::Seconds()+3600.;
    for (int32 I=0;I<12;++I) Tick(.1f);
    Check(TEXT("resume_completes_all_six_inspection_joints"),!State.bPaused && State.Phase==EPhase::Adhesive
        && R4AtActive() && CaptureInactive() && CaptureSerial==BeforeApproachCapture);

    // Start a fresh body so thermal hold checks see substantial initial cooling.
    ResetToDoors(); Decision(TEXT("G")); Decision(TEXT("G"));
    const float AdhesiveStart=ProcessScene->GetMaximumAdhesiveC();
    const double AdhesiveClock=State.ClockSeconds,EffectClock=WeldingCell->GetEffectTime();
    const auto BeforeThermalAuditChecks=ModelSmokeThermalChecks;
    const bool BeforeThermalAuditLimits=bModelSmokeThermalLimitsMatch;
    ModelSmokeThermalChecks.Reset(); bModelSmokeThermalLimitsMatch=true;
    RecordModelSmokeThermalGate();
    Check(TEXT("acceptance_audit_marks_hot_adhesive_not_ready"),AdhesiveStart>45.f
        && ModelSmokeThermalChecks.Num()==1 && !ModelSmokeThermalChecks.Last()->AsObject()->GetBoolField(TEXT("ready_at_acceptance"))
        && !bModelSmokeThermalLimitsMatch && State.Phase==EPhase::Adhesive && !WeldingCell->IsWelding());
    Decision(TEXT("R"));
    Check(TEXT("hot_adhesive_red_holds_without_return"),State.Phase==EPhase::Adhesive && State.Gates[1]==EGate::Waiting && State.ConsecutivePasses==0);
    Check(TEXT("adhesive_hold_outlasts_legacy_cycle_without_welding"),AdvanceHold(EPhase::Adhesive,300));
    Check(TEXT("adhesive_cools_during_gate_hold"),ProcessScene->GetMaximumAdhesiveC()<AdhesiveStart-10.f,
        FString::Printf(TEXT("before=%.3f after=%.3f"),AdhesiveStart,ProcessScene->GetMaximumAdhesiveC()));
    Check(TEXT("simulation_and_effect_clocks_advance_during_hold"),State.ClockSeconds>AdhesiveClock+29.9
        && WeldingCell->GetEffectTime()>EffectClock+29.9);
    Check(TEXT("cool_adhesive_cannot_auto_accept"),State.Phase==EPhase::Adhesive && State.Gates[1]!=EGate::Passed && AtFixture());
    Check(TEXT("inspection_robot_stays_active_during_adhesive_hold"),R4AtActive());
    RecordModelSmokeThermalGate();
    Check(TEXT("acceptance_audit_marks_cool_adhesive_ready_without_erasing_failure"),ProcessScene->GetMaximumAdhesiveC()<45.f
        && ModelSmokeThermalChecks.Num()==2 && ModelSmokeThermalChecks.Last()->AsObject()->GetBoolField(TEXT("ready_at_acceptance"))
        && !bModelSmokeThermalLimitsMatch && State.Phase==EPhase::Adhesive);
    ModelSmokeThermalChecks=BeforeThermalAuditChecks; bModelSmokeThermalLimitsMatch=BeforeThermalAuditLimits;
    Decision(TEXT("G"));
    Check(TEXT("first_adhesive_green_holds"),State.Phase==EPhase::Adhesive && State.ConsecutivePasses==1);
    Decision(TEXT("G"));
    Check(TEXT("two_adhesive_greens_authorize_welding"),State.Phase==EPhase::Welding && State.ActiveGate==-1
        && State.Gates[0]==EGate::Passed && State.Gates[1]==EGate::Passed && State.Gates[2]==EGate::Pending);

    bool SawWelding=false,OnlyWeldingRobots=true,StayedAtFixture=true;
    bool R4RestDuringWelding=true,SawSettledWelding=false;
    float PeakWeldC=0;
    for (int32 I=0;I<180 && State.Phase==EPhase::Welding;++I) {
        Tick(.1f); const auto Emitters=WeldingCell->GetWeldEmitters();
        SawWelding |= !Emitters.IsEmpty(); StayedAtFixture &= AtFixture();
        for (const auto& Emitter:Emitters) OnlyWeldingRobots &= Emitter.EmitterIndex>=0 && Emitter.EmitterIndex<3;
        PeakWeldC=FMath::Max(PeakWeldC,ProcessScene->GetMaximumWeldC());
        if (State.Phase==EPhase::Welding && State.StageSeconds>=.9) {
            SawSettledWelding=true; R4RestDuringWelding &= R4AtRest();
        }
    }
    Check(TEXT("authorized_cycle_produces_welding"),SawWelding && PeakWeldC>100.f,FString::Printf(TEXT("peak_weld_c=%.3f"),PeakWeldC));
    Check(TEXT("inspection_robot_never_emits_welding"),OnlyWeldingRobots);
    Check(TEXT("inspection_robot_withdraws_to_rest_during_welding"),SawSettledWelding && R4RestDuringWelding);
    Check(TEXT("body_stays_at_fixture_during_welding"),StayedAtFixture);
    Check(TEXT("completed_weld_enters_third_gate"),State.Phase==EPhase::WeldCooling && State.ActiveGate==2
        && State.Gates[2]==EGate::Inspecting && !WeldingCell->IsWelding());
    const float WeldStart=ProcessScene->GetMaximumWeldC();
    Decision(TEXT("R"));
    Check(TEXT("hot_weld_red_holds_without_return"),State.Phase==EPhase::WeldCooling && State.Gates[2]==EGate::Waiting && State.ConsecutivePasses==0);
    Check(TEXT("weld_cooling_hold_outlasts_legacy_cycle"),AdvanceHold(EPhase::WeldCooling,300));
    Check(TEXT("weld_heat_cools_during_gate_hold"),ProcessScene->GetMaximumWeldC()<WeldStart-5.f,
        FString::Printf(TEXT("before=%.3f after=%.3f"),WeldStart,ProcessScene->GetMaximumWeldC()));
    Check(TEXT("cold_weld_cannot_auto_depart"),State.Phase==EPhase::WeldCooling && AtFixture() && State.Gates[2]!=EGate::Passed);
    Check(TEXT("inspection_robot_redeploys_during_weld_cooling"),R4AtActive());
    Decision(TEXT("G"));
    Check(TEXT("first_cooling_green_holds"),State.Phase==EPhase::WeldCooling && State.ConsecutivePasses==1);
    Decision(TEXT("G"));
    Check(TEXT("two_cooling_greens_enable_departure"),State.Phase==EPhase::Departing && State.ActiveGate==-1
        && State.Gates[0]==EGate::Passed && State.Gates[1]==EGate::Passed && State.Gates[2]==EGate::Passed);
    const FString PassedBody=State.BodyId; const int32 PassedBefore=State.PassedBodies;
    Tick(.1f);
    Check(TEXT("accepted_body_moves_forward"),WeldingCell->GetCarriageRoot()->GetRelativeLocation().Y>0);
    bool R4RestDuringDeparture=true,SawSettledDeparture=false;
    for (int32 I=0;I<40 && State.BodyId==PassedBody;++I) {
        Tick(.1f);
        if (State.BodyId==PassedBody && State.Phase==EPhase::Departing && State.StageSeconds>=.9) {
            SawSettledDeparture=true; R4RestDuringDeparture &= R4AtRest();
        }
    }
    Check(TEXT("inspection_robot_rests_during_departure_and_next_arrival"),SawSettledDeparture && R4RestDuringDeparture && R4AtRest());
    Check(TEXT("departure_counts_body_and_creates_new_identity"),State.PassedBodies==PassedBefore+1 && State.BodyId!=PassedBody
        && State.Phase==EPhase::Arriving && State.ConsecutivePasses==0);

    ResetToDoors(2); // Exercise detached-door cleanup without classifying its pixels.
    const auto FallenBefore=ProcessScene->BuildMetadata();
    Check(TEXT("rejection_fixture_has_detached_door"),FallenBefore->GetBoolField(TEXT("fallen_detached")));
    Decision(TEXT("R"));
    Check(TEXT("door_red_returns_immediately"),State.Phase==EPhase::Returning && State.Gates[0]==EGate::Rejected
        && State.Gates[1]==EGate::Pending && State.Gates[2]==EGate::Pending);
    const FString RejectedBody=State.BodyId; const int32 ReturnedBefore=State.ReturnedBodies;
    ForcedFault=0; Tick(.1f);
    Check(TEXT("rejected_body_moves_to_entry"),WeldingCell->GetCarriageRoot()->GetRelativeLocation().Y<0 && !WeldingCell->IsWelding());
    bool R4RestDuringReturn=true,SawSettledReturn=false;
    for (int32 I=0;I<40 && State.BodyId==RejectedBody;++I) {
        Tick(.1f);
        if (State.BodyId==RejectedBody && State.Phase==EPhase::Returning && State.StageSeconds>=.9) {
            SawSettledReturn=true; R4RestDuringReturn &= R4AtRest();
        }
    }
    Check(TEXT("inspection_robot_rests_during_return_and_next_arrival"),SawSettledReturn && R4RestDuringReturn && R4AtRest());
    const auto FreshBody=ProcessScene->BuildMetadata();
    Check(TEXT("return_counts_body_and_resets_gates"),State.ReturnedBodies==ReturnedBefore+1 && State.BodyId!=RejectedBody
        && State.Gates[0]==EGate::Pending && State.Gates[1]==EGate::Pending && State.Gates[2]==EGate::Pending);
    Check(TEXT("loose_parts_do_not_survive_replacement"),!FreshBody->GetBoolField(TEXT("fallen_detached"))
        && !FreshBody->GetBoolField(TEXT("fallen_resting")) && FreshBody->GetIntegerField(TEXT("fault_door"))==-1);

    ResetToDoors();
    bModelRequestsEnabled=false; NextInspection=0;
    const uint64 BeforeDisabledCapture=CaptureSerial;
    bool StayedDisabled=true;
    for (int32 I=0;I<12;++I) {
        Tick(.1f); CaptureTick();
        StayedDisabled &= !bCaptureActive && !State.bRequestPending && !Request.IsValid()
            && !Readback.IsBusy() && !EncodeFuture.IsValid() && CaptureSerial==BeforeDisabledCapture;
    }
    Check(TEXT("no_inference_does_not_start_capture_or_http"),StayedDisabled);
    Check(TEXT("no_inference_holds_body_at_door_gate"),State.Phase==EPhase::Doors && State.ActiveGate==0
        && State.ConsecutivePasses==0 && AtFixture() && !WeldingCell->IsWelding());
    bModelRequestsEnabled=true; NextInspection=FPlatformTime::Seconds()+3600.;

    ResetToDoors(); Decision(TEXT("G")); StartCapture();
    Check(TEXT("capture_starts_with_one_pending_batch"),bCaptureActive && State.bRequestPending && CaptureSlot==0 && ExpectedImages==2);
    // Complete only the first encoder mailbox. The bytes are deliberately not a
    // PNG and never leave this process: the batch is canceled before submission.
    TPromise<TArray<uint8>> FirstEncoder;
    EncodeFuture=FirstEncoder.GetFuture(); FirstEncoder.SetValue(TArray<uint8>{11,22,33});
    CaptureTick();
    Check(TEXT("encoder_completion_is_consumed_once"),!EncodeFuture.IsValid());
    Check(TEXT("one_encoder_completion_fills_exactly_one_camera"),BatchImages.Num()==1 && CaptureSlot==1 && bCaptureActive && !Request.IsValid());
    CancelInspection();
    if (EncodeFuture.IsValid() && EncodeFuture.IsReady()) EncodeFuture.Consume(); // Cleanup even when the regression above fails.

    StartCapture();
    TPromise<TArray<uint8>> LateEncoder; EncodeFuture=LateEncoder.GetFuture();
    // Unsubmitted HTTP fixture verifies callback unbinding and identity cleanup;
    // ProcessRequest is intentionally never called.
    Request=FHttpModule::Get().CreateRequest(); Request->SetURL(Endpoint);
    ActiveRequestId=TEXT("policy-cancel-only");
    bool CallbackRan=false;
    Request->OnProcessRequestComplete().BindLambda([&CallbackRan](FHttpRequestPtr,FHttpResponsePtr,bool) { CallbackRan=true; });
    const auto PendingRequestFixture=Request;
    const double PausedClock=State.ClockSeconds; const float PausedAdhesive=ProcessScene->GetMaximumAdhesiveC();
    TogglePaused();
    Check(TEXT("pause_cancels_capture_and_request_identity"),State.bPaused && !bCaptureActive && !State.bRequestPending
        && !Request.IsValid() && ActiveRequestId.IsEmpty() && BatchImages.IsEmpty() && CaptureSlot==-1);
    Check(TEXT("pause_unbinds_http_callback"),!PendingRequestFixture->OnProcessRequestComplete().IsBound() && !CallbackRan);
    Check(TEXT("pause_erases_partial_green_streak"),State.ConsecutivePasses==0 && State.Gates[0]==EGate::Waiting);
    LateEncoder.SetValue(TArray<uint8>{44,55,66}); CaptureTick();
    Check(TEXT("late_encoder_completion_is_drained_after_cancel"),!EncodeFuture.IsValid());
    Check(TEXT("late_pixels_do_not_restore_batch_or_green"),BatchImages.IsEmpty() && CaptureSlot==-1 && !State.bRequestPending
        && State.ConsecutivePasses==0 && State.Phase==EPhase::Doors);
    if (EncodeFuture.IsValid() && EncodeFuture.IsReady()) EncodeFuture.Consume();
    Decision(TEXT("G"));
    Check(TEXT("paused_state_ignores_even_green_policy_input"),State.Phase==EPhase::Doors && State.ConsecutivePasses==0);
    for (int32 I=0;I<10;++I) Tick(.1f);
    Check(TEXT("pause_freezes_clock_and_adhesive_temperature"),State.ClockSeconds==PausedClock
        && ProcessScene->GetMaximumAdhesiveC()==PausedAdhesive && !WeldingCell->IsAnimating());
    TogglePaused(); NextInspection=FPlatformTime::Seconds()+3600.;
    Decision(TEXT("G"));
    Check(TEXT("resume_requires_two_new_greens"),!State.bPaused && State.Phase==EPhase::Doors && State.ConsecutivePasses==1);
    const FString BeforeReset=State.BodyId; BeginBody();
    Check(TEXT("new_body_cannot_inherit_old_green_streak"),State.BodyId!=BeforeReset && State.ConsecutivePasses==0
        && State.Gates[0]==EGate::Pending && State.Gates[1]==EGate::Pending && State.Gates[2]==EGate::Pending);

    Check(TEXT("inspection_robot_rests_during_all_settled_arrivals"),SawSettledArrival && AllSettledArrivalsAtRest);
    CancelInspection(); bReady=false; bModelRequestsEnabled=OriginalModelRequestsEnabled; WeldingCell->SetAnimationEnabled(false);
    auto Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("schema"),TEXT("darkfactory-process-policy-smoke-v1"));
    Report->SetStringField(TEXT("source"),TEXT("policy_test"));
    Report->SetStringField(TEXT("session_id"),SessionId);
    Report->SetStringField(TEXT("scope"),TEXT("Native state-machine and physical hold/cooling checks with scripted decisions and synthetic encoder completions; no model accuracy or rendered image validation"));
    Report->SetBoolField(TEXT("model_accuracy_evidence"),false); Report->SetNumberField(TEXT("http_requests_submitted"),0);
    Report->SetNumberField(TEXT("checks_count"),Checks.Num()); Report->SetArrayField(TEXT("checks"),Checks);
    Report->SetBoolField(TEXT("passed"),Passed);
    FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<TCHAR,TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
    const FString Path=FPaths::Combine(OutputRoot,TEXT("process_policy.json"));
    IFileManager::Get().MakeDirectory(*OutputRoot,true);
    const bool Saved=FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_POLICY_SMOKE checks=%d passed=%d saved=%d model_requests=0"),Checks.Num(),Passed,Saved);
    return Passed && Saved;
}
