#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryThermalRig.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

void ADarkFactoryWeldingCell::InitializeEquipmentHeat() {
    EquipmentHeatGroups.Reset(); EquipmentHeatClock=-1;
    if (!bProcessControlled) return; // Preserve legacy thermal fixtures.
    FString Text; TSharedPtr<FJsonObject> Root;
    const FString Path=FPaths::Combine(FPaths::ProjectDir(),TEXT("../assets/derived/welding_cell/equipment_heat.json"));
    if (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root) {
        UE_LOG(LogTemp,Warning,TEXT("DF_EQUIPMENT_HEAT missing descriptor; run prepare_equipment_heat.py")); return;
    }
    double Version=0; const TArray<TSharedPtr<FJsonValue>>* Groups=nullptr;
    if (!Root->TryGetNumberField(TEXT("version"),Version) || Version!=1 || !Root->TryGetArrayField(TEXT("groups"),Groups) || Groups->Num()!=5) return;
    TArray<FEquipmentHeatGroup> Loaded;
    for (const auto& Value:*Groups) {
        const auto O=Value->AsObject(); FString Name; FEquipmentHeatGroup Group;
        const TArray<TSharedPtr<FJsonValue>>* Segments=nullptr;
        if (!O || !O->TryGetStringField(TEXT("mesh"),Name) || !O->TryGetStringField(TEXT("role"),Group.Role)
            || !O->TryGetArrayField(TEXT("segments"),Segments) || Segments->IsEmpty() || Segments->Num()>16) return;
        if (Group.Role!=TEXT("carriage_motor") && Group.Role!=TEXT("cabinet") && Group.Role!=TEXT("screen")) return;
        for (const auto& Surface:ThermalSurfaces) if (Surface.Mesh && Surface.Mesh->GetName()==Name) Group.Mesh=Surface.Mesh;
        if (!Group.Mesh.IsValid()) return;
        for (const auto& Item:*Segments) {
            const auto S=Item->AsObject(); FEquipmentHeatSegment Segment; double Radius;
            auto Read=[&](const TCHAR* Key,FVector& P) {
                const TArray<TSharedPtr<FJsonValue>>* V=nullptr; double X,Y,Z;
                if (!S || !S->TryGetArrayField(Key,V) || V->Num()!=3 || !(*V)[0]->TryGetNumber(X) || !(*V)[1]->TryGetNumber(Y) || !(*V)[2]->TryGetNumber(Z)) return false;
                P=FVector(X,Y,Z); return !P.ContainsNaN();
            };
            if (!Read(TEXT("start_cm"),Segment.Start) || !Read(TEXT("end_cm"),Segment.End)
                || !S->TryGetNumberField(TEXT("radius_cm"),Radius) || !FMath::IsFinite(Radius) || Radius<=0 || Radius>100) return;
            Segment.Radius=Radius; Group.Segments.Add(Segment);
        }
        Loaded.Add(MoveTemp(Group));
    }
    EquipmentHeatGroups=MoveTemp(Loaded);
    UE_LOG(LogTemp,Display,TEXT("DF_EQUIPMENT_HEAT_READY groups=%d added_profiles=0"),EquipmentHeatGroups.Num());
}

void ADarkFactoryWeldingCell::UpdateEquipmentHeat() {
    auto* Thermal=ToolThermalRig.Get();
    if (!bProcessControlled || !Thermal || EquipmentHeatGroups.IsEmpty()) return;
    const bool Reset=EquipmentHeatClock<0 || ProcessClock<EquipmentHeatClock;
    if (Reset) { CarriageMotorC=26; CabinetSurfaceC=34; CarriageDriveActivity=0; }
    const double Delta=Reset ? 0.:FMath::Max(0.,ProcessClock-EquipmentHeatClock);
    if (Delta>0) {
        // Actual carriage travel drives heat; a new body's offscreen teleport
        // and equal-clock QA pose changes cannot be interpreted as motor power.
        const double Travel=FMath::Abs(ProcessCarriageOffset-EquipmentPreviousOffset);
        CarriageDriveActivity=bProcessVisible && bEquipmentPreviousVisible && Travel<2000.
            ? FMath::Clamp(Travel/(Delta*500.),0.,1.):0.;
        const double Target=20.+42.*CarriageDriveActivity;
        const double Tau=CarriageDriveActivity>0 ? 2.8:24.;
        CarriageMotorC=Target+(CarriageMotorC-Target)*FMath::Exp(-Delta/Tau);
        // Powered controller has a standing load even while the conveyor rests.
        const double CabinetTarget=38.+6.*CarriageDriveActivity;
        CabinetSurfaceC=CabinetTarget+(CabinetSurfaceC-CabinetTarget)*FMath::Exp(-Delta/18.);
    }
    EquipmentHeatClock=ProcessClock; EquipmentPreviousOffset=ProcessCarriageOffset;
    bEquipmentPreviousVisible=bProcessVisible;
    for (const auto& Group:EquipmentHeatGroups) if (auto* Mesh=Group.Mesh.Get()) {
        const float Temperature=Group.Role==TEXT("carriage_motor") ? CarriageMotorC
            :Group.Role==TEXT("screen") ? 30.f+(CabinetSurfaceC-30.f)*.45f:CabinetSurfaceC;
        TArray<FDarkFactoryContactHeatSegment> Fields;
        for (const auto& Segment:Group.Segments) {
            FDarkFactoryContactHeatSegment F; F.LocalStartCm=Segment.Start; F.LocalEndCm=Segment.End;
            F.RadiusCm=Segment.Radius; F.SurfaceC=Temperature; Fields.Add(F);
        }
        Thermal->SetImportedContactHeat(Mesh,Fields);
    }
}

TSharedRef<FJsonObject> ADarkFactoryWeldingCell::BuildEquipmentHeatMetadata() const {
    auto R=MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("enabled"),bProcessControlled && !EquipmentHeatGroups.IsEmpty());
    R->SetStringField(TEXT("model"),TEXT("localized-equipment-surface-v1"));
    R->SetStringField(TEXT("scope"),TEXT("Illustrative exterior motor/cabinet heat; GPU capsule fields, no imported bulk solver, no visible/NIR glow"));
    R->SetNumberField(TEXT("clock_seconds"),EquipmentHeatClock);
    R->SetNumberField(TEXT("motor_core_c"),CarriageMotorC);
    R->SetNumberField(TEXT("cabinet_core_c"),CabinetSurfaceC);
    R->SetNumberField(TEXT("drive_activity"),CarriageDriveActivity);
    R->SetNumberField(TEXT("groups"),EquipmentHeatGroups.Num());
    R->SetNumberField(TEXT("additional_thermal_profiles"),0);
    TArray<TSharedPtr<FJsonValue>> Groups;
    for (const auto& Group:EquipmentHeatGroups) if (auto* Mesh=Group.Mesh.Get()) {
        auto G=MakeShared<FJsonObject>(); G->SetStringField(TEXT("mesh"),Mesh->GetName());
        G->SetBoolField(TEXT("visible"),Mesh->IsVisible()); G->SetBoolField(TEXT("custom_depth"),Mesh->bRenderCustomDepth);
        G->SetNumberField(TEXT("stencil"),Mesh->CustomDepthStencilValue);
        TArray<TSharedPtr<FJsonValue>> Positions;
        for (const auto& S:Group.Segments) {
            auto V=Mesh->GetComponentTransform().TransformPosition((S.Start+S.End)*.5);
            auto Point=MakeShared<FJsonObject>(); Point->SetNumberField(TEXT("x"),V.X); Point->SetNumberField(TEXT("y"),V.Y); Point->SetNumberField(TEXT("z"),V.Z);
            Positions.Add(MakeShared<FJsonValueObject>(Point));
        }
        G->SetArrayField(TEXT("world_centers"),Positions); Groups.Add(MakeShared<FJsonValueObject>(G));
    }
    R->SetArrayField(TEXT("surface_fields"),Groups);
    return R;
}
