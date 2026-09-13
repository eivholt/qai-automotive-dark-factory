#include "DarkFactoryCellMechanisms.h"
#include "DarkFactoryWeldingCell.h"
#include "Components/BoxComponent.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace {
// Source tile/rail survey in UE centimetres. Fixtures occupy the two raised
// sidewalks, not the central rail beds; posts stand on the ground tile.
constexpr double GroundZ=6.3477106,PlatformZ=85.592865;
constexpr double CloseStart=3.,CloseEnd=3.35,OpenAngle=75.,DefaultArmLength=42.;
constexpr double BeamZ=175.,PostOffset=40.,FixtureOffset=48.,PadWidth=8.,ContactGap=.2;
const FVector TowerBase(280,-710,GroundZ);
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& P) {
    return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
}
double Ease(double X) { X=FMath::Clamp(X,0.,1.); return X*X*X*(X*(X*6.-15.)+10.); }
// Segment-vs-oriented collision box: callers transform both endpoints into the
// component's unscaled local frame. This is a presence proxy, not optical tracing.
bool SegmentBox(const FVector& A,const FVector& B,const FVector& Extent) {
    double Enter=0,Leave=1;
    const FVector D=B-A;
    for (int32 Axis=0;Axis<3;++Axis) {
        if (FMath::Abs(D[Axis])<1.e-9) {
            if (A[Axis]<-Extent[Axis] || A[Axis]>Extent[Axis]) return false;
        } else {
            double T0=(-Extent[Axis]-A[Axis])/D[Axis],T1=(Extent[Axis]-A[Axis])/D[Axis];
            if (T0>T1) Swap(T0,T1);
            Enter=FMath::Max(Enter,T0); Leave=FMath::Min(Leave,T1);
            if (Enter>Leave) return false;
        }
    }
    return true;
}
}

ADarkFactoryCellMechanisms::ADarkFactoryCellMechanisms() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("CellMechanismsRoot"));
    SceneRoot->SetMobility(EComponentMobility::Movable); SetRootComponent(SceneRoot);
}
UStaticMeshComponent* ADarkFactoryCellMechanisms::AddPart(const FString& Name,UStaticMesh* Shape,
    UMaterialInterface* Material,const FVector& Position,const FVector& Size,const FRotator& Rotation,bool Shadow) {
    auto* Part=NewObject<UStaticMeshComponent>(this,*Name);
    Part->SetupAttachment(SceneRoot); Part->SetMobility(EComponentMobility::Movable);
    Part->SetStaticMesh(Shape); Part->SetMaterial(0,Material);
    Part->SetWorldLocationAndRotation(Position,Rotation); Part->SetWorldScale3D(Size/100.);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(Shadow);
    Part->SetLightingChannels(true,true,false); Part->SetReceivesDecals(false);
    Part->SetAffectDynamicIndirectLighting(false); Part->SetAffectDistanceFieldLighting(false);
    Part->SetEmissiveLightSource(false);
    // Depth still occludes profiled geometry. ID0 uses the imported rig's 20C
    // ambient fallback without consuming any of the 255 profile slots.
    Part->SetRenderCustomDepth(false); Part->SetCustomDepthStencilValue(0);
    AddInstanceComponent(Part); Part->RegisterComponent(); Meshes.Add(Part);
    return Part;
}
UMaterialInstanceDynamic* ADarkFactoryCellMechanisms::MakePaint(UMaterialInterface* Parent,
    const FLinearColor& Color,float Roughness) {
    auto* M=UMaterialInstanceDynamic::Create(Parent,this);
    M->SetVectorParameterValue(TEXT("BaseColor"),Color); M->SetScalarParameterValue(TEXT("Roughness"),Roughness);
    Materials.Add(M); return M;
}
UMaterialInstanceDynamic* ADarkFactoryCellMechanisms::MakeIndicator(UMaterialInterface* Parent,const FLinearColor& Color) {
    auto* M=UMaterialInstanceDynamic::Create(Parent,this);
    M->SetVectorParameterValue(TEXT("SparkColor"),Color); M->SetScalarParameterValue(TEXT("EmissiveStrength"),.04f);
    Materials.Add(M); return M;
}
void ADarkFactoryCellMechanisms::SetSegment(UStaticMeshComponent* Part,const FVector& A,const FVector& B,double DiameterCm) {
    if (!Part) return;
    const FVector D=B-A;
    Part->SetWorldLocationAndRotation((A+B)*.5,FRotationMatrix::MakeFromZ(D).Rotator());
    Part->SetWorldScale3D(FVector(DiameterCm,DiameterCm,D.Size())/100.);
}
bool ADarkFactoryCellMechanisms::Initialize(ADarkFactoryWeldingCell* Cell) {
    if (bInitialized || !IsValid(Cell)) return false;
    const FBox Body=Cell->GetCarriageBounds();
    if (!Body.IsValid || Body.Min.ContainsNaN() || Body.Max.ContainsNaN() || Body.GetSize().X<200. || Body.GetSize().X>400.) {
        UE_LOG(LogTemp,Error,TEXT("DF_MECHANISMS_INVALID_BODY expected prepared welding carriage bounds")); return false;
    }
    Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Paint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    auto* Indicator=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingArc.M_WeldingArc"));
    if (!Cube || !Cylinder || !Paint || !Indicator) {
        UE_LOG(LogTemp,Error,TEXT("DF_MECHANISMS_MISSING_CONTENT run setup_content.py and setup_welding_effects.py")); return false;
    }
    SourceCell=Cell; AddTickPrerequisiteActor(Cell); BodyMinX=Body.Min.X; BodyMaxX=Body.Max.X;
    auto* Blue=MakePaint(Paint,FLinearColor(.055f,.19f,.30f),.50f);
    auto* Steel=MakePaint(Paint,FLinearColor(.36f,.40f,.43f),.32f);
    auto* Dark=MakePaint(Paint,FLinearColor(.018f,.023f,.027f),.65f);
    auto* Yellow=MakePaint(Paint,FLinearColor(.8f,.41f,.015f),.56f);
    auto* Reflector=MakePaint(Paint,FLinearColor(.8f,.52f,.06f),.25f);
    // Startup-only calibration pose: complete any deferred mesh/Chaos state
    // while the car is stationary and visible, then restore its exact clock.
    // No simulation ticks or captures occur during this reversible pose change.
    const double OriginalMotionSeconds=Cell->GetMotionTime();
    Cell->SetMotionTime(3.4);
    // Nine rays cover the 17x9 cm pad face; use the outermost sheet hit, keeping
    // the authored flat pad .2 cm outside the sampled surface envelope.
    TArray<UStaticMeshComponent*> Panels; Cell->GetComponents<UStaticMeshComponent>(Panels);
    Panels.RemoveAll([](const UStaticMeshComponent* P) { return !P || !P->GetName().StartsWith(TEXT("SM_BodyPanel_")); });
    for (auto* Panel:Panels) {
#if WITH_EDITOR
        if (Panel->GetStaticMesh()->IsCompiling()) {
            TArray<UStaticMesh*> PendingMeshes={Panel->GetStaticMesh()};
            FStaticMeshCompilingManager::Get().FinishCompilation(PendingMeshes);
        }
#endif
        // Match the proven body-weld binding path: creation can be deferred even
        // after registration, and a component trace requires a valid Chaos body.
        if (!Panel->IsPhysicsStateCreated()) { Panel->DestroyPhysicsState(); Panel->CreatePhysicsState(false); }
    }
    FVector CarriageTranslation=FVector::ZeroVector;
    if (!Cell->GetCarriageColliders().IsEmpty() && Cell->GetCarriageColliders()[0]->GetAttachParent())
        CarriageTranslation=Cell->GetCarriageColliders()[0]->GetAttachParent()->GetComponentLocation();
    FCollisionQueryParams ContactParams(SCENE_QUERY_STAT(CellMechanismContact),true);
    for (int32 SideIndex=0;SideIndex<2;++SideIndex) for (int32 Station=0;Station<2;++Station) {
        FFixture F; F.Side=SideIndex==0 ? -1.:1.; F.BodySideX=SideIndex==0 ? BodyMinX:BodyMaxX;
        F.Id=FString::Printf(TEXT("fixture_%s_%s"),SideIndex==0 ? TEXT("nx"):TEXT("px"),Station==0 ? TEXT("ny"):TEXT("py"));
        const double X=F.BodySideX+F.Side*FixtureOffset,Y=Station==0 ? -92.:92.;
        F.Pivot=FVector(X,Y,147); F.ActuatorBase=FVector(X+F.Side*5.,Y,110);
        double Outermost=-TNumericLimits<double>::Max();
        for (const double DY:{-8.5,0.,8.5}) for (const double DZ:{-4.5,0.,4.5}) {
            const FVector Start=FVector(X,Y+DY,147+DZ)+CarriageTranslation;
            const FVector End=FVector((BodyMinX+BodyMaxX)*.5,Y+DY,147+DZ)+CarriageTranslation;
            for (auto* Panel:Panels) {
                FHitResult Hit;
                if (Panel->LineTraceComponent(Hit,Start,End,ContactParams)) {
                    const FVector Contact=Hit.ImpactPoint-CarriageTranslation;
                    if (F.Side*Contact.X>Outermost) {
                        Outermost=F.Side*Contact.X; F.SurfaceContact=Contact; F.ContactMesh=Panel->GetName();
                    }
                }
            }
        }
        if (Outermost>-TNumericLimits<double>::Max()) {
            const double PadX=F.Side*(Outermost+PadWidth*.5+ContactGap);
            const double Length=F.Side*(X-PadX);
            if (FMath::IsFinite(Length) && Length>=30. && Length<=65.) {
                F.LengthCm=Length; F.SurfaceGapCm=ContactGap; F.bSurfaceBound=true;
                UE_LOG(LogTemp,Display,TEXT("DF_MECHANISM_CONTACT_BOUND fixture=%s mesh=%s arm_cm=%.4f gap_cm=%.3f"),*F.Id,*F.ContactMesh,F.LengthCm,F.SurfaceGapCm);
            }
        }
        if (!F.bSurfaceBound) {
            F.LengthCm=DefaultArmLength;
            UE_LOG(LogTemp,Warning,TEXT("DF_MECHANISM_CONTACT_FALLBACK fixture=%s; using noncontact AABB guide"),*F.Id);
        }
        AddPart(F.Id+TEXT("_foot"),Cube,Steel,FVector(X,Y,PlatformZ+2),FVector(36,28,4));
        AddPart(F.Id+TEXT("_housing"),Cube,Blue,FVector(X,Y,116),FVector(25,19,57));
        AddPart(F.Id+TEXT("_hinge"),Cylinder,Steel,F.Pivot,FVector(13,13,27),FRotator(0,0,90));
        F.Arm=AddPart(F.Id+TEXT("_arm"),Cube,Yellow,F.Pivot,FVector(8,8,F.LengthCm));
        F.Pad=AddPart(F.Id+TEXT("_locating_pad"),Cube,Dark,F.Pivot,FVector(PadWidth,17,9));
        F.Rod=AddPart(F.Id+TEXT("_actuator_rod"),Cylinder,Steel,F.Pivot,FVector(3,3,30));
        F.Toggle=AddPart(F.Id+TEXT("_toggle_link"),Cylinder,Steel,F.Pivot,FVector(5,5,18));
        AddPart(F.Id+TEXT("_cylinder"),Cylinder,Dark,F.ActuatorBase+FVector(0,0,-9),FVector(12,12,30));
        Fixtures.Add(F);
    }
    Cell->SetMotionTime(OriginalMotionSeconds);
    AddPart(TEXT("tower_foot"),Cube,Steel,TowerBase+FVector(0,0,3),FVector(38,38,6));
    AddPart(TEXT("tower_pole"),Cylinder,Steel,TowerBase+FVector(0,0,113),FVector(7,7,220));
    AddPart(TEXT("tower_socket"),Cylinder,Dark,TowerBase+FVector(0,0,226),FVector(19,19,10));
    GreenMaterial=MakeIndicator(Indicator,FLinearColor(.015f,1.f,.055f));
    AmberMaterial=MakeIndicator(Indicator,FLinearColor(1.f,.30f,.006f));
    AddPart(TEXT("tower_green"),Cylinder,GreenMaterial,TowerBase+FVector(0,0,239),FVector(18,18,16),FRotator::ZeroRotator,false);
    AddPart(TEXT("tower_divider"),Cylinder,Dark,TowerBase+FVector(0,0,249),FVector(19,19,4));
    AddPart(TEXT("tower_amber"),Cylinder,AmberMaterial,TowerBase+FVector(0,0,259),FVector(18,18,16),FRotator::ZeroRotator,false);
    AddPart(TEXT("tower_cap"),Cylinder,Dark,TowerBase+FVector(0,0,269),FVector(19,19,4));
    for (int32 Index=0;Index<2;++Index) {
        FPhotoeye Eye; Eye.Id=Index==0 ? TEXT("entry_photoeye"):TEXT("exit_photoeye");
        const double Y=Index==0 ? -1050.:1050.;
        Eye.Start=FVector(BodyMaxX+PostOffset-6,Y,BeamZ); Eye.End=FVector(BodyMinX-PostOffset+4,Y,BeamZ);
        Eye.Indicator=MakeIndicator(Indicator,FLinearColor(1.f,.25f,.005f));
        for (int32 EndIndex=0;EndIndex<2;++EndIndex) {
            const double X=EndIndex==0 ? BodyMaxX+PostOffset:BodyMinX-PostOffset;
            const FString Prefix=Eye.Id+(EndIndex==0 ? TEXT("_sensor"):TEXT("_reflector"));
            AddPart(Prefix+TEXT("_foot"),Cube,Yellow,FVector(X,Y,GroundZ+2),FVector(25,25,4));
            AddPart(Prefix+TEXT("_post"),Cube,Steel,FVector(X,Y,(BeamZ+GroundZ)*.5),FVector(7,7,BeamZ-GroundZ));
            AddPart(Prefix+TEXT("_head"),Cube,EndIndex==0 ? Blue:Dark,FVector(X,Y,BeamZ),FVector(12,17,20));
            if (EndIndex==0) {
                AddPart(Prefix+TEXT("_lens"),Cylinder,Dark,Eye.Start,FVector(7,7,1),FRotator(90,0,0));
                // Front-facing side indicator is visible from the approach aisle.
                AddPart(Prefix+TEXT("_occupied"),Cylinder,Eye.Indicator.Get(),FVector(X,Y-8.7,BeamZ+4),FVector(4.5,4.5,.6),FRotator(0,0,90),false);
            } else AddPart(Prefix+TEXT("_reflective_face"),Cube,Reflector,Eye.End,FVector(1,13,15));
        }
        Photoeyes.Add(Eye);
    }
    bInitialized=true; UpdateFromCell(); SetActorHiddenInGame(!bEnabled);
    UE_LOG(LogTemp,Display,TEXT("DF_MECHANISMS_READY fixtures=%d photoeyes=%d meshes=%d thermal_profiles=0 lights=0"),Fixtures.Num(),Photoeyes.Num(),Meshes.Num());
    return true;
}
void ADarkFactoryCellMechanisms::SetEnabled(bool Enabled) {
    bEnabled=Enabled; SetActorHiddenInGame(!Enabled); SetActorTickEnabled(Enabled); UpdateFromCell();
}
void ADarkFactoryCellMechanisms::SetProcessStatus(const FDarkFactoryProcessStatus& Status) {
    ProcessStatus=Status;
    // World pushes process status after its controller update; avoid a duplicate
    // per-frame mechanism/occupancy pass while that update path is active.
    SetActorTickEnabled(bEnabled && !Status.bEnabled);
    // Added only for the gated process; legacy smoke fixtures retain identical
    // geometry, materials, stencil IDs and mesh counts.
    if (Status.bEnabled && bInitialized && !RedMaterial) {
        auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WeldingArc.M_WeldingArc"));
        auto* Paint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
        if (Parent && Paint) {
            RedMaterial=MakeIndicator(Parent,FLinearColor(1.f,.012f,.005f));
            auto* Dark=MakePaint(Paint,FLinearColor(.018f,.023f,.027f),.65f);
            ProcessTowerParts.Add(AddPart(TEXT("tower_red"),Cylinder,RedMaterial,TowerBase+FVector(0,0,279),FVector(18,18,16),FRotator::ZeroRotator,false));
            ProcessTowerParts.Add(AddPart(TEXT("tower_process_cap"),Cylinder,Dark,TowerBase+FVector(0,0,289),FVector(19,19,4)));
        }
    }
    for (auto& Part:ProcessTowerParts) if (Part.IsValid()) Part->SetVisibility(Status.bEnabled);
    UpdateFromCell();
}
void ADarkFactoryCellMechanisms::HideProcessIndicatorsFrom(USceneCaptureComponent2D* Capture) const {
    if (!Capture) return;
    // Keep fixture supports, clamps and all other physical occluders visible.
    // Reapply after enabling process mode because the red segment is lazy.
    for (const auto& Part:Meshes) if (Part) {
        const FString Name=Part->GetName();
        if (Name==TEXT("tower_green") || Name==TEXT("tower_amber") || Name==TEXT("tower_red"))
            Capture->HideComponent(Part);
    }
}
void ADarkFactoryCellMechanisms::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); UpdateFromCell(); }
void ADarkFactoryCellMechanisms::UpdateFromCell() {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactoryCellMechanisms);
    if (!bInitialized || !SourceCell.IsValid()) return;
    const auto* Cell=SourceCell.Get(); AppliedSeconds=Cell->GetMotionTime();
    CycleSeconds=Cell->GetCycleSeconds(); CycleDuration=Cell->GetCycleDurationSeconds(); bRandomized=Cell->IsRobotTimingRandomized();
    DepartureStart=Cell->GetCarriageDepartureStartSeconds();
    OpenStart=DepartureStart-.5; OpenEnd=DepartureStart-.1;
    FirstApproach=CycleDuration; LatestWithdrawal=0;
    bFinite=FMath::IsFinite(AppliedSeconds) && FMath::IsFinite(CycleSeconds);
    for (int32 I=0;I<4;++I) {
        const auto R=Cell->GetRobotTiming(I,AppliedSeconds); bFinite &= R.Valid;
        FirstApproach=FMath::Min(FirstApproach,R.ApproachStartSeconds);
        LatestWithdrawal=FMath::Max(LatestWithdrawal,R.WithdrawEndSeconds);
    }
    Closure=CycleSeconds<OpenStart ? Ease((CycleSeconds-CloseStart)/(CloseEnd-CloseStart))
        :1.-Ease((CycleSeconds-OpenStart)/(OpenEnd-OpenStart));
    if (ProcessStatus.bEnabled) {
        const auto Phase=ProcessStatus.Phase;
        const bool Secured=Phase==EDarkFactoryProcessPhase::Doors || Phase==EDarkFactoryProcessPhase::Adhesive
            || Phase==EDarkFactoryProcessPhase::Welding || Phase==EDarkFactoryProcessPhase::WeldCooling;
        Closure=Secured ? (Phase==EDarkFactoryProcessPhase::Doors ? Ease(ProcessStatus.StageSeconds/.35):1.):0.;
    }
    ArmAngleDegrees=OpenAngle*(1.-Closure);
    const double A=FMath::DegreesToRadians(ArmAngleDegrees);
    for (auto& F:Fixtures) {
        const FVector D(-F.Side*FMath::Cos(A),0,FMath::Sin(A));
        F.PadPosition=F.Pivot+D*F.LengthCm;
        SetSegment(F.Arm.Get(),F.Pivot,F.PadPosition,8.);
        if (F.Pad.IsValid()) F.Pad->SetWorldLocation(F.PadPosition);
        const FVector Elbow=F.Pivot+D*16.;
        SetSegment(F.Rod.Get(),F.ActuatorBase,Elbow,3.);
        SetSegment(F.Toggle.Get(),Elbow,F.Pivot+FVector(F.Side*6,0,-9),5.);
        bFinite &= !F.PadPosition.ContainsNaN();
    }
    bool Green=bEnabled && CycleSeconds>=CloseEnd && CycleSeconds<OpenStart;
    bool Amber=bEnabled && !Green,Red=false;
    if (ProcessStatus.bEnabled) {
        bool Exception=ProcessStatus.Phase==EDarkFactoryProcessPhase::Returning;
        for (const auto Gate:ProcessStatus.Gates)
            Exception |= Gate==EDarkFactoryGateState::Rejected || Gate==EDarkFactoryGateState::Error;
        const bool Working=ProcessStatus.Phase==EDarkFactoryProcessPhase::Arriving
            || ProcessStatus.Phase==EDarkFactoryProcessPhase::Welding || ProcessStatus.Phase==EDarkFactoryProcessPhase::Departing;
        Green=bEnabled && !ProcessStatus.bPaused && !Exception && Working;
        Amber=bEnabled && (ProcessStatus.bPaused || (!Exception && !Working));
        Red=bEnabled && !ProcessStatus.bPaused && Exception && FMath::Fmod(FMath::Max(0.,ProcessStatus.ClockSeconds),1.)<.5;
    }
    if (Green!=bGreen) { GreenMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"),Green ? 12.f:.04f); bGreen=Green; }
    if (Amber!=bAmber) { AmberMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"),Amber ? 12.f:.04f); bAmber=Amber; }
    if (RedMaterial && Red!=bRed) { RedMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"),Red ? 16.f:.04f); bRed=Red; }
    for (auto& Eye:Photoeyes) {
        Eye.BlockingColliders.Reset(); Eye.TestedBoxes.Reset();
        for (const auto& Box:Cell->GetCarriageColliders()) {
            if (!Box) { bFinite=false; continue; }
            FBeamBox B; B.Name=Box->GetName(); B.bEnabled=Box->GetCollisionEnabled()!=ECollisionEnabled::NoCollision;
            B.WorldTransform=Box->GetComponentTransform(); B.HalfExtent=Box->GetUnscaledBoxExtent();
            const bool Valid=!B.WorldTransform.ContainsNaN() && !B.HalfExtent.ContainsNaN(); bFinite &= Valid;
            if (Valid) {
                B.StartLocal=B.WorldTransform.InverseTransformPosition(Eye.Start);
                B.EndLocal=B.WorldTransform.InverseTransformPosition(Eye.End);
                if (B.bEnabled && SegmentBox(B.StartLocal,B.EndLocal,B.HalfExtent)) Eye.BlockingColliders.Add(B.Name);
            }
            Eye.TestedBoxes.Add(B);
        }
        const bool Occupied=!Eye.BlockingColliders.IsEmpty();
        if (Occupied!=Eye.Occupied) {
            Eye.Occupied=Occupied;
            if (Eye.Indicator.IsValid()) Eye.Indicator->SetScalarParameterValue(TEXT("EmissiveStrength"),Occupied ? 10.f:.04f);
        }
    }
}
TSharedRef<FJsonObject> ADarkFactoryCellMechanisms::BuildMetadata() const {
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("model"),TEXT("authored-cell-mechanisms-v1"));
    O->SetStringField(TEXT("scope"),TEXT("Kinematic fixture staging and geometric carriage presence; not clamp-force, optical-sensor, safety-interlock or inspection-quality simulation"));
    O->SetBoolField(TEXT("initialized"),bInitialized); O->SetBoolField(TEXT("enabled"),bEnabled);
    O->SetBoolField(TEXT("finite_state"),bInitialized && bFinite);
    O->SetBoolField(TEXT("randomized_robot_timing"),bRandomized);
    O->SetNumberField(TEXT("motion_seconds"),AppliedSeconds); O->SetNumberField(TEXT("cycle_seconds"),CycleSeconds);
    O->SetNumberField(TEXT("cycle_duration_seconds"),CycleDuration);
    O->SetNumberField(TEXT("mesh_count"),Meshes.Num()); O->SetNumberField(TEXT("fixture_count"),Fixtures.Num());
    O->SetNumberField(TEXT("photoeye_count"),Photoeyes.Num()); O->SetNumberField(TEXT("new_light_count"),0);
    O->SetNumberField(TEXT("thermal_profile_count"),0); O->SetNumberField(TEXT("thermal_stencil_id"),0);
    O->SetNumberField(TEXT("thermal_ambient_fallback_c"),20); O->SetBoolField(TEXT("collision_enabled"),false);
    O->SetNumberField(TEXT("fixture_platform_z_cm"),PlatformZ); O->SetNumberField(TEXT("ground_z_cm"),GroundZ);
    O->SetNumberField(TEXT("close_start_seconds"),CloseStart); O->SetNumberField(TEXT("close_end_seconds"),CloseEnd);
    O->SetNumberField(TEXT("open_start_seconds"),OpenStart); O->SetNumberField(TEXT("open_end_seconds"),OpenEnd);
    O->SetNumberField(TEXT("first_robot_approach_seconds"),FirstApproach); O->SetNumberField(TEXT("latest_robot_withdrawal_seconds"),LatestWithdrawal);
    O->SetNumberField(TEXT("carriage_departure_start_seconds"),DepartureStart);
    O->SetNumberField(TEXT("closed_before_approach_margin_seconds"),FirstApproach-CloseEnd);
    O->SetNumberField(TEXT("opening_after_withdrawal_margin_seconds"),OpenStart-LatestWithdrawal);
    O->SetNumberField(TEXT("open_before_departure_margin_seconds"),DepartureStart-OpenEnd);
    O->SetBoolField(TEXT("process_enabled"),ProcessStatus.bEnabled);
    O->SetNumberField(TEXT("fixture_closure"),Closure); O->SetNumberField(TEXT("fixture_angle_deg"),ArmAngleDegrees);
    O->SetStringField(TEXT("fixture_phase"),Closure<=0 ? TEXT("open") : Closure>=1 ? TEXT("closed") : CycleSeconds<OpenStart ? TEXT("closing"):TEXT("opening"));
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& F:Fixtures) {
        auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("id"),F.Id);
        R->SetArrayField(TEXT("pivot_world_cm"),Vec(F.Pivot)); R->SetArrayField(TEXT("pad_world_cm"),Vec(F.PadPosition));
        R->SetNumberField(TEXT("body_side_x_cm"),F.BodySideX);
        R->SetNumberField(TEXT("pad_to_body_aabb_gap_cm"),F.Side*(F.PadPosition.X-F.BodySideX)-PadWidth*.5);
        R->SetNumberField(TEXT("arm_angle_deg"),ArmAngleDegrees);
        R->SetNumberField(TEXT("arm_length_cm"),F.LengthCm);
        R->SetBoolField(TEXT("surface_contact_bound"),F.bSurfaceBound);
        R->SetStringField(TEXT("contact_mesh"),F.ContactMesh);
        R->SetArrayField(TEXT("surface_contact_world_cm"),Vec(F.SurfaceContact));
        R->SetNumberField(TEXT("closed_surface_gap_cm"),F.SurfaceGapCm);
        R->SetNumberField(TEXT("pad_face_sample_rays"),9);
        Rows.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("fixtures"),Rows);
    auto Tower=MakeShared<FJsonObject>(); Tower->SetArrayField(TEXT("base_world_cm"),Vec(TowerBase));
    Tower->SetBoolField(TEXT("green_run"),bGreen); Tower->SetBoolField(TEXT("amber_transfer_clearance"),bAmber);
    Tower->SetStringField(TEXT("state"),!bEnabled ? TEXT("disabled") : bGreen ? TEXT("secured_cycle"):TEXT("transfer_clearance"));
    Tower->SetStringField(TEXT("meaning"),TEXT("Authored cycle status only; no model quality, fault or industrial safety claim"));
    if (ProcessStatus.bEnabled) {
        bool Exception=ProcessStatus.Phase==EDarkFactoryProcessPhase::Returning;
        for (const auto Gate:ProcessStatus.Gates)
            Exception |= Gate==EDarkFactoryGateState::Rejected || Gate==EDarkFactoryGateState::Error;
        Tower->SetBoolField(TEXT("red_exception"),bRed);
        Tower->SetBoolField(TEXT("exception_active"),Exception);
        Tower->SetBoolField(TEXT("paused"),ProcessStatus.bPaused);
        Tower->SetStringField(TEXT("state"),!bEnabled ? TEXT("disabled"):ProcessStatus.bPaused ? TEXT("paused")
            :Exception ? TEXT("exception"):bGreen ? TEXT("working"):TEXT("inspection_hold"));
        Tower->SetStringField(TEXT("meaning"),TEXT("Process status: green active motion/work, amber inspection/cooling/pause, flashing red rejection/transport error; not a safety system"));
    }
    O->SetObjectField(TEXT("stack_light"),Tower);
    Rows.Reset();
    for (const auto& Eye:Photoeyes) {
        auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("id"),Eye.Id);
        R->SetArrayField(TEXT("beam_start_world_cm"),Vec(Eye.Start)); R->SetArrayField(TEXT("beam_end_world_cm"),Vec(Eye.End));
        R->SetBoolField(TEXT("occupied"),Eye.Occupied); R->SetBoolField(TEXT("indicator_emitting"),bEnabled && Eye.Occupied);
        TArray<TSharedPtr<FJsonValue>> Names; for (const auto& Name:Eye.BlockingColliders) Names.Add(MakeShared<FJsonValueString>(Name));
        R->SetArrayField(TEXT("blocking_carriage_colliders"),Names);
        TArray<TSharedPtr<FJsonValue>> Boxes;
        for (const auto& B:Eye.TestedBoxes) {
            auto Box=MakeShared<FJsonObject>(); Box->SetStringField(TEXT("name"),B.Name); Box->SetBoolField(TEXT("enabled"),B.bEnabled);
            Box->SetArrayField(TEXT("center_world_cm"),Vec(B.WorldTransform.GetLocation()));
            Box->SetArrayField(TEXT("half_extent_cm"),Vec(B.HalfExtent));
            Box->SetArrayField(TEXT("beam_start_local_cm"),Vec(B.StartLocal)); Box->SetArrayField(TEXT("beam_end_local_cm"),Vec(B.EndLocal));
            const FMatrix M=B.WorldTransform.ToInverseMatrixWithScale();
            TArray<TSharedPtr<FJsonValue>> TransformRows;
            for (int32 Axis=0;Axis<3;++Axis) {
                TArray<TSharedPtr<FJsonValue>> Values;
                for (int32 Column=0;Column<4;++Column) Values.Add(MakeShared<FJsonValueNumber>(M.M[Column][Axis]));
                TransformRows.Add(MakeShared<FJsonValueArray>(Values));
            }
            Box->SetArrayField(TEXT("world_to_local_rows"),TransformRows);
            Boxes.Add(MakeShared<FJsonValueObject>(Box));
        }
        R->SetArrayField(TEXT("tested_carriage_boxes"),Boxes);
        R->SetStringField(TEXT("detection"),TEXT("Finite segment versus enabled oriented carriage shell/holder boxes; no phase-label shortcut or world trace"));
        Rows.Add(MakeShared<FJsonValueObject>(R));
    }
    O->SetArrayField(TEXT("photoeyes"),Rows);
    return O;
}
