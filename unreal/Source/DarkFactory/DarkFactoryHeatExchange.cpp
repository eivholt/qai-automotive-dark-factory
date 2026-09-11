#include "DarkFactoryHeatExchange.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"

namespace {
constexpr double VolumetricCapacity=1500000; // J/m3/K, illustrative coating/body effective material.
constexpr double SkinThicknessM=.0001;
constexpr double Conductivity=.2; // W/m/K; insulation-like proxy, not inferred metal properties.
constexpr double AirH=5,ContactH=100; // W/m2/K.
constexpr double ContactToleranceCm=.75,MaxRadiationDistanceCm=150;
FVector Axis(int I) { return I==0 ? FVector::ForwardVector:I==1 ? FVector::RightVector:FVector::UpVector; }
bool SegmentBlocked(const FBox& Box,const FMatrix& Inverse,const FVector& Start,const FVector& End) {
    const FVector A=Inverse.TransformPosition(Start),B=Inverse.TransformPosition(End);
    return FMath::LineBoxIntersection(Box,A,B,B-A);
}
}
void FDarkFactoryHeatExchange::Reset(const TArray<FDarkFactoryHeatInput>& InInputs,const TArray<TWeakObjectPtr<UStaticMeshComponent>>& InBlockers,double InAmbientC) {
    Inputs=InInputs; ExtraBlockers=InBlockers; AmbientC=InAmbientC;
    Nodes.clear(); Nodes.push_back({AmbientC,1,true});
    ElapsedSeconds=SuppliedEnergyJ=EnergyResidualJ=0; Steps=0; Valid=true;
    for (const auto& Input:Inputs) {
        const auto* Mesh=Input.Mesh.Get();
        const FVector Size=Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetBoundingBox().GetSize()*Mesh->GetComponentScale().GetAbs()*.01:FVector(.01);
        const double Volume=FMath::Max(1e-9,Size.X*Size.Y*Size.Z);
        const double Area=FMath::Max(1e-9,2*(Size.X*Size.Y+Size.X*Size.Z+Size.Y*Size.Z));
        const double TotalCapacity=FMath::Max(.001,Volume*VolumetricCapacity);
        const double SurfaceCapacity=FMath::Min(TotalCapacity*.4,Area*SkinThicknessM*VolumetricCapacity);
        Nodes.push_back({Input.InitialC,TotalCapacity-SurfaceCapacity,Input.Powered});
        for (int Face=0;Face<6;++Face) {
            const int A=Face/2,U=(A+1)%3,V=(A+2)%3;
            const double Fraction=Size[U]*Size[V]/Area;
            Nodes.push_back({Input.InitialC,FMath::Max(1e-6,SurfaceCapacity*Fraction),Input.Powered});
        }
    }
    BuildLinks();
}
void FDarkFactoryHeatExchange::BuildLinks() {
    Links.clear(); ContactLinks=RadiationLinks=BlockedPairs=0;
    TArray<FBody> Bodies; TArray<FFace> Faces;
    for (int Body=0;Body<Inputs.Num();++Body) {
        const auto* Mesh=Inputs[Body].Mesh.Get();
        FBody G;
        if (Mesh && Mesh->GetStaticMesh()) {
            G.Transform=Mesh->GetComponentTransform(); G.Inverse=G.Transform.ToInverseMatrixWithScale();
            G.Bounds=Mesh->GetStaticMesh()->GetBoundingBox(); G.Visible=Mesh->IsVisible();
        }
        Bodies.Add(G);
        if (!G.Visible) continue;
        const FVector Extent=G.Bounds.GetExtent(),Scale=G.Transform.GetScale3D().GetAbs();
        for (int Face=0;Face<6;++Face) {
            const int A=Face/2,U=(A+1)%3,V=(A+2)%3;
            FFace F; F.Body=Body; F.Node=2+Body*7+Face;
            F.Center=G.Transform.TransformPosition(G.Bounds.GetCenter()+Axis(A)*Extent[A]*(Face%2 ? -1:1));
            F.Normal=G.Transform.TransformVectorNoScale(Axis(A)*(Face%2 ? -1:1)*(G.Transform.GetScale3D()[A]<0 ? -1:1)).GetSafeNormal();
            F.U=G.Transform.TransformVectorNoScale(Axis(U)).GetSafeNormal(); F.V=G.Transform.TransformVectorNoScale(Axis(V)).GetSafeNormal();
            F.HalfU=Extent[U]*Scale[U]; F.HalfV=Extent[V]*Scale[V]; F.Area=4*F.HalfU*F.HalfV*.0001;
            Faces.Add(F);
            const double DistanceM=FMath::Max(.001,Extent[A]*Scale[A]*.01);
            Links.push_back({1+Body*7,F.Node,Conductivity*F.Area/DistanceM,0});
        }
    }
    struct FPair { int A,B; double Area; };
    TArray<FPair> Contacts,Radiation;
    TArray<double> ContactArea,ViewArea; ContactArea.Init(0,Faces.Num()); ViewArea.Init(0,Faces.Num());
    auto Blocked=[&](const FFace& A,const FFace& B) {
        const FVector Start=A.Center+A.Normal*.02,End=B.Center+B.Normal*.02;
        for (int I=0;I<Bodies.Num();++I) if (I!=A.Body && I!=B.Body && Bodies[I].Visible && SegmentBlocked(Bodies[I].Bounds,Bodies[I].Inverse,Start,End)) return true;
        for (const auto& Weak:ExtraBlockers) if (const auto* Mesh=Weak.Get())
            if (Mesh->IsVisible() && Mesh->GetStaticMesh() && SegmentBlocked(Mesh->GetStaticMesh()->GetBoundingBox(),Mesh->GetComponentTransform().ToInverseMatrixWithScale(),Start,End)) return true;
        return false;
    };
    for (int I=0;I<Faces.Num();++I) for (int J=I+1;J<Faces.Num();++J) {
        const auto& A=Faces[I]; const auto& B=Faces[J];
        if (A.Body==B.Body) continue;
        const FVector Delta=B.Center-A.Center;
        // Contact needs opposed nearly parallel planes plus overlapping projected footprints.
        const double Gap=FVector::DotProduct(Delta,A.Normal);
        if (FVector::DotProduct(A.Normal,B.Normal)<-.995 && FMath::Abs(Gap)<=ContactToleranceCm) {
            const double BU=FMath::Abs(FVector::DotProduct(B.U,A.U))*B.HalfU+FMath::Abs(FVector::DotProduct(B.V,A.U))*B.HalfV;
            const double BV=FMath::Abs(FVector::DotProduct(B.U,A.V))*B.HalfU+FMath::Abs(FVector::DotProduct(B.V,A.V))*B.HalfV;
            const double U=FVector::DotProduct(Delta,A.U),V=FVector::DotProduct(Delta,A.V);
            const double OverU=FMath::Max(0.0,FMath::Min(A.HalfU,U+BU)-FMath::Max(-A.HalfU,U-BU));
            const double OverV=FMath::Max(0.0,FMath::Min(A.HalfV,V+BV)-FMath::Max(-A.HalfV,V-BV));
            const double Area=FMath::Min(FMath::Min(A.Area,B.Area),OverU*OverV*.0001);
            if (Area>1e-9) { if (Blocked(A,B)) { ++BlockedPairs; continue; } Contacts.Add({I,J,Area}); ContactArea[I]+=Area; ContactArea[J]+=Area; continue; }
        }
        const double Distance=Delta.Size();
        if (Distance<=ContactToleranceCm || Distance>MaxRadiationDistanceCm) continue;
        const FVector Direction=Delta/Distance;
        const double CosA=FVector::DotProduct(A.Normal,Direction),CosB=-FVector::DotProduct(B.Normal,Direction);
        if (CosA<=0 || CosB<=0) continue;
        if (Blocked(A,B)) { ++BlockedPairs; continue; }
        // Symmetric exchange area, bounded in the near field. This is a view-factor proxy.
        const double Area=A.Area*B.Area*CosA*CosB/(PI*FMath::Square(Distance*.01)+FMath::Max(A.Area,B.Area));
        if (Area>1e-12) { Radiation.Add({I,J,Area}); ViewArea[I]+=Area; ViewArea[J]+=Area; }
    }
    TArray<double> UsedContact,UsedRadiation; UsedContact.Init(0,Faces.Num()); UsedRadiation.Init(0,Faces.Num());
    for (const auto& Pair:Contacts) {
        const double Scale=FMath::Min(1.0,FMath::Min(Faces[Pair.A].Area/ContactArea[Pair.A],Faces[Pair.B].Area/ContactArea[Pair.B]));
        const double Area=Pair.Area*Scale;
        UsedContact[Pair.A]+=Area; UsedContact[Pair.B]+=Area;
        Links.push_back({Faces[Pair.A].Node,Faces[Pair.B].Node,ContactH*Area,0}); ++ContactLinks;
    }
    for (const auto& Pair:Radiation) {
        const double AvailableA=FMath::Max(0.0,Faces[Pair.A].Area-UsedContact[Pair.A]);
        const double AvailableB=FMath::Max(0.0,Faces[Pair.B].Area-UsedContact[Pair.B]);
        const double Scale=FMath::Min(1.0,FMath::Min(AvailableA/ViewArea[Pair.A],AvailableB/ViewArea[Pair.B]));
        const double Area=Pair.Area*Scale;
        if (Area<=1e-12) continue;
        UsedRadiation[Pair.A]+=Area; UsedRadiation[Pair.B]+=Area;
        const double Emission=Inputs[Faces[Pair.A].Body].Emissivity*Inputs[Faces[Pair.B].Body].Emissivity;
        Links.push_back({Faces[Pair.A].Node,Faces[Pair.B].Node,0,Area*Emission}); ++RadiationLinks;
    }
    for (int I=0;I<Faces.Num();++I) {
        const double AirArea=FMath::Max(0.0,Faces[I].Area-UsedContact[I]);
        const double EnvironmentArea=FMath::Max(0.0,AirArea-UsedRadiation[I]);
        Links.push_back({Faces[I].Node,0,AirH*AirArea,EnvironmentArea*Inputs[Faces[I].Body].Emissivity});
    }
}
bool FDarkFactoryHeatExchange::Advance(double Seconds) {
    const double Started=FPlatformTime::Seconds();
    BuildLinks(); // Only simulated-time advances rebuild geometry, never image pixels.
    const auto Result=DarkFactoryHeat::Advance(Nodes,Links,Seconds,1.0);
    LastAdvanceMilliseconds=(FPlatformTime::Seconds()-Started)*1000;
    Valid &= Result.Valid;
    if (Result.Valid) { ElapsedSeconds+=Seconds; SuppliedEnergyJ+=Result.SuppliedEnergyJ; EnergyResidualJ+=Result.EnergyResidualJ; Steps+=Result.Steps; }
    return Result.Valid;
}
TSharedRef<FJsonObject> FDarkFactoryHeatExchange::Metadata() const {
    auto Data=MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("valid"),Valid);
    Data->SetNumberField(TEXT("elapsed_seconds"),ElapsedSeconds);
    Data->SetNumberField(TEXT("last_advance_cpu_ms"),LastAdvanceMilliseconds);
    Data->SetNumberField(TEXT("nodes"),Nodes.size()); Data->SetNumberField(TEXT("links"),Links.size());
    Data->SetNumberField(TEXT("contact_links"),ContactLinks); Data->SetNumberField(TEXT("radiation_links"),RadiationLinks); Data->SetNumberField(TEXT("blocked_pairs"),BlockedPairs);
    Data->SetNumberField(TEXT("integration_steps"),Steps);
    Data->SetNumberField(TEXT("supplied_energy_j"),SuppliedEnergyJ); Data->SetNumberField(TEXT("energy_residual_j"),EnergyResidualJ);
    Data->SetNumberField(TEXT("volumetric_capacity_j_m3_k"),VolumetricCapacity); Data->SetNumberField(TEXT("skin_thickness_m"),SkinThicknessM);
    Data->SetNumberField(TEXT("conductivity_w_m_k"),Conductivity); Data->SetNumberField(TEXT("air_h_w_m2_k"),AirH); Data->SetNumberField(TEXT("contact_h_w_m2_k"),ContactH);
    Data->SetNumberField(TEXT("contact_tolerance_cm"),ContactToleranceCm); Data->SetNumberField(TEXT("radiation_cutoff_cm"),MaxRadiationDistanceCm);
    Data->SetStringField(TEXT("model"),TEXT("Bulk + six skin nodes per box; symmetric contact and direct gray radiation; uniform ambient reservoir; approximate reciprocal view areas with opaque box occlusion; no reflected bounces or fluid simulation"));
    return Data;
}
