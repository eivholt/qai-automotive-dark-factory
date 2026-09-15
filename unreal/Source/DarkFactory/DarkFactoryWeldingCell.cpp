#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryProcessTypes.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace {
constexpr double ApproachTimes[] = {0.,.20,.56,.80,1.};
constexpr double MotorDutyStep=1./120.;
constexpr double MotorAmbientC=20.;
// The rear roof crossmember obstructs the authored panel-welding sweep.
// Keep its source asset and legacy preview intact; process bodies omit it.
const TCHAR* ProcessRemovedRoof=TEXT("SM_BodyPanel_0385");
// Complete source decks and scaffolds move together: support the retained
// forward clamps, with their rear ends ahead of the relocated robot plinths.
const FVector ProcessFloorOffsets[]={FVector(70.,600.,0.),FVector(-80.,600.,0.)};
// CPU FK/IK-authored camera poses for the imported robot_nx_ny chain, ordered
// base/shoulder/elbow/forearm_roll/wrist/tool_roll. These visual trajectories
// have a separate process envelope; the source welding limits stay unchanged.
constexpr double InspectionPoseAngles[3][6]={
    {.20291161690341683,19.41616904474995,8.970136629778356,.02810221653294898,-.936262352041913,.04559720616352198},
    {18.12272923363434,20.011840168672226,12.366808389843781,-10.55376311394919,-9.17458595045043,-8.187771298205302},
    {-1.1845869586489253,34.2108279877618,4.266951720556133,-27.485913451300778,-44.16891319732776,-51.2120748647157}
};
const TCHAR* InspectionJointRoles[]={TEXT("base"),TEXT("shoulder"),TEXT("elbow"),TEXT("forearm_roll"),TEXT("wrist"),TEXT("tool_roll")};
bool Scalar(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double& V) {
    return O && O->TryGetNumberField(Key,V) && FMath::IsFinite(V);
}
bool Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FVector& V) {
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    if (!O || !O->TryGetArrayField(Key,A) || A->Num()!=3) return false;
    double C[3];
    for (int I=0;I<3;++I) if (!(*A)[I]->TryGetNumber(C[I]) || !FMath::IsFinite(C[I]) || FMath::Abs(C[I])>1.e7) return false;
    V=FVector(C[0],C[1],C[2]); return true;
}
bool VectorRows(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,TArray<FVector>& Values) {
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if (!O->TryGetArrayField(Key,Rows) || Rows->Num()!=5) return false;
    for (const auto& Row:*Rows) {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if (!Row->TryGetArray(A) || A->Num()!=3) return false;
        double C[3]; for (int32 I=0;I<3;++I) if (!(*A)[I]->TryGetNumber(C[I]) || !FMath::IsFinite(C[I]) || FMath::Abs(C[I])>1.e7) return false;
        Values.Add(FVector(C[0],C[1],C[2]));
    }
    return true;
}
bool Rotation(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FQuat& Q) {
    FVector V; if (!Vector(O,Key,V)) return false;
    Q=FRotator(V.X,V.Y,V.Z).Quaternion(); return true;
}
bool Identifier(const FString& S) {
    if (S.IsEmpty() || S.Len()>96) return false;
    for (TCHAR C:S) if (!FChar::IsAlnum(C) && C!=TEXT('_') && C!=TEXT('-')) return false;
    return true;
}
bool Camera(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FVector& Location,FVector& Target,float* FOV=nullptr) {
    const TSharedPtr<FJsonObject>* C=nullptr;
    if (!O->TryGetObjectField(Key,C) || !Vector(*C,TEXT("location_cm"),Location) || !Vector(*C,TEXT("look_at_cm"),Target) || Location.Equals(Target,.01)) return false;
    if (FOV) { double V=58; if ((*C)->HasField(TEXT("horizontal_fov_deg")) && !Scalar(*C,TEXT("horizontal_fov_deg"),V)) return false; if (V<10 || V>140) return false; *FOV=V; }
    return true;
}
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V) { return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}; }
struct FJointSpec { FString Id,Parent; FVector Pivot,Axis; FQuat Rotation; double Minimum=0,Maximum=0,Period=12,Phase=0,RestAngle=0,WeldAmplitude=0; TArray<double> WorkAngles,ApproachAngles; };
struct FEmitterSpec { FString Id,Parent; FVector Position,Direction; TArray<FVector> SurfacePositions,SurfaceNormals; };
struct FMeshSpec { FString Id,Parent,Asset; FVector Position,Scale=FVector::OneVector; FQuat Rotation; UStaticMesh* Mesh=nullptr; bool bThermal=false,bHolder=false; double Temperature=24,Emissivity=.9,Drop=0; };
}

ADarkFactoryWeldingCell::ADarkFactoryWeldingCell() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("WeldingCellRoot"));
    SetRootComponent(SceneRoot);
    PanelPhase=EDarkFactoryProcessPhase::Arriving;
    CarriageRoot=CreateDefaultSubobject<USceneComponent>(TEXT("CarriageRoot"));
    CarriageRoot->SetupAttachment(SceneRoot);
    CarriageRoot->SetMobility(EComponentMobility::Movable);
}

bool ADarkFactoryWeldingCell::LoadScene(const FString& Path,FString& Error) {
    if (bLoaded) { Error=TEXT("Scene already loaded"); return false; }
    FParse::Value(FCommandLine::Get(),TEXT("DFSeed="),RobotTimingSeed);
    auto Fail=[&](const FString& Why) { Error=Why; UE_LOG(LogTemp,Warning,TEXT("DF_WELDING_LOAD_FAILED %s"),*Why); return false; };
    const int64 Bytes=IFileManager::Get().FileSize(*Path);
    if (Bytes<=0 || Bytes>8*1024*1024) return Fail(TEXT("Missing or oversized local scene manifest"));
    FString Text; TSharedPtr<FJsonObject> Document;
    if (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Document) || !Document) return Fail(TEXT("Invalid scene JSON"));
    double Version=0; FString Units,Coordinates;
    if (!Scalar(Document,TEXT("schema_version"),Version) || Version!=1 || !Document->TryGetStringField(TEXT("units"),Units) || Units!=TEXT("cm") || !Document->TryGetStringField(TEXT("coordinates"),Coordinates) || Coordinates!=TEXT("Unreal X-forward Y-right Z-up")) return Fail(TEXT("Expected scene schema 1 with Unreal centimetre coordinates"));
    if (!Camera(Document,TEXT("observer"),ObserverLocation,ObserverLookAt) || !Camera(Document,TEXT("sensor"),SensorLocation,SensorLookAt,&SensorFOV)) return Fail(TEXT("Invalid observer or fixed sensor camera"));
    const TArray<TSharedPtr<FJsonValue>> *JointRows=nullptr,*MeshRows=nullptr;
    if (!Document->TryGetArrayField(TEXT("joints"),JointRows) || JointRows->Num()>256 || !Document->TryGetArrayField(TEXT("components"),MeshRows) || MeshRows->IsEmpty() || MeshRows->Num()>4096) return Fail(TEXT("Expected 1..4096 components and at most 256 joints"));
    TArray<FJointSpec> Joints; TArray<FMeshSpec> Meshes; TSet<FName> Ids;
    int32 ApproachJointCount=0;
    for (const auto& Row:*JointRows) {
        const auto O=Row->AsObject(); FJointSpec J;
        const TArray<TSharedPtr<FJsonValue>>* Range=nullptr;
        if (!O || !O->TryGetStringField(TEXT("id"),J.Id) || !Identifier(J.Id) || Ids.Contains(FName(*J.Id)) || !O->TryGetStringField(TEXT("parent_joint"),J.Parent) || !Vector(O,TEXT("pivot_cm"),J.Pivot) || !Rotation(O,TEXT("rotation_deg"),J.Rotation) || !Vector(O,TEXT("axis"),J.Axis) || J.Axis.IsNearlyZero() || !O->TryGetArrayField(TEXT("range_deg"),Range) || Range->Num()!=2 || !(*Range)[0]->TryGetNumber(J.Minimum) || !(*Range)[1]->TryGetNumber(J.Maximum) || !Scalar(O,TEXT("period_seconds"),J.Period)) return Fail(TEXT("Invalid or duplicate joint specification"));
        if (O->HasField(TEXT("phase_deg")) && !Scalar(O,TEXT("phase_deg"),J.Phase)) return Fail(TEXT("Invalid joint phase"));
        if (!FMath::IsFinite(J.Minimum) || !FMath::IsFinite(J.Maximum) || J.Minimum>J.Maximum || J.Minimum< -180 || J.Maximum>180 || J.Period<2 || J.Period>300) return Fail(TEXT("Joint motion exceeds bounded preview limits"));
        if (O->HasField(TEXT("rest_angle_deg"))) {
            if (!Scalar(O,TEXT("rest_angle_deg"),J.RestAngle) || !Scalar(O,TEXT("weld_amplitude_deg"),J.WeldAmplitude) || J.WeldAmplitude<0 || J.WeldAmplitude>2 || J.RestAngle<J.Minimum || J.RestAngle>J.Maximum) return Fail(TEXT("Invalid process pose or precision motion bounds"));
        } else {
            // Older generated manifests still have the same reconstructed joint names.
            J.RestAngle=J.Id.EndsWith(TEXT("_base")) ? 180 : J.Id.EndsWith(TEXT("_shoulder")) ? 25 : J.Id.EndsWith(TEXT("_elbow")) ? -65 : 0;
            J.WeldAmplitude=J.Id.EndsWith(TEXT("_wrist")) ? .55 : J.Id.EndsWith(TEXT("_elbow")) ? .16 : 0;
            J.Minimum=FMath::Min(J.RestAngle,-J.WeldAmplitude); J.Maximum=FMath::Max(J.RestAngle,J.WeldAmplitude);
        }
        if (O->HasField(TEXT("weld_angles_deg"))) {
            const TArray<TSharedPtr<FJsonValue>>* Angles=nullptr;
            if (!O->TryGetArrayField(TEXT("weld_angles_deg"),Angles) || Angles->Num()!=5) return Fail(TEXT("Expected five solved seam joint angles"));
            for (const auto& Value:*Angles) { double A=0; if (!Value->TryGetNumber(A) || !FMath::IsFinite(A) || FMath::Abs(A)>12 || A<J.Minimum || A>J.Maximum) return Fail(TEXT("Solved working joint angle exceeds bounds")); J.WorkAngles.Add(A); }
        } else if (-J.WeldAmplitude<J.Minimum || J.WeldAmplitude>J.Maximum) return Fail(TEXT("Precision motion outside joint limits"));
        if (O->HasField(TEXT("approach_angles_deg"))) {
            const TArray<TSharedPtr<FJsonValue>>* Angles=nullptr;
            if (!O->TryGetArrayField(TEXT("approach_angles_deg"),Angles) || Angles->Num()!=3)
                return Fail(TEXT("Expected exactly three authored approach joint angles"));
            for (const auto& Value:*Angles) {
                double A=0;
                if (!Value->TryGetNumber(A) || !FMath::IsFinite(A) || FMath::Abs(A)>180 || A<J.Minimum || A>J.Maximum)
                    return Fail(TEXT("Authored approach joint angle exceeds bounds"));
                J.ApproachAngles.Add(A);
            }
            ++ApproachJointCount;
        }
        J.Axis.Normalize(); Ids.Add(FName(*J.Id)); Joints.Add(J);
    }
    if (ApproachJointCount>0 && ApproachJointCount!=Joints.Num())
        return Fail(TEXT("Approach keyframes must be provided for every joint or none"));
    TSet<FName> Resolved; TArray<FJointSpec> Ordered;
    while (Ordered.Num()<Joints.Num()) {
        bool Progress=false;
        for (const auto& J:Joints) if (!Resolved.Contains(FName(*J.Id)) && (J.Parent.IsEmpty() || Resolved.Contains(FName(*J.Parent)))) { Ordered.Add(J); Resolved.Add(FName(*J.Id)); Progress=true; }
        if (!Progress) return Fail(TEXT("Joint hierarchy has a cycle or missing parent"));
    }
    TArray<FEmitterSpec> Emitters;
    const TSharedPtr<FJsonObject>* Cycle=nullptr;
    if (Document->TryGetObjectField(TEXT("cycle"),Cycle)) {
        double Duration=0, CycleVersion=0;
        if (!Scalar(*Cycle,TEXT("version"),CycleVersion) || CycleVersion!=1 || !Scalar(*Cycle,TEXT("duration_seconds"),Duration) || (Duration!=LegacyCycleDurationSeconds && Duration!=CycleDurationSeconds) || !Scalar(*Cycle,TEXT("arrival_offset_cm"),ArrivalOffset) || !Scalar(*Cycle,TEXT("departure_offset_cm"),DepartureOffset) || ArrivalOffset> -1000 || ArrivalOffset< -10000 || DepartureOffset<1000 || DepartureOffset>10000) return Fail(TEXT("Invalid source process cycle or carriage travel (expected18s baseline or21s delayed recipe)"));
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        TSet<FName> EmitterIds;
        if (!(*Cycle)->TryGetArrayField(TEXT("emitters"),Rows) || Rows->Num()>32) return Fail(TEXT("Invalid torch emitter list"));
        for (const auto& Row:*Rows) {
            auto O=Row->AsObject(); FEmitterSpec E;
            if (!O || !O->TryGetStringField(TEXT("id"),E.Id) || !Identifier(E.Id) || EmitterIds.Contains(FName(*E.Id)) || !O->TryGetStringField(TEXT("parent_joint"),E.Parent) || !Resolved.Contains(FName(*E.Parent)) || !Vector(O,TEXT("location_cm"),E.Position) || !Vector(O,TEXT("direction"),E.Direction) || E.Direction.IsNearlyZero()) return Fail(TEXT("Invalid torch emitter pose"));
            if (O->HasField(TEXT("surface_positions_cm"))) {
                if (!VectorRows(O,TEXT("surface_positions_cm"),E.SurfacePositions) || !VectorRows(O,TEXT("surface_normals"),E.SurfaceNormals)) return Fail(TEXT("Invalid solved weld surface sites"));
                for (auto& N:E.SurfaceNormals) { if (N.IsNearlyZero()) return Fail(TEXT("Degenerate weld surface normal")); N.Normalize(); }
            }
            E.Direction.Normalize(); EmitterIds.Add(FName(*E.Id)); Emitters.Add(E);
        }
    }
    int32 ThermalCount=0;
    for (const auto& Row:*MeshRows) {
        const auto O=Row->AsObject(); FMeshSpec M;
        if (!O || !O->TryGetStringField(TEXT("id"),M.Id) || !Identifier(M.Id) || Ids.Contains(FName(*M.Id)) || !O->TryGetStringField(TEXT("asset_path"),M.Asset) || !M.Asset.StartsWith(TEXT("/Game/WeldingCell/")) || !O->TryGetStringField(TEXT("parent_joint"),M.Parent) || (!M.Parent.IsEmpty() && !Resolved.Contains(FName(*M.Parent))) || !Vector(O,TEXT("location_cm"),M.Position) || !Rotation(O,TEXT("rotation_deg"),M.Rotation)) return Fail(TEXT("Invalid mesh, asset path or parent joint"));
        M.bHolder=M.Id.StartsWith(TEXT("SM_CarriageHolder_"));
        if (O->HasField(TEXT("attachment"))) {
            FString Attachment;
            if (!O->TryGetStringField(TEXT("attachment"),Attachment) || Attachment!=TEXT("carriage") || !M.Parent.IsEmpty() || !M.bHolder) return Fail(TEXT("Unsupported carriage attachment"));
        }
        if (O->HasField(TEXT("scale_xyz")) && !Vector(O,TEXT("scale_xyz"),M.Scale)) return Fail(TEXT("Invalid component scale"));
        if (FMath::Min3(FMath::Abs(M.Scale.X),FMath::Abs(M.Scale.Y),FMath::Abs(M.Scale.Z))<.0001 || M.Scale.GetAbsMax()>1000) return Fail(TEXT("Degenerate component scale"));
        const TSharedPtr<FJsonObject>* Thermal=nullptr;
        if (O->HasField(TEXT("thermal"))) {
            bool Exchange=true;
            if (!O->TryGetObjectField(TEXT("thermal"),Thermal) || !Scalar(*Thermal,TEXT("temperature_c"),M.Temperature) || !Scalar(*Thermal,TEXT("emissivity"),M.Emissivity) || !(*Thermal)->TryGetBoolField(TEXT("simulate_exchange"),Exchange) || Exchange) return Fail(TEXT("Imported thermal profiles require explicit authored temperatures and simulate_exchange:false"));
            if ((*Thermal)->HasField(TEXT("core_edge_drop_c")) && !Scalar(*Thermal,TEXT("core_edge_drop_c"),M.Drop)) return Fail(TEXT("Invalid thermal edge drop"));
            if (M.Temperature< -100 || M.Temperature>500 || M.Emissivity<0 || M.Emissivity>1 || M.Drop<0 || M.Drop>100 || ++ThermalCount>255) return Fail(TEXT("Imported thermal profile outside bounds or more than 255 visual surfaces"));
            M.bThermal=true;
        }
        M.Mesh=LoadObject<UStaticMesh>(nullptr,*M.Asset);
        if (!M.Mesh) return Fail(TEXT("Missing generated static mesh: ")+M.Asset);
        Ids.Add(FName(*M.Id)); Meshes.Add(M);
    }
    // Validation and asset loading finish before any visible component is created.
    bDramaticApproach=ApproachJointCount>0;
    TMap<FName,USceneComponent*> ParentMap;
    for (const auto& J:Ordered) {
        auto* Component=NewObject<USceneComponent>(this,*J.Id);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetupAttachment(J.Parent.IsEmpty() ? SceneRoot.Get():ParentMap.FindChecked(FName(*J.Parent)));
        Component->SetRelativeLocation(J.Pivot); Component->SetRelativeRotation(J.Rotation); Component->RegisterComponent();
        JointComponents.Add(Component); ParentMap.Add(FName(*J.Id),Component);
        FJointMotion Motion; Motion.Component=Component; Motion.Id=J.Id; Motion.Axis=J.Axis; Motion.RestRotation=J.Rotation;
        Motion.Minimum=J.Minimum; Motion.Maximum=J.Maximum; Motion.RestAngle=J.RestAngle; Motion.WeldAmplitude=J.WeldAmplitude; Motion.WorkAngles=J.WorkAngles; Motion.ApproachAngles=J.ApproachAngles;
        JointMotion.Add(Motion);
    }
    for (const auto& M:Meshes) {
        auto* Component=NewObject<UStaticMeshComponent>(this,*M.Id);
        Component->SetMobility(EComponentMobility::Movable);
        const bool bBody=M.Id.StartsWith(TEXT("SM_BodyPanel_"));
        Component->SetupAttachment((bBody || M.bHolder) ? CarriageRoot.Get() : M.Parent.IsEmpty() ? SceneRoot.Get():ParentMap.FindChecked(FName(*M.Parent)));
        Component->SetStaticMesh(M.Mesh); Component->SetRelativeTransform(FTransform(M.Rotation,M.Position,M.Scale));
        Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Component->SetCollisionObjectType(ECC_WorldDynamic);
        Component->SetCollisionResponseToAllChannels(ECR_Ignore); Component->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Component->SetGenerateOverlapEvents(false);
        Component->SetSimulatePhysics(false); Component->SetLightingChannels(true,true,false);
        Component->RegisterComponent(); MeshComponents.Add(Component);
        if (bBody) { BodyComponents.Add(Component); WorkingBodyBounds+=Component->Bounds.GetBox(); }
        if (M.bHolder) HolderComponents.Add(Component);
        if (M.bThermal) { FDarkFactoryImportedThermalSurface S; S.Mesh=Component; S.TemperatureC=M.Temperature; S.Emissivity=M.Emissivity; S.CoreEdgeDropC=M.Drop; ThermalSurfaces.Add(S); }
    }
    for (const auto& E:Emitters) { FEmitterRig Rig; Rig.Id=E.Id; Rig.Joint=ParentMap.FindChecked(FName(*E.Parent)); Rig.Position=E.Position; Rig.Direction=E.Direction; Rig.SurfacePositions=E.SurfacePositions; Rig.SurfaceNormals=E.SurfaceNormals; EmitterRigs.Add(Rig); }
    if (EmitterRigs.IsEmpty()) for (const auto& M:Meshes) if (M.Id.EndsWith(TEXT("_tool")) && !M.Parent.IsEmpty()) {
        FEmitterRig Rig; Rig.Id=M.Id; Rig.Joint=ParentMap.FindChecked(FName(*M.Parent));
        const FBox B=M.Mesh->GetBoundingBox(); Rig.Position=FVector(B.GetCenter().X,B.GetCenter().Y,B.Min.Z); EmitterRigs.Add(Rig);
    }
    for (auto& Rig:EmitterRigs) {
        const FString ToolName=Rig.Id.StartsWith(TEXT("SM_")) ? Rig.Id : TEXT("SM_")+Rig.Id+TEXT("_tool");
        for (const auto& Mesh:MeshComponents) if (Mesh && Mesh->GetName()==ToolName) { Rig.SourceTool=Mesh.Get(); break; }
    }
    if (EmitterRigs.Num()!=4) return Fail(TEXT("The delayed welding preview requires four stable robot emitters"));
    for (auto& J:JointMotion) {
        J.RobotIndex=EmitterRigs.IndexOfByPredicate([&](const FEmitterRig& E){return J.Id.StartsWith(E.Id+TEXT("_"));});
        if (J.RobotIndex==INDEX_NONE) return Fail(TEXT("Joint has no stable robot timing identity"));
    }
    if (WorkingBodyBounds.IsValid) {
        ArrivalOffset=FMath::Min(ArrivalOffset,-950.-WorkingBodyBounds.Max.Y);
        DepartureOffset=FMath::Max(DepartureOffset,950.-WorkingBodyBounds.Min.Y);
        BuildCarriageCollision();
    }
    BuildHoses(ParentMap);
    if (!InitializeToolHeat()) return Fail(TEXT("Tool cooling materials missing; run setup_welding_tool_materials.py"));
    BuildRobotHeatCurves();
    if (!InitializeBodyWeldHeat()) return Fail(TEXT("Cannot bind body weld heat to source surfaces/materials; run setup_welding_tool_materials.py"));
    if (!InitializeMotorHeat()) return Fail(TEXT("Invalid casting motor-drive mapping or deterministic heat self-check; regenerate scene profiles with prepare_welding_cell.py --approach-only"));
    bLoaded=true; ManifestPath=FPaths::ConvertRelativePathToFull(Path); SetMotionTime(0);
    UE_LOG(LogTemp,Display,TEXT("DF_WELDING_READY meshes=%d joints=%d thermal_visual_profiles=%d inference=disabled manifest=%s"),MeshComponents.Num(),JointMotion.Num(),ThermalSurfaces.Num(),*ManifestPath);
    return true;
}

namespace {
double Smooth(double T) { T=FMath::Clamp(T,0.,1.); return T*T*T*(10.+T*(-15.+6.*T)); }
double ApproachAngle(double Rest,const TArray<double>& Keys,double Working,double NormalizedTime) {
    check(Keys.Num()==3);
    const double T=FMath::Clamp(NormalizedTime,0.,1.);
    const double Angles[] = {Rest,Keys[0],Keys[1],Keys[2],Working};
    int32 Segment=0;
    while (Segment<3 && T>ApproachTimes[Segment+1]) ++Segment;
    const double Alpha=Smooth((T-ApproachTimes[Segment])/(ApproachTimes[Segment+1]-ApproachTimes[Segment]));
    // Numeric interpolation preserves authored signed 180-degree base sweeps.
    return FMath::Lerp(Angles[Segment],Angles[Segment+1],Alpha);
}
void SeamBlend(double CycleSeconds,int32& Segment,double& Alpha) {
    const double Position=FMath::Clamp((CycleSeconds-4.5)/7.5,0.,1.)*4.;
    Segment=FMath::Min(static_cast<int32>(Position),3); Alpha=Smooth(Position-Segment);
}
}

FDarkFactoryRobotTiming ADarkFactoryWeldingCell::GetRobotTiming(int32 RobotIndex,double AbsoluteSeconds) const {
    FDarkFactoryRobotTiming Timing;
    if (!EmitterRigs.IsValidIndex(RobotIndex) || !FMath::IsFinite(AbsoluteSeconds) || AbsoluteSeconds<0) return Timing;
    Timing.Valid=true; Timing.RobotId=EmitterRigs[RobotIndex].Id; Timing.RobotIndex=RobotIndex;
    Timing.Randomized=bRandomizedRobotTiming; Timing.CycleDurationSeconds=GetCycleDurationSeconds();
    const double Time=FMath::Min(AbsoluteSeconds,1.e9);
    Timing.CycleIndex=static_cast<int32>(FMath::FloorToDouble(Time/Timing.CycleDurationSeconds));
    Timing.CycleSeconds=FMath::Fmod(Time,Timing.CycleDurationSeconds);
    Timing.ScheduleIndex=bRandomizedRobotTiming ? Timing.CycleIndex%RobotTimingBankCycles:0;
    if (bRandomizedRobotTiming) {
        // Fixed uint32 arithmetic gives a reproducible seed/robot/cycle schedule,
        // independent of tick rate, seeks, particle RNG and asset load order.
        auto Mix=[](uint32 V) { V^=V>>16; V*=0x7feb352du; V^=V>>15; V*=0x846ca68bu; return V^(V>>16); };
        const uint32 First=Mix(static_cast<uint32>(RobotTimingSeed)^((RobotIndex+1)*0x9e3779b9u)^((Timing.ScheduleIndex+1)*0x85ebca6bu));
        const uint32 Second=Mix(First^0x68bc21ebu);
        Timing.RestDelaySeconds=.2+.6*double(First&0x00ffffffu)/16777215.;
        Timing.PreWeldDelaySeconds=.5+1.5*double(Second&0x00ffffffu)/16777215.;
    }
    Timing.ApproachStartSeconds=3.5+Timing.RestDelaySeconds;
    Timing.ApproachEndSeconds=Timing.ApproachStartSeconds+GetRobotApproachDurationSeconds();
    Timing.WeldStartSeconds=Timing.ApproachEndSeconds+Timing.PreWeldDelaySeconds;
    Timing.WeldEndSeconds=Timing.WeldStartSeconds+7.5;
    Timing.WithdrawEndSeconds=Timing.WeldEndSeconds+1.;
    return Timing;
}
bool ADarkFactoryWeldingCell::SetRobotTimingForQA(bool Randomized) {
    if (bRandomizedRobotTiming==Randomized) return true;
    bRandomizedRobotTiming=Randomized;
    BuildRobotHeatCurves();
    if (!InitializeMotorHeat()) { UE_LOG(LogTemp,Error,TEXT("DF_ROBOT_TIMING_QA_INVALID_HEAT_MODEL")); return false; }
    SetMotionTime(MotionSeconds);
    return true;
}
double ADarkFactoryWeldingCell::RobotCanonicalPhase(int32 RobotIndex,double Seconds) const {
    const auto Timing=GetRobotTiming(RobotIndex,Seconds);
    if (!Timing.Valid) return 0.;
    const double T=Timing.CycleSeconds;
    if (!bRandomizedRobotTiming && !bProcessControlled) return T;
    if (T<Timing.ApproachStartSeconds) return 0.;
    if (T<Timing.ApproachEndSeconds) return 3.5+(T-Timing.ApproachStartSeconds)/GetRobotApproachDurationSeconds();
    if (T<Timing.WeldStartSeconds) return 4.5; // Hold the first solved contact pose, with heat/emission off.
    if (T<Timing.WeldEndSeconds) return 4.5+T-Timing.WeldStartSeconds;
    if (T<Timing.WithdrawEndSeconds) return 12.+T-Timing.WeldEndSeconds;
    return 13.;
}
double ADarkFactoryWeldingCell::GetCycleSeconds() const { return FMath::Fmod(MotionSeconds,GetCycleDurationSeconds()); }
int32 ADarkFactoryWeldingCell::GetCycleIndex() const { return static_cast<int32>(FMath::FloorToDouble(MotionSeconds/GetCycleDurationSeconds())); }
FString ADarkFactoryWeldingCell::GetCyclePhaseName() const {
    const double T=GetCycleSeconds();
    if (!bRandomizedRobotTiming)
        return T<3 ? TEXT("Body arriving") : T<3.5 ? TEXT("Body positioned") : T<4.5 ? TEXT("Robots approaching") : T<12 ? TEXT("Precision welding") : T<13 ? TEXT("Robots withdrawing") : T<13.5 ? TEXT("Robots parked") : T<17 ? TEXT("Body departing") : TEXT("Awaiting next body");
    if (T<3) return TEXT("Body arriving");
    if (T<GetCarriageDepartureStartSeconds()) {
        if (IsWelding()) return TEXT("Staggered precision welding");
        bool Moving=false,Waiting=false,AllParked=true;
        for (int32 I=0;I<EmitterRigs.Num();++I) {
            const auto R=GetRobotTiming(I,MotionSeconds);
            Moving|=(T>=R.ApproachStartSeconds && T<R.ApproachEndSeconds) || (T>=R.WeldEndSeconds && T<R.WithdrawEndSeconds);
            Waiting|=T>=R.ApproachEndSeconds && T<R.WeldStartSeconds; AllParked&=T>=R.WithdrawEndSeconds;
        }
        return Moving ? TEXT("Robots approaching / withdrawing") : Waiting ? TEXT("Robots waiting at weld sites") : AllParked ? TEXT("Robots parked") : TEXT("Body positioned / robot delay");
    }
    return T<GetCarriageDepartureEndSeconds() ? TEXT("Body departing"):TEXT("Awaiting next body");
}
bool ADarkFactoryWeldingCell::IsWelding() const {
    if (!bLoaded || BodyComponents.IsEmpty() || (bProcessControlled && !bProcessPermitWelding)) return false;
    if (bPanelAssembly) return !GetWeldEmitters().IsEmpty();
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        if (bProcessControlled && I==3) continue;
        const auto R=GetRobotTiming(I,MotionSeconds);
        if (R.CycleSeconds>=R.WeldStartSeconds && R.CycleSeconds<R.WeldEndSeconds) return true;
    }
    return false;
}
TArray<FDarkFactoryWeldEmitter> ADarkFactoryWeldingCell::GetWeldEmitters() const {
    TArray<FDarkFactoryWeldEmitter> Result;
    if (!bLoaded || BodyComponents.IsEmpty() || (bProcessControlled && !bProcessPermitWelding)) return Result;
    if (bPanelAssembly) {
        if (PanelPhase!=EDarkFactoryProcessPhase::Welding) return Result;
        for (int32 Spot=0;Spot<PanelWeldCount;++Spot) {
            const double Age=PanelPhaseSeconds-PanelWeldStartSeconds(Spot);
            if (Age<0 || Age>=.5) continue;
            const auto& Rig=EmitterRigs[PanelWelderRobot]; const auto* Joint=Rig.Joint.Get();
            if (!Joint) return Result;
            FDarkFactoryWeldEmitter E; E.Id=Rig.Id; E.EmitterIndex=PanelWelderRobot; E.SourceTool=Rig.SourceTool.Get();
            E.Position=Joint->GetComponentTransform().TransformPosition(Rig.Position);
            E.Direction=Joint->GetComponentTransform().TransformVectorNoScale(Rig.Direction).GetSafeNormal();
            E.SurfacePosition=CarriageRoot->GetComponentTransform().TransformPosition(PanelContacts[Spot]);
            E.SurfaceNormal=CarriageRoot->GetComponentTransform().TransformVectorNoScale(PanelContactNormals[Spot]).GetSafeNormal();
            E.WeldAgeSeconds=Age; E.WeldDurationSeconds=.5;
            E.Intensity=static_cast<float>(Smooth(Age/.04)*Smooth((.5-Age)/.04)); Result.Add(E);
        }
        return Result;
    }
    for (int32 I=0;I<EmitterRigs.Num();++I) if (auto* Joint=EmitterRigs[I].Joint.Get()) {
        if (bProcessControlled && I==3) continue;
        const auto Timing=GetRobotTiming(I,MotionSeconds);
        if (Timing.CycleSeconds<Timing.WeldStartSeconds || Timing.CycleSeconds>=Timing.WeldEndSeconds) continue;
        const double T=Timing.CycleSeconds-Timing.WeldStartSeconds;
        const double Envelope=Smooth(T/.18)*Smooth((7.5-T)/.18);
        int32 Segment=0; double Blend=0; SeamBlend(4.5+T,Segment,Blend);
        const auto& Rig=EmitterRigs[I]; FDarkFactoryWeldEmitter E; E.Id=Rig.Id; E.EmitterIndex=I;
        E.WeldAgeSeconds=T; E.WeldDurationSeconds=7.5; E.SourceTool=Rig.SourceTool.Get();
        E.Position=Joint->GetComponentTransform().TransformPosition(Rig.Position);
        E.Direction=Joint->GetComponentTransform().TransformVectorNoScale(Rig.Direction).GetSafeNormal();
        if (Rig.SurfacePositions.Num()==5 && Rig.SurfaceNormals.Num()==5) {
            E.SurfacePosition=CarriageRoot->GetComponentTransform().TransformPosition(FMath::Lerp(Rig.SurfacePositions[Segment],Rig.SurfacePositions[Segment+1],Blend));
            E.SurfaceNormal=CarriageRoot->GetComponentTransform().TransformVectorNoScale(FMath::Lerp(Rig.SurfaceNormals[Segment],Rig.SurfaceNormals[Segment+1],Blend)).GetSafeNormal();
        } else { E.SurfacePosition=E.Position; E.SurfaceNormal=-E.Direction; }
        E.Intensity=static_cast<float>(Envelope*(.86+.14*FMath::Sin(T*2*PI*13.+I*1.7)));
        Result.Add(E);
    }
    return Result;
}
FBox ADarkFactoryWeldingCell::GetCarriageBounds() const {
    return WorkingBodyBounds.IsValid ? WorkingBodyBounds.TransformBy(CarriageRoot->GetComponentTransform()) : FBox(ForceInit);
}
void ADarkFactoryWeldingCell::BuildCarriageCollision() {
    // Sparse shell approximations leave the cabin open. Render-mesh Visibility
    // queries supply spark contacts; these simple kinematic shapes push curtains.
    const FVector Min=WorkingBodyBounds.Min,Max=WorkingBodyBounds.Max,C=WorkingBodyBounds.GetCenter(),E=WorkingBodyBounds.GetExtent();
    auto Box=[&](const TCHAR* Name,const FVector& Centre,const FVector& Extent) {
        auto* Shape=NewObject<UBoxComponent>(this,Name);
        Shape->SetupAttachment(CarriageRoot); Shape->SetMobility(EComponentMobility::Movable);
        Shape->SetRelativeLocation(Centre); Shape->SetBoxExtent(Extent.GetAbs());
        Shape->SetCollisionObjectType(ECC_WorldDynamic); Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Shape->SetCollisionResponseToAllChannels(ECR_Ignore); Shape->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
        Shape->SetGenerateOverlapEvents(false); Shape->SetSimulatePhysics(false); Shape->SetHiddenInGame(true);
        Shape->RegisterComponent(); CarriageColliders.Add(Shape);
    };
    Box(TEXT("CarriageFloor"),FVector(C.X,C.Y,Min.Z+8),FVector(E.X-10,E.Y-10,8));
    Box(TEXT("CarriageSideLeft"),FVector(Min.X+12,C.Y,Min.Z+50),FVector(12,E.Y-15,43));
    Box(TEXT("CarriageSideRight"),FVector(Max.X-12,C.Y,Min.Z+50),FVector(12,E.Y-15,43));
    Box(TEXT("CarriageFront"),FVector(C.X,Min.Y+15,Min.Z+44),FVector(E.X-18,12,30));
    Box(TEXT("CarriageRear"),FVector(C.X,Max.Y-15,Min.Z+48),FVector(E.X-18,12,30));
    FBox Roof(ForceInit);
    for (const auto& Panel:BodyComponents) if (Panel && Panel->Bounds.GetBox().Min.Z>Max.Z-40) Roof+=Panel->Bounds.GetBox();
    if (Roof.IsValid) Box(TEXT("CarriageRoof"),FVector(Roof.GetCenter().X,Roof.GetCenter().Y,Roof.GetCenter().Z),FVector(Roof.GetExtent().X,Roof.GetExtent().Y,7));
    Box(TEXT("CarriageHood"),FVector(C.X,Min.Y+100,Min.Z+130),FVector(E.X-35,55,7));
    Box(TEXT("CarriageTrunk"),FVector(C.X,Max.Y-110,Min.Z+106),FVector(E.X-27,65,7));
    for (int32 I=0;I<HolderComponents.Num();++I) {
        const auto* Holder=HolderComponents[I].Get();
        const FBox Bounds=Holder->Bounds.GetBox();
        Box(*FString::Printf(TEXT("CarriageHolderCollision%d"),I),Bounds.GetCenter(),Bounds.GetExtent());
    }
}
void ADarkFactoryWeldingCell::BuildHoses(const TMap<FName,USceneComponent*>& Parents) {
    auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!Cylinder || !Base) return;
    auto* Material=UMaterialInstanceDynamic::Create(Base,this);
    Material->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.025f,.026f,.028f));
    Material->SetScalarParameterValue(TEXT("Roughness"),.95f);
    const TCHAR* Roles[]={TEXT("base"),TEXT("shoulder"),TEXT("elbow"),TEXT("forearm_roll"),TEXT("wrist"),TEXT("tool_roll")};
    const FVector Offsets[]={FVector(48,0,80),FVector(36,0,42),FVector(22,0,60),FVector(20,0,44),FVector(18,0,28),FVector(18,0,10)};
    for (const auto& E:EmitterRigs) {
        FString Prefix=E.Id;
        if (Prefix.StartsWith(TEXT("SM_"))) { Prefix.RightChopInline(3); Prefix.RemoveFromEnd(TEXT("_tool")); }
        USceneComponent* const* BaseJoint=Parents.Find(FName(*(Prefix+TEXT("_base"))));
        if (!BaseJoint) continue;
        const double Side=(*BaseJoint)->GetComponentLocation().X>=0 ? 1. : -1.;
        FHoseRig Rig;
        for (int32 I=0;I<6;++I) {
            USceneComponent* const* Joint=Parents.Find(FName(*(Prefix+TEXT("_")+Roles[I])));
            if (!Joint) break;
            FHoseAnchor A; A.Joint=*Joint; A.LocalPosition=Offsets[I]; A.LocalPosition.X*=Side; Rig.Anchors.Add(A);
        }
        if (Rig.Anchors.Num()!=6 || ThermalSurfaces.Num()+5>255) continue;
        for (int32 I=0;I<5;++I) {
            auto* Hose=NewObject<USplineMeshComponent>(this,*FString::Printf(TEXT("Hose_%s_%d"),*Prefix,I));
            Hose->SetMobility(EComponentMobility::Movable); Hose->SetupAttachment(SceneRoot);
            Hose->SetStaticMesh(Cylinder); Hose->SetMaterial(0,Material); Hose->SetForwardAxis(ESplineMeshAxis::Z,false);
            Hose->SetStartScale(FVector2D(.055,.055),false); Hose->SetEndScale(FVector2D(.055,.055),false);
            Hose->SetSplineUpDir(FVector(0,1,0),false); Hose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Hose->SetLightingChannels(true,true,false); Hose->RegisterComponent(); HoseComponents.Add(Hose); Rig.Sections.Add(Hose);
            FDarkFactoryImportedThermalSurface Surface; Surface.Mesh=Hose; Surface.TemperatureC=28; Surface.Emissivity=.94f; Surface.CoreEdgeDropC=0; ThermalSurfaces.Add(Surface);
        }
        HoseRigs.Add(MoveTemp(Rig));
        for (const auto& Mesh:MeshComponents) if (Mesh && Mesh->GetName()==TEXT("SM_")+Prefix+TEXT("_dress_pack")) {
            Mesh->SetVisibility(false); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
    UpdateHoses();
    UpdatePinchGun();
    UpdateToolHeat(MotionSeconds);
    UpdateBodyWeldHeat(MotionSeconds);
}
void ADarkFactoryWeldingCell::UpdateHoses() {
    for (const auto& Rig:HoseRigs) {
        TArray<FVector> P;
        for (const auto& Anchor:Rig.Anchors) if (auto* Joint=Anchor.Joint.Get()) P.Add(SceneRoot->GetComponentTransform().InverseTransformPosition(Joint->GetComponentTransform().TransformPosition(Anchor.LocalPosition)));
        if (P.Num()!=Rig.Anchors.Num()) continue;
        for (int32 I=0;I<Rig.Sections.Num();++I) if (auto* Section=Rig.Sections[I].Get()) {
            const FVector StartTangent=I==0 ? P[1]-P[0] : (P[I+1]-P[I-1])*.5;
            const FVector EndTangent=I+2>=P.Num() ? P.Last()-P[P.Num()-2] : (P[I+2]-P[I])*.5;
            Section->SetStartAndEnd(P[I],StartTangent,P[I+1],EndTangent,true);
        }
    }
}
double ADarkFactoryWeldingCell::EvaluateJointAngle(const FJointMotion& J,double Seconds) const {
    const double T=RobotCanonicalPhase(J.RobotIndex,Seconds);
    // Shared by actual mesh motion and the deterministic motor-duty precomputation.
    const double BasePullback=T<3.5 ? 1. : T<4.1 ? 1.-Smooth((T-3.5)/.6) : T<12.4 ? 0. : T<13. ? Smooth((T-12.4)/.6) : 1.;
    const double ArmPullback=T<4.1 ? 1. : T<4.5 ? 1.-Smooth((T-4.1)/.4) : T<12. ? 0. : T<12.4 ? Smooth((T-12.)/.4) : 1.;
    const double WeldT=FMath::Clamp((T-4.5)/7.5,0.,1.);
    const double Precision=T>=4.5 && T<12. && !BodyComponents.IsEmpty() ? FMath::Square(FMath::Sin(PI*WeldT))*FMath::Sin(2*PI*2*WeldT) : 0.;
    int32 SeamSegment=0; double SeamAlpha=0; SeamBlend(T,SeamSegment,SeamAlpha);
    const double Working=J.WorkAngles.Num()==5 ? FMath::Lerp(J.WorkAngles[SeamSegment],J.WorkAngles[SeamSegment+1],SeamAlpha) : J.WeldAmplitude*Precision;
    double Angle=Working;
    if (bDramaticApproach) {
        if (T<4.5 || T>=12.) Angle=ApproachAngle(J.RestAngle,J.ApproachAngles,Working,T<4.5 ? T-3.5:13.-T);
    } else {
        const double Pullback=J.Id.EndsWith(TEXT("_base")) ? BasePullback:ArmPullback;
        Angle=J.RestAngle*Pullback+Working*(1.-Pullback);
    }
    return FMath::Clamp(Angle,J.Minimum,J.Maximum);
}
void ADarkFactoryWeldingCell::SetMotionTime(double Seconds) {
    if (!FMath::IsFinite(Seconds) || Seconds<0) return;
    const double Previous=MotionSeconds;
    MotionSeconds=FMath::Min(Seconds,1.e9);
    const double T=GetCycleSeconds();
    for (auto& J:JointMotion) if (auto* Component=J.Component.Get()) {
        J.Angle=bPanelAssembly && J.RobotIndex!=3 ? PanelJointAngle(J):EvaluateJointAngle(J,MotionSeconds);
        if (bProcessControlled && J.RobotIndex==3) {
            const double Blend=Smooth(ProcessInspectionExtension);
            const int32 ScanJoint=InspectionJointIndices.IndexOfByKey(static_cast<int32>(&J-JointMotion.GetData()));
            const double Active=bInspectionScanConfigured && ScanJoint!=INDEX_NONE
                ? InspectionScanAngle(ScanJoint,ProcessClock):J.RestAngle;
            if (bPanelAssembly && bPanelInspectionNIR && InspectionRequestedSpot==2 && ScanJoint!=INDEX_NONE && ProcessInspectionExtension<1.) {
                // Retract the final NIR pose via spot0: the direct diagonal
                // sweep crosses the adjacent parked welder. Endpoints, total
                // withdrawal time and settled capture poses stay unchanged.
                const double Via=PanelNIRInspectionPoses[0][ScanJoint];
                J.Angle=ProcessInspectionExtension>.5
                    ? FMath::Lerp(Via,Active,Smooth(2.*ProcessInspectionExtension-1.))
                    : FMath::Lerp(J.RestAngle,Via,Smooth(2.*ProcessInspectionExtension));
            } else J.Angle=FMath::Lerp(J.RestAngle,Active,Blend);
        }
        Component->SetRelativeRotation(J.RestRotation*FQuat(J.Axis,FMath::DegreesToRadians(J.Angle)));
    }
    const double DepartureStart=GetCarriageDepartureStartSeconds(),DepartureEnd=GetCarriageDepartureEndSeconds();
    const double Offset=bProcessControlled ? ProcessCarriageOffset : T<3 ? ArrivalOffset*(1.-Smooth(T/3.)) : T<DepartureStart ? 0. : T<DepartureEnd ? DepartureOffset*Smooth((T-DepartureStart)/3.5) : DepartureOffset;
    const bool Visible=bProcessControlled ? bProcessVisible:T<DepartureEnd;
    // Explicit seeks and the offscreen cycle reset teleport; ordinary tick travel
    // moves kinematic bodies normally so Chaos receives their surface velocity.
    const bool Jump=FMath::Abs(MotionSeconds-Previous)>.2 || (GetCycleIndex()!=static_cast<int32>(FMath::FloorToDouble(Previous/GetCycleDurationSeconds())));
    if (!Visible) for (const auto& Box:CarriageColliders) if (Box) Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CarriageRoot->SetRelativeLocation(FVector(0,Offset,0),false,nullptr,Jump ? ETeleportType::TeleportPhysics : ETeleportType::None);
    if (Visible!=bCarVisible) {
        bCarVisible=Visible;
        for (const auto& Panel:BodyComponents) if (Panel) {
            const bool Show=Visible && !(bPanelAssembly && Panel->GetFName()==FName(ProcessRemovedRoof));
            Panel->SetVisibility(Show); Panel->SetCollisionEnabled(Show ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
        }
        for (const auto& Holder:HolderComponents) if (Holder) { Holder->SetVisibility(Visible); Holder->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); }
        for (const auto& Box:CarriageColliders) if (Box) Box->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
    UpdateHoses();
    UpdatePinchGun();
    UpdateToolHeat(MotionSeconds);
    UpdateBodyWeldHeat(MotionSeconds);
    UpdateMotorHeat(MotionSeconds);
}
void ADarkFactoryWeldingCell::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (bLoaded && bAnimating && !bProcessControlled) SetMotionTime(MotionSeconds+FMath::Clamp(DeltaSeconds,0.f,.1f));
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildRobotTimingMetadata(double AbsoluteSeconds) const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("profile"),bRandomizedRobotTiming ? TEXT("seeded-robot-delays-v1"):TEXT("canonical-synchronized-qa-v1"));
    Result->SetBoolField(TEXT("randomized"),bRandomizedRobotTiming); Result->SetNumberField(TEXT("seed"),RobotTimingSeed);
    Result->SetNumberField(TEXT("schedule_bank_cycles"),bRandomizedRobotTiming ? RobotTimingBankCycles:1);
    Result->SetNumberField(TEXT("cycle_duration_seconds"),GetCycleDurationSeconds());
    Result->SetNumberField(TEXT("approach_duration_seconds"),GetRobotApproachDurationSeconds());
    Result->SetNumberField(TEXT("approach_speed_scale"),bProcessControlled ? ProcessApproachSpeedScale:1.);
    Result->SetNumberField(TEXT("carriage_departure_start_seconds"),GetCarriageDepartureStartSeconds());
    Result->SetNumberField(TEXT("carriage_departure_end_seconds"),GetCarriageDepartureEndSeconds());
    Result->SetStringField(TEXT("scope"),bProcessControlled
        ? TEXT("Process-only 1.25s approach / 7.5s welding / 1s withdrawal; unchanged seeded delays; carriage held until all robots park")
        : bRandomizedRobotTiming
        ? TEXT("Per-robot seeded rest departure and pre-weld dwell; full1s approach/7.5s welding/1s withdrawal;16-cycle schedule repeats; carriage held until all robots park")
        : TEXT("Explicit QA-only18s synchronized fixture; does not validate default randomized timing"));
    TArray<TSharedPtr<FJsonValue>> Robots;
    double LatestParked=0;
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        const auto R=GetRobotTiming(I,AbsoluteSeconds); auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"),R.RobotId); Row->SetNumberField(TEXT("robot_index"),R.RobotIndex);
        Row->SetNumberField(TEXT("cycle_index"),R.CycleIndex); Row->SetNumberField(TEXT("schedule_index"),R.ScheduleIndex);
        Row->SetNumberField(TEXT("cycle_seconds"),R.CycleSeconds);
        Row->SetNumberField(TEXT("rest_delay_seconds"),R.RestDelaySeconds); Row->SetNumberField(TEXT("pre_weld_delay_seconds"),R.PreWeldDelaySeconds);
        Row->SetNumberField(TEXT("approach_start_seconds"),R.ApproachStartSeconds); Row->SetNumberField(TEXT("approach_end_seconds"),R.ApproachEndSeconds);
        Row->SetNumberField(TEXT("weld_start_seconds"),R.WeldStartSeconds); Row->SetNumberField(TEXT("weld_end_seconds"),R.WeldEndSeconds);
        Row->SetNumberField(TEXT("withdraw_end_seconds"),R.WithdrawEndSeconds);
        const bool Active=R.CycleSeconds>=R.WeldStartSeconds && R.CycleSeconds<R.WeldEndSeconds;
        Row->SetBoolField(TEXT("welding_active"),Active); Row->SetNumberField(TEXT("weld_age_seconds"),FMath::Clamp(R.CycleSeconds-R.WeldStartSeconds,0.,7.5));
        const TCHAR* Phase=R.CycleSeconds<R.ApproachStartSeconds ? TEXT("rest_delay") : R.CycleSeconds<R.ApproachEndSeconds ? TEXT("approach")
            : R.CycleSeconds<R.WeldStartSeconds ? TEXT("pre_weld_wait") : R.CycleSeconds<R.WeldEndSeconds ? TEXT("welding")
            : R.CycleSeconds<R.WithdrawEndSeconds ? TEXT("withdrawal"):TEXT("parked");
        Row->SetStringField(TEXT("phase"),Phase); Robots.Add(MakeShared<FJsonValueObject>(Row));
        LatestParked=FMath::Max(LatestParked,R.WithdrawEndSeconds);
    }
    Result->SetNumberField(TEXT("latest_robot_parked_seconds"),LatestParked);
    Result->SetNumberField(TEXT("carriage_clearance_margin_seconds"),GetCarriageDepartureStartSeconds()-LatestParked);
    Result->SetArrayField(TEXT("robots"),Robots);
    return Result;
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildPreviewMetadata() const {
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("scope"),TEXT("Authored timed visual process; approximate rigid-link poses and hose surrogates, no industrial motion calibration, inference or training contract"));
    O->SetStringField(TEXT("manifest"),ManifestPath);
    O->SetNumberField(TEXT("mesh_count"),MeshComponents.Num()); O->SetNumberField(TEXT("joint_count"),JointMotion.Num());
    O->SetNumberField(TEXT("thermal_visual_profiles"),ThermalSurfaces.Num()); O->SetNumberField(TEXT("motion_seconds"),MotionSeconds);
    O->SetBoolField(TEXT("animation_enabled"),bAnimating); O->SetBoolField(TEXT("inference_enabled"),false); O->SetBoolField(TEXT("training_enabled"),false);
    O->SetNumberField(TEXT("cycle_duration_seconds"),GetCycleDurationSeconds()); O->SetNumberField(TEXT("empty_station_dwell_seconds"),GetCycleDurationSeconds()-GetCarriageDepartureEndSeconds()); O->SetNumberField(TEXT("cycle_index"),GetCycleIndex()); O->SetNumberField(TEXT("body_index"),GetCycleIndex());
    O->SetNumberField(TEXT("cycle_seconds"),GetCycleSeconds()); O->SetStringField(TEXT("cycle_phase"),GetCyclePhaseName());
    O->SetBoolField(TEXT("welding_active"),IsWelding()); O->SetBoolField(TEXT("body_visible"),bCarVisible);
    O->SetNumberField(TEXT("carriage_holder_count"),HolderComponents.Num());
    O->SetNumberField(TEXT("robot_approach_seconds"),GetRobotApproachDurationSeconds()); O->SetNumberField(TEXT("robot_withdraw_seconds"),1.);
    O->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(MotionSeconds));
    O->SetStringField(TEXT("motion_profile"),bDramaticApproach ? TEXT("dramatic-approach-v1"):TEXT("staged-fold-yaw-v1"));
    O->SetStringField(TEXT("parking_sequence"),bProcessControlled
        ? TEXT("Three authored approach poses over 1.25 seconds; reverse over one second, with unchanged rest and weld endpoints")
        : bDramaticApproach
        ? TEXT("Three authored approach poses over one second; reverse poses on withdrawal, with unchanged rest and weld endpoints")
        : TEXT("Fold arm, then rotate base 180 degrees; reverse for approach"));
    TArray<TSharedPtr<FJsonValue>> ApproachTimeline;
    if (bDramaticApproach) for (double Time:ApproachTimes) ApproachTimeline.Add(MakeShared<FJsonValueNumber>(Time));
    O->SetArrayField(TEXT("approach_keyframe_normalized_times"),ApproachTimeline);
    TArray<TSharedPtr<FJsonValue>> HolderRows;
    for (const auto& Holder:HolderComponents) if (Holder) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("id"),Holder->GetName());
        Row->SetArrayField(TEXT("local_position_cm"),Vec(Holder->GetRelativeLocation()));
        Row->SetArrayField(TEXT("world_position_cm"),Vec(Holder->GetComponentLocation()));
        Row->SetBoolField(TEXT("attached_to_carriage"),Holder->GetAttachParent()==CarriageRoot);
        Row->SetBoolField(TEXT("visible"),Holder->IsVisible()); HolderRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    O->SetArrayField(TEXT("carriage_holders"),HolderRows);
    O->SetNumberField(TEXT("body_panel_count"),BodyComponents.Num()); O->SetNumberField(TEXT("carriage_collision_shapes"),CarriageColliders.Num());
    O->SetNumberField(TEXT("carriage_offset_y_cm"),CarriageRoot->GetRelativeLocation().Y);
    O->SetNumberField(TEXT("arrival_offset_y_cm"),ArrivalOffset); O->SetNumberField(TEXT("departure_offset_y_cm"),DepartureOffset);
    O->SetNumberField(TEXT("authored_hose_sections"),HoseComponents.Num()); O->SetStringField(TEXT("hose_representation"),TEXT("Five smooth joint-anchored spline sections per robot; original rigid dress-pack assets retained hidden"));
    if (WorkingBodyBounds.IsValid) {
        const FBox Current=GetCarriageBounds(); O->SetArrayField(TEXT("carriage_bounds_min_cm"),Vec(Current.Min)); O->SetArrayField(TEXT("carriage_bounds_max_cm"),Vec(Current.Max));
        O->SetArrayField(TEXT("working_body_bounds_min_cm"),Vec(WorkingBodyBounds.Min)); O->SetArrayField(TEXT("working_body_bounds_max_cm"),Vec(WorkingBodyBounds.Max));
    }
    O->SetArrayField(TEXT("observer_location_cm"),Vec(ObserverLocation)); O->SetArrayField(TEXT("sensor_location_cm"),Vec(SensorLocation));
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& J:JointMotion) {
        auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"),J.Id);
        V->SetNumberField(TEXT("angle_deg"),J.Angle); V->SetNumberField(TEXT("minimum_deg"),J.Minimum); V->SetNumberField(TEXT("maximum_deg"),J.Maximum);
        V->SetNumberField(TEXT("rest_angle_deg"),J.RestAngle); V->SetNumberField(TEXT("weld_amplitude_deg"),J.WeldAmplitude);
        TArray<TSharedPtr<FJsonValue>> Approach;
        for (double Angle:J.ApproachAngles) Approach.Add(MakeShared<FJsonValueNumber>(Angle));
        V->SetArrayField(TEXT("approach_angles_deg"),Approach);
        TArray<TSharedPtr<FJsonValue>> Weld;
        for (double Angle:J.WorkAngles) Weld.Add(MakeShared<FJsonValueNumber>(Angle));
        V->SetArrayField(TEXT("weld_angles_deg"),Weld);
        if (const auto* Joint=J.Component.Get()) V->SetArrayField(TEXT("joint_pivot_world_cm"),Vec(Joint->GetComponentLocation()));
        Values.Add(MakeShared<FJsonValueObject>(V));
    }
    O->SetArrayField(TEXT("joints"),Values);
    Values.Reset();
    // Present outside the active weld interval as well, for pose/trajectory QA.
    for (const auto& E:EmitterRigs) if (const auto* Joint=E.Joint.Get()) {
        auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"),E.Id);
        V->SetArrayField(TEXT("position_cm"),Vec(Joint->GetComponentTransform().TransformPosition(E.Position)));
        V->SetArrayField(TEXT("direction"),Vec(Joint->GetComponentTransform().TransformVectorNoScale(E.Direction).GetSafeNormal()));
        Values.Add(MakeShared<FJsonValueObject>(V));
    }
    O->SetArrayField(TEXT("robot_tips"),Values);
    Values.Reset();
    for (const auto& E:GetWeldEmitters()) { auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"),E.Id); V->SetStringField(TEXT("source_tool"),E.SourceTool ? E.SourceTool->GetName():TEXT("")); V->SetArrayField(TEXT("position_cm"),Vec(E.Position)); V->SetArrayField(TEXT("direction"),Vec(E.Direction)); V->SetArrayField(TEXT("surface_position_cm"),Vec(E.SurfacePosition)); V->SetArrayField(TEXT("surface_normal"),Vec(E.SurfaceNormal)); V->SetNumberField(TEXT("tip_to_surface_cm"),FVector::Distance(E.Position,E.SurfacePosition)); V->SetNumberField(TEXT("intensity"),E.Intensity); V->SetNumberField(TEXT("emitter_index"),E.EmitterIndex); V->SetNumberField(TEXT("weld_age_seconds"),E.WeldAgeSeconds); V->SetNumberField(TEXT("weld_duration_seconds"),E.WeldDurationSeconds); Values.Add(MakeShared<FJsonValueObject>(V)); }
    O->SetArrayField(TEXT("active_weld_emitters"),Values); O->SetObjectField(TEXT("tool_heat"),BuildToolHeatMetadata()); O->SetObjectField(TEXT("body_weld_heat"),BuildBodyWeldHeatMetadata()); O->SetObjectField(TEXT("motor_heat"),BuildMotorHeatMetadata()); return O;
}
namespace {
constexpr float ToolHeatRadiusCm=26.f;
}
void ADarkFactoryWeldingCell::BuildRobotHeatCurves() {
    const double Period=GetCycleDurationSeconds();
    const int32 BankCycles=bRandomizedRobotTiming ? RobotTimingBankCycles:1;
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        auto Build=[&](FToolHeatCurve& Curve,double Target,double HeatingTau,double CoolingTau) {
            Curve.Target=Target; Curve.HeatingTau=HeatingTau; Curve.CoolingTau=CoolingTau;
            const double HeatingDecay=FMath::Exp(-7.5/HeatingTau);
            Curve.CycleDecay=HeatingDecay*FMath::Exp(-(Period-7.5)/CoolingTau);
            Curve.BankDecay=FMath::Pow(Curve.CycleDecay,BankCycles);
            Curve.PrefixRise.SetNum(BankCycles+1); Curve.PrefixRise[0]=0;
            for (int32 Cycle=0;Cycle<BankCycles;++Cycle) {
                const auto Timing=GetRobotTiming(I,Cycle*Period);
                const double AddedRise=(Target-20.)*(1.-HeatingDecay)*FMath::Exp(-(Period-Timing.WeldEndSeconds)/CoolingTau);
                Curve.PrefixRise[Cycle+1]=Curve.CycleDecay*Curve.PrefixRise[Cycle]+AddedRise;
            }
        };
        Build(EmitterRigs[I].BodyCurve,105.,2.,3.8); Build(EmitterRigs[I].TipCurve,500.,.8,2.3);
    }
    bToolHeatContinuous=true;
    for (int32 I=0;I<EmitterRigs.Num();++I) for (int32 Cycle=0;Cycle<=BankCycles;++Cycle) {
        const auto Timing=GetRobotTiming(I,Cycle*Period);
        for (double Phase:{Timing.WeldStartSeconds,Timing.WeldEndSeconds,Period}) {
            const double Boundary=Cycle*Period+Phase;
            for (const FToolHeatCurve* Curve:{&EmitterRigs[I].BodyCurve,&EmitterRigs[I].TipCurve})
                bToolHeatContinuous &= FMath::Abs(EvaluateToolTemperature(I,*Curve,Boundary-.00001)-EvaluateToolTemperature(I,*Curve,Boundary+.00001))<.02;
        }
    }
}
double ADarkFactoryWeldingCell::EvaluateToolTemperature(int32 RobotIndex,const FToolHeatCurve& Curve,double Seconds) const {
    if (bProcessControlled) {
        if (RobotIndex==3) return 20.;
        const auto Timing=GetRobotTiming(RobotIndex,MotionSeconds);
        const float Initial=Curve.Target>200 ? ProcessInitialTipC[RobotIndex]:ProcessInitialBodyC[RobotIndex];
        const double Age=FMath::Max(0.,ProcessClock-ProcessBirthClock);
        if (!bProcessWeldStarted || ProcessClock<ProcessWeldEpoch+Timing.WeldStartSeconds)
            return 20.+(Initial-20.)*FMath::Exp(-Age/Curve.CoolingTau);
        const double Delay=FMath::Max(0.,ProcessWeldEpoch+Timing.WeldStartSeconds-ProcessBirthClock);
        const double Start=(Initial-20.)*FMath::Exp(-Delay/Curve.CoolingTau);
        const double HeatingAge=FMath::Clamp(ProcessClock-ProcessWeldEpoch-Timing.WeldStartSeconds,0.,7.5);
        const double Rise=(Curve.Target-20.)+(Start-(Curve.Target-20.))*FMath::Exp(-HeatingAge/Curve.HeatingTau);
        return 20.+Rise*FMath::Exp(-FMath::Max(0.,ProcessClock-ProcessWeldEpoch-Timing.WeldEndSeconds)/Curve.CoolingTau);
    }
    const auto Timing=GetRobotTiming(RobotIndex,Seconds);
    if (!Timing.Valid || Curve.PrefixRise.IsEmpty()) return 20.;
    const int32 BankCycles=Curve.PrefixRise.Num()-1;
    const double Banks=FMath::FloorToDouble(double(Timing.CycleIndex)/BankCycles);
    const int32 BankCycle=Timing.CycleIndex%BankCycles;
    // Compose complete seeded banks in constant time; the partial cycle is exact.
    const double BankInitial=Curve.PrefixRise.Last()*(1.-FMath::Pow(Curve.BankDecay,Banks))/(1.-Curve.BankDecay);
    const double Initial=FMath::Pow(Curve.CycleDecay,BankCycle)*BankInitial+Curve.PrefixRise[BankCycle];
    const double T=Timing.CycleSeconds,TargetRise=Curve.Target-20.;
    if (T<Timing.WeldStartSeconds) return 20.+Initial*FMath::Exp(-T/Curve.CoolingTau);
    const double Start=Initial*FMath::Exp(-Timing.WeldStartSeconds/Curve.CoolingTau);
    const double HeatingAge=FMath::Min(T-Timing.WeldStartSeconds,7.5);
    const double Rise=TargetRise+(Start-TargetRise)*FMath::Exp(-HeatingAge/Curve.HeatingTau);
    return 20.+Rise*FMath::Exp(-FMath::Max(0.,T-Timing.WeldEndSeconds)/Curve.CoolingTau);
}
bool ADarkFactoryWeldingCell::InitializeToolHeat() {
    for (auto& Rig:EmitterRigs) {
        auto* Tool=Rig.SourceTool.Get(); auto* Joint=Rig.Joint.Get();
        if (!Tool || !Joint) return false;
        Rig.HeatTipLocal=Tool->GetComponentTransform().InverseTransformPosition(Joint->GetComponentTransform().TransformPosition(Rig.Position));
        for (int32 Slot=0;Slot<Tool->GetNumMaterials();++Slot) {
            auto* Parent=Tool->GetMaterial(Slot); if (!Parent) continue;
            float DefaultGlow=0;
            if (!Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ToolHeatGlow")),DefaultGlow)) return false;
            auto* MID=UMaterialInstanceDynamic::Create(Parent,this);
            Tool->SetMaterial(Slot,MID); Rig.HeatMaterials.Add(MID);
        }
        if (Rig.HeatMaterials.IsEmpty()) return false;
    }
    return true;
}
void ADarkFactoryWeldingCell::BindThermalRig(ADarkFactoryThermalRig* Rig) {
    ToolThermalRig=Rig; BindPanelSharedProfiles(); UpdateToolHeat(ToolHeatSeconds); UpdateBodyWeldHeat(BodyWeldHeatSeconds); UpdateMotorHeat(MotorHeatSeconds,MotorIdleSeconds);
}
void ADarkFactoryWeldingCell::SetToolHeatTimeForQA(double Seconds) {
    if (FMath::IsFinite(Seconds) && Seconds>=0) UpdateToolHeat(FMath::Min(Seconds,1.e9));
}
void ADarkFactoryWeldingCell::UpdateToolHeat(double Seconds) {
    ToolHeatSeconds=Seconds;
    const double PanelDelta=bPanelAssembly && PanelToolHeatClock>=0 ? FMath::Max(0.,ProcessClock-PanelToolHeatClock):0.;
    if (bPanelAssembly && PanelToolHeatClock>ProcessClock) for (auto& Rig:EmitterRigs) { Rig.BodyC=Rig.TipC=20; }
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        auto& Rig=EmitterRigs[I]; auto* Tool=Rig.SourceTool.Get(); if (!Tool) continue;
        if (bPanelAssembly) {
            const bool Active=I==PanelWelderRobot && IsWelding();
            const double TargetBody=Active ? Rig.BodyCurve.Target:20.,TargetTip=Active ? Rig.TipCurve.Target:20.;
            Rig.BodyC=TargetBody+(Rig.BodyC-TargetBody)*FMath::Exp(-PanelDelta/(Active ? Rig.BodyCurve.HeatingTau:Rig.BodyCurve.CoolingTau));
            Rig.TipC=FMath::Max(Rig.BodyC,float(TargetTip+(Rig.TipC-TargetTip)*FMath::Exp(-PanelDelta/(Active ? Rig.TipCurve.HeatingTau:Rig.TipCurve.CoolingTau))));
        } else {
            Rig.BodyC=static_cast<float>(EvaluateToolTemperature(I,Rig.BodyCurve,Seconds));
            Rig.TipC=FMath::Max(Rig.BodyC,static_cast<float>(EvaluateToolTemperature(I,Rig.TipCurve,Seconds)));
        }
        const float Heat=FMath::Clamp((Rig.TipC-20.f)/480.f,0.f,1.f);
        Rig.Glow=150.f*Heat*Heat;
        const FVector Point=Tool->GetComponentTransform().TransformPosition(Rig.HeatTipLocal);
        for (const auto& Entry:Rig.HeatMaterials) if (auto* Material=Entry.Get()) {
            Material->SetVectorParameterValue(TEXT("ToolHotspotWorld"),FLinearColor(Point.X,Point.Y,Point.Z,1));
            Material->SetScalarParameterValue(TEXT("ToolHeatRadiusCm"),ToolHeatRadiusCm);
            Material->SetScalarParameterValue(TEXT("ToolHeatGlow"),Rig.Glow);
            Material->SetVectorParameterValue(TEXT("ToolGlowColor"),FLinearColor(1.f,.045f+.28f*FMath::Sqrt(Heat),.006f+.045f*Heat));
        }
        if (auto* Thermal=ToolThermalRig.Get()) Thermal->SetImportedToolHeat(Tool,Rig.BodyC,Rig.TipC,Rig.HeatTipLocal,ToolHeatRadiusCm);
    }
    if (bPanelAssembly) PanelToolHeatClock=ProcessClock;
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildToolHeatMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),TEXT("authored-first-order-cycle-cooling-v1"));
    Result->SetStringField(TEXT("scope"),TEXT("Illustrative tool thermal field and visible/NIR glow; not calibrated blackbody emission or measured welding temperatures"));
    Result->SetNumberField(TEXT("heat_time_seconds"),ToolHeatSeconds);
    Result->SetNumberField(TEXT("tip_cooling_tau_seconds"),2.3); Result->SetNumberField(TEXT("housing_cooling_tau_seconds"),3.8);
    Result->SetBoolField(TEXT("cycle_boundary_continuous"),bToolHeatContinuous);
    Result->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(ToolHeatSeconds));
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& Rig:EmitterRigs) if (const auto* Tool=Rig.SourceTool.Get()) {
        auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("id"),Rig.Id);
        Value->SetStringField(TEXT("surface"),Tool->GetName()); Value->SetNumberField(TEXT("stencil_id"),Tool->CustomDepthStencilValue);
        Value->SetNumberField(TEXT("body_temperature_c"),Rig.BodyC); Value->SetNumberField(TEXT("tip_temperature_c"),Rig.TipC);
        Value->SetNumberField(TEXT("glow_strength"),Rig.Glow); Value->SetNumberField(TEXT("heat_radius_cm"),ToolHeatRadiusCm);
        Value->SetArrayField(TEXT("tip_world_cm"),Vec(Tool->GetComponentTransform().TransformPosition(Rig.HeatTipLocal)));
        Value->SetArrayField(TEXT("tip_local_cm"),Vec(Rig.HeatTipLocal)); Values.Add(MakeShared<FJsonValueObject>(Value));
    }
    Result->SetArrayField(TEXT("tools"),Values); return Result;
}

double ADarkFactoryWeldingCell::EvaluateBodyWeldTemperature(int32 RobotIndex,double Seconds,bool Halo) const {
    // Each replacement body starts cold; only this robot's active weld heats its patch.
    if (bProcessControlled) {
        if (RobotIndex==3 || !bProcessWeldStarted) return 20.;
        const auto Timing=GetRobotTiming(RobotIndex,MotionSeconds);
        const double Age=ProcessClock-ProcessWeldEpoch-Timing.WeldStartSeconds;
        if (Age<=0) return 20.;
        const double Peak=Halo ? ProcessWeldHaloPeakC:ProcessWeldCorePeakC;
        const double CoolingTau=Halo ? 2.5:ProcessWeldCoreCoolingTauSeconds;
        return 20.+(Peak-20.)*(1.-FMath::Exp(-FMath::Min(Age,7.5)/.65))*FMath::Exp(-FMath::Max(0.,Age-7.5)/CoolingTau);
    }
    const auto Timing=GetRobotTiming(RobotIndex,Seconds);
    if (!Timing.Valid || Timing.CycleSeconds<=Timing.WeldStartSeconds) return 20.;
    const double Rise=400.*(1.-FMath::Exp(-FMath::Min(Timing.CycleSeconds-Timing.WeldStartSeconds,7.5)/.65));
    return 20.+Rise*FMath::Exp(-FMath::Max(0.,Timing.CycleSeconds-Timing.WeldEndSeconds)/2.5);
}
bool ADarkFactoryWeldingCell::InitializeBodyWeldHeat() {
    // One-time exact component queries avoid a per-frame mesh search. Bind the
    // middle of each validated seam, before the carriage leaves its work origin.
    for (const auto& Rig:EmitterRigs) {
        if (Rig.SurfacePositions.Num()!=5 || Rig.SurfaceNormals.Num()!=5) return false;
        const FVector Point=CarriageRoot->GetComponentTransform().TransformPosition(Rig.SurfacePositions[2]);
        const FVector Normal=CarriageRoot->GetComponentTransform().TransformVectorNoScale(Rig.SurfaceNormals[2]).GetSafeNormal();
        FHitResult Best; UStaticMeshComponent* Target=nullptr; double Error=.5;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BodyWeldBinding),true);
        for (const auto& Panel:BodyComponents) {
            if (!Panel || !Panel->Bounds.GetBox().ExpandBy(.5).IsInside(Point)) continue;
            // Editor asset loading may still be compiling a panel, which prevents
            // its collision state from existing. Finish only this candidate.
#if WITH_EDITOR
            if (Panel->GetStaticMesh()->IsCompiling()) {
                TArray<UStaticMesh*> Assets{Panel->GetStaticMesh()};
                FStaticMeshCompilingManager::Get().FinishCompilation(Assets);
            }
#endif
            // Registration may also defer Chaos creation until the end of the
            // frame. Finish only the candidate before this startup-only query.
            if (!Panel->IsPhysicsStateCreated()) { Panel->DestroyPhysicsState(); Panel->CreatePhysicsState(false); }
            FHitResult Hit;
            if ((Panel->LineTraceComponent(Hit,Point+Normal*3.,Point-Normal*3.,Query) ||
                Panel->LineTraceComponent(Hit,Point-Normal*3.,Point+Normal*3.,Query))) {
                const double Distance=FVector::Distance(Hit.ImpactPoint,Point);
                if (Distance<Error) { Error=Distance; Best=Hit; Target=Panel.Get(); }
            }
        }
        if (!Target) {
            UE_LOG(LogTemp,Error,TEXT("DF_BODY_WELD_BIND_FAILED %s point=%s normal=%s"),*Rig.Id,*Point.ToString(),*Normal.ToString());
            for (const auto& Panel:BodyComponents) if (Panel && Panel->Bounds.GetBox().ExpandBy(3).IsInside(Point))
                UE_LOG(LogTemp,Error,TEXT("DF_BODY_WELD_BIND_CANDIDATE %s physics=%d query=%d bounds=%s"),*Panel->GetName(),Panel->IsPhysicsStateCreated(),Panel->IsQueryCollisionEnabled(),*Panel->Bounds.GetBox().ToString());
            return false;
        }
        FBodyWeldSpot Spot; Spot.Id=Rig.Id; Spot.Mesh=Target;
        Spot.LocalPosition=Target->GetComponentTransform().InverseTransformPosition(Best.ImpactPoint);
        Spot.LocalNormal=Target->GetComponentTransform().InverseTransformVectorNoScale(Best.ImpactNormal).GetSafeNormal();
        Spot.ContactErrorCm=static_cast<float>(Error);
        const int32 SpotIndex=BodyWeldSpots.Add(Spot);
        FBodyWeldMaterial* Binding=BodyWeldMaterials.FindByPredicate([Target](const FBodyWeldMaterial& B){return B.Mesh.Get()==Target;});
        if (!Binding) {
            FBodyWeldMaterial New; New.Mesh=Target;
            for (int32 Slot=0;Slot<Target->GetNumMaterials();++Slot) {
                auto* Parent=Target->GetMaterial(Slot); if (!Parent) continue;
                float DefaultGlow=0;
                if (!Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("WeldHeatGlow0")),DefaultGlow)) return false;
                auto* Material=UMaterialInstanceDynamic::Create(Parent,this);
                Target->SetMaterial(Slot,Material); New.Materials.Add(Material);
            }
            if (New.Materials.IsEmpty()) return false;
            Binding=&BodyWeldMaterials[BodyWeldMaterials.Add(MoveTemp(New))];
        }
        Binding->SpotIndices.Add(SpotIndex);
        if (Binding->SpotIndices.Num()>4) return false;
    }
    return BodyWeldSpots.Num()==4;
}
void ADarkFactoryWeldingCell::SetBodyWeldHeatTimeForQA(double Seconds) {
    if (FMath::IsFinite(Seconds) && Seconds>=0) UpdateBodyWeldHeat(FMath::Min(Seconds,1.e9));
}
void ADarkFactoryWeldingCell::SetWeldWitnessEnabled(bool Enabled) {
    bWeldWitnessEnabled=Enabled; UpdateBodyWeldHeat(BodyWeldHeatSeconds);
}
void ADarkFactoryWeldingCell::UpdateBodyWeldHeat(double Seconds) {
    BodyWeldHeatSeconds=Seconds;
    // New assembly heat belongs to ProcessScene's current replacement panel.
    // Never stamp the previous four unrelated body sites or the shared cold rack profile.
    if (bPanelAssembly) return;
    for (auto& Spot:BodyWeldSpots) {
        const int32 RobotIndex=EmitterRigs.IndexOfByPredicate([&](const FEmitterRig& E){return E.Id==Spot.Id;});
        Spot.TemperatureC=static_cast<float>(EvaluateBodyWeldTemperature(RobotIndex,Seconds));
        Spot.HaloTemperatureC=bProcessControlled ? static_cast<float>(EvaluateBodyWeldTemperature(RobotIndex,Seconds,true)):Spot.TemperatureC;
        const auto Timing=GetRobotTiming(RobotIndex,Seconds);
        const double WitnessAge=bProcessControlled ? (!bProcessWeldStarted || RobotIndex==3 ? -1.:ProcessClock-ProcessWeldEpoch-Timing.WeldStartSeconds):Timing.CycleSeconds-Timing.WeldStartSeconds;
        Spot.WitnessStrength=bWeldWitnessEnabled ? FMath::Clamp(static_cast<float>((WitnessAge-.2)/.8),0.f,1.f):0.f;
        // Preserve the broad visible/NIR HAZ glow. The hotter exposed-metal
        // core is a separate compact thermal proxy, never a 12 cm molten patch.
        const float Incandescence=FMath::Clamp((Spot.HaloTemperatureC-70.f)/350.f,0.f,1.f);
        Spot.Glow=180.f*Incandescence*Incandescence;
    }
    for (const auto& Binding:BodyWeldMaterials) if (auto* Mesh=Binding.Mesh.Get()) {
        TArray<FDarkFactoryWeldHeatPatch> Patches;
        for (int32 Slot=0;Slot<Binding.SpotIndices.Num();++Slot) {
            const auto& Spot=BodyWeldSpots[Binding.SpotIndices[Slot]];
            const FVector Point=Mesh->GetComponentTransform().TransformPosition(Spot.LocalPosition);
            const int32 RobotIndex=EmitterRigs.IndexOfByPredicate([&](const FEmitterRig& E){return E.Id==Spot.Id;});
            if (!bProcessControlled || RobotIndex!=3) {
                FDarkFactoryWeldHeatPatch Patch; Patch.LocalPositionCm=Spot.LocalPosition; Patch.RadiusCm=Spot.RadiusCm; Patch.SurfaceC=Spot.HaloTemperatureC;
                Patches.Add(Patch);
                if (bProcessControlled) {
                    // The shader max-composes the compact hot core and cooler
                    // halo; it does not sum their temperatures or see through metal.
                    Patch.RadiusCm=ProcessWeldCoreRadiusCm; Patch.SurfaceC=Spot.TemperatureC; Patches.Add(Patch);
                }
            }
            for (const auto& Entry:Binding.Materials) if (auto* Material=Entry.Get()) {
                Material->SetScalarParameterValue(TEXT("WeldWitnessEnabled"),bWeldWitnessEnabled ? 1.f:0.f);
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("WitnessCenter%d"),Slot)),FLinearColor(Point.X,Point.Y,Point.Z,1));
                const FVector Normal=Mesh->GetComponentTransform().TransformVectorNoScale(Spot.LocalNormal).GetSafeNormal();
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("WitnessNormal%d"),Slot)),FLinearColor(Normal.X,Normal.Y,Normal.Z,0));
                Material->SetScalarParameterValue(FName(*FString::Printf(TEXT("WitnessStrength%d"),Slot)),Spot.WitnessStrength);
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("WeldHotspotWorld%d"),Slot)),FLinearColor(Point.X,Point.Y,Point.Z,1));
                Material->SetScalarParameterValue(FName(*FString::Printf(TEXT("WeldHeatRadiusCm%d"),Slot)),Spot.RadiusCm);
                Material->SetScalarParameterValue(FName(*FString::Printf(TEXT("WeldHeatGlow%d"),Slot)),Spot.Glow);
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("WeldGlowColor%d"),Slot)),FLinearColor(1.f,.08f,.008f,1));
            }
        }
        if (auto* Thermal=ToolThermalRig.Get()) Thermal->SetImportedWeldHeat(Mesh,Patches);
    }
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildBodyWeldHeatMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),bProcessControlled ? TEXT("authored-exposed-weld-core-halo-v2"):TEXT("authored-local-body-weld-cooling-v1"));
    Result->SetStringField(TEXT("scope"),bProcessControlled
        ? TEXT("Three illustrative exposed-hot-metal cores, exaggerated 2 cm radius, within cooler 12 cm halos; maximum composition, not subsurface thermography or calibrated conduction")
        : TEXT("Four surface-bound heat fields over the authored short seams; no bulk sheet-metal conduction or calibrated spectral emission"));
    Result->SetNumberField(TEXT("heat_time_seconds"),BodyWeldHeatSeconds);
    Result->SetBoolField(TEXT("witness_enabled"),bWeldWitnessEnabled);
    Result->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(BodyWeldHeatSeconds));
    Result->SetNumberField(TEXT("body_index"),FMath::FloorToDouble(BodyWeldHeatSeconds/GetCycleDurationSeconds()));
    Result->SetNumberField(TEXT("heating_tau_seconds"),.65); Result->SetNumberField(TEXT("cooling_tau_seconds"),2.5);
    Result->SetNumberField(TEXT("maximum_temperature_c"),bProcessControlled ? ProcessWeldCorePeakC:420.); Result->SetNumberField(TEXT("unique_heated_panels"),BodyWeldMaterials.Num());
    if (bProcessControlled) {
        Result->SetNumberField(TEXT("core_radius_cm"),ProcessWeldCoreRadiusCm);
        Result->SetNumberField(TEXT("halo_peak_temperature_c"),ProcessWeldHaloPeakC);
        Result->SetNumberField(TEXT("core_cooling_tau_seconds"),ProcessWeldCoreCoolingTauSeconds);
        Result->SetStringField(TEXT("composition"),TEXT("maximum, not sum"));
    }
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& Spot:BodyWeldSpots) if (const auto* Mesh=Spot.Mesh.Get()) {
        auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("id"),Spot.Id); Value->SetStringField(TEXT("surface"),Mesh->GetName());
        Value->SetNumberField(TEXT("stencil_id"),Mesh->CustomDepthStencilValue);
        Value->SetArrayField(TEXT("local_position_cm"),Vec(Spot.LocalPosition));
        Value->SetArrayField(TEXT("world_position_cm"),Vec(Mesh->GetComponentTransform().TransformPosition(Spot.LocalPosition)));
        Value->SetNumberField(TEXT("radius_cm"),Spot.RadiusCm); Value->SetNumberField(TEXT("temperature_c"),Spot.TemperatureC);
        if (bProcessControlled) { Value->SetNumberField(TEXT("halo_temperature_c"),Spot.HaloTemperatureC); Value->SetNumberField(TEXT("core_radius_cm"),ProcessWeldCoreRadiusCm); }
        Value->SetArrayField(TEXT("world_normal"),Vec(Mesh->GetComponentTransform().TransformVectorNoScale(Spot.LocalNormal).GetSafeNormal()));
        Value->SetNumberField(TEXT("witness_strength"),Spot.WitnessStrength);
        Value->SetNumberField(TEXT("glow_strength"),Spot.Glow); Value->SetNumberField(TEXT("contact_error_cm"),Spot.ContactErrorCm);
        Values.Add(MakeShared<FJsonValueObject>(Value));
    }
    Result->SetArrayField(TEXT("spots"),Values); return Result;
}


bool ADarkFactoryWeldingCell::InitializeMotorHeat() {
    MotorHeatRigs.Reset(); MotorHeatSelfCheck.Reset();
    MotorDutySteps=FMath::RoundToInt(GetTimingBankSeconds()/MotorDutyStep);
    struct FRole {
        const TCHAR* Name; const TCHAR* Driver0; const TCHAR* Driver1;
        double Weight0,MaximumC,Rate,Tau,ReferenceSpeed;
    };
    // Authored casting-wide drive loads; these are not measured motor power/torque.
    const FRole Roles[] = {
        {TEXT("fixed_base"),TEXT("base"),nullptr,1,60,30,35,120},
        {TEXT("shoulder"),TEXT("shoulder"),TEXT("base"),.8,92,65,30,90},
        {TEXT("upper_arm"),TEXT("shoulder"),TEXT("elbow"),.8,78,45,24,90},
        {TEXT("elbow"),TEXT("elbow"),nullptr,1,95,70,28,90},
        {TEXT("forearm"),TEXT("forearm_roll"),TEXT("elbow"),.7,85,58,24,45},
        {TEXT("wrist"),TEXT("wrist"),TEXT("forearm_roll"),.7,90,60,22,15},
        {TEXT("flange"),TEXT("tool_roll"),nullptr,1,75,42,18,120}
    };
    TArray<TArray<double>> Angles;
    Angles.SetNum(JointMotion.Num());
    for (int32 J=0;J<JointMotion.Num();++J) {
        Angles[J].SetNumUninitialized(MotorDutySteps+1);
        for (int32 I=0;I<=MotorDutySteps;++I) Angles[J][I]=EvaluateJointAngle(JointMotion[J],I*MotorDutyStep);
    }
    for (const auto& Profile:ThermalSurfaces) {
        auto* Mesh=Profile.Mesh;
        if (!Mesh || Profile.CoreEdgeDropC<=0 || !Mesh->GetName().StartsWith(TEXT("SM_robot_"))) continue;
        const FString Name=Mesh->GetName();
        for (const auto& RoleConfig:Roles) {
            const FString Suffix=TEXT("_")+FString(RoleConfig.Name);
            if (!Name.EndsWith(Suffix)) continue;
            FMotorHeatRig Rig; Rig.Mesh=Mesh; Rig.Role=RoleConfig.Name; Rig.CoreEdgeDropC=Profile.CoreEdgeDropC;
            Rig.MaximumC=RoleConfig.MaximumC; Rig.HeatingRate=RoleConfig.Rate; Rig.CoolingTau=RoleConfig.Tau; Rig.ReferenceSpeed=RoleConfig.ReferenceSpeed;
            const FString Robot=Name.Mid(3,Name.Len()-3-Suffix.Len());
            for (int32 D=0;D<(RoleConfig.Driver1 ? 2:1);++D) {
                const FString Id=Robot+TEXT("_")+(D==0 ? RoleConfig.Driver0:RoleConfig.Driver1);
                const int32 Index=JointMotion.IndexOfByPredicate([&](const FJointMotion& Joint){return Joint.Id==Id;});
                if (Index==INDEX_NONE) return false;
                Rig.Drivers.Add(Index); Rig.DriverWeights.Add(D==0 ? RoleConfig.Weight0:1.-RoleConfig.Weight0);
            }
            Rig.PrefixDecay.SetNumUninitialized(MotorDutySteps+1); Rig.PrefixRise.SetNumUninitialized(MotorDutySteps+1);
            Rig.Duty.SetNumUninitialized(MotorDutySteps); Rig.PrefixDecay[0]=1.; Rig.PrefixRise[0]=0.;
            for (int32 I=0;I<MotorDutySteps;++I) {
                double Activity=0;
                for (int32 D=0;D<Rig.Drivers.Num();++D) {
                    const auto& Driver=Angles[Rig.Drivers[D]];
                    Activity+=Rig.DriverWeights[D]*FMath::Clamp(FMath::Abs(Driver[I+1]-Driver[I])/(MotorDutyStep*Rig.ReferenceSpeed),0.,1.);
                }
                Rig.Duty[I]=static_cast<float>(Activity);
                const double K=Rig.HeatingRate*Rig.Duty[I]/(Rig.MaximumC-MotorAmbientC)+1./Rig.CoolingTau;
                const double Decay=FMath::Exp(-K*MotorDutyStep);
                const double Rise=Rig.HeatingRate*Rig.Duty[I]/K*(1.-Decay);
                Rig.PrefixDecay[I+1]=Decay*Rig.PrefixDecay[I];
                Rig.PrefixRise[I+1]=Decay*Rig.PrefixRise[I]+Rise;
            }
            MotorHeatRigs.Add(MoveTemp(Rig)); break;
        }
    }
    return CheckMotorHeatModel();
}

double ADarkFactoryWeldingCell::EvaluateMotorTemperature(const FMotorHeatRig& Rig,double Seconds,double IdleSeconds) const {
    const double Cycles=FMath::FloorToDouble(Seconds/GetTimingBankSeconds());
    const double Phase=FMath::Fmod(Seconds,GetTimingBankSeconds());
    const int32 Step=FMath::Clamp(FMath::FloorToInt(Phase/MotorDutyStep),0,MotorDutySteps-1);
    const double CycleDecay=Rig.PrefixDecay.Last(),CycleRise=Rig.PrefixRise.Last();
    const double Initial=CycleRise*(1.-FMath::Pow(CycleDecay,Cycles))/(1.-CycleDecay);
    double Rise=Rig.PrefixDecay[Step]*Initial+Rig.PrefixRise[Step];
    const double Remainder=FMath::Clamp(Phase-Step*MotorDutyStep,0.,MotorDutyStep);
    const double K=Rig.HeatingRate*Rig.Duty[Step]/(Rig.MaximumC-MotorAmbientC)+1./Rig.CoolingTau;
    const double Decay=FMath::Exp(-K*Remainder);
    Rise=Rise*Decay+Rig.HeatingRate*Rig.Duty[Step]/K*(1.-Decay);
    Rise*=FMath::Exp(-IdleSeconds/Rig.CoolingTau);
    return MotorAmbientC+FMath::Clamp(Rise,0.,Rig.MaximumC-MotorAmbientC);
}

bool ADarkFactoryWeldingCell::CheckMotorHeatModel() {
    MotorHeatSelfCheck=MakeShared<FJsonObject>();
    bool Cold=true,Finite=true,Deterministic=true,Cooling=true;
    bool CompleteMapping=MotorHeatRigs.Num()==28;
    for (const TCHAR* CastingRole:{TEXT("fixed_base"),TEXT("shoulder"),TEXT("upper_arm"),TEXT("elbow"),TEXT("forearm"),TEXT("wrist"),TEXT("flange")}) {
        int32 Count=0; for (const auto& Rig:MotorHeatRigs) Count+=Rig.Role==CastingRole;
        CompleteMapping &= Count==4;
    }
    double MaximumBoundaryJump=0;
    TArray<double> Boundaries;
    const double Period=GetCycleDurationSeconds();
    const int32 BankCycles=bRandomizedRobotTiming ? RobotTimingBankCycles:1;
    for (int32 Cycle=0;Cycle<=BankCycles;++Cycle) {
        Boundaries.Add((Cycle+1)*Period);
        for (int32 Robot=0;Robot<EmitterRigs.Num();++Robot) {
            const auto Timing=GetRobotTiming(Robot,Cycle*Period);
            for (double Phase:{Timing.ApproachStartSeconds,Timing.ApproachEndSeconds,Timing.WeldStartSeconds,Timing.WeldEndSeconds,Timing.WithdrawEndSeconds}) Boundaries.Add(Cycle*Period+Phase);
            for (int32 I=1;I<4;++I) { Boundaries.Add(Cycle*Period+Timing.ApproachStartSeconds+ApproachTimes[I]); Boundaries.Add(Cycle*Period+Timing.WithdrawEndSeconds-ApproachTimes[I]); }
        }
    }
    for (const auto& Rig:MotorHeatRigs) {
        Cold &= EvaluateMotorTemperature(Rig,0)==MotorAmbientC;
        const double Before=EvaluateMotorTemperature(Rig,84);
        for (double Time:{0.,12.,84.,5.,1.e9,18.,84.}) {
            const double T=EvaluateMotorTemperature(Rig,Time);
            Finite &= FMath::IsFinite(T) && T>=MotorAmbientC && T<=Rig.MaximumC;
        }
        Deterministic &= Before==EvaluateMotorTemperature(Rig,84);
        Cooling &= EvaluateMotorTemperature(Rig,84,30)<=EvaluateMotorTemperature(Rig,84,8) && EvaluateMotorTemperature(Rig,84,8)<=Before;
        for (double Boundary:Boundaries)
            MaximumBoundaryJump=FMath::Max(MaximumBoundaryJump,FMath::Abs(EvaluateMotorTemperature(Rig,Boundary+1.e-5)-EvaluateMotorTemperature(Rig,Boundary-1.e-5)));
    }
    const bool Continuous=MaximumBoundaryJump<.005;
    const bool Passed=CompleteMapping && Cold && Finite && Deterministic && Cooling && Continuous;
    MotorHeatSelfCheck->SetBoolField(TEXT("passed"),Passed);
    MotorHeatSelfCheck->SetBoolField(TEXT("complete_casting_mapping"),CompleteMapping);
    MotorHeatSelfCheck->SetBoolField(TEXT("cold_start"),Cold);
    MotorHeatSelfCheck->SetBoolField(TEXT("large_time_finite_bounded"),Finite);
    MotorHeatSelfCheck->SetBoolField(TEXT("seek_order_independent"),Deterministic);
    MotorHeatSelfCheck->SetBoolField(TEXT("passive_cooling_monotone"),Cooling);
    MotorHeatSelfCheck->SetBoolField(TEXT("cycle_phase_knot_continuity"),Continuous);
    MotorHeatSelfCheck->SetNumberField(TEXT("maximum_boundary_jump_c"),MaximumBoundaryJump);
    MotorHeatSelfCheck->SetNumberField(TEXT("boundary_epsilon_seconds"),1.e-5);
    MotorHeatSelfCheck->SetNumberField(TEXT("boundary_count"),Boundaries.Num());
    return Passed;
}

void ADarkFactoryWeldingCell::SetMotorHeatTimeForQA(double Seconds,double IdleSeconds) {
    if (!FMath::IsFinite(Seconds) || !FMath::IsFinite(IdleSeconds) || Seconds<0 || IdleSeconds<0) return;
    UpdateMotorHeat(FMath::Min(Seconds,1.e9),FMath::Min(IdleSeconds,1.e9));
}
void ADarkFactoryWeldingCell::UpdateMotorHeat(double Seconds,double IdleSeconds) {
    TRACE_CPUPROFILER_EVENT_SCOPE(DarkFactory_UpdateMotorHeat);
    if (bProcessControlled) { UpdateProcessMotorHeat(); return; }
    MotorHeatSeconds=Seconds; MotorIdleSeconds=IdleSeconds;
    const int32 Step=FMath::Clamp(FMath::FloorToInt(FMath::Fmod(Seconds,GetTimingBankSeconds())/MotorDutyStep),0,MotorDutySteps-1);
    for (auto& Rig:MotorHeatRigs) if (auto* Mesh=Rig.Mesh.Get()) {
        Rig.TemperatureC=static_cast<float>(EvaluateMotorTemperature(Rig,Seconds,IdleSeconds));
        Rig.Activity=IdleSeconds>0 ? 0.f:Rig.Duty[Step];
        if (auto* Thermal=ToolThermalRig.Get()) Thermal->SetImportedCastingTemperature(Mesh,Rig.TemperatureC);
    }
}
void ADarkFactoryWeldingCell::SetProcessMotorCaptureVariation(int32 Seed) {
    // Only offline episode capture calls this. Casting heat is independent of
    // the target label and frozen across every view/case in the same episode.
    bProcessMotorCaptureVariation=true; ProcessMotorCaptureSeed=Seed;
}
void ADarkFactoryWeldingCell::UpdateProcessMotorHeat() {
    // Unlike the repeating preview, a model gate can reject a body before any
    // welding movement. Integrate only the joint travel that actually occurred.
    // Every hold advances the same clock with zero duty and therefore cools.
    MotorHeatSeconds=ProcessClock; MotorIdleSeconds=0;
    const bool Rewind=ProcessClock<ProcessMotorLastClock-1.e-8;
    const bool First=ProcessMotorLastClock<0 || ProcessPreviousJointAngles.Num()!=JointMotion.Num();
    if (First || Rewind) {
        ProcessPreviousJointAngles.Reset(JointMotion.Num());
        for (const auto& Joint:JointMotion) ProcessPreviousJointAngles.Add(Joint.Angle);
        if (Rewind) for (auto& Rig:MotorHeatRigs) { Rig.TemperatureC=MotorAmbientC; Rig.Activity=0; }
        ProcessMotorLastClock=ProcessClock; ProcessMotorSampleSeconds=0;
    }
    const double Delta=FMath::Max(0.,ProcessClock-ProcessMotorLastClock);
    if (Delta>0) {
        ProcessMotorSampleSeconds=Delta;
        for (auto& Rig:MotorHeatRigs) {
            double Activity=0;
            for (int32 D=0;D<Rig.Drivers.Num();++D) {
                const int32 Joint=Rig.Drivers[D];
                const double Difference=JointMotion[Joint].Angle-ProcessPreviousJointAngles[Joint];
                const double Travel=FMath::Abs(bPanelAssembly ? FMath::UnwindDegrees(Difference):Difference);
                Activity+=Rig.DriverWeights[D]*FMath::Clamp(Travel/(Delta*Rig.ReferenceSpeed),0.,1.);
            }
            Rig.Activity=static_cast<float>(FMath::Clamp(Activity,0.,1.));
            const double MaxRise=Rig.MaximumC-MotorAmbientC;
            const double K=Rig.HeatingRate*Rig.Activity/MaxRise+1./Rig.CoolingTau;
            const double Decay=FMath::Exp(-K*Delta);
            const double Rise=(Rig.TemperatureC-MotorAmbientC)*Decay+Rig.HeatingRate*Rig.Activity/K*(1.-Decay);
            Rig.TemperatureC=static_cast<float>(MotorAmbientC+FMath::Clamp(Rise,0.,MaxRise));
        }
    }
    // Sampling an equal-clock pose change is a QA seek, not infinite motor power.
    for (int32 J=0;J<JointMotion.Num();++J) ProcessPreviousJointAngles[J]=JointMotion[J].Angle;
    ProcessMotorLastClock=ProcessClock;
    if (bProcessMotorCaptureVariation) {
        FRandomStream Random(ProcessMotorCaptureSeed);
        for (auto& Rig:MotorHeatRigs) {
            Rig.TemperatureC=Random.FRandRange(MotorAmbientC,Rig.MaximumC);
            Rig.Activity=0;
        }
    }
    for (const auto& Rig:MotorHeatRigs) if (auto* Mesh=Rig.Mesh.Get())
        if (auto* Thermal=ToolThermalRig.Get()) Thermal->SetImportedCastingTemperature(Mesh,Rig.TemperatureC);
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildMotorHeatMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),bProcessControlled ? TEXT("actual-process-joint-duty-thermal-v1"):TEXT("authored-joint-duty-thermal-v1"));
    Result->SetStringField(TEXT("scope"),TEXT("Casting-wide illustrative drive activity from angular travel; static holding torque, motor efficiency and physical heat transport are not modeled. No visible/NIR emission."));
    Result->SetBoolField(TEXT("offline_casting_heat_variation"),bProcessMotorCaptureVariation);
    Result->SetNumberField(TEXT("duty_time_seconds"),MotorHeatSeconds); Result->SetNumberField(TEXT("idle_seconds"),MotorIdleSeconds);
    Result->SetNumberField(TEXT("ambient_temperature_c"),MotorAmbientC); Result->SetNumberField(TEXT("cycle_duration_seconds"),GetCycleDurationSeconds());
    Result->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(MotorHeatSeconds));
    Result->SetNumberField(TEXT("duty_bank_seconds"),GetTimingBankSeconds());
    Result->SetNumberField(TEXT("duty_steps_per_profile"),MotorDutySteps);
    Result->SetNumberField(TEXT("integration_step_seconds"),MotorDutyStep); Result->SetNumberField(TEXT("profile_count"),MotorHeatRigs.Num());
    Result->SetStringField(TEXT("equation"),TEXT("dRise/dt=rate*activity-(rate*activity/maxRise+1/tau)*Rise; exact piecewise-constant duty steps and periodic affine recurrence; then exp(-idle/tau)"));
    if (!bProcessControlled && MotorHeatSelfCheck) Result->SetObjectField(TEXT("self_check"),MotorHeatSelfCheck);
    if (bProcessControlled) {
        Result->SetStringField(TEXT("timebase"),TEXT("continuous process clock; actual joint-angle deltas including inspection R4; no scheduled-bank heat"));
        Result->SetNumberField(TEXT("duty_bank_seconds"),0);
        Result->SetNumberField(TEXT("duty_steps_per_profile"),0);
        Result->SetNumberField(TEXT("integration_step_seconds"),ProcessMotorSampleSeconds);
        Result->SetStringField(TEXT("equation"),TEXT("dRise/dt=rate*actualActivity-(rate*actualActivity/maxRise+1/tau)*Rise; exact frame interval with measured mean joint travel; reset only on clock rewind"));
    }
    TArray<TSharedPtr<FJsonValue>> Profiles;
    for (const auto& Rig:MotorHeatRigs) if (const auto* Mesh=Rig.Mesh.Get()) {
        auto P=MakeShared<FJsonObject>(); P->SetStringField(TEXT("id"),Mesh->GetName()); P->SetStringField(TEXT("surface"),Mesh->GetName());
        P->SetNumberField(TEXT("stencil_id"),Mesh->CustomDepthStencilValue); P->SetStringField(TEXT("role"),Rig.Role);
        P->SetNumberField(TEXT("temperature_c"),Rig.TemperatureC); P->SetNumberField(TEXT("activity"),Rig.Activity);
        P->SetNumberField(TEXT("maximum_temperature_c"),Rig.MaximumC); P->SetNumberField(TEXT("heating_rate_c_per_second"),Rig.HeatingRate);
        P->SetNumberField(TEXT("cooling_tau_seconds"),Rig.CoolingTau); P->SetNumberField(TEXT("core_edge_drop_c"),Rig.CoreEdgeDropC);
        P->SetNumberField(TEXT("reference_speed_deg_per_second"),Rig.ReferenceSpeed);
        TArray<TSharedPtr<FJsonValue>> Drivers,Weights;
        for (int32 D=0;D<Rig.Drivers.Num();++D) { Drivers.Add(MakeShared<FJsonValueString>(JointMotion[Rig.Drivers[D]].Id)); Weights.Add(MakeShared<FJsonValueNumber>(Rig.DriverWeights[D])); }
        P->SetArrayField(TEXT("driving_joint_ids"),Drivers); P->SetArrayField(TEXT("driving_joint_weights"),Weights);
        Profiles.Add(MakeShared<FJsonValueObject>(P));
    }
    Result->SetArrayField(TEXT("profiles"),Profiles);
    return Result;
}

void ADarkFactoryWeldingCell::SetProcessControlled(bool Enabled) {
    bProcessControlled=Enabled;
    if (Enabled) {
        ProcessInitialBodyC.Init(20.f,EmitterRigs.Num()); ProcessInitialTipC.Init(20.f,EmitterRigs.Num());
        ProcessWeldEpoch=0; bProcessWeldStarted=false; ProcessClock=ProcessBirthClock=0; ProcessInspectionExtension=0;
        InitializeInspectionScan();
        ProcessMotorLastClock=0; ProcessMotorSampleSeconds=0; ProcessPreviousJointAngles.Reset(JointMotion.Num());
        for (const auto& Joint:JointMotion) ProcessPreviousJointAngles.Add(Joint.Angle);
    }
    if (EmitterRigs.IsValidIndex(3)) if (auto* Tool=EmitterRigs[3].SourceTool.Get()) {
        Tool->SetVisibility(!Enabled); Tool->SetCollisionEnabled(Enabled ? ECollisionEnabled::NoCollision:ECollisionEnabled::QueryOnly);
    }
}
void ADarkFactoryWeldingCell::BeginProcessBody(int32 BodyIndex,double ClockSeconds) {
    if (!bProcessControlled || !FMath::IsFinite(ClockSeconds)) return;
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        ProcessInitialBodyC[I]=EmitterRigs[I].BodyC; ProcessInitialTipC[I]=EmitterRigs[I].TipC;
    }
    ProcessBodyIndex=FMath::Max(0,BodyIndex); ProcessClock=ProcessBirthClock=ClockSeconds;
    if (bPanelAssembly) bPanelHolderEngaged=false;
    ProcessWeldEpoch=0; bProcessWeldStarted=false; bProcessPermitWelding=false;
}
void ADarkFactoryWeldingCell::SetProcessFrame(double PoseSeconds,double ClockSeconds,double CarriageOffsetCm,bool Visible,bool PermitWelding,double InspectionExtension) {
    if (!bProcessControlled || !FMath::IsFinite(PoseSeconds) || !FMath::IsFinite(ClockSeconds) || !FMath::IsFinite(CarriageOffsetCm) || !FMath::IsFinite(InspectionExtension)) return;
    const double Pose=FMath::Clamp(PoseSeconds,0.,GetCarriageDepartureStartSeconds());
    ProcessInspectionExtension=FMath::Clamp(InspectionExtension,0.,1.);
    ProcessClock=ClockSeconds; ProcessCarriageOffset=CarriageOffsetCm; bProcessVisible=Visible; bProcessPermitWelding=PermitWelding;
    if (PermitWelding && !bProcessWeldStarted) { ProcessWeldEpoch=ClockSeconds-Pose; bProcessWeldStarted=true; }
    SetMotionTime(ProcessBodyIndex*GetCycleDurationSeconds()+Pose);
}
USceneComponent* ADarkFactoryWeldingCell::GetInspectionJoint() const {
    return EmitterRigs.IsValidIndex(3) ? EmitterRigs[3].Joint.Get():nullptr;
}
void ADarkFactoryWeldingCell::InitializeInspectionScan() {
    InspectionJointIndices.Reset(); InspectionScanFromAngles.Reset();
    InspectionRequestedSpot=0; InspectionScanStartClock=-InspectionScanDurationSeconds;
    bInspectionScanConfigured=false;
    for (int32 ScanRole=0;ScanRole<6;++ScanRole) {
        const FString Id=FString(TEXT("robot_nx_ny_"))+InspectionJointRoles[ScanRole];
        const int32 Index=JointMotion.IndexOfByPredicate([&](const FJointMotion& J){ return J.Id==Id; });
        if (Index==INDEX_NONE || !JointMotion[Index].Component.IsValid() || JointMotion[Index].ApproachAngles.Num()!=3
            || FMath::Abs(JointMotion[Index].ApproachAngles[2]-InspectionPoseAngles[0][ScanRole])>.001) {
            UE_LOG(LogTemp,Error,TEXT("R4 inspection scan requires its authored imported joint profile: %s"),*Id);
            InspectionJointIndices.Reset(); InspectionScanFromAngles.Reset(); return;
        }
        InspectionJointIndices.Add(Index); InspectionScanFromAngles.Add(InspectionPoseAngles[0][ScanRole]);
    }
    bInspectionScanConfigured=true;
}
double ADarkFactoryWeldingCell::InspectionScanAngle(int32 JointIndex,double ClockSeconds) const {
    const double Progress=FMath::Clamp((ClockSeconds-InspectionScanStartClock)/InspectionScanDurationSeconds,0.,1.);
    return FMath::Lerp(InspectionScanFromAngles[JointIndex],InspectionTargetAngle(InspectionRequestedSpot,JointIndex),Smooth(Progress));
}
void ADarkFactoryWeldingCell::RequestInspectionSpot(int32 Spot,double ClockSeconds,bool Snap,bool NIR,bool Adhesive) {
    if (!bProcessControlled || !bInspectionScanConfigured || Spot<0 || Spot>=3 || !FMath::IsFinite(ClockSeconds)) return;
    if (Spot!=InspectionRequestedSpot || Snap || (bPanelAssembly && (NIR!=bPanelInspectionNIR || (Adhesive && !NIR)!=bPanelInspectionAdhesive))) {
        // Latch the current scan pose before retargeting. Reading a camera pose
        // never changes the robot, mount, simulation clock or motor heat.
        for (int32 J=0;J<6;++J) InspectionScanFromAngles[J]=InspectionScanAngle(J,ProcessClock);
        bPanelInspectionNIR=bPanelAssembly && NIR;
        bPanelInspectionAdhesive=bPanelAssembly && Adhesive && !NIR;
        InspectionRequestedSpot=Spot;
        InspectionScanStartClock=Snap ? ClockSeconds-InspectionScanDurationSeconds:ClockSeconds;
        if (Snap) for (int32 J=0;J<6;++J) InspectionScanFromAngles[J]=InspectionTargetAngle(Spot,J);
    }
}
bool ADarkFactoryWeldingCell::IsInspectionSpotSettled(int32 Spot) const {
    if (!bProcessControlled || !bInspectionScanConfigured || Spot!=InspectionRequestedSpot
        || ProcessInspectionExtension<1.-1.e-6 || ProcessClock-InspectionScanStartClock<InspectionScanDurationSeconds-1.e-6) return false;
    for (int32 J=0;J<6;++J)
        if (FMath::Abs(JointMotion[InspectionJointIndices[J]].Angle-InspectionTargetAngle(Spot,J))>1.e-4) return false;
    return true;
}
FTransform ADarkFactoryWeldingCell::GetInspectionCameraMountTransform() const {
    return bPanelAssembly ? PanelInspectionMount:FTransform(FQuat(.010681581977827105,.33021788281088893,.6191164447113378,.7124162277533669).GetNormalized(),FVector(0,0,-24));
}
FTransform ADarkFactoryWeldingCell::InspectionEndpointTransform(int32 Spot) const {
    FTransform World=SceneRoot->GetComponentTransform();
    if (!bInspectionScanConfigured || Spot<0 || Spot>=3) return World;
    for (int32 J=0;J<6;++J) {
        const auto& Motion=JointMotion[InspectionJointIndices[J]];
        const auto* Component=Motion.Component.Get();
        const FTransform Local(Motion.RestRotation*FQuat(Motion.Axis,FMath::DegreesToRadians(InspectionTargetAngle(Spot,J))),
            Component->GetRelativeLocation(),Component->GetRelativeScale3D());
        World=Local*World;
    }
    const FTransform Mount=GetInspectionCameraMountTransform()*World;
    return FTransform(Mount.GetRotation(),Mount.TransformPosition(FVector(21,0,0)));
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::GetInspectionScanMetadata() const {
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("schema"),TEXT("authored-r4-inspection-scan-v1"));
    O->SetStringField(TEXT("scope"),TEXT("Authored rigid camera mount and six-joint visual poses; not industrial collision certification"));
    O->SetBoolField(TEXT("configured"),bInspectionScanConfigured);
    O->SetStringField(TEXT("modality"),bPanelAssembly && bPanelInspectionNIR ? TEXT("nir"):TEXT("thermal"));
    O->SetStringField(TEXT("scan_modality"),bPanelAssembly && bPanelInspectionNIR ? TEXT("nir"):TEXT("thermal"));
    O->SetStringField(TEXT("scan_family"),bPanelAssembly && bPanelInspectionNIR ? TEXT("nir"):bPanelAssembly && bPanelInspectionAdhesive ? TEXT("adhesive"):TEXT("weld"));
    O->SetNumberField(TEXT("requested_spot"),InspectionRequestedSpot);
    O->SetBoolField(TEXT("settled"),IsInspectionSpotSettled(InspectionRequestedSpot));
    O->SetNumberField(TEXT("transition_seconds"),InspectionScanDurationSeconds);
    O->SetNumberField(TEXT("elapsed_seconds"),FMath::Clamp(ProcessClock-InspectionScanStartClock,0.,InspectionScanDurationSeconds));
    O->SetNumberField(TEXT("progress"),FMath::Clamp((ProcessClock-InspectionScanStartClock)/InspectionScanDurationSeconds,0.,1.));
    O->SetNumberField(TEXT("inspection_extension"),ProcessInspectionExtension);
    const FTransform Mount=GetInspectionCameraMountTransform(); const FQuat Q=Mount.GetRotation();
    O->SetArrayField(TEXT("fixed_mount_location_cm"),Vec(Mount.GetLocation()));
    O->SetArrayField(TEXT("fixed_mount_quaternion_xyzw"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});
    O->SetArrayField(TEXT("optical_offset_cm"),Vec(FVector(21,0,0)));
    TArray<TSharedPtr<FJsonValue>> Ids,Current,Target,Poses;
    if (bInspectionScanConfigured) {
        for (int32 J=0;J<6;++J) {
            const auto& Motion=JointMotion[InspectionJointIndices[J]];
            Ids.Add(MakeShared<FJsonValueString>(Motion.Id)); Current.Add(MakeShared<FJsonValueNumber>(Motion.Angle));
            Target.Add(MakeShared<FJsonValueNumber>(InspectionTargetAngle(InspectionRequestedSpot,J)));
        }
        for (int32 Spot=0;Spot<3;++Spot) {
            auto P=MakeShared<FJsonObject>(); P->SetNumberField(TEXT("spot_index"),Spot);
            TArray<TSharedPtr<FJsonValue>> Angles; for (int32 J=0;J<6;++J) Angles.Add(MakeShared<FJsonValueNumber>(InspectionTargetAngle(Spot,J)));
            P->SetArrayField(TEXT("joint_angles_deg"),Angles);
            const FTransform Lens=InspectionEndpointTransform(Spot); const FRotator R=Lens.Rotator();
            P->SetArrayField(TEXT("lens_location_cm"),Vec(Lens.GetLocation()));
            P->SetArrayField(TEXT("lens_rotation_deg"),Vec(FVector(R.Pitch,R.Yaw,R.Roll))); Poses.Add(MakeShared<FJsonValueObject>(P));
        }
    }
    O->SetArrayField(TEXT("joint_ids"),Ids); O->SetArrayField(TEXT("current_joint_angles_deg"),Current);
    O->SetArrayField(TEXT("target_joint_angles_deg"),Target); O->SetArrayField(TEXT("poses"),Poses);
    if (const auto* Joint=GetInspectionJoint()) {
        const FTransform Actual=Mount*Joint->GetComponentTransform(); const FRotator R=Actual.Rotator();
        O->SetArrayField(TEXT("lens_location_cm"),Vec(Actual.TransformPosition(FVector(21,0,0))));
        O->SetArrayField(TEXT("lens_rotation_deg"),Vec(FVector(R.Pitch,R.Yaw,R.Roll)));
    }
    return O;
}
UStaticMeshComponent* ADarkFactoryWeldingCell::FindSceneMesh(const FString& Name) const {
    for (const auto& Mesh:MeshComponents) if (Mesh && Mesh->GetName()==Name) return Mesh;
    return nullptr;
}
bool ADarkFactoryWeldingCell::GetWeldSite(int32 Index,FVector& LocalPosition,FVector& LocalNormal) const {
    if (!EmitterRigs.IsValidIndex(Index) || EmitterRigs[Index].SurfacePositions.Num()!=5) return false;
    LocalPosition=EmitterRigs[Index].SurfacePositions[2]; LocalNormal=EmitterRigs[Index].SurfaceNormals[2]; return true;
}
float ADarkFactoryWeldingCell::GetMaximumProcessWeldC() const {
    float Maximum=23; for (const auto& Spot:BodyWeldSpots) Maximum=FMath::Max3(Maximum,Spot.TemperatureC,Spot.HaloTemperatureC); return Maximum;
}


double ADarkFactoryWeldingCell::InspectionTargetAngle(int32 Spot,int32 Joint) const {
    return bPanelAssembly && PanelInspectionPoses.IsValidIndex(Spot)
        ? (bPanelInspectionNIR ? PanelNIRInspectionPoses[Spot][Joint]:bPanelInspectionAdhesive ? PanelAdhesiveInspectionPoses[Spot][Joint]:PanelInspectionPoses[Spot][Joint]):InspectionPoseAngles[Spot][Joint];
}

FString ADarkFactoryWeldingCell::GetPanelFramePath() const {
    return FPaths::Combine(FPaths::GetPath(ManifestPath),TEXT("panel_frame.meshbin"));
}
bool ADarkFactoryWeldingCell::InitializePanelAssembly() {
    if (bPanelAssembly) return true;
    if (!bLoaded || !bProcessControlled || !bInspectionScanConfigured) return false;
    const FString Path=FPaths::Combine(FPaths::GetPath(ManifestPath),TEXT("panel_assembly.json"));
    FString Text; TSharedPtr<FJsonObject> Layout;
    if (IFileManager::Get().FileSize(*Path)>256*1024 || !FFileHelper::LoadFileToString(Text,*Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Layout) || !Layout
        || Layout->GetStringField(TEXT("schema"))!=TEXT("panel-assembly-authored-poses-v1")) return false;
    auto Rows=[&](const TCHAR* Key,int32 Count,TArray<TArray<double>>& Output) {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if (!Layout->TryGetArrayField(Key,Values) || Values->Num()!=Count) return false;
        Output.Reset();
        for (const auto& Value:*Values) {
            const TArray<TSharedPtr<FJsonValue>>* Angles=nullptr;
            if (!Value->TryGetArray(Angles) || Angles->Num()!=6) return false;
            TArray<double> Row;
            for (const auto& A:*Angles) { double D=0; if (!A->TryGetNumber(D) || !FMath::IsFinite(D) || FMath::Abs(D)>270) return false; Row.Add(D); }
            Output.Add(Row);
        }
        return true;
    };
    if (!Rows(TEXT("poses"),18,PanelAssemblyPoses) || !Rows(TEXT("welder_waypoints"),3,PanelWelderWaypoints)
        || !Rows(TEXT("weld_cross_poses"),51,PanelWeldCrossPoses)
        || !Rows(TEXT("weld_poses"),PanelWeldCount,PanelWeldPoses)
        || !Rows(TEXT("adhesive_waypoints"),7,PanelAdhesiveWaypoints) || !Rows(TEXT("holder_waypoints"),10,PanelHolderWaypoints)
        || !Rows(TEXT("camera_poses"),3,PanelInspectionPoses)
        || !Rows(TEXT("nir_camera_poses"),3,PanelNIRInspectionPoses)
        || !Rows(TEXT("adhesive_inspection_poses"),3,PanelAdhesiveInspectionPoses)
        || !Rows(TEXT("adhesive_stroke_poses"),82,PanelAdhesiveStrokePoses)) return false;
    if (!Layout->TryGetStringField(TEXT("authoring_sha256"),PanelAuthoringSHA256) || PanelAuthoringSHA256.Len()!=64
        || !Layout->TryGetStringField(TEXT("motion_recipe"),PanelMotionRecipe) || PanelMotionRecipe.Len()>128
        || !Layout->TryGetStringField(TEXT("layout_recipe"),PanelLayoutRecipe) || PanelLayoutRecipe!=TEXT("left-stations-swapped-v1")) return false;
    double HolderOffset=0,HolderStem=0;
    const TArray<TSharedPtr<FJsonValue>>* AdhesiveRest=nullptr;
    if (!Scalar(Layout,TEXT("holder_offset_cm"),HolderOffset) || HolderOffset<80 || HolderOffset>200
        || !Scalar(Layout,TEXT("holder_stem_length_cm"),HolderStem) || HolderStem<50 || HolderStem>170
        || FMath::Abs(HolderOffset-HolderStem-28.)>.001
        || !Layout->TryGetArrayField(TEXT("adhesive_rest_pose"),AdhesiveRest) || AdhesiveRest->Num()!=6) return false;
    double SeatingLift=0;
    if (!Scalar(Layout,TEXT("panel_seating_lift_cm"),SeatingLift)
        || FMath::Abs(SeatingLift-PanelSeatingLiftCm)>.00001) return false;
    FVector HolderPanelOffset;
    if (!Vector(Layout,TEXT("holder_panel_offset_cm"),HolderPanelOffset)
        || FMath::Abs(HolderPanelOffset.Z+HolderOffset)>.001
        || FVector2D(HolderPanelOffset.X,HolderPanelOffset.Y).Size()>75.) return false;
    const TArray<TSharedPtr<FJsonValue>>* HolderQValues=nullptr;
    if (!Layout->TryGetArrayField(TEXT("holder_panel_quaternion_xyzw"),HolderQValues) || HolderQValues->Num()!=4) return false;
    double HQ[4]; for (int32 I=0;I<4;++I) if (!(*HolderQValues)[I]->TryGetNumber(HQ[I]) || !FMath::IsFinite(HQ[I])) return false;
    FQuat HolderPanelRotation(HQ[0],HQ[1],HQ[2],HQ[3]);
    if (FMath::Abs(HolderPanelRotation.SizeSquared()-1.)>.00001) return false;
    HolderPanelRotation.Normalize();
    struct FGripPadMount { FVector Anchor,Contact,Normal; double Diameter=0; };
    TArray<FGripPadMount> GripPads;
    const TArray<TSharedPtr<FJsonValue>>* GripPadValues=nullptr;
    if (!Layout->TryGetArrayField(TEXT("gripper_pad_mounts"),GripPadValues) || GripPadValues->Num()!=4) return false;
    for (const auto& Value:*GripPadValues) {
        const auto Pad=Value->AsObject(); FGripPadMount Mount;
        if (!Pad || !Vector(Pad,TEXT("anchor_cm"),Mount.Anchor) || !Vector(Pad,TEXT("contact_cm"),Mount.Contact)
            || !Vector(Pad,TEXT("normal"),Mount.Normal) || FMath::Abs(Mount.Normal.SizeSquared()-1.)>.001
            || !Scalar(Pad,TEXT("diameter_cm"),Mount.Diameter) || Mount.Diameter<6. || Mount.Diameter>13.
            || Mount.Anchor.Size()>300. || Mount.Contact.Size()>300.) return false;
        const double RodLength=(Mount.Anchor-(Mount.Contact+Mount.Normal*3.)).Size();
        if (RodLength<1. || RodLength>80.) return false;
        GripPads.Add(Mount);
    }
    TArray<double> AdhesiveRestAngles;
    for (const auto& A:*AdhesiveRest) { double V=0; if (!A->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V)>270) return false; AdhesiveRestAngles.Add(V); }

    TArray<double> WelderRestAngles,InspectionRestAngles;
    const TArray<TSharedPtr<FJsonValue>> *WelderRest=nullptr,*InspectionRest=nullptr,*GunQValues=nullptr;
    FVector GunTranslation; double GunScale=0; FString GunMountRecipe;
    if (!Layout->TryGetArrayField(TEXT("welder_rest_pose"),WelderRest) || WelderRest->Num()!=6
        || !Layout->TryGetArrayField(TEXT("inspection_rest_pose"),InspectionRest) || InspectionRest->Num()!=6
        || !Layout->TryGetStringField(TEXT("source_welder_mount_recipe"),GunMountRecipe) || GunMountRecipe!=TEXT("native-cap-direct-v1")
        || !Layout->TryGetArrayField(TEXT("source_welder_mount_quaternion_xyzw"),GunQValues) || GunQValues->Num()!=4
        || !Vector(Layout,TEXT("source_welder_mount_translation_cm"),GunTranslation)
        || !Scalar(Layout,TEXT("source_welder_scale"),GunScale) || FMath::Abs(GunScale-.7)>.00001) return false;
    for (const auto& A:*WelderRest) { double V=0; if (!A->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V)>270) return false; WelderRestAngles.Add(V); }
    for (const auto& A:*InspectionRest) { double V=0; if (!A->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V)>270) return false; InspectionRestAngles.Add(V); }
    double GQ[4];
    for (int32 I=0;I<4;++I) if (!(*GunQValues)[I]->TryGetNumber(GQ[I]) || !FMath::IsFinite(GQ[I])) return false;
    FQuat GunRotation(GQ[0],GQ[1],GQ[2],GQ[3]);
    if (FMath::Abs(GunRotation.SizeSquared()-1.)>.00001) return false;
    GunRotation.Normalize();
    const FTransform DirectGunMount(GunRotation,GunTranslation,FVector(GunScale));
    // The actual source mating cap stays on the native wrist flange. A long
    // translation or tilted attachment must not silently reintroduce a spacer.
    const FVector CapCenter(.05189510,.03320415,-6.88595441),CapNormal(-.00753607,-.00482182,.999959978);
    if (!DirectGunMount.TransformPosition(CapCenter).Equals(CapCenter,.001)
        || FVector::DotProduct(GunRotation.RotateVector(CapNormal),CapNormal)<.999999) return false;

    const TArray<TSharedPtr<FJsonValue>>* CameraQValues=nullptr; FVector CameraTranslation;
    if (!Layout->TryGetArrayField(TEXT("inspection_mount_quaternion_xyzw"),CameraQValues) || CameraQValues->Num()!=4
        || !Vector(Layout,TEXT("inspection_mount_translation_cm"),CameraTranslation)
        || !CameraTranslation.Equals(FVector(0,0,-24),.00001)) return false;
    double CQ[4]; for (int32 I=0;I<4;++I) if (!(*CameraQValues)[I]->TryGetNumber(CQ[I]) || !FMath::IsFinite(CQ[I])) return false;
    FQuat CameraRotation(CQ[0],CQ[1],CQ[2],CQ[3]);
    if (FMath::Abs(CameraRotation.SizeSquared()-1.)>.00001) return false;
    CameraRotation.Normalize();
    const FTransform InspectionMount(CameraRotation,CameraTranslation);

    const TArray<TSharedPtr<FJsonValue>>* Paths=nullptr;
    if (!Layout->TryGetArrayField(TEXT("adhesive_paths"),Paths) || Paths->Num()!=2) return false;
    PanelAdhesivePathPoints.Reset(); PanelAdhesivePathNormals.Reset(); PanelAdhesivePathProgress.Reset();
    for (const auto& Value:*Paths) {
        const auto Object=Value->AsObject();
        const TArray<TSharedPtr<FJsonValue>> *Points=nullptr,*Normals=nullptr,*Progress=nullptr;
        if (!Object || !Object->TryGetArrayField(TEXT("points_cm"),Points) || Points->Num()!=41
            || !Object->TryGetArrayField(TEXT("normals"),Normals) || Normals->Num()!=41
            || !Object->TryGetArrayField(TEXT("progress"),Progress) || Progress->Num()!=41) return false;
        TArray<FVector> P,N; TArray<double> U;
        for (int32 I=0;I<41;++I) {
            const auto& PA=(*Points)[I]->AsArray(); const auto& NA=(*Normals)[I]->AsArray();
            if (PA.Num()!=3 || NA.Num()!=3) return false;
            const FVector PP(PA[0]->AsNumber(),PA[1]->AsNumber(),PA[2]->AsNumber()),NN(NA[0]->AsNumber(),NA[1]->AsNumber(),NA[2]->AsNumber());
            double UU=0; if (PP.ContainsNaN() || NN.ContainsNaN() || PP.Size()>5000 || FMath::Abs(NN.SizeSquared()-1.)>.001
                || !(*Progress)[I]->TryGetNumber(UU) || !FMath::IsFinite(UU) || UU<0 || UU>1 || (I>0 && UU<=U.Last())) return false;
            P.Add(PP); N.Add(NN); U.Add(UU);
        }
        if (FMath::Abs(U[0])>1.e-8 || FMath::Abs(U.Last()-1.)>1.e-8) return false;
        PanelAdhesivePathPoints.Add(P); PanelAdhesivePathNormals.Add(N); PanelAdhesivePathProgress.Add(U);
    }
    const TArray<TSharedPtr<FJsonValue>> *Offsets=nullptr,*PedestalOffsetsJSON=nullptr,*Contacts=nullptr,*RotationValues=nullptr;
    FVector SupplyDelta;
    if (!Layout->TryGetArrayField(TEXT("base_offsets_cm"),Offsets) || Offsets->Num()!=4
        || !Layout->TryGetArrayField(TEXT("pedestal_offsets_cm"),PedestalOffsetsJSON) || PedestalOffsetsJSON->Num()!=3
        || !Layout->TryGetArrayField(TEXT("contacts"),Contacts) || Contacts->Num()!=PanelWeldCount
        || !Layout->TryGetArrayField(TEXT("supply_panel_quaternion_xyzw"),RotationValues) || RotationValues->Num()!=4
        || !Vector(Layout,TEXT("supply_delta_cm"),SupplyDelta)) return false;
    TArray<FVector> BaseOffsets;
    for (const auto& Value:*Offsets) {
        const auto& A=Value->AsArray(); if (A.Num()!=3) return false;
        FVector V(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()); if (V.ContainsNaN() || V.Size()>1000) return false;
        BaseOffsets.Add(V);
    }
    TArray<FVector> PedestalOffsets;
    for (const auto& Value:*PedestalOffsetsJSON) {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if (!Value->TryGetArray(A) || A->Num()!=3) return false;
        double V[3]; for (int32 I=0;I<3;++I) if (!(*A)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I])) return false;
        const FVector Offset(V[0],V[1],V[2]); if (Offset.Size()>1000) return false; PedestalOffsets.Add(Offset);
    }
    PanelContacts.Reset(); PanelContactNormals.Reset();
    for (const auto& Value:*Contacts) {
        FVector P,N; if (!Vector(Value->AsObject(),TEXT("position_cm"),P) || !Vector(Value->AsObject(),TEXT("normal"),N) || N.IsNearlyZero()) return false;
        PanelContacts.Add(P); PanelContactNormals.Add(N.GetSafeNormal());
    }
    PanelSupplyRotation=FQuat((*RotationValues)[0]->AsNumber(),(*RotationValues)[1]->AsNumber(),(*RotationValues)[2]->AsNumber(),(*RotationValues)[3]->AsNumber());
    if (PanelSupplyRotation.ContainsNaN() || FMath::Abs(PanelSupplyRotation.SizeSquared()-1.)>.001) return false;
    PanelSupplyRotation.Normalize();
    const TCHAR* Required[]={TEXT("SM_AssemblyFence_Black"),TEXT("SM_AssemblyFence_Yellow"),TEXT("SM_AssemblyPedestal_R1"),TEXT("SM_AssemblyPedestal_R2"),TEXT("SM_AssemblyPedestal_R4"),TEXT("SM_AssemblyFloorFixture_R1"),TEXT("SM_AssemblyFloorFixture_R2"),TEXT("SM_AssemblyFloorDeck_R1"),TEXT("SM_AssemblyFloorDeck_R2"),TEXT("SM_PanelSupply_Rack"),TEXT("SM_BodyPanel_0386")};
    for (const TCHAR* Name:Required) if (!FindSceneMesh(Name)) return false;
    auto* RearRoof=FindSceneMesh(ProcessRemovedRoof);
    UBoxComponent* RoofCollider=nullptr;
    for (const auto& Shape:CarriageColliders) if (Shape && Shape->GetFName()==FName(TEXT("CarriageRoof"))) RoofCollider=Shape;
    FBox RetainedRoof(ForceInit);
    for (const auto& Panel:BodyComponents) if (Panel && Panel!=RearRoof && Panel->GetStaticMesh()) {
        // Carriage-local bounds remain valid even if initialization follows a seek.
        const FBox Bounds=Panel->GetStaticMesh()->GetBoundingBox().TransformBy(Panel->GetRelativeTransform());
        if (Bounds.Min.Z>WorkingBodyBounds.Max.Z-40.) RetainedRoof+=Bounds;
    }
    if (!RearRoof || !RoofCollider || !RetainedRoof.IsValid) return false;
    PanelSupplyMeshes.Reset();
    for (int32 I=0;I<5;++I) { auto* M=FindSceneMesh(FString::Printf(TEXT("SM_PanelSupply_%d"),I)); if (!M) return false; PanelSupplyMeshes.Add(M); }
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Cone=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cone.Cone"));
    auto* ToolMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PanelTool.M_PanelTool"));
    auto* RackBar=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/WeldingCell/Meshes/SM_PanelGripperRackBar.SM_PanelGripperRackBar"));
    auto* RackMaterial=FindSceneMesh(TEXT("SM_PanelSupply_Rack"))->GetMaterial(0);
    if (!Cube || !Cylinder || !Cone || !ToolMaterial || !RackBar || !RackMaterial || RackBar->GetBoundingBox().GetSize().GetMin()<.1) return false;
    for (int32 I=0;I<4;++I) if (!FindSceneMesh(TEXT("SM_")+EmitterRigs[I].Id+TEXT("_fixed_base"))) return false;
    const TSharedPtr<FJsonObject>* Pinch=nullptr;
    FVector PinchTCP=FVector::ZeroVector,PinchDirection=FVector::UpVector; FString PinchRecipe;
    if (!Layout->TryGetObjectField(TEXT("pinch_gun"),Pinch) || !Pinch || !Pinch->IsValid()
        || !(*Pinch)->TryGetStringField(TEXT("recipe"),PinchRecipe) || PinchRecipe!=TEXT("source-two-hinge-pinch-v1")
        || !Vector(*Pinch,TEXT("hinge_axis"),PinchAxis) || FMath::Abs(PinchAxis.SizeSquared()-1.)>.001
        || !Vector(*Pinch,TEXT("tcp_cm"),PinchTCP) || PinchTCP.Size()>150. || !Vector(*Pinch,TEXT("pinch_axis"),PinchDirection) || FMath::Abs(PinchDirection.SizeSquared()-1.)>.001
        || !Scalar(*Pinch,TEXT("clamp_angle_deg"),PinchClampAngle) || PinchClampAngle<0 || PinchClampAngle>5
        || !Scalar(*Pinch,TEXT("open_angle_deg"),PinchOpenAngle) || PinchOpenAngle<5 || PinchOpenAngle>20) return false;
    const TArray<TSharedPtr<FJsonValue>> *Pivots=nullptr,*Tips=nullptr;
    if (!(*Pinch)->TryGetArrayField(TEXT("hinge_pivots_cm"),Pivots) || Pivots->Num()!=2
        || !(*Pinch)->TryGetArrayField(TEXT("source_tip_centers_cm"),Tips) || Tips->Num()!=2) return false;
    for (int32 I=0;I<2;++I) {
        const TArray<TSharedPtr<FJsonValue>> *P=nullptr,*T=nullptr;
        if (!(*Pivots)[I]->TryGetArray(P) || P->Num()!=3 || !(*Tips)[I]->TryGetArray(T) || T->Num()!=3) return false;
        double PV[3],TV[3];
        for (int32 K=0;K<3;++K) if (!(*P)[K]->TryGetNumber(PV[K]) || !(*T)[K]->TryGetNumber(TV[K]) || !FMath::IsFinite(PV[K]) || !FMath::IsFinite(TV[K])) return false;
        PinchPivots[I]=FVector(PV[0],PV[1],PV[2]); PinchTips[I]=FVector(TV[0],TV[1],TV[2]);
        if (PinchPivots[I].Size()>150 || PinchTips[I].Size()>150) return false;
    }
    const TCHAR* PinchNames[]={TEXT("SM_ProcessPinchChassis"),TEXT("SM_ProcessPinchJawA"),TEXT("SM_ProcessPinchJawB")};
    TArray<UStaticMesh*> PinchAssets;
    for (const TCHAR* Name:PinchNames) {
        auto* Asset=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/WeldingCell/Meshes/%s.%s"),Name,Name));
        if (!Asset) return false; PinchAssets.Add(Asset);
    }
    // All validation precedes mutation. The original FBX and source grouping stay intact.
    RearRoof->SetVisibility(false); RearRoof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Trim the curtain collider too, so no invisible roof remains in the opening.
    RoofCollider->SetRelativeLocation(RetainedRoof.GetCenter());
    RoofCollider->SetBoxExtent(FVector(RetainedRoof.GetExtent().X,RetainedRoof.GetExtent().Y,7.));
    PanelInspectionMount=InspectionMount;
    for (auto& J:JointMotion) if (BaseOffsets.IsValidIndex(J.RobotIndex) && J.Id.EndsWith(TEXT("_base")))
        if (auto* C=J.Component.Get()) C->SetRelativeLocation(C->GetRelativeLocation()+BaseOffsets[J.RobotIndex]);
    for (int32 I=0;I<4;++I) {
        auto* Base=FindSceneMesh(TEXT("SM_")+EmitterRigs[I].Id+TEXT("_fixed_base"));
        Base->SetRelativeLocation(Base->GetRelativeLocation()+BaseOffsets[I]);
    }
    // Pedestals belong to physical stations, independently of logical tool roles.
    const TCHAR* PedestalNames[]={TEXT("SM_AssemblyPedestal_R1"),TEXT("SM_AssemblyPedestal_R2"),TEXT("SM_AssemblyPedestal_R4")};
    for (int32 I=0;I<3;++I) {
        auto* Pedestal=FindSceneMesh(PedestalNames[I]);
        Pedestal->SetRelativeLocation(Pedestal->GetRelativeLocation()+PedestalOffsets[I]);
    }
    for (const TCHAR* Name:{TEXT("SM_AssemblyFence_Black"),TEXT("SM_AssemblyFence_Yellow")}) {
        auto* M=FindSceneMesh(Name); M->SetVisibility(false); M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    // Bring the complete source platforms inward and forward beneath the two
    // retained clamps. Keep deck heights and scaffold/deck registration intact.
    for (int32 I=0;I<2;++I) for (const TCHAR* Kind:{TEXT("FloorFixture"),TEXT("FloorDeck")})
        FindSceneMesh(FString::Printf(TEXT("SM_Assembly%s_R%d"),Kind,I+1))->AddLocalOffset(ProcessFloorOffsets[I]);
    for (auto& M:PanelSupplyMeshes) if (auto* Mesh=M.Get()) Mesh->AddLocalOffset(SupplyDelta);
    FindSceneMesh(TEXT("SM_PanelSupply_Rack"))->AddLocalOffset(SupplyDelta);
    // Process-only parked posture clears the relocated stock with the long
    // rigid gripper; legacy actors retain every source rest angle.
    for (auto& Joint:JointMotion) if (Joint.RobotIndex==PanelHolderRobot) {
        const FString JointRole=Joint.Id.Mid(EmitterRigs[PanelHolderRobot].Id.Len()+1);
        for (int32 I=0;I<6;++I) if (JointRole==InspectionJointRoles[I]) Joint.RestAngle=PanelHolderWaypoints[9][I];
    }
    // Rotate the dispenser's outer wrist at rest so its reservoir clears the
    // fixed-base housing. Work strokes retain their fitted nozzle endpoints.
    for (auto& Joint:JointMotion) if (Joint.RobotIndex==PanelAdhesiveRobot) {
        const FString JointRole=Joint.Id.Mid(EmitterRigs[PanelAdhesiveRobot].Id.Len()+1);
        for (int32 I=0;I<6;++I) if (JointRole==InspectionJointRoles[I]) Joint.RestAngle=AdhesiveRestAngles[I];
    }
    for (auto& Joint:JointMotion) if (Joint.RobotIndex==PanelWelderRobot) {
        const FString JointRole=Joint.Id.Mid(EmitterRigs[PanelWelderRobot].Id.Len()+1);
        for (int32 I=0;I<6;++I) if (JointRole==InspectionJointRoles[I]) Joint.RestAngle=WelderRestAngles[I];
    }
    // Use the equivalent negative rest yaw to avoid a full extra revolution
    // when the rear-facing camera deploys from its new front-left station.
    for (auto& Joint:JointMotion) if (Joint.RobotIndex==3) {
        const FString JointRole=Joint.Id.Mid(EmitterRigs[3].Id.Len()+1);
        for (int32 I=0;I<6;++I) if (JointRole==InspectionJointRoles[I]) Joint.RestAngle=InspectionRestAngles[I];
    }
    PanelHeldOffset=FTransform(HolderPanelRotation,HolderPanelOffset);
    auto& Welder=EmitterRigs[PanelWelderRobot];
    if (auto* Tool=Welder.SourceTool.Get()) {
        // Preserve the purchased mesh, normals, UVs and all heat-enabled MIDs.
        // A positive uniform scale and proper rotation align its actual electrode.
        const FVector SourceTip=PinchTCP;
        const FVector SourceDirection=PinchDirection;
        Tool->SetRelativeTransform(DirectGunMount);
        Welder.HeatTipLocal=SourceTip; Welder.Position=Tool->GetRelativeTransform().TransformPosition(SourceTip);
        Welder.Direction=Tool->GetRelativeTransform().TransformVectorNoScale(SourceDirection).GetSafeNormal();
    }
    PinchMount=DirectGunMount;
    if (auto* Source=Welder.SourceTool.Get()) {
        Source->SetVisibility(false); Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        for (int32 I=0;I<3;++I) {
            auto* Part=NewObject<UStaticMeshComponent>(this,PinchNames[I]);
            Part->SetMobility(EComponentMobility::Movable); Part->SetupAttachment(Welder.Joint.Get());
            Part->SetStaticMesh(PinchAssets[I]); Part->SetRelativeTransform(DirectGunMount);
            for (int32 M=0;M<Source->GetNumMaterials();++M) Part->SetMaterial(M,Source->GetMaterial(M));
            Part->SetLightingChannels(true,true,false);
            // Authored jaw kinematics: particle collision belongs to the sheet stack.
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->RegisterComponent();
            MeshComponents.Add(Part); PinchParts.Add(Part);
            FPanelProfileShare Share; Share.Target=Part; Share.Source=Source; PanelProfileShares.Add(Share);
        }
    }
    for (int32 I:{PanelAdhesiveRobot,PanelHolderRobot}) if (auto* M=EmitterRigs[I].SourceTool.Get()) {
        M->SetVisibility(false); M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    auto Material=[&](const FLinearColor& Color,float Roughness,float Metallic) {
        auto* MID=UMaterialInstanceDynamic::Create(ToolMaterial,this);
        MID->SetVectorParameterValue(TEXT("BaseColor"),Color); MID->SetScalarParameterValue(TEXT("Roughness"),Roughness);
        MID->SetScalarParameterValue(TEXT("Metallic"),Metallic); return MID;
    };
    auto* Gunmetal=Material(FLinearColor(.045f,.055f,.072f),.35f,.85f);
    auto* Silver=Material(FLinearColor(.55f,.61f,.68f),.24f,.95f);
    auto* Copper=Material(FLinearColor(.62f,.24f,.085f),.28f,.88f);
    auto* Purple=Material(FLinearColor(FColor(117,59,189)),.31f,.25f);
    auto* Blue=Material(FLinearColor(.025f,.13f,.31f),.36f,.4f);
    auto* Rubber=Material(FLinearColor(.012f,.015f,.021f),.78f,0);
    auto Prop=[&](const TCHAR* Name,int32 Robot,UStaticMesh* Shape,const FVector& Position,const FVector& Dimensions,UMaterialInterface* Mat,const FRotator& Rotation=FRotator::ZeroRotator) {
        auto* M=NewObject<UStaticMeshComponent>(this,Name); M->SetMobility(EComponentMobility::Movable);
        M->SetupAttachment(EmitterRigs[Robot].Joint.Get()); M->SetStaticMesh(Shape);
        M->SetRelativeLocation(Position); M->SetRelativeRotation(Rotation); M->SetRelativeScale3D(Dimensions/100.);
        M->SetMaterial(0,Mat); M->SetLightingChannels(true,true,false);
        M->SetCollisionEnabled(ECollisionEnabled::QueryOnly); M->SetCollisionResponseToAllChannels(ECR_Ignore);
        M->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); M->SetGenerateOverlapEvents(false); M->RegisterComponent();
        MeshComponents.Add(M); FPanelProfileShare Share; Share.Target=M;
        Share.Source=EmitterRigs[Robot==PanelWelderRobot ? PanelHolderRobot:Robot].SourceTool.Get(); PanelProfileShares.Add(Share);
        return M;
    };
    // The purchased gun now mates directly to its original wrist flange.
    const FVector Tip=EmitterRigs[PanelAdhesiveRobot].Position;
    const double MountYaw=FMath::RadiansToDegrees(FMath::Atan2(Tip.Y,Tip.X));
    Prop(TEXT("PanelDispenserFlange"),PanelAdhesiveRobot,Cylinder,FVector(0,0,-3),FVector(28,28,6),Silver);
    Prop(TEXT("PanelDispenserMount"),PanelAdhesiveRobot,Cube,FVector(Tip.X*.5,Tip.Y*.5,-10),FVector(45,24,8),Gunmetal,FRotator(0,MountYaw,0));
    Prop(TEXT("PanelAdhesiveReservoir"),PanelAdhesiveRobot,Cylinder,FVector(Tip.X,Tip.Y,-37),FVector(18,18,44),Purple);
    for (int32 I=0;I<2;++I) Prop(*FString::Printf(TEXT("PanelDispenserBand%d"),I),PanelAdhesiveRobot,Cylinder,FVector(Tip.X,Tip.Y,I==0 ? -18:-56),FVector(19.5,19.5,3),Silver);
    Prop(TEXT("PanelDispenserValve"),PanelAdhesiveRobot,Cube,FVector(Tip.X,Tip.Y,-65),FVector(14,14,12),Gunmetal);
    Prop(TEXT("PanelDispenserReducer"),PanelAdhesiveRobot,Cone,FVector(Tip.X,Tip.Y,-75),FVector(12,12,8),Copper,FRotator(180,0,0));
    Prop(TEXT("PanelAdhesiveNozzle"),PanelAdhesiveRobot,Cylinder,FVector(Tip.X,Tip.Y,(-79+Tip.Z)*.5),FVector(2.4,2.4,-79-Tip.Z),Silver);
    Prop(TEXT("PanelDispenserController"),PanelAdhesiveRobot,Cube,FVector(Tip.X+11,Tip.Y,-39),FVector(6,12,23),Blue);
    Prop(TEXT("PanelDispenserFeedTube"),PanelAdhesiveRobot,Cylinder,FVector(Tip.X-11,Tip.Y,-30),FVector(4,4,38),Rubber);
    for (int32 I=0;I<6;++I) {
        const double Angle=2.*PI*I/6.;
        Prop(*FString::Printf(TEXT("PanelDispenserBolt%d"),I),PanelAdhesiveRobot,Cylinder,FVector(10.8*FMath::Cos(Angle),10.8*FMath::Sin(Angle),-6.7),FVector(2.6,2.6,2.2),Gunmetal);
    }
    const double FrameShift=122.-HolderStem;
    Prop(TEXT("PanelGripperFlange"),PanelHolderRobot,Cylinder,FVector(0,0,-3),FVector(25,25,6),Silver);
    Prop(TEXT("PanelGripperStem"),PanelHolderRobot,Cylinder,FVector(0,0,-HolderStem*.5),FVector(14,14,HolderStem),Silver);
    auto RackBeam=[&](const TCHAR* Name,const FVector& Position,const FVector& NativeDimensions,const FVector& Axis) {
        // Exact purchased H-profile, source normals/UVs and the rack's own blue
        // material. Only instance dimensions/orientation differ from the rack.
        auto* M=Prop(Name,PanelHolderRobot,RackBar,Position,FVector(100),RackMaterial,FRotationMatrix::MakeFromZ(Axis).Rotator());
        M->SetRelativeScale3D(NativeDimensions/RackBar->GetBoundingBox().GetSize());
    };
    // Eight-centimeter depth puts the upper flange exactly at the stem end.
    RackBeam(TEXT("PanelGripperCrossX"),FVector(0,0,-126+FrameShift),FVector(8,8,112),FVector::ForwardVector);
    for (int32 I=0;I<2;++I) RackBeam(*FString::Printf(TEXT("PanelGripperCrossY%d"),I),FVector(I ? 44:-44,0,-128+FrameShift),FVector(7,7,70),FVector::RightVector);
    // Articulated pad feet land on authored solid panel surfaces. Short angled
    // struts connect them to the purchased blue rack bars at their real anchors.
    for (int32 I=0;I<GripPads.Num();++I) {
        const auto& Pad=GripPads[I];
        const FVector Back=Pad.Contact+Pad.Normal*3.;
        const FVector Rod=Pad.Anchor-Back;
        Prop(*FString::Printf(TEXT("PanelGripperRod%d"),I),PanelHolderRobot,Cylinder,(Pad.Anchor+Back)*.5,
            FVector(4,4,Rod.Size()),Silver,FRotationMatrix::MakeFromZ(Rod).Rotator());
        Prop(*FString::Printf(TEXT("PanelGripperPad%d"),I),PanelHolderRobot,Cylinder,Pad.Contact+Pad.Normal*1.5,
            FVector(Pad.Diameter,Pad.Diameter,3),Rubber,FRotationMatrix::MakeFromZ(Pad.Normal).Rotator());
    }
    for (auto& Spot:BodyWeldSpots) { Spot.TemperatureC=Spot.HaloTemperatureC=20; Spot.Glow=Spot.WitnessStrength=0; }
    for (const auto& Binding:BodyWeldMaterials) for (const auto& W:Binding.Materials) if (auto* M=W.Get()) {
        M->SetScalarParameterValue(TEXT("WeldWitnessEnabled"),0);
        for (int32 I=0;I<6;++I) M->SetScalarParameterValue(FName(*FString::Printf(TEXT("WeldHeatGlow%d"),I)),0);
    }
    bPanelAssembly=true; PanelPhase=EDarkFactoryProcessPhase::Arriving; PanelPhaseSeconds=0; bPanelHolderEngaged=false;
    PanelToolHeatClock=ProcessClock; InspectionRequestedSpot=0; InspectionScanStartClock=ProcessClock-InspectionScanDurationSeconds;
    for (int32 J=0;J<6;++J) InspectionScanFromAngles[J]=PanelInspectionPoses[0][J];
    SetPanelSupplyCount(5); BindPanelSharedProfiles();
    return true;
}

UStaticMeshComponent* ADarkFactoryWeldingCell::GetPanelBodyMesh() const { return FindSceneMesh(TEXT("SM_BodyPanel_0386")); }
FTransform ADarkFactoryWeldingCell::GetPanelSupplyTransform(int32 Slot) const {
    if (!PanelSupplyMeshes.IsValidIndex(Slot) || !PanelSupplyMeshes[Slot].IsValid()) return FTransform::Identity;
    return FTransform(PanelSupplyRotation,PanelSupplyMeshes[Slot]->GetRelativeLocation())*SceneRoot->GetComponentTransform();
}
FTransform ADarkFactoryWeldingCell::GetPanelHeldTransform() const {
    return EmitterRigs.IsValidIndex(PanelHolderRobot) && EmitterRigs[PanelHolderRobot].Joint.IsValid()
        ? PanelHeldOffset*EmitterRigs[PanelHolderRobot].Joint->GetComponentTransform():FTransform::Identity;
}
bool ADarkFactoryWeldingCell::GetPanelContact(int32 Index,FVector& LocalPosition,FVector& LocalNormal) const {
    if (!bPanelAssembly || !PanelContacts.IsValidIndex(Index)) return false;
    LocalPosition=PanelContacts[Index]; LocalNormal=PanelContactNormals[Index]; return true;
}
bool ADarkFactoryWeldingCell::GetPanelAdhesivePath(int32 Bead,TArray<FVector>& LocalPoints,TArray<FVector>& LocalNormals) const {
    if (!bPanelAssembly || !PanelAdhesivePathPoints.IsValidIndex(Bead) || !PanelAdhesivePathNormals.IsValidIndex(Bead)) return false;
    LocalPoints=PanelAdhesivePathPoints[Bead]; LocalNormals=PanelAdhesivePathNormals[Bead]; return true;
}
double ADarkFactoryWeldingCell::GetPanelAdhesiveStartSeconds(int32 Bead) { return Bead==0 ? 1.25:Bead==1 ? 6.80:-1.; }
double ADarkFactoryWeldingCell::GetPanelAdhesiveProgress(int32 Bead,double PhaseSeconds) {
    if (Bead<0 || Bead>1 || !FMath::IsFinite(PhaseSeconds)) return 0.;
    const double U=FMath::Clamp((PhaseSeconds-GetPanelAdhesiveStartSeconds(Bead))/4.75,0.,1.);
    return U*U*(3.-2.*U);
}
void ADarkFactoryWeldingCell::SetPanelSupplyCount(int32 Count) {
    PanelSupplyCount=FMath::Clamp(Count,0,5);
    for (int32 I=0;I<PanelSupplyMeshes.Num();++I) if (auto* M=PanelSupplyMeshes[I].Get()) {
        // Stock stays fixed; the outer sheet reappears when the carrier departs.
        M->SetVisibility(I<PanelSupplyCount); M->SetCollisionEnabled(I<PanelSupplyCount ? ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    }
}
void ADarkFactoryWeldingCell::SetPanelMotionState(EDarkFactoryProcessPhase Phase,double PhaseSeconds,bool HoldingPanel) {
    if (!bPanelAssembly || !FMath::IsFinite(PhaseSeconds) || PhaseSeconds<0) return;
    if (Phase!=PanelPhase) {
        if (Phase==EDarkFactoryProcessPhase::PanelPlacement) { PanelPickupSlot=4; bPanelHolderEngaged=true; }
        if (Phase==EDarkFactoryProcessPhase::Arriving || Phase==EDarkFactoryProcessPhase::Departing) bPanelHolderEngaged=false;
    }
    bPanelHolderEngaged=Phase!=EDarkFactoryProcessPhase::Arriving && Phase!=EDarkFactoryProcessPhase::Departing
        && (Phase==EDarkFactoryProcessPhase::PanelPlacement || HoldingPanel);
    PanelPhase=Phase; PanelPhaseSeconds=PhaseSeconds;
}
double ADarkFactoryWeldingCell::PanelJointAngle(const FJointMotion& Joint) const {
    const int32 MotionIndex=JointMotion.IndexOfByPredicate([&](const FJointMotion& J){return &J==&Joint;});
    const FString Suffix=Joint.Id.Mid(EmitterRigs[Joint.RobotIndex].Id.Len()+1);
    int32 J=INDEX_NONE; for (int32 I=0;I<6;++I) if (Suffix==InspectionJointRoles[I]) { J=I; break; }
    if (J==INDEX_NONE || MotionIndex==INDEX_NONE) return Joint.RestAngle;
    const double T=PanelPhaseSeconds,Rest=Joint.RestAngle;
    auto Pose=[&](int32 P) { return PanelAssemblyPoses[P][J]; };
    auto Blend=[&](double A,double B,double Alpha) { return A+FMath::UnwindDegrees(B-A)*Smooth(Alpha); };
    auto W=[&](int32 P) { return PanelWelderWaypoints[P][J]; };
    auto A=[&](int32 P) { return PanelAdhesiveWaypoints[P][J]; };
    auto H=[&](int32 P) { return PanelHolderWaypoints[P][J]; };
    auto FrontTurn=[&](double From,double To,double Alpha) { return J==0 ? FMath::Lerp(From,To,Smooth(Alpha)):Blend(From,To,Alpha); };
    if (Joint.RobotIndex==PanelHolderRobot) {
        if (!bPanelHolderEngaged) return Rest;
        if (PanelPhase==EDarkFactoryProcessPhase::PanelPlacement) {
            const int32 Pick=6,Lift=7;
            if (T<.65) return Blend(Rest,Pose(Lift),T/.65);
            if (T<1.25) return Blend(Pose(Lift),Pose(Pick),(T-.65)/.60);
            if (T<1.5) return Pose(Pick);
            if (T<2.2) return Blend(Pose(Pick),Pose(Lift),(T-1.5)/.7);
            if (T<2.6) return Blend(Pose(Lift),H(0),(T-2.2)/.4);
            // The long base arc goes around the rack; unwrap only its display angle.
            if (T<3.45) return FMath::UnwindDegrees(FrontTurn(H(0),H(1)+(J==0 ? 360.:0.),(T-2.6)/.85));
            if (T<3.85) return Blend(H(1),H(2),(T-3.45)/.4);
            if (T<4.15) return Blend(H(2),H(3),(T-3.85)/.3);
            if (T<4.45) return Blend(H(3),H(4),(T-4.15)/.3);
            if (T<4.75) return Blend(H(4),H(5),(T-4.45)/.3);
            if (T<5.15) return Blend(H(5),H(6),(T-4.75)/.4);
            if (T<5.55) return Blend(H(6),H(7),(T-5.15)/.4);
            if (T<6.) return Blend(H(7),Pose(9),(T-5.55)/.45);
            return Pose(9);
        }
        if (PanelPhase==EDarkFactoryProcessPhase::HolderRetract) {
            const int32 Lift=7;
            if (T<.35) return Blend(Pose(9),H(8),T/.35);
            if (T<.7) return Blend(H(8),H(2),(T-.35)/.35);
            if (T<1.) return Blend(H(2),H(1),(T-.7)/.3);
            if (T<1.8) return FMath::UnwindDegrees(FrontTurn(H(1)+(J==0 ? 360.:0.),H(0),(T-1.)/.8));
            if (T<2.1) return Blend(H(0),Pose(Lift),(T-1.8)/.3);
            return Blend(Pose(Lift),Rest,(T-2.1)/(PanelRetractSeconds-2.1));
        }
        return Pose(9);
    }
    // R1 temporarily faces the front while R3 takes its rack detour. The
    // dispenser wrist retains its process-only clearance pose during holds.
    if (Joint.RobotIndex==PanelAdhesiveRobot && J==0) {
        if (PanelPhase==EDarkFactoryProcessPhase::PanelPlacement)
            return T<.65 ? Blend(Rest,-90.,T/.65):T<5.25 ? -90.:Blend(-90.,Rest,(T-5.25)/.75);
        if (PanelPhase==EDarkFactoryProcessPhase::HolderRetract)
            return T<.35 ? Blend(Rest,-90.,T/.35):T<2.1 ? -90.:Blend(-90.,Rest,(T-2.1)/(PanelRetractSeconds-2.1));
    }
    if (Joint.RobotIndex==PanelAdhesiveRobot && PanelPhase==EDarkFactoryProcessPhase::AdhesiveApplication) {
        auto Stroke=[&](int32 Bead,double U) {
            const auto& Progress=PanelAdhesivePathProgress[Bead]; int32 Segment=0;
            while (Segment<39 && Progress[Segment+1]<U) ++Segment;
            const double Alpha=FMath::Clamp((U-Progress[Segment])/(Progress[Segment+1]-Progress[Segment]),0.,1.);
            const double From=PanelAdhesiveStrokePoses[Bead*41+Segment][J],To=PanelAdhesiveStrokePoses[Bead*41+Segment+1][J];
            return From+FMath::UnwindDegrees(To-From)*Alpha;
        };
        if (T<.30) return Blend(Rest,A(0),T/.30);
        if (T<.60) return FrontTurn(A(0),A(1),(T-.30)/.30);
        if (T<.85) return Blend(A(1),A(2),(T-.60)/.25);
        if (T<1.25) return Blend(A(2),Stroke(0,0),(T-.85)/.40);
        if (T<6.00) return Stroke(0,GetPanelAdhesiveProgress(0,T));
        if (T<6.25) return Blend(Stroke(0,1),A(3),(T-6.00)/.25);
        if (T<6.55) return Blend(A(3),A(4),(T-6.25)/.30);
        if (T<6.80) return Blend(A(4),Stroke(1,0),(T-6.55)/.25);
        if (T<11.55) return Stroke(1,GetPanelAdhesiveProgress(1,T));
        if (T<11.85) return Blend(Stroke(1,1),A(5),(T-11.55)/.30);
        if (T<12.15) return Blend(A(5),A(6),(T-11.85)/.30);
        if (T<13.15) return FrontTurn(A(6),A(0),(T-12.15));
        return Blend(A(0),Rest,(T-13.15)/.35);
    }
    if (Joint.RobotIndex==PanelWelderRobot && PanelPhase==EDarkFactoryProcessPhase::Welding) {
        auto Work=[&](int32 I) { return PanelWeldPoses[I][J]; };
        auto Travel=[&](int32 Segment,double U) {
            const double Scaled=FMath::Clamp(U,0.,1.)*10.;
            const int32 K=FMath::Min(9,FMath::FloorToInt(Scaled));
            const double From=PanelWeldCrossPoses[Segment*10+K][J],To=PanelWeldCrossPoses[Segment*10+K+1][J];
            return From+FMath::UnwindDegrees(To-From)*(Scaled-K);
        };
        if (T<.35) return Blend(Rest,W(0),T/.35);
        if (T<.90) return Blend(W(0),W(1),(T-.35)/.55);
        if (T<1.3) return Blend(W(1),Work(0),(T-.90)/.40);
        for (int32 I=0;I<PanelWeldCount;++I) {
            const double End=PanelWeldStartSeconds(I)+.5;
            // Settle, squeeze, current pulse, hold, open; translate only after release.
            if (T<End+.20) return Work(I);
            if (I+1<PanelWeldCount && T<PanelWeldStartSeconds(I+1)-.15)
                return Travel(I,Smooth((T-End-.20)/.50));
        }
        const double Last=PanelWeldStartSeconds(PanelWeldCount-1)+.70;
        if (T<9.6) return Blend(Work(5),W(2),(T-Last)/(9.6-Last));
        if (T<10.4) return Blend(W(2),W(0),(T-9.6)/.8);
        return Blend(W(0),Rest,(T-10.4)/(PanelWeldingSeconds-10.4));
    }
    return Rest;
}
void ADarkFactoryWeldingCell::UpdatePinchGun() {
    if (!bPanelAssembly || PinchParts.Num()!=3) return;
    double Angle=PinchOpenAngle;
    if (PanelPhase==EDarkFactoryProcessPhase::Welding) for (int32 I=0;I<PanelWeldCount;++I) {
        const double Age=PanelPhaseSeconds-PanelWeldStartSeconds(I);
        if (Age>=-.15 && Age<.70) {
            const double Closed=Age<0 ? Smooth((Age+.15)/.15):Age<.58 ? 1.:1.-Smooth((Age-.58)/.12);
            Angle=FMath::Lerp(PinchOpenAngle,PinchClampAngle,Closed); break;
        }
    }
    PinchCurrentAngle=Angle;
    for (int32 I=0;I<2;++I) if (auto* Jaw=PinchParts[I+1].Get()) {
        const FQuat Q(PinchAxis,FMath::DegreesToRadians((I==0 ? -1.:1.)*Angle));
        const FTransform Hinge(Q,PinchPivots[I]-Q.RotateVector(PinchPivots[I]));
        Jaw->SetRelativeTransform(Hinge*PinchMount);
    }
}
void ADarkFactoryWeldingCell::BindPanelSharedProfiles() {
    auto Share=[&](UStaticMeshComponent* Target,UStaticMeshComponent* Source) {
        if (Target && Source && Source->CustomDepthStencilValue>0) {
            Target->SetRenderCustomDepth(true); Target->SetCustomDepthStencilValue(Source->CustomDepthStencilValue);
        }
    };
    for (const auto& M:MeshComponents) if (M) {
        const FString Name=M->GetName();
        if (Name.StartsWith(TEXT("SM_PanelSupply_"))) Share(M,FindSceneMesh(Name.EndsWith(TEXT("Rack")) ? TEXT("SM_Background_robots_blue"):
            bPanelAssembly ? TEXT("SM_BodyPanel_0386"):TEXT("SM_Background_chrome")));
        else if (Name.StartsWith(TEXT("SM_AssemblyPedestal_"))) Share(M,FindSceneMesh(TEXT("SM_Background_robots_tiles")));
        else if (Name.StartsWith(TEXT("SM_AssemblyFloorFixture_"))) Share(M,FindSceneMesh(TEXT("SM_Background_chrome")));
        else if (Name.StartsWith(TEXT("SM_AssemblyFloorDeck_"))) Share(M,FindSceneMesh(TEXT("SM_Background_robots_tiles")));
        else if (Name==TEXT("SM_AssemblyFence_Black")) Share(M,FindSceneMesh(TEXT("SM_Background_robots_plastic_black")));
        else if (Name==TEXT("SM_AssemblyFence_Yellow")) Share(M,FindSceneMesh(TEXT("SM_Background_robots_yellow")));
    }
    for (const auto& Pair:PanelProfileShares) Share(Pair.Target.Get(),Pair.Source.Get());
    if (bPanelAssembly) if (auto* Thermal=ToolThermalRig.Get()) {
        for (const auto& Binding:BodyWeldMaterials) if (auto* M=Binding.Mesh.Get()) Thermal->SetImportedWeldHeat(M,{});
    }
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::GetPanelAssemblyMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetBoolField(TEXT("enabled"),bPanelAssembly);
    O->SetStringField(TEXT("recipe"),TEXT("authored-panel-assembly-v1"));
    O->SetStringField(TEXT("motion_recipe"),PanelMotionRecipe);
    O->SetNumberField(TEXT("pinch_angle_deg"),PinchCurrentAngle);
    O->SetNumberField(TEXT("pinch_part_count"),PinchParts.Num());
    TArray<TSharedPtr<FJsonValue>> PinchTipValues; FVector TipWorld[2];
    if (PinchParts.Num()==3) for (int32 I=0;I<2;++I) if (auto* Jaw=PinchParts[I+1].Get()) {
        TipWorld[I]=Jaw->GetComponentTransform().TransformPosition(PinchTips[I]);
        PinchTipValues.Add(MakeShared<FJsonValueArray>(Vec(TipWorld[I])));
    }
    O->SetArrayField(TEXT("pinch_tips_world_cm"),PinchTipValues);
    if (PinchTipValues.Num()==2) O->SetNumberField(TEXT("pinch_gap_cm"),FVector::Distance(TipWorld[0],TipWorld[1]));
    O->SetStringField(TEXT("layout_recipe"),PanelLayoutRecipe);
    O->SetStringField(TEXT("motion_authoring_sha256"),PanelAuthoringSHA256);
    O->SetNumberField(TEXT("adhesive_bead_count"),2);
    O->SetStringField(TEXT("floor_platform_layout"),TEXT("front-clamp-support-v1"));
    O->SetArrayField(TEXT("floor_platform_offsets_cm"),{MakeShared<FJsonValueArray>(Vec(ProcessFloorOffsets[0])),MakeShared<FJsonValueArray>(Vec(ProcessFloorOffsets[1]))});
    O->SetArrayField(TEXT("adhesive_progress"),{MakeShared<FJsonValueNumber>(GetPanelAdhesiveProgress(0,PanelPhaseSeconds)),MakeShared<FJsonValueNumber>(GetPanelAdhesiveProgress(1,PanelPhaseSeconds))});
    if (const auto* Joint=EmitterRigs[PanelAdhesiveRobot].Joint.Get()) O->SetArrayField(TEXT("dispenser_tip_world_cm"),Vec(Joint->GetComponentTransform().TransformPosition(EmitterRigs[PanelAdhesiveRobot].Position)));
    O->SetStringField(TEXT("stock_feed"),TEXT("fixed outer slot4; picked sheet reappears when carrier departs"));
    O->SetNumberField(TEXT("stock_pickup_slot"),4);
    if (bPanelAssembly) {
        if (const auto* Rack=FindSceneMesh(TEXT("SM_PanelSupply_Rack")))
            O->SetArrayField(TEXT("supply_rack_world_location_cm"),Vec(Rack->GetComponentLocation()));
        O->SetArrayField(TEXT("supply_slot4_world_center_cm"),Vec(GetPanelSupplyTransform(4).GetLocation()));
    }
    O->SetStringField(TEXT("welding_head"),TEXT("original purchased gun; uniform scale0.7; direct native-cap mount with axial rollÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢90 degrees; no extension rod; source normals and heat MIDs preserved"));
    O->SetBoolField(TEXT("welder_extension_rod"),false);
    O->SetStringField(TEXT("body_template"),TEXT("SM_BodyPanel_0386"));
    O->SetStringField(TEXT("body_geometry_recipe"),bPanelAssembly ? TEXT("rear-roof-open-v1"):TEXT("source-body-v1"));
    if (bPanelAssembly) {
        const auto* Roof=FindSceneMesh(ProcessRemovedRoof);
        O->SetStringField(TEXT("removed_roof_component"),ProcessRemovedRoof);
        O->SetBoolField(TEXT("rear_roof_hidden"),Roof && !Roof->IsVisible());
        O->SetBoolField(TEXT("rear_roof_collision_disabled"),Roof && Roof->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        for (const auto& Shape:CarriageColliders) if (Shape && Shape->GetFName()==FName(TEXT("CarriageRoof"))) {
            O->SetArrayField(TEXT("roof_collider_min_cm"),Vec(Shape->GetRelativeLocation()-Shape->GetUnscaledBoxExtent()));
            O->SetArrayField(TEXT("roof_collider_max_cm"),Vec(Shape->GetRelativeLocation()+Shape->GetUnscaledBoxExtent()));
        }
    }
    O->SetNumberField(TEXT("welder_robot_index"),PanelWelderRobot); O->SetNumberField(TEXT("adhesive_robot_index"),PanelAdhesiveRobot);
    O->SetNumberField(TEXT("holder_robot_index"),PanelHolderRobot); O->SetNumberField(TEXT("camera_robot_index"),3);
    O->SetBoolField(TEXT("holder_engaged"),bPanelHolderEngaged); O->SetNumberField(TEXT("supply_remaining"),PanelSupplyCount);
    O->SetNumberField(TEXT("pickup_slot"),PanelPickupSlot); O->SetNumberField(TEXT("phase_seconds"),PanelPhaseSeconds);
    O->SetBoolField(TEXT("legacy_body_heat_disabled"),bPanelAssembly);
    TArray<TSharedPtr<FJsonValue>> Robots;
    auto Quaternion=[](const FQuat& Q) -> TArray<TSharedPtr<FJsonValue>> {
        return {MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)};
    };
    if (bPanelAssembly) if (const auto* Tool=EmitterRigs[PanelWelderRobot].SourceTool.Get()) {
        const auto& Mount=Tool->GetRelativeTransform();
        O->SetStringField(TEXT("source_welder_mount_recipe"),TEXT("native-cap-direct-v1"));
        O->SetArrayField(TEXT("source_welder_relative_location_cm"),Vec(Mount.GetLocation()));
        O->SetArrayField(TEXT("source_welder_relative_quaternion_xyzw"),Quaternion(Mount.GetRotation()));
        O->SetArrayField(TEXT("source_welder_relative_scale"),Vec(Mount.GetScale3D()));
        O->SetArrayField(TEXT("source_welder_electrode_local_cm"),Vec(EmitterRigs[PanelWelderRobot].Position));
    }
    for (int32 RobotIndex=0;RobotIndex<EmitterRigs.Num();++RobotIndex) {
        auto Robot=MakeShared<FJsonObject>(); Robot->SetNumberField(TEXT("robot_index"),RobotIndex);
        Robot->SetStringField(TEXT("id"),EmitterRigs[RobotIndex].Id);
        TArray<TSharedPtr<FJsonValue>> Joints;
        for (const auto& Joint:JointMotion) if (Joint.RobotIndex==RobotIndex) if (const auto* Component=Joint.Component.Get()) {
            auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"),Joint.Id);
            J->SetNumberField(TEXT("angle_deg"),Joint.Angle); J->SetNumberField(TEXT("rest_angle_deg"),Joint.RestAngle);
            J->SetArrayField(TEXT("axis"),Vec(Joint.Axis));
            J->SetArrayField(TEXT("relative_location_cm"),Vec(Component->GetRelativeLocation()));
            J->SetArrayField(TEXT("relative_quaternion_xyzw"),Quaternion(Component->GetRelativeRotation().Quaternion()));
            J->SetArrayField(TEXT("world_location_cm"),Vec(Component->GetComponentLocation()));
            J->SetArrayField(TEXT("world_quaternion_xyzw"),Quaternion(Component->GetComponentQuat()));
            Joints.Add(MakeShared<FJsonValueObject>(J));
        }
        Robot->SetArrayField(TEXT("joints"),Joints); Robots.Add(MakeShared<FJsonValueObject>(Robot));
    }
    O->SetArrayField(TEXT("robots"),Robots);
    O->SetStringField(TEXT("decorative_thermal_profiles"),TEXT("Explicit shared uniform source stencils: cold template for stock, blue rack, tiles pedestals, matching fence backgrounds, cold non-welding tools for gripper/nozzle"));
    if (bPanelAssembly) {
        const FTransform P=GetPanelHeldTransform(); O->SetArrayField(TEXT("held_panel_location_cm"),Vec(P.GetLocation()));
        O->SetArrayField(TEXT("fixed_holder_offset_cm"),Vec(PanelHeldOffset.GetLocation()));
        O->SetArrayField(TEXT("fixed_holder_quaternion_xyzw"),Quaternion(PanelHeldOffset.GetRotation()));
        O->SetNumberField(TEXT("holder_stem_length_cm"),-PanelHeldOffset.GetLocation().Z-28.);
        O->SetStringField(TEXT("gripper_frame_source"),TEXT("SM_PanelGripperRackBar / robots_blue island158; original rack material"));
        TArray<TSharedPtr<FJsonValue>> Contacts;
        for (int32 I=0;I<PanelWeldCount;++I) { auto C=MakeShared<FJsonObject>(); C->SetArrayField(TEXT("position_cm"),Vec(PanelContacts[I])); C->SetArrayField(TEXT("normal"),Vec(PanelContactNormals[I])); Contacts.Add(MakeShared<FJsonValueObject>(C)); }
        O->SetArrayField(TEXT("contacts"),Contacts);
    }
    return O;
}
