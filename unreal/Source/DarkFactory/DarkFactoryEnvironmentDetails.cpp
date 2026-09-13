#include "DarkFactoryEnvironmentDetails.h"
#include "DarkFactoryWeldingCell.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"

namespace {
constexpr double FloorZ=6.3477106;
constexpr int32 MaximumInstances=512;
TArray<TSharedPtr<FJsonValue>> VJson(const FVector& V) {
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
}
ADarkFactoryEnvironmentDetails::ADarkFactoryEnvironmentDetails() {
    PrimaryActorTick.bCanEverTick=false;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("EnvironmentDetailsRoot")); SetRootComponent(SceneRoot);
}
UInstancedStaticMeshComponent* ADarkFactoryEnvironmentDetails::Batch(EFinish Finish,bool Round) {
    for (const auto& Key:BatchKeys) if (Key.Finish==Finish && Key.Round==Round) return Batches[Key.Component];
    auto* Mesh=NewObject<UInstancedStaticMeshComponent>(this,*FString::Printf(TEXT("EnvironmentBatch%d"),Batches.Num()));
    Mesh->SetupAttachment(SceneRoot.Get()); Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(Round ? Cylinder.Get():Cube.Get()); Mesh->SetMaterial(0,Materials[static_cast<int32>(Finish)]);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false); Mesh->SetCastHiddenShadow(false);
    Mesh->SetLightingChannels(true,true,false); // Primitive channels are distinct from view channels.
    Mesh->SetRenderCustomDepth(false); Mesh->SetCustomDepthStencilValue(0);
    Mesh->SetAffectDynamicIndirectLighting(false); Mesh->SetAffectDistanceFieldLighting(false);
    Mesh->SetEmissiveLightSource(false); Mesh->SetVisibleInRayTracing(false);
    Mesh->bVisibleInReflectionCaptures=false; Mesh->bVisibleInRealTimeSkyCaptures=false; Mesh->bVisibleInReflections=false;
    Mesh->SetReceivesDecals(false); Mesh->SetVisibility(false);
    AddInstanceComponent(Mesh); Mesh->RegisterComponent();
    FBatch Key; Key.Finish=Finish; Key.Round=Round; Key.Component=Batches.Add(Mesh); BatchKeys.Add(Key);
    return Mesh;
}
void ADarkFactoryEnvironmentDetails::Instance(int32 Feature,EFinish Finish,const FTransform& T,bool Round) {
    if (!Features.IsValidIndex(Feature) || T.ContainsNaN() || T.GetScale3D().GetMin()<=0 || TotalInstances>=MaximumInstances) { bFinite=false; return; }
    Batch(Finish,Round)->AddInstance(T);
    Features[Feature].Bounds+=(Round ? Cylinder.Get():Cube.Get())->GetBoundingBox().TransformBy(T);
    ++Features[Feature].Instances; ++TotalInstances;
}
void ADarkFactoryEnvironmentDetails::Box(int32 Feature,EFinish Finish,FVector Center,FVector Size,FRotator Rotation) {
    Instance(Feature,Finish,FTransform(Rotation,Center,Size/100.),false);
}
void ADarkFactoryEnvironmentDetails::Bar(int32 Feature,EFinish Finish,FVector A,FVector B,float Width,float Depth) {
    const FVector Delta=B-A;
    Box(Feature,Finish,(A+B)*.5,FVector(Delta.Size(),Width,Depth),Delta.Rotation());
}
void ADarkFactoryEnvironmentDetails::Pipe(int32 Feature,EFinish Finish,FVector A,FVector B,float Diameter) {
    const FVector Delta=B-A;
    if (Delta.IsNearlyZero()) { bFinite=false; return; }
    Instance(Feature,Finish,FTransform(FRotationMatrix::MakeFromZ(Delta).ToQuat(),(A+B)*.5,
        FVector(Diameter/100.,Diameter/100.,Delta.Size()/100.)),true);
}
void ADarkFactoryEnvironmentDetails::StationSign(int32 Number,FVector Center,FVector Normal) {
    StationCenters.Add(Center);
    Normal.Z=0; Normal.Normalize();
    const FVector Right=FVector::CrossProduct(FVector::UpVector,Normal).GetSafeNormal();
    const FTransform T(FRotationMatrix::MakeFromZX(Normal,Right).ToQuat(),Center);
    auto Panel=[&](EFinish Finish,FVector Position,FVector Size,FRotator Rotation=FRotator::ZeroRotator) {
        // The outward-facing Unreal view reverses this panel's local X axis.
        // Reflect glyph centers and stroke directions, retaining world-up and
        // the blue face's outward normal; box cross-sections are symmetric.
        Position.X=-Position.X;
        Rotation.Yaw=180.-Rotation.Yaw;
        Instance(0,Finish,FTransform(Rotation,Position,Size/100.)*T,false);
    };
    Panel(EFinish::Steel,FVector::ZeroVector,FVector(98,68,3));
    Panel(EFinish::Blue,FVector(0,0,1.6),FVector(92,62,.4));
    Panel(EFinish::Yellow,FVector(0,-24,1.9),FVector(78,3,.3));
    // W01-W04 are geometric strokes, so no font texture/material or text pass is required.
    auto Stroke=[&](FVector A,FVector B) {
        const FVector D=B-A;
        Panel(EFinish::White,(A+B)*.5,FVector(D.Size(),2.1,.35),D.Rotation());
    };
    Stroke(FVector(-39,15,2),FVector(-35,-14,2));
    Stroke(FVector(-35,-14,2),FVector(-29,1,2));
    Stroke(FVector(-29,1,2),FVector(-23,-14,2));
    Stroke(FVector(-23,-14,2),FVector(-19,15,2));
    const uint8 Digits[]={0x3f,0x06,0x5b,0x4f,0x66};
    auto Digit=[&](int32 Value,double X) {
        const FVector P[]={FVector(X,15,2),FVector(X+7,7.5,2),FVector(X+7,-7.5,2),
            FVector(X,-15,2),FVector(X-7,-7.5,2),FVector(X-7,7.5,2),FVector(X,0,2)};
        for (int32 S=0;S<7;++S) if (Digits[Value]&(1<<S))
            Panel(EFinish::White,P[S],(S==0 || S==3 || S==6) ? FVector(14,2.2,.35):FVector(2.2,13,.35));
    };
    Digit(0,-2); Digit(Number,24);
    const FVector PostBase(Center.X,Center.Y,FloorZ+1.5);
    Box(0,EFinish::Steel,PostBase,FVector(30,30,3));
    Box(0,EFinish::Steel,FVector(Center.X,Center.Y,(FloorZ+Center.Z-34)*.5),FVector(5,5,Center.Z-34-FloorZ));
}
void ADarkFactoryEnvironmentDetails::BuildFloor() {
    const double PaintZ=FloorZ+.19;
    // Two paths outside the robot/fence and rail envelopes; no marking crosses the carriage.
    for (const double X:{1100.,-1320.}) {
        Box(0,EFinish::Lane,FVector(X,60,PaintZ),FVector(110,2520,.18));
        for (const double Side:{-1.,1.})
            Box(0,EFinish::White,FVector(X+Side*57,60,PaintZ+.12),FVector(4,2520,.10));
        for (int32 I=0;I<6;++I) {
            const double Y=-950+I*400,Direction=X>0 ? 1.:-1.;
            Bar(0,EFinish::White,FVector(X,Y-30*Direction,PaintZ+.18),FVector(X,Y+30*Direction,PaintZ+.18),5,.12);
            Bar(0,EFinish::White,FVector(X,Y+30*Direction,PaintZ+.18),FVector(X-18,Y+8*Direction,PaintZ+.18),5,.12);
            Bar(0,EFinish::White,FVector(X,Y+30*Direction,PaintZ+.18),FVector(X+18,Y+8*Direction,PaintZ+.18),5,.12);
        }
    }
    // Restricted service pads flank the portals. The centerline/track stays clear.
    for (const double X:{-980.,980.}) for (const double Y:{-1160.,1160.}) {
        Box(0,EFinish::Dark,FVector(X,Y,PaintZ),FVector(120,150,.18));
        for (int32 I=0;I<5;++I) {
            const double DY=-55+I*27.5;
            Bar(0,EFinish::Yellow,FVector(X-45,Y+DY-12,PaintZ+.15),FVector(X+45,Y+DY+12,PaintZ+.15),9,.12);
        }
        for (const double Side:{-1.,1.})
            Box(0,EFinish::Yellow,FVector(X+Side*60,Y,PaintZ+.15),FVector(3,150,.12));
    }
    const FVector Observer(970,-1130,610);
    int32 Number=1;
    for (const double X:{-960.,960.}) for (const double Y:{-360.,360.}) {
        const FVector Center(X,Y,286);
        StationSign(Number++,Center,Observer-Center);
    }
}
void ADarkFactoryEnvironmentDetails::BuildServices() {
    // Suspended ladder trays at7.7m; the upper grid represents the building's utility support.
    for (const double X:{-750.,750.}) {
        for (const double Side:{-1.,1.})
            Box(1,EFinish::Steel,FVector(X+Side*27,0,777),FVector(4,2060,14));
        for (int32 Rung=0;Rung<=20;++Rung)
            Box(1,EFinish::Steel,FVector(X,-1000+Rung*100,771),FVector(54,5,3));
        for (int32 Support=0;Support<5;++Support) {
            const double Y=-1000+Support*500;
            Box(1,EFinish::Steel,FVector(X,Y,765),FVector(76,7,6));
            for (const double Side:{-1.,1.})
                Pipe(1,EFinish::Steel,FVector(X+Side*35,Y,767),FVector(X+Side*35,Y,936),2.2f);
        }
        for (int32 Cable=0;Cable<3;++Cable)
            Pipe(1,Cable==1 ? EFinish::Blue:EFinish::Cable,FVector(X-15+Cable*15,-1020,779),
                FVector(X-15+Cable*15,1020,779),Cable==1 ? 4.2f:5.f);
        Box(1,EFinish::Dark,FVector(X,-910,793),FVector(44,70,18)); // junction enclosure
        for (const double Y:{-1000.,1000.})
            Box(1,EFinish::Steel,FVector(X,Y,940),FVector(130,14,8));
    }
    for (const double Y:{-1000.,0.,1000.})
        Box(1,EFinish::Steel,FVector(0,Y,949),FVector(1660,18,18));
    // Small parallel plant-service pipes share the rear rack above all moving links.
    for (int32 I=0;I<2;++I)
        Pipe(1,I==0 ? EFinish::Blue:EFinish::Steel,FVector(-790,1000,805+I*18),FVector(790,1000,805+I*18),8.f);
}
void ADarkFactoryEnvironmentDetails::BuildExtraction() {
    // Open-bottom modular canopy housings. No curtains or geometry descend into the robot sweep.
    for (const double Y:{-320.,320.}) {
        const FVector H(0,Y,670); HoodCenters.Add(H);
        Box(2,EFinish::Steel,H+FVector(0,0,34),FVector(440,490,4));
        for (const double Side:{-1.,1.}) {
            Box(2,EFinish::Steel,H+FVector(Side*219,0,3),FVector(3,490,62));
            Box(2,EFinish::Steel,H+FVector(0,Side*244,3),FVector(440,3,62));
            Box(2,EFinish::Dark,H+FVector(Side*205,0,-28),FVector(16,462,6));
            Box(2,EFinish::Dark,H+FVector(0,Side*231,-28),FVector(426,16,6));
        }
        // Recessed extraction slots and stiffening ribs remain above the open lower edge.
        for (int32 Rib=0;Rib<5;++Rib)
            Box(2,EFinish::Dark,H+FVector(-152+Rib*76,0,28),FVector(18,426,3));
        Pipe(2,EFinish::Steel,H+FVector(0,0,36),FVector(0,Y,840),68);
        for (const double Z:{730.,798.})
            Pipe(2,EFinish::Dark,FVector(0,Y,Z-1.5),FVector(0,Y,Z+1.5),72);
        for (const double X:{-180.,180.})
            Pipe(2,EFinish::Steel,FVector(X,Y,706),FVector(X,Y,940),2.5f);
    }
    Pipe(2,EFinish::Steel,FVector(0,-730,850),FVector(0,1320,850),84);
    for (int32 I=0;I<8;++I) {
        const double Y=-650+I*260;
        Pipe(2,EFinish::Dark,FVector(0,Y-2,850),FVector(0,Y+2,850),88);
    }
    // Upward outlet continues toward the unseen roof filtration plant.
    Pipe(2,EFinish::Steel,FVector(0,1320,850),FVector(0,1320,1030),84);
}
bool ADarkFactoryEnvironmentDetails::Initialize(ADarkFactoryWeldingCell* Cell) {
    if (bInitialized) return true;
    if (!IsValid(Cell) || !GetWorld()) return false;
    Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!Cube || !Cylinder || !Base) return false;
    const FLinearColor Colors[]={FLinearColor(.025,.16,.105),FLinearColor(.70,.73,.69),FLinearColor(.82,.52,.035),
        FLinearColor(.28,.32,.36),FLinearColor(.023,.029,.036),FLinearColor(.02,.10,.20),FLinearColor(.009,.013,.018)};
    for (int32 I=0;I<static_cast<int32>(EFinish::Count);++I) {
        auto* MID=UMaterialInstanceDynamic::Create(Base,this);
        MID->SetVectorParameterValue(TEXT("BaseColor"),Colors[I]); MID->SetScalarParameterValue(TEXT("Roughness"),I==3 ? .45f:.72f);
        Materials.Add(MID);
    }
    for (const TCHAR* Name:{TEXT("pedestrian_markings_and_station_signs"),TEXT("overhead_utility_trays"),TEXT("welding_extraction_hoods")}) {
        FFeature Feature; Feature.Name=Name; Features.Add(Feature);
    }
    BuildFloor(); BuildServices(); BuildExtraction();
    bInitialized=bFinite && TotalInstances>0;
    SetActorHiddenInGame(true); // Root enables live/new QA only; legacy captures stay unchanged.
    UE_LOG(LogTemp,Display,TEXT("DF_ENVIRONMENT_DETAILS_READY valid=%d instances=%d batches=%d"),bInitialized,TotalInstances,Batches.Num());
    return bInitialized;
}
void ADarkFactoryEnvironmentDetails::SetEnabled(bool Enabled) {
    if (!bInitialized || bEnabled==Enabled) return;
    bEnabled=Enabled; SetActorHiddenInGame(!Enabled);
    for (const auto& Mesh:Batches) Mesh->SetVisibility(Enabled);
}
TSharedRef<FJsonObject> ADarkFactoryEnvironmentDetails::BuildMetadata() const {
    auto Meta=MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("model"),TEXT("factory-environment-details-v1"));
    Meta->SetBoolField(TEXT("initialized"),bInitialized); Meta->SetBoolField(TEXT("enabled"),bEnabled);
    Meta->SetBoolField(TEXT("finite_state"),bFinite); Meta->SetNumberField(TEXT("feature_count"),Features.Num());
    Meta->SetNumberField(TEXT("instance_count"),TotalInstances); Meta->SetNumberField(TEXT("instance_budget"),MaximumInstances);
    Meta->SetNumberField(TEXT("draw_components"),Batches.Num()); Meta->SetNumberField(TEXT("thermal_profiles_added"),0);
    Meta->SetNumberField(TEXT("lights_added"),0); Meta->SetBoolField(TEXT("tick_enabled"),false);
    Meta->SetNumberField(TEXT("floor_datum_cm"),FloorZ);
    Meta->SetNumberField(TEXT("pedestrian_lane_count"),2); Meta->SetNumberField(TEXT("pedestrian_lane_width_cm"),110);
    Meta->SetNumberField(TEXT("station_sign_count"),StationCenters.Num()); Meta->SetNumberField(TEXT("extraction_hood_count"),HoodCenters.Num());
    Meta->SetNumberField(TEXT("utility_tray_count"),2); Meta->SetNumberField(TEXT("collision_shapes_added"),0);
    Meta->SetStringField(TEXT("thermal"),TEXT("Unregistered opaque scene depth: ambient fallback; no thermal IDs, heat source, absorption or ventilation solver"));
    Meta->SetStringField(TEXT("scope"),TEXT("Authored plant dressing informed by manufacturer examples; no certified aisle, structural support or fume-capture design"));
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 I=0;I<Features.Num();++I) {
        const auto& F=Features[I]; auto Row=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("feature_number"),I+1); Row->SetStringField(TEXT("name"),F.Name); Row->SetNumberField(TEXT("instances"),F.Instances);
        Row->SetArrayField(TEXT("bounds_min_cm"),VJson(F.Bounds.IsValid ? F.Bounds.Min:FVector::ZeroVector));
        Row->SetArrayField(TEXT("bounds_max_cm"),VJson(F.Bounds.IsValid ? F.Bounds.Max:FVector::ZeroVector));
        Row->SetNumberField(TEXT("minimum_z_cm"),F.Bounds.IsValid ? F.Bounds.Min.Z:0);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Meta->SetArrayField(TEXT("features"),Rows);
    TArray<TSharedPtr<FJsonValue>> Stations,Hoods;
    for (const auto& P:StationCenters) Stations.Add(MakeShared<FJsonValueArray>(VJson(P)));
    for (const auto& P:HoodCenters) Hoods.Add(MakeShared<FJsonValueArray>(VJson(P)));
    Meta->SetArrayField(TEXT("station_centers_cm"),Stations); Meta->SetArrayField(TEXT("hood_centers_cm"),Hoods);
    return Meta;
}
