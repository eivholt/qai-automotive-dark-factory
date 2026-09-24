#include "DarkFactoryColliderOverlay.h"
#include "DarkFactoryGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "PhysicsEngine/BodySetup.h"
#include "SceneView.h"

namespace {
constexpr int32 MaxColliderBoxes=512;
const TCHAR* KindNames[]={TEXT("static"),TEXT("kinematic"),TEXT("simulated"),TEXT("query")};
const TCHAR* GeometryNames[]={TEXT("box"),TEXT("simple_bounds"),TEXT("complex_bounds"),TEXT("component_bounds")};
const TCHAR* ModeNames[]={TEXT("Off"),TEXT("Physics bodies"),TEXT("Physics + query bounds")};
const FLinearColor Colors[]={FLinearColor(.25f,1.f,.30f),FLinearColor(0.f,.9f,1.f),FLinearColor(1.f,.45f,.06f),FLinearColor(.92f,.30f,1.f)};
FVector Corner(const FDarkFactoryColliderBox& Box,int32 Index) {
    const FVector P((Index&1) ? Box.LocalBounds.Max.X:Box.LocalBounds.Min.X,
        (Index&2) ? Box.LocalBounds.Max.Y:Box.LocalBounds.Min.Y,
        (Index&4) ? Box.LocalBounds.Max.Z:Box.LocalBounds.Min.Z);
    return Box.Transform.TransformPosition(P);
}
TArray<TSharedPtr<FJsonValue>> VectorJSON(const FVector& P) {
    return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
}
// Liang-Barsky screen clipping after world-space near-plane clipping.
bool ClipScreen(FVector2D& A,FVector2D& B,double Width,double Height) {
    if (A.ContainsNaN() || B.ContainsNaN()) return false;
    const FVector2D D=B-A; double Low=0,High=1;
    const double P[]={-D.X,D.X,-D.Y,D.Y},Q[]={A.X,Width-A.X,A.Y,Height-A.Y};
    for (int32 I=0;I<4;++I) {
        if (FMath::Abs(P[I])<1.e-10) { if (Q[I]<0) return false; }
        else { const double T=Q[I]/P[I]; if (P[I]<0) Low=FMath::Max(Low,T); else High=FMath::Min(High,T); if (Low>High) return false; }
    }
    B=A+High*D; A+=Low*D; return true;
}
}
void ADarkFactoryHUD::SetColliderOverlayMode(int32 Mode) {
    ColliderOverlayMode=FMath::Clamp(Mode,0,2); ColliderCacheTime=-1;
    ColliderDrawnLines=ColliderVisibleBoxes=0; ColliderScreenBounds=FBox2D(ForceInit);
    if (!ColliderOverlayMode) { ColliderComponents.Reset(); ColliderBoxes.Reset(); bColliderTruncated=false; }
}
void ADarkFactoryHUD::GatherColliderBoxes() {
    ColliderBoxes.Reset(); bColliderTruncated=false;
    if (!ColliderOverlayMode || !GetWorld()) return;
    const double Now=FPlatformTime::Seconds();
    if (ColliderCacheTime<0 || Now-ColliderCacheTime>=1.) {
        ColliderCacheTime=Now; ColliderComponents.Reset();
        for (TActorIterator<AActor> It(GetWorld());It;++It) {
            TInlineComponentArray<UPrimitiveComponent*> Components; It->GetComponents(Components);
            // Retain disabled bodies too: an incoming car enables its existing proxies.
            for (auto* Component:Components) if (Component && Component->IsRegistered()) ColliderComponents.Add(Component);
        }
    }
    for (const auto& Weak:ColliderComponents) {
        auto* Component=Weak.Get();
        if (!Component || !Component->IsRegistered() || !Component->IsCollisionEnabled()) continue;
        const bool Physics=Component->IsPhysicsCollisionEnabled();
        if (!Physics && (ColliderOverlayMode!=2 || !Component->IsQueryCollisionEnabled())) continue;
        FBox Bounds(ForceInit); uint8 Geometry=3;
        if (const auto* Box=Cast<UBoxComponent>(Component)) {
            const FVector Extent=Box->GetUnscaledBoxExtent(); Bounds=FBox(-Extent,Extent); Geometry=0;
        } else {
            auto* Setup=Component->GetBodySetup();
            auto* MeshComponent=Cast<UStaticMeshComponent>(Component);
            UStaticMesh* Mesh=MeshComponent ? MeshComponent->GetStaticMesh().Get():nullptr;
            const bool Complex=Setup && Setup->GetCollisionTraceFlag()==CTF_UseComplexAsSimple;
            if (Setup && !Complex && Setup->AggGeom.GetElementCount()>0) { Bounds=Setup->AggGeom.CalcAABB(FTransform::Identity); Geometry=1; }
            // Complex meshes use enclosing bounds, not fictitious box colliders.
            // In all-query mode include triangle coverage if simple and complex coexist.
            const bool IncludeTriangles=Complex || !Bounds.IsValid || (ColliderOverlayMode==2 && Component->IsQueryCollisionEnabled() && Setup && Setup->GetCollisionTraceFlag()!=CTF_UseSimpleAsComplex);
            if (Mesh && IncludeTriangles) {
#if WITH_EDITORONLY_DATA
                if (Mesh->ComplexCollisionMesh) Mesh=Mesh->ComplexCollisionMesh;
#endif
                Bounds+=Mesh->GetBoundingBox(); Geometry=2;
            }
            if (!Bounds.IsValid && Setup) Bounds=Component->CalcBounds(FTransform::Identity).GetBox();
        }
        if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()) continue;
        const uint8 Kind=!Physics ? 3 : (Component->IsSimulatingPhysics() || Component->GetCollisionObjectType()==ECC_PhysicsBody) ? 2 : Component->GetCollisionObjectType()==ECC_WorldDynamic ? 1:0;
        auto* Instances=Cast<UInstancedStaticMeshComponent>(Component);
        const int32 Count=Instances ? Instances->GetInstanceCount():1;
        for (int32 I=0;I<Count;++I) {
            if (ColliderBoxes.Num()>=MaxColliderBoxes) { bColliderTruncated=true; return; }
            FTransform Transform=Component->GetComponentTransform();
            if (Instances && !Instances->GetInstanceTransform(I,Transform,true)) continue;
            if (Transform.ContainsNaN()) continue;
            FDarkFactoryColliderBox Box; Box.Component=Component; Box.LocalBounds=Bounds; Box.Transform=Transform;
            Box.Instance=Instances ? I:-1; Box.Kind=Kind; Box.Geometry=Geometry; ColliderBoxes.Add(MoveTemp(Box));
        }
    }
}
void ADarkFactoryHUD::DrawColliderOverlay() {
    ColliderDrawnLines=ColliderVisibleBoxes=0; ColliderScreenBounds=FBox2D(ForceInit);
    if (!ColliderOverlayMode || !Canvas || !Canvas->SceneView) return;
    GatherColliderBoxes();
    const FVector Origin=Canvas->SceneView->ViewMatrices.GetViewOrigin(),Forward=Canvas->SceneView->GetViewDirection();
    const double Near=FMath::Max(.1f,GNearClippingPlane);
    for (const auto& Box:ColliderBoxes) {
        FVector Corners[8]; for (int32 I=0;I<8;++I) Corners[I]=Corner(Box,I);
        bool Visible=false;
        for (int32 I=0;I<8;++I) for (int32 Bit:{1,2,4}) if (!(I&Bit)) {
            FVector A=Corners[I],B=Corners[I|Bit];
            const double DA=FVector::DotProduct(A-Origin,Forward),DB=FVector::DotProduct(B-Origin,Forward);
            if (DA<Near && DB<Near) continue;
            if (DA<Near) A=FMath::Lerp(A,B,(Near-DA)/(DB-DA));
            else if (DB<Near) B=FMath::Lerp(B,A,(Near-DB)/(DA-DB));
            const FVector PA=Canvas->Project(A,false),PB=Canvas->Project(B,false);
            FVector2D SA(PA.X,PA.Y),SB(PB.X,PB.Y);
            if (!ClipScreen(SA,SB,Canvas->ClipX,Canvas->ClipY)) continue;
            DrawLine(SA.X,SA.Y,SB.X,SB.Y,Colors[Box.Kind],Box.Kind==3 ? .85f:1.4f);
            ColliderScreenBounds+=SA; ColliderScreenBounds+=SB; ++ColliderDrawnLines; Visible=true;
        }
        ColliderVisibleBoxes+=Visible ? 1:0;
    }
}
void ADarkFactoryHUD::DrawColliderLegend(float RightPanelX,bool Imported) {
    if (!ColliderOverlayMode) return;
    const float X=14.f,Y=Imported ? (RightPanelX-304.f<550.f ? 198.f:106.f):172.f;
    const float Width=FMath::Min(680.f,RightPanelX-X-14.f);
    if (Width<270) return;
    DrawRect(FLinearColor(.008f,.015f,.024f,.92f),X,Y,Width,46);
    const FString Title=FString::Printf(TEXT("Collider bounds: %s (%d) | through surfaces%s"),ModeNames[ColliderOverlayMode],ColliderBoxes.Num(),bColliderTruncated ? TEXT(" | limit reached"):TEXT(""));
    DrawHUDText(Title,FLinearColor(.9f,.96f,1.f),X+10,Y+5,GEngine->GetSmallFont(),.86f);
    const TCHAR* Labels[]={TEXT("Static"),TEXT("Kinematic"),TEXT("Dynamic"),TEXT("Query mesh bounds")};
    const float Column=(Width-20)/4;
    for (int32 I=0;I<4;++I) DrawHUDText(Labels[I],Colors[I],X+10+I*Column,Y+25,GEngine->GetSmallFont(),.80f);
}
TSharedRef<FJsonObject> ADarkFactoryHUD::BuildColliderOverlayMetadata() {
    GatherColliderBoxes(); auto R=MakeShared<FJsonObject>();
    R->SetNumberField(TEXT("mode"),ColliderOverlayMode); R->SetStringField(TEXT("mode_name"),ModeNames[ColliderOverlayMode]);
    R->SetStringField(TEXT("scope"),TEXT("Observer HUD-only collider bounds; non-box geometry is enclosing, not exact collision shape; hidden active bodies included"));
    R->SetNumberField(TEXT("box_count"),ColliderBoxes.Num()); R->SetNumberField(TEXT("box_limit"),MaxColliderBoxes);
    R->SetNumberField(TEXT("last_drawn_lines"),ColliderDrawnLines); R->SetNumberField(TEXT("last_visible_boxes"),ColliderVisibleBoxes);
    R->SetBoolField(TEXT("truncated"),bColliderTruncated); R->SetBoolField(TEXT("screen_bounds_valid"),ColliderScreenBounds.bIsValid);
    if (ColliderScreenBounds.bIsValid) {
        R->SetArrayField(TEXT("screen_min"),{MakeShared<FJsonValueNumber>(ColliderScreenBounds.Min.X),MakeShared<FJsonValueNumber>(ColliderScreenBounds.Min.Y)});
        R->SetArrayField(TEXT("screen_max"),{MakeShared<FJsonValueNumber>(ColliderScreenBounds.Max.X),MakeShared<FJsonValueNumber>(ColliderScreenBounds.Max.Y)});
    }
    int32 Physics=0,Queries=0,Simulated=0; TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& Box:ColliderBoxes) {
        const auto* Component=Box.Component.Get(); if (!Component) continue;
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("actor"),GetNameSafe(Component->GetOwner())); Row->SetStringField(TEXT("component"),Component->GetName());
        Row->SetStringField(TEXT("kind"),KindNames[Box.Kind]); Row->SetStringField(TEXT("geometry"),GeometryNames[Box.Geometry]);
        Row->SetNumberField(TEXT("instance"),Box.Instance); Row->SetBoolField(TEXT("approximate"),Box.Geometry!=0);
        Row->SetBoolField(TEXT("simulating_now"),Component->IsSimulatingPhysics()); Row->SetBoolField(TEXT("hidden"),Component->bHiddenInGame);
        Row->SetArrayField(TEXT("local_min_cm"),VectorJSON(Box.LocalBounds.Min)); Row->SetArrayField(TEXT("local_max_cm"),VectorJSON(Box.LocalBounds.Max));
        TArray<TSharedPtr<FJsonValue>> Corners,Matrix;
        for (int32 I=0;I<8;++I) Corners.Add(MakeShared<FJsonValueArray>(VectorJSON(Corner(Box,I))));
        const FVector X=Box.Transform.TransformVector(FVector(1,0,0)),Y=Box.Transform.TransformVector(FVector(0,1,0)),Z=Box.Transform.TransformVector(FVector(0,0,1)),P=Box.Transform.GetLocation();
        for (int32 I=0;I<3;++I) Matrix.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(X[I]),MakeShared<FJsonValueNumber>(Y[I]),MakeShared<FJsonValueNumber>(Z[I]),MakeShared<FJsonValueNumber>(P[I])}));
        Row->SetArrayField(TEXT("world_corners_cm"),Corners); Row->SetArrayField(TEXT("transform_rows"),Matrix); Rows.Add(MakeShared<FJsonValueObject>(Row));
        Physics+=Box.Kind!=3; Queries+=Box.Kind==3; Simulated+=Box.Kind==2;
    }
    R->SetNumberField(TEXT("physics_count"),Physics); R->SetNumberField(TEXT("query_count"),Queries); R->SetNumberField(TEXT("simulated_count"),Simulated);
    R->SetArrayField(TEXT("boxes"),Rows); return R;
}
