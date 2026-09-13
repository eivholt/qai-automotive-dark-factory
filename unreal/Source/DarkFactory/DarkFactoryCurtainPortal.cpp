#include "DarkFactoryCurtainPortal.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Dom/JsonObject.h"

namespace {
constexpr float BottomClearanceCm=8.f;
constexpr float NativeFloorRecessCm=.2f; // Avoid coplanar overlap with the imported tile surface.
constexpr float StripThicknessCm=1.6f;
constexpr float SegmentMassKg=.65f;
constexpr float HingeLimitDegrees=82.f;
// Measured source upper rails: world centres X=-49.70725/+58.71854 cm,
// top Z=90.97492 cm; source ground-tile top Z=6.3477106 cm. World places
// the portal on that ground datum, centered on the rail gauge at X=4.505665.
constexpr float RailHalfGaugeCm=54.212878f;
constexpr float RailWidthCm=30.0364f;
constexpr float RailHeightCm=13.332f;
constexpr float RailTopAboveFloorCm=84.62721f;
constexpr float RailStripClearanceCm=2.f;
constexpr float SegmentEndGapCm=.3f;
constexpr float SlidingFriction=.08f;
constexpr float StaticFriction=.10f;
constexpr int32 PositionIterations=32;
constexpr int32 VelocityIterations=12;
FTransform RigidTransform(const UStaticMeshComponent* Mesh) {
    FTransform Transform=Mesh->GetComponentTransform();
    Transform.RemoveScaling(); // Constraint reference frames use the unscaled body frame.
    return Transform;
}
}

ADarkFactoryCurtainPortal::ADarkFactoryCurtainPortal() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("CurtainPortalRoot"));
    SceneRoot->SetMobility(EComponentMobility::Movable);
    SetRootComponent(SceneRoot);
}

UStaticMeshComponent* ADarkFactoryCurtainPortal::Box(const FString& Name,
    const FVector& Center,const FVector& Size,int32 MaterialIndex,bool Simulated) {
    auto* Mesh=NewObject<UStaticMeshComponent>(this,*Name);
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(Cube);
    Mesh->SetRelativeLocation(Center);
    Mesh->SetRelativeScale3D(Size/100.f);
    Mesh->SetMaterial(0,Materials[MaterialIndex]);
    Mesh->SetLightingChannels(true,true,false);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Mesh->SetCollisionObjectType(Simulated ? ECC_PhysicsBody:ECC_WorldStatic);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    if (Simulated) {
        // Carriage boxes are kinematic WorldDynamic bodies. Adjacent strips and
        // their own segments ignore PhysicsBody, avoiding an overlapping-chain
        // contact explosion; hinge constraints still join each physical segment.
        Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
        Mesh->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
        // A low-friction contact proxy lets the strip slide over the sparse car
        // shell instead of dragging a caught segment through its hinge chain.
        // These are illustrative contact coefficients, not measured PVC values.
        Mesh->SetPhysMaterialOverride(StripContactMaterial);
        Mesh->SetLinearDamping(.5f);
        Mesh->SetAngularDamping(3.2f);
        Mesh->BodyInstance.SetPositionSolverIterationCount(PositionIterations);
        Mesh->BodyInstance.SetVelocitySolverIterationCount(VelocityIterations);
        Mesh->SetUseCCD(true);
        Mesh->SetNotifyRigidBodyCollision(true);
        Mesh->OnComponentHit.AddDynamic(this,&ADarkFactoryCurtainPortal::OnStripHit);
    } else {
        // Floors/frames support the strips. The prescribed carriage is not
        // blocked by a tunnel cap during an offscreen reset.
        Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
    }
    Mesh->RegisterComponent();
    if (Simulated) {
        StripMeshes.Add(Mesh);
        RestTransforms.Add(Mesh->GetComponentTransform());
        Mesh->SetMassOverrideInKg(NAME_None,SegmentMassKg,true);
        Mesh->SetEnableGravity(true);
        Mesh->SetSimulatePhysics(true);
        Mesh->SetPhysicsMaxAngularVelocityInDegrees(360.f,false);
        Mesh->SetMaxDepenetrationVelocity(NAME_None,800.f);
    } else StaticMeshes.Add(Mesh);
    FDarkFactoryImportedThermalSurface Surface;
    Surface.Mesh=Mesh; Surface.TemperatureC=20.f;
    Surface.Emissivity=Simulated ? .95f:.88f;
    Surface.CoreEdgeDropC=0;
    ThermalSurfaces.Add(Surface);
    return Mesh;
}

bool ADarkFactoryCurtainPortal::Initialize(float OpeningWidthCm,float OpeningHeightCm,
    float TunnelLengthCm,int32 StripCount,int32 SegmentsPerStrip) {
    if (bInitialized || !FMath::IsFinite(OpeningWidthCm) || !FMath::IsFinite(OpeningHeightCm) ||
        !FMath::IsFinite(TunnelLengthCm) || OpeningWidthCm<300.f || OpeningWidthCm>600.f ||
        OpeningHeightCm<340.f || OpeningHeightCm>600.f || TunnelLengthCm<800.f || TunnelLengthCm>1400.f ||
        StripCount<12 || StripCount>18 || SegmentsPerStrip<2 || SegmentsPerStrip>3 ||
        StripCount*SegmentsPerStrip>44 || !GetActorScale3D().Equals(FVector::OneVector,.0001f) ||
        GetActorTransform().ContainsNaN()) {
        UE_LOG(LogTemp,Error,TEXT("DF_CURTAIN_FAILED invalid dimensions, scale, body budget or repeated initialization"));
        return false;
    }
    Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!Cube || !Parent) {
        UE_LOG(LogTemp,Error,TEXT("DF_CURTAIN_FAILED missing cube or generated factory material"));
        return false;
    }
    StripContactMaterial=NewObject<UPhysicalMaterial>(this,TEXT("CurtainSlidingContact"));
    StripContactMaterial->Friction=SlidingFriction;
    StripContactMaterial->StaticFriction=StaticFriction;
    StripContactMaterial->Restitution=0.f;
    StripContactMaterial->bOverrideFrictionCombineMode=true;
    StripContactMaterial->FrictionCombineMode=EFrictionCombineMode::Min;
    StripContactMaterial->bOverrideRestitutionCombineMode=true;
    StripContactMaterial->RestitutionCombineMode=EFrictionCombineMode::Min;
    const FLinearColor Colors[]={FLinearColor(.055f,.08f,.105f),
        FLinearColor(.16f,.25f,.30f),FLinearColor(.34f,.19f,.105f),
        FLinearColor(.66f,.43f,.035f)};
    for (const auto& Color:Colors) {
        auto* Material=UMaterialInstanceDynamic::Create(Parent,this);
        Material->SetVectorParameterValue(TEXT("BaseColor"),Color);
        Material->SetScalarParameterValue(TEXT("Roughness"),.78f);
        Materials.Add(Material);
    }
    WidthCm=OpeningWidthCm; HeightCm=OpeningHeightCm; TunnelCm=TunnelLengthCm;
    Strips=StripCount; Segments=SegmentsPerStrip;
    // Ten static opaque meshes per portal: 42 default strip bodies + 10 = 52
    // thermal profiles. The measured rail continuations add four for the pair.
    auto* Header=Box(TEXT("CurtainHeader"),FVector(0,0,HeightCm+15),FVector(WidthCm+40,36,30),0,false);
    for (int32 Side=-1;Side<=1;Side+=2) {
        Box(FString::Printf(TEXT("CurtainJamb%d"),Side),FVector(Side*(WidthCm*.5f+10),0,HeightCm*.5f),FVector(20,36,HeightCm),0,false);
        Box(FString::Printf(TEXT("TunnelWall%d"),Side),FVector(Side*(WidthCm*.5f+10),TunnelCm*.5f,(HeightCm+30)*.5f),FVector(20,TunnelCm,HeightCm+30),0,false);
    }
    Box(TEXT("TunnelCeiling"),FVector(0,TunnelCm*.5f,HeightCm+40),FVector(WidthCm+40,TunnelCm,20),0,false);
    Box(TEXT("TunnelResetCover"),FVector(0,TunnelCm,HeightCm*.5f+15),FVector(WidthCm+40,20,HeightCm+30),0,false);
    Box(TEXT("PortalFloor"),FVector(0,TunnelCm*.5f,-5-NativeFloorRecessCm),FVector(WidthCm+60,TunnelCm+80,10),0,false);
    for (int32 Side=-1;Side<=1;Side+=2)
        Box(FString::Printf(TEXT("TunnelRail%d"),Side),
            FVector(Side*RailHalfGaugeCm,TunnelCm*.5f,RailTopAboveFloorCm-RailHeightCm*.5f),
            FVector(RailWidthCm,TunnelCm,RailHeightCm),3,false);
    const float Pitch=WidthCm/Strips;
    for (int32 Strip=0;Strip<Strips;++Strip) {
        UStaticMeshComponent* Previous=Header;
        const float X=-WidthCm*.5f+Pitch*(Strip+.5f);
        // Include the central walk plate between rails. Leaving these six strips
        // floor-length would start them visibly inside the raised track bed.
        const bool AboveRailBed=FMath::Abs(X)-(Pitch-.35f)*.5f < RailHalfGaugeCm+RailWidthCm*.5f;
        const float Bottom=AboveRailBed ? RailTopAboveFloorCm+RailStripClearanceCm:BottomClearanceCm;
        const float Step=(HeightCm-Bottom)/Segments;
        if (AboveRailBed) { ++RailCutStrips; ShortenedStripIndices.Add(Strip); }
        for (int32 Segment=0;Segment<Segments;++Segment) {
            const FVector HingeLocal(X,0,HeightCm-Step*Segment);
            const FVector Center(X,0,HeightCm-Step*(Segment+.5f));
            auto* Mesh=Box(FString::Printf(TEXT("CurtainStrip%02dSegment%d"),Strip,Segment),
                Center,FVector(Pitch-.35f,StripThicknessCm,Step-2*SegmentEndGapCm),Strip%4==0 ? 2:1,true);
            auto* Joint=NewObject<UPhysicsConstraintComponent>(this,
                *FString::Printf(TEXT("CurtainHinge%02dSegment%d"),Strip,Segment));
            Joint->SetupAttachment(SceneRoot);
            Joint->SetRelativeLocation(HingeLocal);
            Joint->SetRelativeRotation(FRotator::ZeroRotator); // Twist axis X: deflection through +/-Y.
            Joint->SetLinearXLimit(LCM_Locked,0);
            Joint->SetLinearYLimit(LCM_Locked,0);
            Joint->SetLinearZLimit(LCM_Locked,0);
            Joint->SetAngularSwing1Limit(ACM_Locked,0);
            Joint->SetAngularSwing2Limit(ACM_Locked,0);
            Joint->SetAngularTwistLimit(ACM_Limited,HingeLimitDegrees);
            Joint->SetDisableCollision(true);
            Joint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);
            Joint->SetOrientationDriveTwistAndSwing(true,false);
            Joint->SetAngularVelocityDriveTwistAndSwing(true,false);
            Joint->SetAngularOrientationTarget(FRotator::ZeroRotator);
            Joint->SetAngularVelocityTarget(FVector::ZeroVector);
            Joint->SetAngularDriveParams(12.f,8.f,300.f); // Weak hinge return, gravity supplies most restoration.
            // Large rotations plus simultaneous car contacts need the more
            // convergent solve. Equal-size/mass segments need no conditioning.
            Joint->ConstraintInstance.ProfileInstance.bUseLinearJointSolver=false;
            Joint->ConstraintInstance.DisableMassConditioning();
            Joint->ConstraintInstance.SetShockPropagationParams(false,0.f);
            // Post-solve projection can teleport one anchor into another car
            // contact and stretch the next link. Resolve contacts/joints in the
            // solver instead; retain real error telemetry rather than snapping.
            Joint->SetProjectionEnabled(false);
            Joint->RegisterComponent();
            Joint->SetConstrainedComponents(Previous,NAME_None,Mesh,NAME_None);
            Constraints.Add(Joint);
            FJointFrames Frames;
            Frames.Parent=Previous; Frames.Child=Mesh;
            Frames.ParentFrame=Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1);
            Frames.ChildFrame=Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2);
            JointFrames.Add(Frames);
            Previous=Mesh;
        }
    }
    bInitialized=true; bSimulationEnabled=true;
    UE_LOG(LogTemp,Display,TEXT("DF_CURTAIN_READY bodies=%d constraints=%d profiles=%d opening_cm=%.0fx%.0f tunnel_cm=%.0f collision=Chaos_rigid_segments"),
        GetDynamicBodyCount(),GetConstraintCount(),ThermalSurfaces.Num(),WidthCm,HeightCm,TunnelCm);
    return true;
}

void ADarkFactoryCurtainPortal::RestoreConstraints() {
    for (int32 Index=0;Index<Constraints.Num();++Index) {
        auto* Joint=Constraints[Index].Get();
        const auto& Frames=JointFrames[Index];
        if (!Frames.Parent.IsValid() || !Frames.Child.IsValid()) continue;
        Joint->SetConstrainedComponents(Frames.Parent.Get(),NAME_None,Frames.Child.Get(),NAME_None);
        // SetConstrainedComponents derives frames from the fixed scene component.
        // Restore the original BODY-local anchors when resuming a deflected curtain.
        Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame1,Frames.ParentFrame);
        Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame2,Frames.ChildFrame);
    }
}

void ADarkFactoryCurtainPortal::SetSimulationEnabled(bool Enabled) {
    if (!bInitialized || bSimulationEnabled==Enabled) return;
    if (!Enabled) for (UPhysicsConstraintComponent* Joint:Constraints) Joint->BreakConstraint();
    for (UStaticMeshComponent* Mesh:StripMeshes) {
        if (!Enabled) {
            Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
        Mesh->SetSimulatePhysics(Enabled);
        if (Enabled) Mesh->WakeAllRigidBodies();
    }
    bSimulationEnabled=Enabled;
    if (Enabled) RestoreConstraints();
}

void ADarkFactoryCurtainPortal::ResetSimulation() {
    if (!bInitialized) return;
    const bool WasEnabled=bSimulationEnabled;
    SetSimulationEnabled(false);
    for (int32 Index=0;Index<StripMeshes.Num();++Index)
        StripMeshes[Index]->SetWorldTransform(RestTransforms[Index],false,nullptr,ETeleportType::TeleportPhysics);
    CarriageContactCount=0; PeakDeflectionDegrees=0; PeakAnchorErrorCm=0; PeakLinearSpeedCmS=0;
    PeakAnchorSegment.Reset(); ContactCounts.Reset();
    bFinitePhysicsState=true;
    if (WasEnabled) SetSimulationEnabled(true);
}

float ADarkFactoryCurtainPortal::GetCurrentDeflectionDegrees() const {
    float Maximum=0;
    for (int32 Index=0;Index<StripMeshes.Num();++Index) {
        const FQuat Rotation=StripMeshes[Index]->GetComponentQuat();
        if (Rotation.ContainsNaN()) return TNumericLimits<float>::Max();
        Maximum=FMath::Max(Maximum,float(FMath::RadiansToDegrees(Rotation.AngularDistance(RestTransforms[Index].GetRotation()))));
    }
    return Maximum;
}

float ADarkFactoryCurtainPortal::GetMaximumAnchorErrorCm() const {
    float Maximum=0;
    for (const auto& Frames:JointFrames) {
        if (!Frames.Parent.IsValid() || !Frames.Child.IsValid()) continue;
        const FVector A=RigidTransform(Frames.Parent.Get()).TransformPosition(Frames.ParentFrame.GetLocation());
        const FVector B=RigidTransform(Frames.Child.Get()).TransformPosition(Frames.ChildFrame.GetLocation());
        Maximum=FMath::Max(Maximum,float(FVector::Distance(A,B)));
    }
    return Maximum;
}

void ADarkFactoryCurtainPortal::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!bInitialized) return;
    PeakDeflectionDegrees=FMath::Max(PeakDeflectionDegrees,GetCurrentDeflectionDegrees());
    for (const auto& Frames:JointFrames) {
        if (!Frames.Parent.IsValid() || !Frames.Child.IsValid()) continue;
        const FVector A=RigidTransform(Frames.Parent.Get()).TransformPosition(Frames.ParentFrame.GetLocation());
        const FVector B=RigidTransform(Frames.Child.Get()).TransformPosition(Frames.ChildFrame.GetLocation());
        const float Error=FVector::Distance(A,B);
        if (Error>PeakAnchorErrorCm) {
            PeakAnchorErrorCm=Error;
            PeakAnchorSegment=Frames.Child->GetName();
        }
    }
    for (UStaticMeshComponent* Mesh:StripMeshes) {
        const FVector Velocity=Mesh->GetPhysicsLinearVelocity();
        bFinitePhysicsState &= !Mesh->GetComponentTransform().ContainsNaN() && !Velocity.ContainsNaN();
        if (!Velocity.ContainsNaN()) PeakLinearSpeedCmS=FMath::Max(PeakLinearSpeedCmS,float(Velocity.Size()));
    }
}

void ADarkFactoryCurtainPortal::OnStripHit(UPrimitiveComponent*,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,FVector,const FHitResult&) {
    if (!bSimulationEnabled || !OtherComponent) return;
    const FName Name=OtherComponent->GetFName();
    // Bounded diagnostics include our tunnel/frame contacts as well as the car.
    if (ContactCounts.Contains(Name) || ContactCounts.Num()<64) ++ContactCounts.FindOrAdd(Name);
    if (OtherActor!=this && OtherComponent->GetCollisionObjectType()==ECC_WorldDynamic)
        ++CarriageContactCount;
}

TSharedRef<FJsonObject> ADarkFactoryCurtainPortal::BuildPhysicsMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),TEXT("Chaos rigid strip segments with bounded twist hinges; no prescribed strip animation"));
    Result->SetBoolField(TEXT("simulation_enabled"),bSimulationEnabled);
    Result->SetBoolField(TEXT("finite_physics_state"),bFinitePhysicsState);
    Result->SetNumberField(TEXT("dynamic_bodies"),GetDynamicBodyCount());
    Result->SetNumberField(TEXT("constraints"),GetConstraintCount());
    Result->SetNumberField(TEXT("thermal_profiles"),ThermalSurfaces.Num());
    Result->SetNumberField(TEXT("opening_width_cm"),WidthCm);
    Result->SetNumberField(TEXT("opening_height_cm"),HeightCm);
    Result->SetNumberField(TEXT("tunnel_length_cm"),TunnelCm);
    auto VectorJson=[](const FVector& V) {
        return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
    };
    const FTransform Pose=GetActorTransform();
    Result->SetArrayField(TEXT("actor_origin_cm"),VectorJson(Pose.GetLocation()));
    Result->SetNumberField(TEXT("actor_yaw_degrees"),GetActorRotation().Yaw);
    Result->SetNumberField(TEXT("portal_ground_datum_world_z_cm"),Pose.TransformPosition(FVector::ZeroVector).Z);
    Result->SetNumberField(TEXT("native_floor_recess_cm"),NativeFloorRecessCm);
    Result->SetNumberField(TEXT("portal_floor_top_world_z_cm"),Pose.TransformPosition(FVector(0,0,-NativeFloorRecessCm)).Z);
    Result->SetNumberField(TEXT("world_rail_top_z_cm"),Pose.TransformPosition(FVector(0,0,RailTopAboveFloorCm)).Z);
    Result->SetNumberField(TEXT("rail_top_above_portal_floor_cm"),RailTopAboveFloorCm);
    Result->SetNumberField(TEXT("rail_gauge_cm"),2*RailHalfGaugeCm);
    Result->SetNumberField(TEXT("rail_width_cm"),RailWidthCm);
    Result->SetNumberField(TEXT("rail_height_cm"),RailHeightCm);
    Result->SetNumberField(TEXT("rail_continuation_count"),2);
    Result->SetNumberField(TEXT("rail_continuation_length_cm"),TunnelCm);
    Result->SetNumberField(TEXT("rail_cut_strips"),RailCutStrips);
    TArray<TSharedPtr<FJsonValue>> CutIndices;
    for (int32 Index:ShortenedStripIndices) CutIndices.Add(MakeShared<FJsonValueNumber>(Index));
    Result->SetArrayField(TEXT("shortened_strip_indices"),CutIndices);
    Result->SetNumberField(TEXT("nominal_outer_strip_ground_clearance_cm"),BottomClearanceCm);
    Result->SetNumberField(TEXT("outer_strip_rest_ground_clearance_cm"),BottomClearanceCm+SegmentEndGapCm);
    Result->SetNumberField(TEXT("outer_strip_rest_native_floor_clearance_cm"),BottomClearanceCm+SegmentEndGapCm+NativeFloorRecessCm);
    Result->SetNumberField(TEXT("nominal_cut_strip_rail_clearance_cm"),RailStripClearanceCm);
    Result->SetNumberField(TEXT("cut_strip_rest_rail_clearance_cm"),RailStripClearanceCm+SegmentEndGapCm);
    const FBox Opening=FBox(FVector(-WidthCm*.5f,0,0),FVector(WidthCm*.5f,0,HeightCm)).TransformBy(Pose);
    Result->SetArrayField(TEXT("opening_world_min_cm"),VectorJson(Opening.Min));
    Result->SetArrayField(TEXT("opening_world_max_cm"),VectorJson(Opening.Max));
    TArray<TSharedPtr<FJsonValue>> Rails;
    for (int32 Side=-1;Side<=1;Side+=2) {
        auto Rail=MakeShared<FJsonObject>();
        Rail->SetArrayField(TEXT("center_world_cm"),VectorJson(Pose.TransformPosition(FVector(Side*RailHalfGaugeCm,TunnelCm*.5f,RailTopAboveFloorCm-RailHeightCm*.5f))));
        Rail->SetArrayField(TEXT("start_top_world_cm"),VectorJson(Pose.TransformPosition(FVector(Side*RailHalfGaugeCm,0,RailTopAboveFloorCm))));
        Rail->SetArrayField(TEXT("end_top_world_cm"),VectorJson(Pose.TransformPosition(FVector(Side*RailHalfGaugeCm,TunnelCm,RailTopAboveFloorCm))));
        Rails.Add(MakeShared<FJsonValueObject>(Rail));
    }
    Result->SetArrayField(TEXT("rail_continuations"),Rails);
    Result->SetNumberField(TEXT("strips"),Strips);
    Result->SetNumberField(TEXT("segments_per_strip"),Segments);
    Result->SetNumberField(TEXT("mass_per_segment_kg"),SegmentMassKg);
    Result->SetNumberField(TEXT("hinge_limit_degrees"),HingeLimitDegrees);
    Result->SetNumberField(TEXT("sliding_friction"),SlidingFriction);
    Result->SetNumberField(TEXT("static_friction"),StaticFriction);
    Result->SetNumberField(TEXT("restitution"),0);
    Result->SetNumberField(TEXT("position_solver_iterations"),PositionIterations);
    Result->SetNumberField(TEXT("velocity_solver_iterations"),VelocityIterations);
    Result->SetBoolField(TEXT("linear_joint_solver"),false);
    Result->SetBoolField(TEXT("constraint_projection"),false);
    Result->SetNumberField(TEXT("carriage_contact_events"),CarriageContactCount);
    Result->SetNumberField(TEXT("current_deflection_degrees"),GetCurrentDeflectionDegrees());
    Result->SetNumberField(TEXT("peak_deflection_degrees"),PeakDeflectionDegrees);
    Result->SetNumberField(TEXT("current_anchor_error_cm"),GetMaximumAnchorErrorCm());
    Result->SetNumberField(TEXT("peak_anchor_error_cm"),PeakAnchorErrorCm);
    Result->SetStringField(TEXT("peak_anchor_segment"),PeakAnchorSegment);
    auto Contacts=MakeShared<FJsonObject>();
    for (const auto& Pair:ContactCounts) Contacts->SetNumberField(Pair.Key.ToString(),Pair.Value);
    Result->SetObjectField(TEXT("contact_events_by_component"),Contacts);
    Result->SetNumberField(TEXT("peak_linear_speed_cm_per_second"),PeakLinearSpeedCmS);
    Result->SetStringField(TEXT("tuning_scope"),TEXT("Illustrative masses, dimensions and damping; not calibrated PVC material mechanics; adjacent strips do not collide"));
    return Result;
}

void ADarkFactoryCurtainPortal::EndPlay(const EEndPlayReason::Type Reason) {
    for (UPhysicsConstraintComponent* Joint:Constraints) if (Joint) Joint->BreakConstraint();
    Super::EndPlay(Reason);
}
