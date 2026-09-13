#include "DarkFactoryIQ9Props.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
constexpr float HalfSize=5.3f,HalfHeight=2.15f,FaceGap=.025f,LedStrength=100.f,LedLumens=.45f;
const FVector LedPositions[]={FVector(3.25,.35,2.36),FVector(2.60,.35,2.36),FVector(1.35,4.65,2.36)};
const FLinearColor LedColors[]={FLinearColor(1,.24,.005),FLinearColor(.01,1,.05),FLinearColor(.01,1,.05)};
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& P) {
    return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
}
bool ReadVec(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FVector& V) {
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    if (!O || !O->TryGetArrayField(Key,A) || A->Num()!=3) return false;
    double X,Y,Z;
    if (!(*A)[0]->TryGetNumber(X) || !(*A)[1]->TryGetNumber(Y) || !(*A)[2]->TryGetNumber(Z)) return false;
    V=FVector(X,Y,Z); return !V.ContainsNaN();
}
bool AmberOn(double Seconds,int32 Index) {
    const double Clock=FMath::Max(0.,Seconds)+Index*.173;
    const int64 Slot=static_cast<int64>(FMath::FloorToDouble(Clock/1.6));
    uint32 Hash=static_cast<uint32>(Slot)^((Index+1)*0x9e3779b9u);
    Hash^=Hash>>16; Hash*=0x7feb352du; Hash^=Hash>>15;
    const double Phase=FMath::Fmod(Clock,1.6);
    const double Start=.10+.24*(Hash&255)/255.;
    const double Duration=.14+.20*((Hash>>8)&255)/255.;
    return (Phase>=Start && Phase<Start+Duration) || (Phase>=.86 && Phase<.93);
}
}

ADarkFactoryIQ9Props::ADarkFactoryIQ9Props() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("IQ9PropsRoot")); SetRootComponent(SceneRoot);
}
UStaticMeshComponent* ADarkFactoryIQ9Props::AddMesh(const FString& Name,USceneComponent* Parent,UStaticMesh* Mesh,
    UMaterialInterface* Material,const FVector& Position,const FRotator& Rotation,const FVector& Scale,bool Shadow) {
    auto* Part=NewObject<UStaticMeshComponent>(this,*Name);
    Part->SetupAttachment(Parent); Part->SetMobility(EComponentMobility::Movable);
    Part->SetStaticMesh(Mesh); Part->SetMaterial(0,Material);
    Part->SetRelativeLocationAndRotation(Position,Rotation); Part->SetRelativeScale3D(Scale);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(Shadow);
    Part->SetLightingChannels(true,true,false); Part->SetReceivesDecals(false);
    Part->SetAffectDynamicIndirectLighting(false); Part->SetAffectDistanceFieldLighting(false);
    Part->SetEmissiveLightSource(false);
    AddInstanceComponent(Part); Part->RegisterComponent(); Meshes.Add(Part);
    return Part;
}
bool ADarkFactoryIQ9Props::Initialize(ADarkFactoryWeldingCell* Cell,const FString& ManifestPath) {
    if (bInitialized || !IsValid(Cell)) return false;
    SourceManifest=ManifestPath.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),TEXT("../assets/iq9_evk/mounts.json"))):ManifestPath;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text,*SourceManifest) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if (!Root || !Root->TryGetArrayField(TEXT("mounts"),Rows) || Rows->Num()!=4) return false;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Body=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/IQ9EVK/M_IQ9_Body.M_IQ9_Body"));
    auto* Plate=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/IQ9EVK/M_IQ9_Plate.M_IQ9_Plate"));
    auto* LED=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/IQ9EVK/M_IQ9_LED.M_IQ9_LED"));
    const TCHAR* FaceNames[]={TEXT("Top"),TEXT("Front"),TEXT("Back"),TEXT("Right"),TEXT("Left")};
    UMaterialInterface* Faces[5]={};
    for (int32 F=0;F<5;++F) Faces[F]=LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/IQ9EVK/M_IQ9_%s.M_IQ9_%s"),FaceNames[F],FaceNames[F]));
    if (!Cube || !Plane || !Sphere || !Body || !Plate || !LED) {
        UE_LOG(LogTemp,Error,TEXT("DF_IQ9_MISSING_CONTENT run prepare_iq9_evk.py and setup_iq9_evk.py")); return false;
    }
    for (auto* Face:Faces) if (!Face) return false;
    TSet<FString> Seen;
    struct FValidatedMount { FString Id,ParentName; UStaticMeshComponent* Parent=nullptr; FVector Contact,Normal,Right; };
    TArray<FValidatedMount> Validated;
    for (const auto& Value:*Rows) {
        const auto Row=Value->AsObject(); FValidatedMount M;
        if (!Row || !Row->TryGetStringField(TEXT("id"),M.Id) || !Row->TryGetStringField(TEXT("parent_mesh"),M.ParentName)
            || !ReadVec(Row,TEXT("contact_local_cm"),M.Contact) || !ReadVec(Row,TEXT("normal_local"),M.Normal)
            || !ReadVec(Row,TEXT("right_local"),M.Right) || Seen.Contains(M.ParentName)
            || M.Normal.SizeSquared()<.99 || M.Normal.SizeSquared()>1.01 || M.Right.SizeSquared()<.99
            || M.Right.SizeSquared()>1.01 || FMath::Abs(FVector::DotProduct(M.Normal,M.Right))>.01) return false;
        for (const auto& Surface:Cell->GetThermalSurfaces()) if (Surface.Mesh && Surface.Mesh->GetStaticMesh()
            && Surface.Mesh->GetStaticMesh()->GetName()==M.ParentName) { M.Parent=Surface.Mesh; break; }
        if (!M.Parent || !M.ParentName.EndsWith(TEXT("_upper_arm"))) return false;
        Seen.Add(M.ParentName); Validated.Add(M);
    }
    SourceCell=Cell; AddTickPrerequisiteActor(Cell);
    for (int32 Index=0;Index<Validated.Num();++Index) {
        const auto& M=Validated[Index];
        auto* Anchor=NewObject<USceneComponent>(this,*FString::Printf(TEXT("IQ9Mount_%s"),*M.Id));
        Anchor->SetupAttachment(M.Parent); Anchor->SetMobility(EComponentMobility::Movable);
        // Root is the enclosure centre. Bracket back is0.30cm from sampled surface.
        Anchor->SetRelativeLocationAndRotation(M.Contact+M.Normal*(.30+.35+HalfHeight),FRotationMatrix::MakeFromZX(M.Normal,M.Right).ToQuat());
        AddInstanceComponent(Anchor); Anchor->RegisterComponent(); Anchors.Add(Anchor);
        FMount Mount; Mount.Id=M.Id; Mount.ParentName=M.ParentName; Mount.Parent=M.Parent;
        Mount.Anchor=Anchor; Mount.Contact=M.Contact; Mount.Normal=M.Normal; Mount.FirstLed=LedMeshes.Num();
        auto AddSurface=[&](UStaticMeshComponent* Mesh,float Temperature) {
            FDarkFactoryImportedThermalSurface S; S.Mesh=Mesh; S.TemperatureC=Temperature; S.Emissivity=.92f; S.CoreEdgeDropC=0;
            ThermalSurfaces.Add(S);
        };
        const FString Prefix=TEXT("IQ9_")+M.Id+TEXT("_");
        AddSurface(AddMesh(Prefix+TEXT("Enclosure"),Anchor,Cube,Body,FVector::ZeroVector,FRotator::ZeroRotator,FVector(.106,.106,.043),true),30);
        AddSurface(AddMesh(Prefix+TEXT("MountingPlate"),Anchor,Cube,Plate,FVector(0,0,-HalfHeight-.175),FRotator::ZeroRotator,FVector(.13,.13,.0035),true),24);
        const FVector P[]={FVector(0,0,HalfHeight+FaceGap),FVector(HalfSize+FaceGap,0,0),FVector(-HalfSize-FaceGap,0,0),FVector(0,HalfSize+FaceGap,0),FVector(0,-HalfSize-FaceGap,0)};
        const FRotator R[]={FRotator::ZeroRotator,FRotator(90,0,0),FRotator(-90,0,0),FRotator(0,0,-90),FRotator(0,0,90)};
        const FVector S[]={FVector(.106,.106,1),FVector(.043,.106,1),FVector(.043,.106,1),FVector(.106,.043,1),FVector(.106,.043,1)};
        for (int32 F=0;F<5;++F) AddSurface(AddMesh(Prefix+FaceNames[F],Anchor,Plane,Faces[F],P[F],R[F],S[F],false),30);
        for (int32 L=0;L<3;++L) {
            auto* MID=UMaterialInstanceDynamic::Create(LED,this); MID->SetVectorParameterValue(TEXT("LedColor"),LedColors[L]);
            MID->SetScalarParameterValue(TEXT("LedStrength"),0);
            LedMaterials.Add(MID);
            LedMeshes.Add(AddMesh(Prefix+FString::Printf(TEXT("LED%d"),L),Anchor,Sphere,MID,LedPositions[L],FRotator::ZeroRotator,FVector(.004),false));
            auto* Light=NewObject<UPointLightComponent>(this,*FString::Printf(TEXT("%sLedLight%d"),*Prefix,L));
            Light->SetupAttachment(Anchor); Light->SetRelativeLocation(LedPositions[L]+FVector(0,0,.25));
            Light->SetMobility(EComponentMobility::Movable); Light->SetLightColor(FLinearColor::Black);
            // Keep a positive-intensity registered proxy; blinking updates color only.
            Light->SetIntensityUnits(ELightUnits::Lumens); Light->SetIntensity(LedLumens); Light->SetAttenuationRadius(12);
            Light->SetSourceRadius(.15f); Light->SetCastShadows(false);
            Light->SetIndirectLightingIntensity(0); Light->SetVolumetricScatteringIntensity(0);
            Light->SetAffectGlobalIllumination(false); Light->SetAffectReflection(false);
            Light->ViewLightingChannels.bViewChannel0=true; Light->ViewLightingChannels.bViewChannel1=true; Light->ViewLightingChannels.bViewChannel2=true;
            Light->LightingChannels.bChannel0=true; Light->LightingChannels.bChannel1=true; Light->LightingChannels.bChannel2=false;
            Light->bAffectTranslucentLighting=false; Light->SetVisibility(bEnabled);
            AddInstanceComponent(Light); Light->RegisterComponent(); LedLights.Add(Light); LedStates.Add(false);
        }
        Mounts.Add(Mount);
    }
    bInitialized=true; UpdateLeds();
    UE_LOG(LogTemp,Display,TEXT("DF_IQ9_PROPS_READY mounts=%d thermal_profiles=%d"),Mounts.Num(),ThermalSurfaces.Num());
    return true;
}
void ADarkFactoryIQ9Props::SetEnabled(bool Enabled) {
    if (bEnabled==Enabled) return;
    bEnabled=Enabled; SetActorHiddenInGame(!Enabled);
    // Whole-prop enable/disable may change registration. Ordinary LED pulses
    // never change visibility/intensity or reorder the deferred-light proxies.
    for (const auto& Light:LedLights) if (Light) Light->SetVisibility(Enabled);
    UpdateLeds();
}
void ADarkFactoryIQ9Props::SetLedTimeForQA(double Seconds) {
    if (!FMath::IsFinite(Seconds)) return;
    LedTimeOverride=Seconds>=0 ? Seconds:-1; UpdateLeds();
}
void ADarkFactoryIQ9Props::UpdateLeds() {
    if (!bInitialized) return;
    AppliedLedSeconds=LedTimeOverride>=0 ? LedTimeOverride:(SourceCell.IsValid() ? SourceCell->GetMotionTime():0);
    for (int32 M=0;M<Mounts.Num();++M) for (int32 L=0;L<3;++L) {
        const int32 I=Mounts[M].FirstLed+L;
        const bool On=bEnabled && (L>0 || AmberOn(AppliedLedSeconds,M));
        if (LedStates[I]==On) continue;
        LedStates[I]=On;
        LedMaterials[I]->SetScalarParameterValue(TEXT("LedStrength"),On ? LedStrength:0);
        LedLights[I]->SetLightColor(On ? LedColors[L]:FLinearColor::Black);
    }
}
void ADarkFactoryIQ9Props::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); UpdateLeds(); }
TSharedRef<FJsonObject> ADarkFactoryIQ9Props::BuildMetadata() const {
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("model"),TEXT("iq9-evk-attached-props-v1"));
    Result->SetStringField(TEXT("scope"),TEXT("Prior local photographed five-face prop; authored LED animation and fixed illustrative30C enclosure/24C bracket; no device telemetry, model result, or measured NIR spectra"));
    Result->SetBoolField(TEXT("initialized"),bInitialized); Result->SetBoolField(TEXT("enabled"),bEnabled);
    Result->SetNumberField(TEXT("mount_count"),Mounts.Num()); Result->SetNumberField(TEXT("thermal_profile_count"),ThermalSurfaces.Num());
    Result->SetNumberField(TEXT("led_count"),LedMeshes.Num()); Result->SetNumberField(TEXT("led_light_count"),LedLights.Num());
    Result->SetNumberField(TEXT("led_time_seconds"),AppliedLedSeconds); Result->SetBoolField(TEXT("led_time_pinned"),LedTimeOverride>=0);
    Result->SetArrayField(TEXT("enclosure_size_cm"),Vec(FVector(10.6,10.6,4.3)));
    Result->SetStringField(TEXT("appearance_source"),TEXT("assets/iq9_evk/Impostor; SHA256 provenance in private mounts.json"));
    bool Finite=true,Attached=true; TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& M:Mounts) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("id"),M.Id); Row->SetStringField(TEXT("parent_mesh"),M.ParentName);
        const auto* Anchor=M.Anchor.Get(); const auto* Parent=M.Parent.Get();
        const bool Valid=Anchor && Parent;
        const FTransform T=Valid ? Anchor->GetComponentTransform():FTransform::Identity;
        const FVector N=T.TransformVectorNoScale(FVector::UpVector),X=T.TransformVectorNoScale(FVector::ForwardVector),Y=T.TransformVectorNoScale(FVector::RightVector);
        Finite &= Valid && !T.ContainsNaN(); Attached &= Valid && Anchor->GetAttachParent()==Parent;
        Row->SetBoolField(TEXT("attached_to_arm"),Valid && Anchor->GetAttachParent()==Parent);
        Row->SetArrayField(TEXT("world_position_cm"),Vec(T.GetLocation())); Row->SetArrayField(TEXT("world_normal"),Vec(N));
        Row->SetArrayField(TEXT("world_right"),Vec(X)); Row->SetArrayField(TEXT("world_up"),Vec(Y));
        Row->SetArrayField(TEXT("contact_local_cm"),Vec(M.Contact));
        Row->SetArrayField(TEXT("relative_position_cm"),Vec(Valid ? Anchor->GetRelativeLocation():FVector::ZeroVector));
        const FRotator R=Valid ? Anchor->GetRelativeRotation():FRotator::ZeroRotator;
        Row->SetArrayField(TEXT("relative_rotation_deg"),Vec(FVector(R.Pitch,R.Yaw,R.Roll)));
        TArray<TSharedPtr<FJsonValue>> Leds;
        for (int32 L=0;L<3;++L) {
            const int32 I=M.FirstLed+L; auto Led=MakeShared<FJsonObject>();
            Led->SetNumberField(TEXT("index"),L); Led->SetBoolField(TEXT("enabled"),LedStates[I]);
            Led->SetStringField(TEXT("kind"),L==0 ? TEXT("decorative amber activity"):TEXT("steady green power/link"));
            Led->SetArrayField(TEXT("world_position_cm"),Vec(LedMeshes[I]->GetComponentLocation()));
            Led->SetArrayField(TEXT("color"),Vec(FVector(LedColors[L].R,LedColors[L].G,LedColors[L].B)));
            Led->SetNumberField(TEXT("emissive_strength"),LedStates[I] ? LedStrength:0);
            Led->SetNumberField(TEXT("local_light_lumens"),LedStates[I] ? LedLumens:0);
            Leds.Add(MakeShared<FJsonValueObject>(Led));
        }
        Row->SetArrayField(TEXT("leds"),Leds); Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Result->SetBoolField(TEXT("finite_state"),Finite); Result->SetBoolField(TEXT("attached_to_arm"),Attached);
    Result->SetArrayField(TEXT("mounts"),Rows);
    Result->SetStringField(TEXT("channels"),TEXT("visible0, fixed activeNIR1 and observer activeNIR2; local12cm LEDs use stable positive-intensity proxies with black/color blinking; no GI/reflection/indirect/volumetric/translucent light; thermal uses assigned opaque profiles"));
    return Result;
}
