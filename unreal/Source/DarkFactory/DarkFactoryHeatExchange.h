#pragma once
#include "CoreMinimal.h"
#include "DarkFactoryHeatSolver.h"
class UStaticMeshComponent;
class FJsonObject;

struct FDarkFactoryHeatInput {
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    double InitialC=24;
    double Emissivity=.9;
    bool Powered=false;
};
/** Small deterministic thermal network. GPU rendering consumes its temperatures. */
class FDarkFactoryHeatExchange {
public:
    void Reset(const TArray<FDarkFactoryHeatInput>& Inputs,const TArray<TWeakObjectPtr<UStaticMeshComponent>>& ExtraBlockers,double AmbientC);
    bool Advance(double Seconds);
    double BulkC(int32 Body) const { return Nodes[1+Body*7].TemperatureC; }
    double FaceC(int32 Body,int32 Face) const { return Nodes[2+Body*7+Face].TemperatureC; }
    bool IsReady(int32 Count) const { return Inputs.Num()==Count && Nodes.size()==size_t(1+Count*7); }
    TSharedRef<FJsonObject> Metadata() const;
    double ElapsedSeconds=0;
    double LastAdvanceMilliseconds=0;
    double SuppliedEnergyJ=0;
    double EnergyResidualJ=0;
    int32 ContactLinks=0,RadiationLinks=0,BlockedPairs=0,Steps=0;
    bool Valid=true;
private:
    struct FFace { FVector Center,Normal,U,V; double HalfU=0,HalfV=0,Area=0; int32 Node=0,Body=0; };
    struct FBody { FTransform Transform; FMatrix Inverse; FBox Bounds; bool Visible=false; };
    TArray<FDarkFactoryHeatInput> Inputs;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> ExtraBlockers;
    std::vector<DarkFactoryHeat::Node> Nodes;
    std::vector<DarkFactoryHeat::Link> Links;
    double AmbientC=24;
    void BuildLinks();
};
