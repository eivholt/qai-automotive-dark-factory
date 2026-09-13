#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryWeldingCell.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
const TCHAR* PhaseNames[]={TEXT("Rest"),TEXT("Approach"),TEXT("At contact"),TEXT("Welding"),TEXT("Withdraw"),TEXT("Parked")};
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& P) { return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)}; }
bool ReadVec(const TSharedPtr<FJsonObject>& O,const TCHAR* Name,FVector& P) {
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    if (!O || !O->TryGetArrayField(Name,A) || A->Num()!=3) return false;
    double X,Y,Z; if (!(*A)[0]->TryGetNumber(X) || !(*A)[1]->TryGetNumber(Y) || !(*A)[2]->TryGetNumber(Z)) return false;
    P=FVector(X,Y,Z); return !P.ContainsNaN();
}
}
ADarkFactoryControlCabinet::ADarkFactoryControlCabinet() {
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
}
bool ADarkFactoryControlCabinet::Initialize(ADarkFactoryWeldingCell* Cell) {
    if (bInitialized || !IsValid(Cell)) return false;
    SourceManifest=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),TEXT("../assets/derived/welding_cell/control_cabinets.json")));
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text,*SourceManifest) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root) {
        UE_LOG(LogTemp,Warning,TEXT("DF_CABINET_MISSING_GEOMETRY run scripts/assets/prepare_control_cabinets.py")); return false;
    }
    const TSharedPtr<FJsonObject>* Screen=nullptr; const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if (!Root->TryGetStringField(TEXT("parent_mesh"),ParentName) || !Root->TryGetObjectField(TEXT("screen"),Screen)
        || !Root->TryGetArrayField(TEXT("buttons"),Rows) || Rows->Num()!=12
        || !ReadVec(*Screen,TEXT("center_local_cm"),Center) || !ReadVec(*Screen,TEXT("normal_local"),Normal)
        || !ReadVec(*Screen,TEXT("right_local"),Right) || !ReadVec(*Screen,TEXT("up_local"),Up)
        || !(*Screen)->TryGetNumberField(TEXT("width_cm"),Width) || !(*Screen)->TryGetNumberField(TEXT("height_cm"),Height)) return false;
    if (!FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width<20 || Width>100 || Height<15 || Height>80
        || !FMath::IsNearlyEqual(Normal.SizeSquared(),1.,.001) || !FMath::IsNearlyEqual(Right.SizeSquared(),1.,.001)
        || !FMath::IsNearlyEqual(Up.SizeSquared(),1.,.001) || FMath::Abs(Normal|Right)>.001 || FMath::Abs(Normal|Up)>.001 || FMath::Abs(Right|Up)>.001) return false;
    for (const auto& R:*Rows) {
        const auto O=R->AsObject(); FButton B; double Radius=0;
        if (!O || !O->TryGetStringField(TEXT("id"),B.Id) || !O->TryGetStringField(TEXT("swatch_color"),B.Color)
            || !ReadVec(O,TEXT("center_local_cm"),B.Center) || !O->TryGetNumberField(TEXT("radius_cm"),Radius)
            || !FMath::IsFinite(Radius) || Radius<.1 || Radius>10 || (B.Color!=TEXT("red") && B.Color!=TEXT("green"))) return false;
        B.Radius=Radius; Buttons.Add(B);
    }
    for (const auto& S:Cell->GetThermalSurfaces()) if (S.Mesh && S.Mesh->GetStaticMesh() && S.Mesh->GetStaticMesh()->GetName()==ParentName) { Glass=S.Mesh; break; }
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_ControlCabinet.M_ControlCabinet"));
    if (!Glass.IsValid() || !Parent) { UE_LOG(LogTemp,Warning,TEXT("DF_CABINET_MISSING_CONTENT run setup_control_cabinet.py after asset import")); return false; }
    if (Glass->GetNumMaterials()!=1) return false;
    OriginalMaterial=Glass->GetMaterial(0); DisplayMaterial=UMaterialInstanceDynamic::Create(Parent,this);
    DisplayMaterial->SetScalarParameterValue(TEXT("ScreenWidth"),Width); DisplayMaterial->SetScalarParameterValue(TEXT("ScreenHeight"),Height);
    DisplayMaterial->SetScalarParameterValue(TEXT("ScreenStrength"),2.5f); DisplayMaterial->SetScalarParameterValue(TEXT("ButtonStrength"),5.f);
    SourceCell=Cell; AddTickPrerequisiteActor(Cell);
    ScreenLight=NewObject<URectLightComponent>(this,TEXT("CabinetScreenSpill")); ScreenLight->SetupAttachment(Glass.Get());
    ScreenLight->SetRelativeLocationAndRotation(Center+Normal*1.2,FRotationMatrix::MakeFromXZ(Normal,Up).Rotator());
    ScreenLight->SetMobility(EComponentMobility::Movable); ScreenLight->SetIntensityUnits(ELightUnits::Lumens);
    ScreenLight->SetIntensity(10.f); ScreenLight->SetAttenuationRadius(110.f); ScreenLight->SetSourceWidth(Width*.9); ScreenLight->SetSourceHeight(Height*.9);
    ScreenLight->SetCastShadows(false); ScreenLight->SetIndirectLightingIntensity(0); ScreenLight->SetVolumetricScatteringIntensity(0);
    ScreenLight->SetAffectGlobalIllumination(false); ScreenLight->SetAffectReflection(false); ScreenLight->SetAffectTranslucentLighting(false);
    ScreenLight->ViewLightingChannels.bViewChannel0=true; ScreenLight->ViewLightingChannels.bViewChannel1=true; ScreenLight->ViewLightingChannels.bViewChannel2=true;
    ScreenLight->LightingChannels.bChannel0=true; ScreenLight->LightingChannels.bChannel1=true; ScreenLight->LightingChannels.bChannel2=false;
    // Stable positive-intensity proxy; power changes color, never light membership.
    ScreenLight->SetLightColor(FLinearColor::Black); AddInstanceComponent(ScreenLight); ScreenLight->RegisterComponent();
    bInitialized=true; SetActorTickEnabled(false); UpdateFromCell(true);
    UE_LOG(LogTemp,Display,TEXT("DF_CABINET_READY parent=%s lcd=1 buttons=%d new_meshes=0 lights=1 new_thermal_profiles=0"),*ParentName,Buttons.Num());
    return true;
}
void ADarkFactoryControlCabinet::SetEnabled(bool Enabled) {
    if (!bInitialized) return;
    bEnabled=Enabled; Glass->SetMaterial(0,Enabled ? DisplayMaterial.Get():OriginalMaterial.Get());
    SetActorTickEnabled(Enabled); UpdateFromCell(true);
}
void ADarkFactoryControlCabinet::SetPanelPower(bool Powered) { bPowered=Powered; UpdateFromCell(true); }
void ADarkFactoryControlCabinet::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); UpdateFromCell(); }
void ADarkFactoryControlCabinet::UpdateFromCell(bool Force) {
    if (!bInitialized || !SourceCell.IsValid() || !Glass.IsValid()) return;
    const double Seconds=SourceCell->GetMotionTime(); const bool Paused=!SourceCell->IsAnimating();
    const int64 Sample=FMath::FloorToInt64(Seconds*20.); const FTransform Transform=Glass->GetComponentTransform();
    if (!Force && Sample==LastSample && Paused==bPaused && Transform.Equals(LastTransform,1.e-6)) return;
    LastSample=Sample; LastTransform=Transform; AppliedSeconds=Seconds; bPaused=Paused;
    CycleSeconds=SourceCell->GetCycleSeconds(); CycleProgress=CycleSeconds/SourceCell->GetCycleDurationSeconds();
    const bool On=bEnabled && bPowered;
    DisplayMaterial->SetScalarParameterValue(TEXT("PanelPower"),On ? 1.f:0.f);
    DisplayMaterial->SetScalarParameterValue(TEXT("CycleSeconds"),CycleSeconds); DisplayMaterial->SetScalarParameterValue(TEXT("CycleProgress"),CycleProgress);
    DisplayMaterial->SetScalarParameterValue(TEXT("Paused"),bPaused ? 1.f:0.f);
    auto Vector=[&](const TCHAR* Key,const FVector& V) { DisplayMaterial->SetVectorParameterValue(Key,FLinearColor(V.X,V.Y,V.Z,0)); };
    Vector(TEXT("ScreenCenter"),Transform.TransformPosition(Center)); Vector(TEXT("ScreenNormal"),Transform.TransformVectorNoScale(Normal));
    Vector(TEXT("ScreenRight"),Transform.TransformVectorNoScale(Right)); Vector(TEXT("ScreenUp"),Transform.TransformVectorNoScale(Up));
    int32 Active=0;
    for (int32 I=0;I<4;++I) {
        const auto T=SourceCell->GetRobotTiming(I,Seconds); const double C=T.CycleSeconds;
        int32 Phase=0; double P=0;
        if (C<T.ApproachStartSeconds) { Phase=0; }
        else if (C<T.ApproachEndSeconds) { Phase=1; P=(C-T.ApproachStartSeconds)/(T.ApproachEndSeconds-T.ApproachStartSeconds); }
        else if (C<T.WeldStartSeconds) { Phase=2; P=1; }
        else if (C<T.WeldEndSeconds) { Phase=3; P=(C-T.WeldStartSeconds)/(T.WeldEndSeconds-T.WeldStartSeconds); }
        else if (C<T.WithdrawEndSeconds) { Phase=4; P=(C-T.WeldEndSeconds)/(T.WithdrawEndSeconds-T.WeldEndSeconds); }
        else { Phase=5; P=1; }
        Phases[I]=Phase; Progress[I]=FMath::Clamp(P,0.,1.); Active+=Phase==3;
        DisplayMaterial->SetScalarParameterValue(*FString::Printf(TEXT("RobotPhase%d"),I),Phase);
        DisplayMaterial->SetScalarParameterValue(*FString::Printf(TEXT("RobotProgress%d"),I),Progress[I]);
    }
    DisplayMaterial->SetScalarParameterValue(TEXT("ActiveRobotCount"),Active);
    for (int32 I=0;I<Buttons.Num();++I) {
        auto& B=Buttons[I]; const bool Green=B.Color==TEXT("green");
        // Existing red/green caps remain backlit. Extra emphasis follows machine
        // phase; it is illumination, not a model quality or fault declaration.
        B.Level=I==0 ? 1.f:Green ? .48f+(Phases[(I-1)%4]==3 ? .52f:0.f):.30f+(bPaused ? .30f:0.f);
        Vector(*FString::Printf(TEXT("ButtonCenter%d"),I),Transform.TransformPosition(B.Center));
        DisplayMaterial->SetScalarParameterValue(*FString::Printf(TEXT("ButtonRadius%d"),I),B.Radius);
        DisplayMaterial->SetScalarParameterValue(*FString::Printf(TEXT("ButtonLevel%d"),I),B.Level);
    }
    ScreenLight->SetLightColor(On ? FLinearColor(.12f,.55f,1.f):FLinearColor::Black);
}
void ADarkFactoryControlCabinet::GetInspectionView(FVector& Location,FRotator& Rotation) const {
    const FTransform T=Glass.IsValid() ? Glass->GetComponentTransform():FTransform::Identity;
    const FVector Target=T.TransformPosition(Center-Up*22.);
    Location=Target+T.TransformVectorNoScale(Normal)*215.; Rotation=(Target-Location).Rotation();
}
TSharedRef<FJsonObject> ADarkFactoryControlCabinet::BuildMetadata() const {
    auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("enabled"),bEnabled); R->SetBoolField(TEXT("powered"),bPowered);
    R->SetBoolField(TEXT("initialized"),bInitialized); R->SetBoolField(TEXT("paused"),bPaused);
    R->SetNumberField(TEXT("clock_seconds"),AppliedSeconds); R->SetNumberField(TEXT("cycle_seconds"),CycleSeconds); R->SetNumberField(TEXT("cycle_progress"),CycleProgress);
    R->SetStringField(TEXT("parent_mesh"),ParentName); R->SetNumberField(TEXT("mesh_count_added"),0); R->SetNumberField(TEXT("light_count"),1); R->SetNumberField(TEXT("thermal_profiles_added"),0);
    R->SetStringField(TEXT("scope"),TEXT("Authored cabinet backlight and simulation-clock HMI; no inspection result or measured electronics telemetry; thermal unchanged"));
    const FTransform T=Glass.IsValid() ? Glass->GetComponentTransform():FTransform::Identity;
    auto S=MakeShared<FJsonObject>(); S->SetArrayField(TEXT("center_world_cm"),Vec(T.TransformPosition(Center)));
    S->SetArrayField(TEXT("normal_world"),Vec(T.TransformVectorNoScale(Normal))); S->SetArrayField(TEXT("right_world"),Vec(T.TransformVectorNoScale(Right))); S->SetArrayField(TEXT("up_world"),Vec(T.TransformVectorNoScale(Up)));
    S->SetNumberField(TEXT("width_cm"),Width); S->SetNumberField(TEXT("height_cm"),Height); R->SetObjectField(TEXT("screen"),S);
    TArray<TSharedPtr<FJsonValue>> RobotRows,ButtonRows;
    for (int32 I=0;I<4;++I) { auto O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("index"),I); O->SetStringField(TEXT("phase"),PhaseNames[Phases[I]]); O->SetNumberField(TEXT("phase_code"),Phases[I]); O->SetNumberField(TEXT("progress"),Progress[I]); RobotRows.Add(MakeShared<FJsonValueObject>(O)); }
    for (int32 I=0;I<Buttons.Num();++I) { const auto& B=Buttons[I]; auto O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("index"),I); O->SetStringField(TEXT("id"),B.Id); O->SetStringField(TEXT("color"),B.Color); O->SetNumberField(TEXT("level"),bEnabled && bPowered ? B.Level:0); O->SetNumberField(TEXT("radius_cm"),B.Radius); O->SetArrayField(TEXT("center_world_cm"),Vec(T.TransformPosition(B.Center))); ButtonRows.Add(MakeShared<FJsonValueObject>(O)); }
    R->SetArrayField(TEXT("robots"),RobotRows); R->SetArrayField(TEXT("buttons"),ButtonRows); return R;
}
