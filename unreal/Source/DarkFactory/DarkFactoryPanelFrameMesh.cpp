#include "DarkFactoryPanelFrameMesh.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "PhysicsEngine/BodySetup.h"

UStaticMesh* MakeDarkFactoryPanelFrame(UObject* Outer,const FString& Path,
    UMaterialInterface* Material,FVector& Origin) {
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes,*Path) || Bytes.Num()<36 ||
        FMemory::Memcmp(Bytes.GetData(),"DFFRAME1",8)!=0) return nullptr;
    FMemoryReader Reader(Bytes); Reader.Seek(8);
    uint32 Count=0; Reader<<Count;
    double X,Y,Z; Reader<<X<<Y<<Z; Origin=FVector(X,Y,Z);
    if (Count<1000 || Count>100000 || Bytes.Num()!=36+int64(Count)*96 || Origin.ContainsNaN()) return nullptr;
    FMeshDescription D; FStaticMeshAttributes A(D); A.Register();
    auto Position=A.GetVertexPositions(); auto Normal=A.GetVertexInstanceNormals();
    auto Tangent=A.GetVertexInstanceTangents(); auto Sign=A.GetVertexInstanceBinormalSigns();
    auto UV=A.GetVertexInstanceUVs(); UV.SetNumChannels(1);
    const auto Group=D.CreatePolygonGroup(); A.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("SourcePanel");
    for (uint32 I=0;I<Count;++I) {
        TArray<FVertexInstanceID> Corners;
        for (int32 J=0;J<3;++J) {
            float V[8]; Reader.Serialize(V,sizeof(V));
            for (float Value:V) if (!FMath::IsFinite(Value)) return nullptr;
            const auto ID=D.CreateVertex(); Position[ID]=FVector3f(V[0],V[1],V[2]);
            const auto VI=D.CreateVertexInstance(ID); Corners.Add(VI);
            const FVector3f N=FVector3f(V[3],V[4],V[5]).GetSafeNormal();
            const FVector3f Axis=FMath::Abs(N.Z)<.9f ? FVector3f(0,0,1):FVector3f(0,1,0);
            Normal[VI]=N; Tangent[VI]=FVector3f::CrossProduct(Axis,N).GetSafeNormal(); Sign[VI]=1.f;
            UV.Set(VI,0,FVector2f(V[6],V[7]));
        }
        D.CreateTriangle(Group,Corners);
    }
    auto* Mesh=NewObject<UStaticMesh>(Outer,NAME_None,RF_Transient);
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Material,TEXT("SourcePanel")));
    Mesh->bAllowCPUAccess=true;
    Mesh->CreateBodySetup(); Mesh->GetBodySetup()->CollisionTraceFlag=CTF_UseComplexAsSimple;
    UStaticMesh::FBuildMeshDescriptionsParams Build; Build.bFastBuild=true; Build.bBuildSimpleCollision=false;
    Mesh->BuildFromMeshDescriptions({&D},Build);
    UE_LOG(LogTemp,Display,TEXT("DF_PANEL_FRAME_READY recipe=source-panel-open-frame-v1 triangles=%u"),Count);
    return Mesh;
}
