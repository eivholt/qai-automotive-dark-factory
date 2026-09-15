#include "DarkFactorySealantMesh.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

UStaticMesh* MakeDarkFactorySealantMesh(UObject* Outer,UMaterialInterface* Material,
    const TArray<FVector>& Points,const TArray<FVector>& Normals,FVector& Origin,double& ArcLength) {
    if (Points.Num()<2 || Normals.Num()!=Points.Num()) return nullptr;
    TArray<double> Distance; Distance.Add(0.);
    for (int32 I=1;I<Points.Num();++I) Distance.Add(Distance.Last()+FVector::Distance(Points[I-1],Points[I]));
    ArcLength=Distance.Last(); if (ArcLength<1.) return nullptr;
    Origin=(Points[0]+Points.Last())*.5;
    constexpr int32 Rings=193,Sides=20;
    TArray<FVector> Vertices,SurfaceNormals,Tangents;
    for (int32 I=0;I<Rings;++I) {
        const double U=double(I)/(Rings-1),Along=U*ArcLength;
        int32 Span=1; while (Span<Distance.Num()-1 && Distance[Span]<Along) ++Span;
        const double Alpha=FMath::Clamp((Along-Distance[Span-1])/FMath::Max(.001,Distance[Span]-Distance[Span-1]),0.,1.);
        const FVector Center=FMath::Lerp(Points[Span-1],Points[Span],Alpha)-Origin;
        const FVector Up=FMath::Lerp(Normals[Span-1],Normals[Span],Alpha).GetSafeNormal();
        const FVector Forward=(Points[Span]-Points[Span-1]).GetSafeNormal();
        const FVector Side=FVector::CrossProduct(Up,Forward).GetSafeNormal();
        // Rounded ends and subtle extrusion ridges replace the former cuboid.
        const double End=FMath::Min(U,1.-U)*ArcLength;
        const double Taper=FMath::Sqrt(FMath::Clamp(End/1.6,0.,1.));
        const double Rib=1.+.035*FMath::Sin(U*ArcLength*4.3);
        const double BeadHalfWidth=.9;
        const double Width=BeadHalfWidth*Taper*Rib,Height=.2*Taper*Rib;
        for (int32 J=0;J<Sides;++J) {
            const double Angle=2.*PI*J/Sides,C=FMath::Cos(Angle),S=FMath::Sin(Angle);
            Vertices.Add(Center+Side*(Width*C)+Up*(Height*(1.+S)));
            FVector N=(Side*(C/BeadHalfWidth)+Up*(S/.2)).GetSafeNormal();
            if (End<1.6) N=(N+Forward*((U<.5 ? -1.:1.)*(1.-Taper)*3.)).GetSafeNormal();
            const FVector SurfaceTangent=(Forward-N*FVector::DotProduct(Forward,N)).GetSafeNormal();
            SurfaceNormals.Add(N); Tangents.Add(SurfaceTangent.IsNearlyZero() ? Side:SurfaceTangent);
        }
    }
    FMeshDescription D; FStaticMeshAttributes A(D); A.Register();
    auto Position=A.GetVertexPositions(); auto Normal=A.GetVertexInstanceNormals();
    auto Tangent=A.GetVertexInstanceTangents(); auto Sign=A.GetVertexInstanceBinormalSigns();
    auto UV=A.GetVertexInstanceUVs(); UV.SetNumChannels(1);
    const FPolygonGroupID Group=D.CreatePolygonGroup(); A.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Sealant");
    TArray<FVertexID> IDs;
    for (const FVector& P:Vertices) { const auto ID=D.CreateVertex(); Position[ID]=FVector3f(P); IDs.Add(ID); }
    auto Triangle=[&](int32 I0,int32 I1,int32 I2) {
        const FVector Cross=FVector::CrossProduct(Vertices[I1]-Vertices[I0],Vertices[I2]-Vertices[I0]);
        if (Cross.SizeSquared()<1.e-12) return;
        if (FVector::DotProduct(Cross,SurfaceNormals[I0]+SurfaceNormals[I1]+SurfaceNormals[I2])<0) Swap(I1,I2);
        TArray<FVertexInstanceID> Corners;
        for (const int32 K:{I0,I1,I2}) {
            const auto VI=D.CreateVertexInstance(IDs[K]); Corners.Add(VI);
            Normal[VI]=FVector3f(SurfaceNormals[K]); Tangent[VI]=FVector3f(Tangents[K]); Sign[VI]=1.f;
            UV.Set(VI,0,FVector2f(float(K/Sides)/(Rings-1),float(K%Sides)/Sides));
        }
        D.CreateTriangle(Group,Corners);
    };
    for (int32 I=0;I<Rings-1;++I) for (int32 J=0;J<Sides;++J) {
        const int32 A0=I*Sides+J,B=I*Sides+(J+1)%Sides,C=(I+1)*Sides+J,E=(I+1)*Sides+(J+1)%Sides;
        Triangle(A0,B,C); Triangle(B,E,C);
    }
    auto* Mesh=NewObject<UStaticMesh>(Outer,NAME_None,RF_Transient);
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Material,TEXT("Sealant")));
    UStaticMesh::FBuildMeshDescriptionsParams Build; Build.bFastBuild=true; Build.bBuildSimpleCollision=true; Mesh->bAllowCPUAccess=true;
    Mesh->BuildFromMeshDescriptions({&D},Build); return Mesh;
}
