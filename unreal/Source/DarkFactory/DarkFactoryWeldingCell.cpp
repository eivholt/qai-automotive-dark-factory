#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRig.h"
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
    Timing.ApproachEndSeconds=Timing.ApproachStartSeconds+1.;
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
    if (!bRandomizedRobotTiming) return T;
    if (T<Timing.ApproachStartSeconds) return 0.;
    if (T<Timing.ApproachEndSeconds) return 3.5+T-Timing.ApproachStartSeconds;
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
    if (!bLoaded || BodyComponents.IsEmpty()) return false;
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        const auto R=GetRobotTiming(I,MotionSeconds);
        if (R.CycleSeconds>=R.WeldStartSeconds && R.CycleSeconds<R.WeldEndSeconds) return true;
    }
    return false;
}
TArray<FDarkFactoryWeldEmitter> ADarkFactoryWeldingCell::GetWeldEmitters() const {
    TArray<FDarkFactoryWeldEmitter> Result;
    if (!bLoaded || BodyComponents.IsEmpty()) return Result;
    for (int32 I=0;I<EmitterRigs.Num();++I) if (auto* Joint=EmitterRigs[I].Joint.Get()) {
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
        J.Angle=EvaluateJointAngle(J,MotionSeconds);
        Component->SetRelativeRotation(J.RestRotation*FQuat(J.Axis,FMath::DegreesToRadians(J.Angle)));
    }
    const double DepartureStart=GetCarriageDepartureStartSeconds(),DepartureEnd=GetCarriageDepartureEndSeconds();
    const double Offset=T<3 ? ArrivalOffset*(1.-Smooth(T/3.)) : T<DepartureStart ? 0. : T<DepartureEnd ? DepartureOffset*Smooth((T-DepartureStart)/3.5) : DepartureOffset;
    const bool Visible=T<DepartureEnd;
    // Explicit seeks and the offscreen cycle reset teleport; ordinary tick travel
    // moves kinematic bodies normally so Chaos receives their surface velocity.
    const bool Jump=FMath::Abs(MotionSeconds-Previous)>.2 || (GetCycleIndex()!=static_cast<int32>(FMath::FloorToDouble(Previous/GetCycleDurationSeconds())));
    if (!Visible) for (const auto& Box:CarriageColliders) if (Box) Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CarriageRoot->SetRelativeLocation(FVector(0,Offset,0),false,nullptr,Jump ? ETeleportType::TeleportPhysics : ETeleportType::None);
    if (Visible!=bCarVisible) {
        bCarVisible=Visible;
        for (const auto& Panel:BodyComponents) if (Panel) { Panel->SetVisibility(Visible); Panel->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); }
        for (const auto& Holder:HolderComponents) if (Holder) { Holder->SetVisibility(Visible); Holder->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); }
        for (const auto& Box:CarriageColliders) if (Box) Box->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
    UpdateHoses();
    UpdateToolHeat(MotionSeconds);
    UpdateBodyWeldHeat(MotionSeconds);
    UpdateMotorHeat(MotionSeconds);
}
void ADarkFactoryWeldingCell::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (bLoaded && bAnimating) SetMotionTime(MotionSeconds+FMath::Clamp(DeltaSeconds,0.f,.1f));
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildRobotTimingMetadata(double AbsoluteSeconds) const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("profile"),bRandomizedRobotTiming ? TEXT("seeded-robot-delays-v1"):TEXT("canonical-synchronized-qa-v1"));
    Result->SetBoolField(TEXT("randomized"),bRandomizedRobotTiming); Result->SetNumberField(TEXT("seed"),RobotTimingSeed);
    Result->SetNumberField(TEXT("schedule_bank_cycles"),bRandomizedRobotTiming ? RobotTimingBankCycles:1);
    Result->SetNumberField(TEXT("cycle_duration_seconds"),GetCycleDurationSeconds());
    Result->SetNumberField(TEXT("carriage_departure_start_seconds"),GetCarriageDepartureStartSeconds());
    Result->SetNumberField(TEXT("carriage_departure_end_seconds"),GetCarriageDepartureEndSeconds());
    Result->SetStringField(TEXT("scope"),bRandomizedRobotTiming
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
    O->SetNumberField(TEXT("robot_approach_seconds"),1.); O->SetNumberField(TEXT("robot_withdraw_seconds"),1.);
    O->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(MotionSeconds));
    O->SetStringField(TEXT("motion_profile"),bDramaticApproach ? TEXT("dramatic-approach-v1"):TEXT("staged-fold-yaw-v1"));
    O->SetStringField(TEXT("parking_sequence"),bDramaticApproach
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
    ToolThermalRig=Rig; UpdateToolHeat(ToolHeatSeconds); UpdateBodyWeldHeat(BodyWeldHeatSeconds); UpdateMotorHeat(MotorHeatSeconds,MotorIdleSeconds);
}
void ADarkFactoryWeldingCell::SetToolHeatTimeForQA(double Seconds) {
    if (FMath::IsFinite(Seconds) && Seconds>=0) UpdateToolHeat(FMath::Min(Seconds,1.e9));
}
void ADarkFactoryWeldingCell::UpdateToolHeat(double Seconds) {
    ToolHeatSeconds=Seconds;
    for (int32 I=0;I<EmitterRigs.Num();++I) {
        auto& Rig=EmitterRigs[I]; auto* Tool=Rig.SourceTool.Get(); if (!Tool) continue;
        Rig.BodyC=static_cast<float>(EvaluateToolTemperature(I,Rig.BodyCurve,Seconds));
        Rig.TipC=FMath::Max(Rig.BodyC,static_cast<float>(EvaluateToolTemperature(I,Rig.TipCurve,Seconds)));
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

double ADarkFactoryWeldingCell::EvaluateBodyWeldTemperature(int32 RobotIndex,double Seconds) const {
    // Each replacement body starts cold; only this robot's active weld heats its patch.
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
    for (auto& Spot:BodyWeldSpots) {
        const int32 RobotIndex=EmitterRigs.IndexOfByPredicate([&](const FEmitterRig& E){return E.Id==Spot.Id;});
        Spot.TemperatureC=static_cast<float>(EvaluateBodyWeldTemperature(RobotIndex,Seconds));
        const auto Timing=GetRobotTiming(RobotIndex,Seconds);
        Spot.WitnessStrength=bWeldWitnessEnabled ? FMath::Clamp(static_cast<float>((Timing.CycleSeconds-Timing.WeldStartSeconds-.2)/.8),0.f,1.f):0.f;
        const float Incandescence=FMath::Clamp((Spot.TemperatureC-70.f)/350.f,0.f,1.f);
        Spot.Glow=180.f*Incandescence*Incandescence;
    }
    for (const auto& Binding:BodyWeldMaterials) if (auto* Mesh=Binding.Mesh.Get()) {
        TArray<FDarkFactoryWeldHeatPatch> Patches;
        for (int32 Slot=0;Slot<Binding.SpotIndices.Num();++Slot) {
            const auto& Spot=BodyWeldSpots[Binding.SpotIndices[Slot]];
            const FVector Point=Mesh->GetComponentTransform().TransformPosition(Spot.LocalPosition);
            FDarkFactoryWeldHeatPatch Patch; Patch.LocalPositionCm=Spot.LocalPosition; Patch.RadiusCm=Spot.RadiusCm; Patch.SurfaceC=Spot.TemperatureC;
            Patches.Add(Patch);
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
    Result->SetStringField(TEXT("model"),TEXT("authored-local-body-weld-cooling-v1"));
    Result->SetStringField(TEXT("scope"),TEXT("Four surface-bound heat fields over the authored short seams; no bulk sheet-metal conduction or calibrated spectral emission"));
    Result->SetNumberField(TEXT("heat_time_seconds"),BodyWeldHeatSeconds);
    Result->SetBoolField(TEXT("witness_enabled"),bWeldWitnessEnabled);
    Result->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(BodyWeldHeatSeconds));
    Result->SetNumberField(TEXT("body_index"),FMath::FloorToDouble(BodyWeldHeatSeconds/GetCycleDurationSeconds()));
    Result->SetNumberField(TEXT("heating_tau_seconds"),.65); Result->SetNumberField(TEXT("cooling_tau_seconds"),2.5);
    Result->SetNumberField(TEXT("maximum_temperature_c"),420); Result->SetNumberField(TEXT("unique_heated_panels"),BodyWeldMaterials.Num());
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& Spot:BodyWeldSpots) if (const auto* Mesh=Spot.Mesh.Get()) {
        auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("id"),Spot.Id); Value->SetStringField(TEXT("surface"),Mesh->GetName());
        Value->SetNumberField(TEXT("stencil_id"),Mesh->CustomDepthStencilValue);
        Value->SetArrayField(TEXT("local_position_cm"),Vec(Spot.LocalPosition));
        Value->SetArrayField(TEXT("world_position_cm"),Vec(Mesh->GetComponentTransform().TransformPosition(Spot.LocalPosition)));
        Value->SetNumberField(TEXT("radius_cm"),Spot.RadiusCm); Value->SetNumberField(TEXT("temperature_c"),Spot.TemperatureC);
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
    MotorHeatSeconds=Seconds; MotorIdleSeconds=IdleSeconds;
    const int32 Step=FMath::Clamp(FMath::FloorToInt(FMath::Fmod(Seconds,GetTimingBankSeconds())/MotorDutyStep),0,MotorDutySteps-1);
    for (auto& Rig:MotorHeatRigs) if (auto* Mesh=Rig.Mesh.Get()) {
        Rig.TemperatureC=static_cast<float>(EvaluateMotorTemperature(Rig,Seconds,IdleSeconds));
        Rig.Activity=IdleSeconds>0 ? 0.f:Rig.Duty[Step];
        if (auto* Thermal=ToolThermalRig.Get()) Thermal->SetImportedCastingTemperature(Mesh,Rig.TemperatureC);
    }
}
TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildMotorHeatMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),TEXT("authored-joint-duty-thermal-v1"));
    Result->SetStringField(TEXT("scope"),TEXT("Casting-wide illustrative drive activity from angular travel; static holding torque, motor efficiency and physical heat transport are not modeled. No visible/NIR emission."));
    Result->SetNumberField(TEXT("duty_time_seconds"),MotorHeatSeconds); Result->SetNumberField(TEXT("idle_seconds"),MotorIdleSeconds);
    Result->SetNumberField(TEXT("ambient_temperature_c"),MotorAmbientC); Result->SetNumberField(TEXT("cycle_duration_seconds"),GetCycleDurationSeconds());
    Result->SetObjectField(TEXT("robot_timing"),BuildRobotTimingMetadata(MotorHeatSeconds));
    Result->SetNumberField(TEXT("duty_bank_seconds"),GetTimingBankSeconds());
    Result->SetNumberField(TEXT("duty_steps_per_profile"),MotorDutySteps);
    Result->SetNumberField(TEXT("integration_step_seconds"),MotorDutyStep); Result->SetNumberField(TEXT("profile_count"),MotorHeatRigs.Num());
    Result->SetStringField(TEXT("equation"),TEXT("dRise/dt=rate*activity-(rate*activity/maxRise+1/tau)*Rise; exact piecewise-constant duty steps and periodic affine recurrence; then exp(-idle/tau)"));
    if (MotorHeatSelfCheck) Result->SetObjectField(TEXT("self_check"),MotorHeatSelfCheck);
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
