#include "DarkFactoryProcessScene.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryThermalRadiometry.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Math/RotationMatrix.h"
#include "Serialization/JsonSerializer.h"
#include "Algo/Reverse.h"

namespace {
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& P) {
    return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
}
// Authored supplemental door shells, not triangles extracted from the purchased
// side frames. Separate sheet and window-frame prisms share one mesh/profile.
UStaticMesh* MakeDoor(UObject* Owner,bool Rear,UMaterialInterface* Material) {
    FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
    auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
    auto Tangents=Attributes.GetVertexInstanceTangents(); auto Signs=Attributes.GetVertexInstanceBinormalSigns();
    auto UVs=Attributes.GetVertexInstanceUVs(); UVs.SetNumChannels(1);
    auto Colors=Attributes.GetVertexInstanceColors();
    const auto Group=Description.CreatePolygonGroup(); Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("DoorSheet");
    const double CenterY=Rear ? 35.:-132.,CenterZ=238.;
    auto Face=[&](const TArray<FVector>& Points) {
        if (Points.Num()<3) return;
        const FVector N=FVector::CrossProduct(Points[1]-Points[0],Points[2]-Points[0]).GetSafeNormal();
        const FVector Tangent=(Points[1]-Points[0]).GetSafeNormal(); TArray<FVertexInstanceID> Instances;
        for (const FVector& P:Points) {
            auto V=Description.CreateVertex(); Positions[V]=FVector3f(P.X,P.Y-CenterY,P.Z-CenterZ);
            auto I=Description.CreateVertexInstance(V); Normals[I]=FVector3f(N); Tangents[I]=FVector3f(Tangent);
            Signs[I]=1.f; UVs.Set(I,0,FVector2f((P.Y+220.)/350.,(P.Z-160.)/160.)); Colors[I]=FVector4f(1,1,1,1); Instances.Add(I);
        }
        // UE uses reversed cross-product normals for its left-handed winding
        // (StaticMeshOperations.cpp). Keep outward normals and reverse indices.
        Algo::Reverse(Instances);
        Description.CreatePolygon(Group,Instances);
    };
    auto Prism=[&](TArray<FVector2D> Poly,double HalfThickness=2.3) {
        double Area=0; for (int32 I=0;I<Poly.Num();++I) { const auto A=Poly[I],B=Poly[(I+1)%Poly.Num()]; Area+=A.X*B.Y-B.X*A.Y; }
        if (Area<0) Algo::Reverse(Poly);
        TArray<FVector> Front,Back;
        for (const auto& P:Poly) Front.Add(FVector(HalfThickness,P.X,P.Y));
        for (int32 I=Poly.Num()-1;I>=0;--I) Back.Add(FVector(-HalfThickness,Poly[I].X,Poly[I].Y));
        Face(Front); Face(Back);
        for (int32 I=0;I<Poly.Num();++I) { const auto A=Poly[I],B=Poly[(I+1)%Poly.Num()]; Face({FVector(-HalfThickness,A.X,A.Y),FVector(-HalfThickness,B.X,B.Y),FVector(HalfThickness,B.X,B.Y),FVector(HalfThickness,A.X,A.Y)}); }
    };
    auto Bar=[&](FVector2D A,FVector2D B,double Width) {
        const FVector2D D=(B-A).GetSafeNormal(),N(-D.Y,D.X); const FVector2D H=N*Width*.5;
        Prism({A+H,B+H,B-H,A-H},2.7);
    };
    if (!Rear) {
        Prism({{-198,246},{-67,246},{-65,185},{-78,169},{-127,173},{-164,200},{-188,229}});
        Bar({-198,246},{-190,272},5); Bar({-190,272},{-155,297},5);
        Bar({-155,297},{-113,308},5); Bar({-113,308},{-73,308},5);
        Bar({-73,308},{-69,246},5); Bar({-198,246},{-67,246},5);
        // Raised stamped character line and a modest physical handle.
        Bar({-186,235},{-76,235},1.5); Prism({{-89,239},{-76,239},{-76,234},{-89,234}},4.1);
    } else {
        Prism({{-34,246},{98,246},{105,215},{99,170},{-3,170},{-17,185}});
        Bar({-34,246},{-40,307},5); Bar({-40,307},{10,297},5);
        Bar({10,297},{63,275},5); Bar({63,275},{95,249},5);
        Bar({95,249},{98,246},5); Bar({-34,246},{98,246},5);
        Bar({-25,235},{94,235},1.5); Prism({{69,239},{83,239},{83,234},{69,234}},4.1);
    }
    auto* Mesh=NewObject<UStaticMesh>(Owner,Rear ? TEXT("AuthoredRearDoor"):TEXT("AuthoredFrontDoor"));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Material,TEXT("DoorSheet")));
    UStaticMesh::FBuildMeshDescriptionsParams Params; Params.bFastBuild=true; Params.bBuildSimpleCollision=true;
    Params.bMarkPackageDirty=false; Params.bCommitMeshDescription=false; Params.bAllowCpuAccess=true;
    if (!Mesh->BuildFromMeshDescriptions({&Description},Params)) return nullptr;
    return Mesh;
}
TSharedRef<FJsonObject> DoorGeometryAudit(const UStaticMeshComponent* Door) {
    auto Result=MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("bounds_min_cm"),Vec(Door->Bounds.GetBox().Min));
    Result->SetArrayField(TEXT("bounds_max_cm"),Vec(Door->Bounds.GetBox().Max));
    const UStaticMesh* Mesh=Door->GetStaticMesh(); const auto* RenderData=Mesh ? Mesh->GetRenderData():nullptr;
    if (!RenderData || RenderData->LODResources.IsEmpty()) { Result->SetBoolField(TEXT("render_data_present"),false); return Result; }
    const auto& LOD=RenderData->LODResources[0]; const auto Indices=LOD.IndexBuffer.GetArrayView();
    const auto& Positions=LOD.VertexBuffers.PositionVertexBuffer; const auto& Attributes=LOD.VertexBuffers.StaticMeshVertexBuffer;
    int32 Mismatch=0,Degenerate=0; double PositiveArea=0,NegativeArea=0;
    for (int32 I=0;I+2<Indices.Num();I+=3) {
        const FVector A(Positions.VertexPosition(Indices[I])),B(Positions.VertexPosition(Indices[I+1])),C(Positions.VertexPosition(Indices[I+2]));
        const FVector Cross=FVector::CrossProduct(C-A,B-A); const double Area=.5*Cross.Size();
        if (Area<1.e-6) { ++Degenerate; continue; }
        const FVector Normal=Cross.GetSafeNormal(); const auto Packed=Attributes.VertexTangentZ(Indices[I]);
        const FVector Shade(Packed.X,Packed.Y,Packed.Z);
        if (FVector::DotProduct(Normal,Shade)<.99) ++Mismatch;
        if (Normal.X>.99) PositiveArea+=Area; if (Normal.X<-.99) NegativeArea+=Area;
    }
    Result->SetBoolField(TEXT("render_data_present"),true); Result->SetNumberField(TEXT("triangles"),Indices.Num()/3);
    Result->SetNumberField(TEXT("vertices"),Positions.GetNumVertices()); Result->SetNumberField(TEXT("degenerate_triangles"),Degenerate);
    Result->SetNumberField(TEXT("winding_normal_mismatches"),Mismatch); Result->SetNumberField(TEXT("facing_px_area_cm2"),PositiveArea);
    Result->SetNumberField(TEXT("facing_nx_area_cm2"),NegativeArea); return Result;
}
}

ADarkFactoryProcessScene::ADarkFactoryProcessScene() {
    PrimaryActorTick.bCanEverTick=false;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ProcessRoot")); SetRootComponent(SceneRoot);
}
bool ADarkFactoryProcessScene::Initialize(ADarkFactoryWeldingCell* InCell,int32 Seed) {
    if (bInitialized || !InCell || !InCell->GetCarriageRoot() || !InCell->GetInspectionJoint()) return false;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!Cube || !Cylinder || !Base) return false;
    auto MakeMaterial=[&](FLinearColor Color,float Roughness) {
        auto* MID=UMaterialInstanceDynamic::Create(Base,this); MID->SetVectorParameterValue(TEXT("BaseColor"),Color); MID->SetScalarParameterValue(TEXT("Roughness"),Roughness); return MID;
    };
    DoorMaterial=MakeMaterial(FLinearColor(.34,.37,.40),.46f);
    auto* GlueMaterial=MakeMaterial(FLinearColor(.11,.16,.12),.78f);
    auto* CameraMaterial=MakeMaterial(FLinearColor(.028,.033,.039),.5f);
    auto* LensMaterial=MakeMaterial(FLinearColor(.08,.13,.16),.2f);
    auto* Front=MakeDoor(this,false,DoorMaterial); auto* Rear=MakeDoor(this,true,DoorMaterial);
    if (!Front || !Rear) return false;
    Cell=InCell; RandomSeed=Seed;
    auto Mesh=[&](const FString& Name,UStaticMesh* Asset,UMaterialInterface* Material,USceneComponent* Parent) {
        auto* Part=NewObject<UStaticMeshComponent>(this,*Name); Part->SetupAttachment(Parent);
        Part->SetMobility(EComponentMobility::Movable); Part->SetStaticMesh(Asset); Part->SetMaterial(0,Material);
        Part->SetLightingChannels(true,true,false); Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part->SetCollisionObjectType(ECC_WorldDynamic); Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Part->SetGenerateOverlapEvents(false); Part->RegisterComponent(); return Part;
    };
    auto Surface=[&](UStaticMeshComponent* Part,float Temperature,float Emissivity) {
        FDarkFactoryImportedThermalSurface S; S.Mesh=Part; S.TemperatureC=Temperature; S.Emissivity=Emissivity; ThermalSurfaces.Add(S);
    };
    for (int32 I=0;I<4;++I) {
        const double Side=I<2 ? 1.:-1.; const bool IsRear=(I%2)==1;
        auto* Box=NewObject<UBoxComponent>(this,*FString::Printf(TEXT("DoorPhysicsProxy%d"),I));
        Box->SetupAttachment(Cell->GetCarriageRoot()); Box->SetMobility(EComponentMobility::Movable);
        Box->SetRelativeLocation(FVector(Side*123.,IsRear ? 35.:-132.,238.));
        Box->SetBoxExtent(FVector(5.,IsRear ? 76.:74.,78.)); Box->SetHiddenInGame(true);
        Box->SetCollisionObjectType(ECC_WorldDynamic); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
        Box->SetGenerateOverlapEvents(false); Box->RegisterComponent(); DoorColliders.Add(Box); DoorHome.Add(Box->GetRelativeTransform());
        auto* Door=Mesh(FString::Printf(TEXT("ProcessDoor_%s_%s"),Side>0 ? TEXT("PX"):TEXT("NX"),IsRear ? TEXT("Rear"):TEXT("Front")),IsRear ? Rear:Front,DoorMaterial,Box);
        DoorMeshes.Add(Door); Surface(Door,23.f,.74f);
    }
    for (int32 I=0;I<3;++I) {
        FVector Position,Normal; if (!Cell->GetWeldSite(I,Position,Normal)) return false;
        SpotLocal.Add(Position); SpotNormals.Add(Normal);
        // A narrow strip beside each electrode site keeps the weld contact clear.
        FVector Tangent=FVector::CrossProduct(Normal,FVector::RightVector).GetSafeNormal();
        if (Tangent.IsNearlyZero()) Tangent=FVector::ForwardVector;
        const FVector Adhesive=Position+Tangent*26.+Normal*.65;
        AdhesiveLocal.Add(Adhesive);
        auto* Strip=Mesh(FString::Printf(TEXT("ProcessAdhesive%d"),I),Cube,GlueMaterial,Cell->GetCarriageRoot());
        Strip->SetRelativeLocation(Adhesive); Strip->SetRelativeRotation(FRotationMatrix::MakeFromXZ(Tangent,Normal).Rotator());
        Strip->SetRelativeScale3D(FVector(.40,.055,.011)); AdhesiveMeshes.Add(Strip); Surface(Strip,90.f,1.f);
        AdhesiveInitialC.Add(90.f); AdhesiveTau.Add(12.f); AdhesiveC.Add(90.f);
    }
    CameraGimbal=NewObject<USceneComponent>(this,TEXT("InspectionCameraGimbal"));
    CameraGimbal->SetupAttachment(Cell->GetInspectionJoint()); CameraGimbal->SetMobility(EComponentMobility::Movable);
    CameraGimbal->SetRelativeLocation(FVector(0,0,-24)); CameraGimbal->RegisterComponent();
    CameraHousing=Mesh(TEXT("InspectionCameraHousing"),Cube,CameraMaterial,CameraGimbal);
    CameraHousing->SetRelativeScale3D(FVector(.23,.19,.16)); Surface(CameraHousing,28.f,.93f);
    CameraLens=Mesh(TEXT("InspectionCameraLens"),Cylinder,LensMaterial,CameraGimbal);
    CameraLens->SetRelativeLocation(FVector(15,0,0)); CameraLens->SetRelativeRotation(FRotator(90,0,0));
    CameraLens->SetRelativeScale3D(FVector(.105,.105,.11)); Surface(CameraLens,27.f,.86f);
    Cell->SetProcessControlled(true); bInitialized=true;
    FVector L; FRotator R; float FOV=0; GetInspectionCameraPose(0,L,R,FOV);
    UE_LOG(LogTemp,Display,TEXT("DF_PROCESS_SCENE_READY doors=4 adhesive=3 inspection_camera=R4 thermal_additions=%d original_meshes=%d"),ThermalSurfaces.Num(),Cell->GetMeshCount());
    return true;
}
void ADarkFactoryProcessScene::ResetDoors() {
    for (int32 I=0;I<DoorColliders.Num();++I) {
        auto* Box=DoorColliders[I].Get(); Box->AttachToComponent(Cell->GetCarriageRoot(),FAttachmentTransformRules::KeepRelativeTransform);
        Box->SetRelativeTransform(DoorHome[I],false,nullptr,ETeleportType::TeleportPhysics);
        DoorMeshes[I]->SetRelativeTransform(FTransform::Identity); DoorMeshes[I]->SetVisibility(true);
    }
    bFallenDetached=bFallenResting=false; FallenVelocity=FVector::ZeroVector; FallenRotation=FRotator::ZeroRotator;
}
void ADarkFactoryProcessScene::BeginBody(int32 BodyIndex,double ClockSeconds,int32 FaultOverride,int32 DoorOverride) {
    if (!bInitialized) return;
    ResetDoors(); BodySequence=FMath::Max(0,BodyIndex); BodyBirthSeconds=Clock=LastClock=ClockSeconds; bBodyActive=true;
    const uint32 BodySeed=static_cast<uint32>(RandomSeed)^(static_cast<uint32>(BodySequence)*196613u+0x28f6c419u);
    FRandomStream Random(static_cast<int32>(BodySeed));
    // This random source is separate from robot schedules, optical noise and inference.
    Fault=Random.RandRange(0,2)==0 ? Random.RandRange(1,3):0;
    FaultDoor=Random.RandRange(0,3);
    if (FaultOverride>=0 && FaultOverride<=3) Fault=FaultOverride;
    if (DoorOverride>=0 && DoorOverride<=3) FaultDoor=DoorOverride;
    if (Fault==0) FaultDoor=-1;
    for (int32 I=0;I<3;++I) {
        AdhesiveInitialC[I]=Random.FRandRange(76.f,108.f); AdhesiveTau[I]=Random.FRandRange(7.f,22.f);
        AdhesiveC[I]=AdhesiveInitialC[I];
    }
    Cell->BeginProcessBody(BodySequence,ClockSeconds); UpdateAdhesive();
}
void ADarkFactoryProcessScene::SetCaptureVariation(int32 Seed) {
    if (!bInitialized) return;
    bCaptureVariation=true; CaptureVariationSeed=Seed; FRandomStream Random(Seed);
    for (int32 Side=0;Side<2;++Side) {
        CaptureCameraOffsets[Side]=FVector(Random.FRandRange(-15.f,15.f),Random.FRandRange(-15.f,15.f),Random.FRandRange(-15.f,15.f));
        CaptureTargetOffsets[Side]=FVector(0,Random.FRandRange(-4.f,4.f),Random.FRandRange(-4.f,4.f));
        CaptureFovOffsets[Side]=Random.FRandRange(-2.f,2.f);
    }
    CaptureMaterialGain=Random.FRandRange(.7f,1.1f);
    if (DoorMaterial) DoorMaterial->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.34,.37,.40)*CaptureMaterialGain);
}
void ADarkFactoryProcessScene::EndBody() {
    if (!bInitialized) return;
    bBodyActive=false; ResetDoors();
    for (int32 I=0;I<DoorMeshes.Num();++I) { DoorMeshes[I]->SetVisibility(false); DoorMeshes[I]->SetCollisionEnabled(ECollisionEnabled::NoCollision); DoorColliders[I]->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    for (const auto& Strip:AdhesiveMeshes) { Strip->SetVisibility(false); Strip->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
}
void ADarkFactoryProcessScene::ApplyFrame(double PoseSeconds,double ClockSeconds,double CarriageOffsetCm,bool BodyVisible,bool PermitWelding,double InspectionExtension) {
    if (!bInitialized || !FMath::IsFinite(ClockSeconds)) return;
    const double Delta=FMath::Max(0.,ClockSeconds-LastClock); Clock=ClockSeconds; LastClock=ClockSeconds;
    Cell->SetProcessFrame(PoseSeconds,ClockSeconds,CarriageOffsetCm,BodyVisible,PermitWelding,InspectionExtension);
    UpdateDoors(Delta,BodyVisible && bBodyActive); UpdateAdhesive();
    for (const auto& Strip:AdhesiveMeshes) { Strip->SetVisibility(BodyVisible && bBodyActive); Strip->SetCollisionEnabled(BodyVisible && bBodyActive ? ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision); }
}
void ADarkFactoryProcessScene::UpdateDoors(double DeltaSeconds,bool Visible) {
    for (int32 I=0;I<DoorMeshes.Num();++I) {
        const bool Missing=I==FaultDoor && Fault==3; DoorMeshes[I]->SetVisibility(Visible && !Missing);
        DoorMeshes[I]->SetCollisionEnabled(Visible && !Missing ? ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        DoorColliders[I]->SetCollisionEnabled(Visible && !Missing ? ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    }
    if (!Visible || FaultDoor<0) return;
    auto* Box=DoorColliders[FaultDoor].Get(); const double Side=FaultDoor<2 ? 1.:-1.;
    const double Age=FMath::Max(0.,Clock-BodyBirthSeconds);
    if (Fault==1) {
        // Loose hinge: large visible sag and outward skew, coherent with carriage.
        FTransform Pose=DoorHome[FaultDoor]; Pose.AddToTranslation(FVector(Side*(29.+5.*FMath::Sin(Age*3.)),0,-24.));
        Pose.SetRotation(FRotator(0,Side*28.,Side*11.).Quaternion()); Box->SetRelativeTransform(Pose); return;
    }
    if (Fault!=2) return;
    if (!bFallenDetached && FMath::Abs(Cell->GetCarriageRoot()->GetRelativeLocation().Y)<250.) {
        Box->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        Box->AddWorldOffset(FVector(Side*52.,0,-15.)); FallenVelocity=FVector(Side*150.,0,20.); bFallenDetached=true;
    }
    if (!bFallenDetached || bFallenResting || DeltaSeconds<=0) return;
    // Bounded swept-ballistic proxy, matching the spark system's visible-surface
    // contact convention. It is an illustrative sheet drop, not flexible metal.
    double Remaining=FMath::Min(DeltaSeconds,3.); const int32 Steps=FMath::Max(1,FMath::CeilToInt(Remaining/(1./120.)));
    const double Dt=Remaining/Steps;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ProcessDoorFall),true,this);
    // Exclude only the moving carriage shell, holders and their proxies. The
    // cell also owns the floor/platform meshes, which must remain collidable.
    TArray<USceneComponent*> CarriageChildren;
    Cell->GetCarriageRoot()->GetChildrenComponents(true,CarriageChildren);
    for (auto* Child:CarriageChildren) if (auto* Primitive=Cast<UPrimitiveComponent>(Child)) Query.AddIgnoredComponent(Primitive);
    for (int32 Step=0;Step<Steps && !bFallenResting;++Step) {
        FallenVelocity.Z-=980.*Dt; const FVector Old=Box->GetComponentLocation(),Next=Old+FallenVelocity*Dt;
        FHitResult Hit; const bool Contact=GetWorld()->SweepSingleByChannel(Hit,Old,Next,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(7.f),Query);
        const double FloorZ=14.;
        if (Contact || Next.Z<=FloorZ) {
            const FVector Normal=Contact ? Hit.Normal:FVector::UpVector;
            FVector Position=Contact ? Hit.Location+Normal*1.:FVector(Next.X,Next.Y,FloorZ);
            const double Closing=FVector::DotProduct(FallenVelocity,Normal);
            if (Closing<0) FallenVelocity-=1.23*Closing*Normal;
            FallenVelocity.X*=.48; FallenVelocity.Y*=.48;
            if (Normal.Z>.4 && (FallenVelocity.Size()<95. || FMath::Abs(FallenVelocity.Z)<55.)) {
                bFallenResting=true; FallenVelocity=FVector::ZeroVector;
                FallenRotation=FRotator(90.,Side*14.,0);
                // Authored handle half-thickness4.1cm plus0.25cm clearance.
                Position=Contact ? Hit.ImpactPoint+Normal*4.35:FVector(Next.X,Next.Y,FloorZ);
            }
            Box->SetWorldLocation(Position);
        } else Box->SetWorldLocation(Next);
        if (!bFallenResting) FallenRotation.Pitch=FMath::Min(90.,FallenRotation.Pitch+Dt*130.);
        Box->SetWorldRotation(FallenRotation);
    }
}
void ADarkFactoryProcessScene::UpdateAdhesive() {
    const double Age=FMath::Max(0.,Clock-BodyBirthSeconds);
    for (int32 I=0;I<AdhesiveMeshes.Num();++I) {
        AdhesiveC[I]=20.f+(AdhesiveInitialC[I]-20.f)*FMath::Exp(-Age/AdhesiveTau[I]);
        ThermalSurfaces[4+I].TemperatureC=AdhesiveC[I];
        if (auto* Rig=ThermalRig.Get()) Rig->SetImportedSurfaceTemperature(AdhesiveMeshes[I],AdhesiveC[I]);
    }
}
void ADarkFactoryProcessScene::BindThermalRig(ADarkFactoryThermalRig* Rig) { ThermalRig=Rig; UpdateAdhesive(); }
void ADarkFactoryProcessScene::GetDoorSensorView(int32 Side,FVector& Location,FRotator& Rotation,float& FOV) const {
    Side=FMath::Clamp(Side,0,1); const double Sign=Side==0 ? 1.:-1.;
    Location=FVector(Sign*600.,-60.,280.)+CaptureCameraOffsets[Side];
    const FVector Target=FVector(Sign*105.,-60.,185.)+CaptureTargetOffsets[Side];
    Rotation=(Target-Location).Rotation(); FOV=58.f+CaptureFovOffsets[Side];
}
void ADarkFactoryProcessScene::GetInspectionCameraPose(int32 Spot,FVector& Location,FRotator& Rotation,float& FOV) {
    if (!bInitialized || !SpotLocal.IsValidIndex(Spot)) { Location=FVector::ZeroVector; Rotation=FRotator::ZeroRotator; FOV=50; return; }
    const FVector Site=Cell->GetCarriageRoot()->GetComponentTransform().TransformPosition(SpotLocal[Spot]);
    const auto* Strip=AdhesiveMeshes[Spot].Get();
    const FBox AdhesiveBounds=Strip->GetStaticMesh()->GetBoundingBox();
    const FTransform AdhesiveTransform=Strip->GetComponentTransform();
    TArray<FVector,TInlineAllocator<16>> Corners;
    for (int32 I=0;I<8;++I) {
        const FVector Sign((I&1) ? 1.:-1.,(I&2) ? 1.:-1.,(I&4) ? 1.:-1.);
        // The weld field is12cm;14cm bounds include its surrounding sheet.
        Corners.Add(Site+Sign*14.);
        Corners.Add(AdhesiveTransform.TransformPosition(AdhesiveBounds.GetCenter()+Sign*AdhesiveBounds.GetExtent()));
    }
    FBox Combined(ForceInit); for (const FVector& Corner:Corners) Combined+=Corner;
    const FVector Target=Combined.GetCenter();
    Rotation=(Target-CameraGimbal->GetComponentLocation()).Rotation(); CameraGimbal->SetWorldRotation(Rotation);
    // The physical gimbal aims the same mounted lens. Fit both targets at the
    // real sensor aspect ratio; fixed horizontal widths clipped long vertical glue.
    Location=CameraGimbal->GetComponentTransform().TransformPosition(FVector(21,0,0));
    Rotation=(Target-Location).Rotation();
    constexpr double Aspect=448./256.; double HalfTan=.04;
    for (const FVector& Corner:Corners) {
        const FVector View=Rotation.UnrotateVector(Corner-Location);
        const double Depth=FMath::Max(1.,View.X);
        HalfTan=FMath::Max(HalfTan,FMath::Max(FMath::Abs(View.Y)/Depth,Aspect*FMath::Abs(View.Z)/Depth));
    }
    FOV=FMath::Clamp(FMath::RadiansToDegrees(2.*FMath::Atan(HalfTan*1.25)),8.,110.);
}
void ADarkFactoryProcessScene::GetInspectionObserverView(FVector& Location,FRotator& Rotation) const {
    const FVector Target=CameraGimbal ? CameraGimbal->GetComponentLocation():FVector(0,0,300);
    Location=Target+FVector(-150,-120,70); Rotation=(Target-Location).Rotation();
}
float ADarkFactoryProcessScene::GetMaximumAdhesiveC() const { float Maximum=20; for (float T:AdhesiveC) Maximum=FMath::Max(Maximum,T); return Maximum; }
float ADarkFactoryProcessScene::GetMaximumWeldC() const { return Cell ? Cell->GetMaximumProcessWeldC():20.f; }
TSharedRef<FJsonObject> ADarkFactoryProcessScene::BuildMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("schema"),TEXT("darkfactory-process-scene-v1"));
    O->SetStringField(TEXT("scope"),TEXT("Private offline ground truth only; authored supplemental doors and temperatures; never model input"));
    O->SetNumberField(TEXT("body_index"),BodySequence); O->SetNumberField(TEXT("clock_seconds"),Clock);
    O->SetNumberField(TEXT("body_age_seconds"),FMath::Max(0.,Clock-BodyBirthSeconds)); O->SetNumberField(TEXT("seed"),RandomSeed);
    O->SetNumberField(TEXT("fault_code"),Fault); O->SetNumberField(TEXT("fault_door"),FaultDoor);
    O->SetStringField(TEXT("door_state"),Fault==0 ? TEXT("seated"):Fault==1 ? TEXT("loose"):Fault==2 ? TEXT("fallen"):TEXT("missing"));
    O->SetBoolField(TEXT("body_active"),bBodyActive); O->SetBoolField(TEXT("fallen_detached"),bFallenDetached); O->SetBoolField(TEXT("fallen_resting"),bFallenResting);
    O->SetBoolField(TEXT("offline_capture_variation"),bCaptureVariation);
    if (bCaptureVariation) { O->SetNumberField(TEXT("capture_variation_seed"),CaptureVariationSeed); O->SetNumberField(TEXT("door_material_gain"),CaptureMaterialGain); }
    O->SetNumberField(TEXT("thermal_profiles_added"),ThermalSurfaces.Num()); O->SetNumberField(TEXT("inspection_robot_index"),3);
    O->SetNumberField(TEXT("inspection_extension"),Cell ? Cell->GetInspectionExtension():0.);
    O->SetNumberField(TEXT("maximum_adhesive_c"),GetMaximumAdhesiveC());
    O->SetNumberField(TEXT("maximum_adhesive_apparent_c"),GetMaximumAdhesiveC());
    O->SetNumberField(TEXT("maximum_weld_c"),GetMaximumWeldC());
    O->SetNumberField(TEXT("maximum_weld_apparent_c"),DarkFactoryThermal::ApparentTemperatureC(GetMaximumWeldC(),.74,20.));
    TArray<TSharedPtr<FJsonValue>> Doors,Strips;
    for (int32 I=0;I<DoorMeshes.Num();++I) {
        auto D=MakeShared<FJsonObject>(); D->SetStringField(TEXT("id"),DoorMeshes[I]->GetName()); D->SetBoolField(TEXT("visible"),DoorMeshes[I]->IsVisible());
        D->SetArrayField(TEXT("position_cm"),Vec(DoorMeshes[I]->GetComponentLocation())); D->SetArrayField(TEXT("rotation_deg"),Vec(FVector(DoorMeshes[I]->GetComponentRotation().Pitch,DoorMeshes[I]->GetComponentRotation().Yaw,DoorMeshes[I]->GetComponentRotation().Roll)));
        D->SetObjectField(TEXT("geometry"),DoorGeometryAudit(DoorMeshes[I]));
        Doors.Add(MakeShared<FJsonValueObject>(D));
    }
    for (int32 I=0;I<AdhesiveMeshes.Num();++I) {
        auto S=MakeShared<FJsonObject>(); S->SetNumberField(TEXT("index"),I); S->SetNumberField(TEXT("surface_c"),AdhesiveC[I]); S->SetNumberField(TEXT("apparent_c"),AdhesiveC[I]);
        S->SetNumberField(TEXT("initial_c"),AdhesiveInitialC[I]); S->SetNumberField(TEXT("cooling_tau_seconds"),AdhesiveTau[I]); S->SetNumberField(TEXT("emissivity"),1.);
        S->SetArrayField(TEXT("position_cm"),Vec(AdhesiveMeshes[I]->GetComponentLocation())); S->SetNumberField(TEXT("stencil_id"),AdhesiveMeshes[I]->CustomDepthStencilValue);
        Strips.Add(MakeShared<FJsonValueObject>(S));
    }
    O->SetArrayField(TEXT("doors"),Doors); O->SetArrayField(TEXT("adhesive"),Strips); return O;
}
