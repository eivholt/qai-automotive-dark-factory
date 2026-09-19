#include "DarkFactoryProcessScene.h"
#include "DarkFactoryEnhancements.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryThermalRadiometry.h"
#include "DarkFactorySealantMesh.h"
#include "DarkFactoryPanelFrameMesh.h"
#include "DarkFactoryContactHeat.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace {
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& P) {
    return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};
}
bool Inspecting(EDarkFactoryProcessPhase Phase) {
    using P=EDarkFactoryProcessPhase;
    return Phase==P::PanelPresence || Phase==P::Adhesive || Phase==P::PanelSeated || Phase==P::WeldCooling;
}
void Visible(UStaticMeshComponent* Mesh,bool Show) {
    if (!Mesh) return;
    Mesh->SetVisibility(Show); Mesh->SetHiddenInGame(!Show);
    Mesh->SetCollisionEnabled(Show ? ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
}
}
ADarkFactoryProcessScene::ADarkFactoryProcessScene() {
    PrimaryActorTick.bCanEverTick=false;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("PanelProcessRoot")); SetRootComponent(SceneRoot);
}
bool ADarkFactoryProcessScene::Initialize(ADarkFactoryWeldingCell* InCell,int32 Seed) {
    if (bInitialized || !InCell || !InCell->GetCarriageRoot() || !InCell->GetInspectionJoint()) return false;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
    if (!Cube || !Cylinder || !Base) return false;
    Cell=InCell; RandomSeed=Seed; Cell->SetProcessControlled(true);
    if (!Cell->InitializePanelAssembly()) return false;
    PanelTemplate=Cell->GetPanelBodyMesh();
    if (!PanelTemplate || !PanelTemplate->GetStaticMesh()) return false;
    PanelTarget=PanelTemplate->GetRelativeTransform();
    PanelTarget.AddToTranslation(FVector(0,0,ADarkFactoryWeldingCell::PanelSeatingLiftCm));
    auto Material=[&](FLinearColor Color,float Roughness) {
        auto* MID=UMaterialInstanceDynamic::Create(Base,this);
        MID->SetVectorParameterValue(TEXT("BaseColor"),Color); MID->SetScalarParameterValue(TEXT("Roughness"),Roughness); return MID;
    };
    auto Mesh=[&](const FString& Name,UStaticMesh* Asset,UMaterialInterface* Mat,USceneComponent* Parent) {
        auto* Part=NewObject<UStaticMeshComponent>(this,*Name); AddInstanceComponent(Part); Part->SetupAttachment(Parent);
        Part->SetMobility(EComponentMobility::Movable); Part->SetStaticMesh(Asset); if (Mat) Part->SetMaterial(0,Mat);
        Part->SetLightingChannels(true,true,false); Part->SetCollisionObjectType(ECC_WorldDynamic);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore); Part->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Part->SetGenerateOverlapEvents(false); Part->RegisterComponent(); return Part;
    };
    auto Surface=[&](UStaticMeshComponent* Part,float Temp,float Emissivity) {
        FDarkFactoryImportedThermalSurface S; S.Mesh=Part; S.TemperatureC=Temp; S.Emissivity=Emissivity; ThermalSurfaces.Add(S);
    };
    auto Clone=[&](const TCHAR* Name) {
        auto* Part=Mesh(Name,PanelTemplate->GetStaticMesh(),nullptr,Cell->GetCarriageRoot()); Part->SetRelativeTransform(PanelTarget);
        for (int32 I=0;I<PanelTemplate->GetNumMaterials();++I) {
            auto* Source=PanelTemplate->GetMaterial(I);
            auto* SourceMID=Cast<UMaterialInstanceDynamic>(Source);
            // A dynamic instance cannot parent another MID. Retain the imported
            // material parent and copy its per-component heat/witness overrides.
            auto* MID=UMaterialInstanceDynamic::Create(SourceMID ? SourceMID->Parent.Get():Source,this);
            if (SourceMID) MID->CopyParameterOverrides(SourceMID);
            Part->SetMaterial(I,MID); PanelMaterials.Add(MID);
        }
        Surface(Part,23.f,.74f); return Part;
    };
    IncomingPanel=Clone(TEXT("PreassembledTrunkPanel")); HeldPanel=Clone(TEXT("PickedTrunkPanel"));
    auto* SealantBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PanelSealant.M_PanelSealant"));
    if (!SealantBase) return false;
    // Deposit onto the source contact ledges. No added backing bands or webs.
    for (int32 I=0;I<2;++I) {
        TArray<FVector> BeadPath,BeadNormals;
        if (!Cell->GetPanelAdhesivePath(I,BeadPath,BeadNormals)) return false;
        int32 FrontIndex=0; double FrontDistance=1.e9;
        const FVector FrontTarget(I==0 ? 55.:-55.,-235.,270.);
        for (int32 K=0;K<BeadPath.Num();++K) if (FVector::DistSquared(BeadPath[K],FrontTarget)<FrontDistance) {
            FrontDistance=FVector::DistSquared(BeadPath[K],FrontTarget); FrontIndex=K;
        }
        const FVector Bead=BeadPath[FrontIndex];
        const FVector Normal=BeadNormals[FrontIndex];
        auto* SealantMaterial=UMaterialInstanceDynamic::Create(SealantBase,this); AdhesiveMaterials.Add(SealantMaterial);
        FVector Origin; double Length=0;
        auto* BeadMesh=MakeDarkFactorySealantMesh(this,SealantMaterial,BeadPath,BeadNormals,Origin,Length);
        if (!BeadMesh) return false;
        auto* Strip=Mesh(FString::Printf(TEXT("PanelSealant%d"),I),BeadMesh,SealantMaterial,Cell->GetCarriageRoot());
        Strip->SetRelativeLocation(Origin); AdhesiveMeshes.Add(Strip); Surface(Strip,20.f,1.f);
        AdhesiveLocal.Add(Bead+FVector(0,0,ADarkFactoryWeldingCell::PanelSeatingLiftCm)); AdhesiveLengths.Add(Length); AdhesiveProgress.Add(0.);
        AdhesiveInitialC.Add(90); AdhesiveTau.Add(12.f*AdhesiveCoolingTimeScale); AdhesiveC.Add(20); ContactSurfaceC.Add(23); AdhesiveDeposited.Add(-1.);
    }
    for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
        FVector Position,Normal; if (!Cell->GetPanelContact(I,Position,Normal)) return false;
        SpotLocal.Add(Position); SpotNormals.Add(Normal);
        WeldDeposited.Add(-1.); WeldCoreC.Add(20); WeldHaloC.Add(20);
    }
    CameraMount=NewObject<USceneComponent>(this,TEXT("InspectionCameraMount")); AddInstanceComponent(CameraMount);
    CameraMount->SetupAttachment(Cell->GetInspectionJoint()); CameraMount->SetMobility(EComponentMobility::Movable);
    CameraMount->SetRelativeTransform(Cell->GetInspectionCameraMountTransform()); CameraMount->RegisterComponent();
    auto* CameraBodyAsset=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/InspectionCamera/Meshes/SM_InspectionCameraHousing.SM_InspectionCameraHousing"));
    auto* CameraLensAsset=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/InspectionCamera/Meshes/SM_InspectionCameraLens.SM_InspectionCameraLens"));
    if (CameraBodyAsset && CameraLensAsset) {
        // Imported vertices already use the rigid mount's centimetre frame.
        // The visible lens rim is at X=20.5; the real optical origin stays X=21.
        CameraHousing=Mesh(TEXT("InspectionCameraHousing"),CameraBodyAsset,nullptr,CameraMount);
        CameraLens=Mesh(TEXT("InspectionCameraLens"),CameraLensAsset,nullptr,CameraMount);
        UE_LOG(LogTemp,Display,TEXT("DF_INSPECTION_CAMERA_PROP imported-camera-rigid-lens-v1"));
    } else {
        // A code-only checkout can still run without the user's private asset.
        CameraHousing=Mesh(TEXT("InspectionCameraHousing"),Cube,Material(FLinearColor(.028,.033,.039),.5f),CameraMount);
        CameraHousing->SetRelativeScale3D(FVector(.23,.19,.16));
        CameraLens=Mesh(TEXT("InspectionCameraLens"),Cylinder,Material(FLinearColor(.08,.13,.16),.2f),CameraMount);
        CameraLens->SetRelativeLocation(FVector(15,0,0)); CameraLens->SetRelativeRotation(FRotator(90,0,0));
        CameraLens->SetRelativeScale3D(FVector(.105,.105,.11));
        UE_LOG(LogTemp,Warning,TEXT("DF_INSPECTION_CAMERA_PROP primitive-fallback; run import_inspection_camera.py"));
    }
    Surface(CameraHousing,28,.93f); Surface(CameraLens,27,.86f);
    // Rigid wrist-to-housing support. Keep the calibrated mount and optical
    // origin untouched; this only fills the visible gap behind the camera.
    const FVector Wrist=CameraMount->GetRelativeTransform().InverseTransformPosition(FVector::ZeroVector);
    const FVector HousingAnchor(0,0,0),Axis=(HousingAnchor-Wrist).GetSafeNormal();
    auto* SupportMaterial=Material(FLinearColor(.16,.19,.22),.36f);
    auto Support=[&](const TCHAR* Name,FVector Center,double Diameter,double Length) {
        auto* Part=Mesh(Name,Cylinder,SupportMaterial,CameraMount);
        Part->SetRelativeLocation(Center);
        Part->SetRelativeRotation(FRotationMatrix::MakeFromZ(Axis).Rotator());
        Part->SetRelativeScale3D(FVector(Diameter/100.,Diameter/100.,Length/100.));
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); CameraSupports.Add(Part);
    };
    Support(TEXT("InspectionCameraSupportRod"),(Wrist+HousingAnchor)*.5,4.,FVector::Distance(Wrist,HousingAnchor)+2.);
    Support(TEXT("InspectionCameraWristCollar"),Wrist,8.,3.);
    Support(TEXT("InspectionCameraHousingCollar"),HousingAnchor-Axis*7.,7.,2.);
    // Offline loss-of-evidence examples use an actual opaque optical cover,
    // excluded from live simulation, rather than painting labels onto images.
    CaptureOccluder=Mesh(TEXT("OfflineInspectionLensCover"),Cube,Material(FLinearColor(.015,.018,.02),.8f),CameraMount);
    CaptureOccluder->SetRelativeLocation(FVector(38,0,0)); CaptureOccluder->SetRelativeScale3D(FVector(.01,1.2,1.2));
    Surface(CaptureOccluder,24,.95f); Visible(CaptureOccluder,false);
    // Keep the original source flanges on every body, before the full panel is
    // fitted. The centre is cut away offline; no primitive backing strips.
    FVector FrameOrigin;
    auto* FrameMesh=MakeDarkFactoryPanelFrame(this,Cell->GetPanelFramePath(),PanelTemplate->GetMaterial(0),FrameOrigin);
    if (!FrameMesh) {
        UE_LOG(LogTemp,Error,TEXT("DF_PANEL_FRAME_MISSING run scripts/assets/prepare_panel_frame.py")); return false;
    }
    MountingFrame=Mesh(TEXT("PermanentPanelMountingFrame"),FrameMesh,PanelTemplate->GetMaterial(0),Cell->GetCarriageRoot());
    MountingFrame->SetRelativeLocation(FrameOrigin);
    // Append so panel/bead/camera profile indices remain stable. The fixed LUT
    // already has room; no extra pass or texture allocation is needed.
    Surface(MountingFrame,23.f,.74f);
    bRecoveryExperiment=FParse::Param(FCommandLine::Get(),TEXT("DFFaultRecovery")) && !FParse::Param(FCommandLine::Get(),TEXT("DFProcessCapture"));
    if (bRecoveryExperiment) {
        auto* Shape=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        auto* RecoveryBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Factory.M_Factory"));
        RecoveryIndicator=UMaterialInstanceDynamic::Create(RecoveryBase,this);
        RecoveryIndicator->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.03,.08,.11));
        auto* Housing=Mesh(TEXT("RecoveryCoolingBlower"),Shape,RecoveryIndicator,SceneRoot);
        Housing->SetRelativeLocation(FVector(180,-285,115)); Housing->SetRelativeScale3D(FVector(.50,.45,.55));
        auto* Stand=Mesh(TEXT("RecoveryCoolingStand"),Shape,RecoveryIndicator,SceneRoot);
        Stand->SetRelativeLocation(FVector(180,-285,46.9)); Stand->SetRelativeScale3D(FVector(.12,.16,.811));
        RecoveryRotor=Mesh(TEXT("RecoveryCoolingRotor"),Shape,RecoveryIndicator,SceneRoot);
        RecoveryRotor->SetRelativeLocation(FVector(153,-285,115)); RecoveryRotor->SetRelativeScale3D(FVector(.035,.40,.055));
    }
    bInitialized=true; UpdatePanelVisibility();
    UE_LOG(LogTemp,Display,TEXT("DF_PANEL_PROCESS_SCENE_READY panel=SM_BodyPanel_0386 stock=5 adhesive=2 thermal_additions=%d"),ThermalSurfaces.Num());
    return true;
}
void ADarkFactoryProcessScene::BeginBody(int32 BodyIndex,double ClockSeconds,int32 PreassembledOverride) {
    if (!bInitialized) return;
    BodySequence=FMath::Max(0,BodyIndex); BodyBirthSeconds=Clock=LastClock=ClockSeconds;
    RecoveryStarted=-1.; RecoveryCoolingAge=0.;
    bBodyActive=true; bBodyVisible=true; bPanelPicked=bPanelWelded=bOfflinePrepared=bOccluded=bMisaligned=false;
    bHolderReleased=false;
    CaptureExpectedDecision.Empty(); CaptureVisibilityRays.Reset(); InspectionExtension=0; LastStageSeconds=0; CurrentPhase=EDarkFactoryProcessPhase::Arriving;
    const uint32 BodySeed=static_cast<uint32>(RandomSeed)^(static_cast<uint32>(BodySequence)*196613u+0x28f6c419u);
    FRandomStream Random(static_cast<int32>(BodySeed)); bPreassembled=Random.RandRange(0,4)==0;
    if (PreassembledOverride==0 || PreassembledOverride==1) bPreassembled=PreassembledOverride==1;
    ContactStarted=bPreassembled ? ClockSeconds:-1.;
    SupplyRemaining=5;
    Cell->SetPanelSupplyCount(SupplyRemaining);
    IncomingPanel->AttachToComponent(Cell->GetCarriageRoot(),FAttachmentTransformRules::KeepRelativeTransform);
    IncomingPanel->SetRelativeTransform(PanelTarget);
    HeldPanel->AttachToComponent(Cell->GetCarriageRoot(),FAttachmentTransformRules::KeepRelativeTransform);
    HeldPanel->SetRelativeTransform(PanelTarget);
    for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
        if (AdhesiveC.IsValidIndex(I)) {
            AdhesiveInitialC[I]=Random.FRandRange(76.f,108.f); AdhesiveTau[I]=Random.FRandRange(7.f,22.f)*AdhesiveCoolingTimeScale;
            // Explicit experiment: every third body has slow passive cooling.
            // This source state is never read by the controller or model payload.
            if (bRecoveryExperiment && BodySequence%3==1) AdhesiveTau[I]*=8.f;
            AdhesiveDeposited[I]=bPreassembled ? ClockSeconds:-1.; AdhesiveProgress[I]=bPreassembled ? 1.:0.;
            AdhesiveC[I]=bPreassembled ? AdhesiveInitialC[I]:20;
        }
        WeldDeposited[I]=-1.; WeldCoreC[I]=WeldHaloC[I]=20;
        // A previously assembled carrier can retain residual process warmth.
        if (bPreassembled) WeldDeposited[I]=ClockSeconds-Random.FRandRange(5.f,11.f);
    }
    Cell->BeginProcessBody(BodySequence,ClockSeconds); UpdateTemperatureFields(); UpdatePanelVisibility();
}
void ADarkFactoryProcessScene::EndBody() {
    bBodyActive=bBodyVisible=false; bOccluded=false; UpdatePanelVisibility();
}
void ADarkFactoryProcessScene::BeginAgentJob(EDarkFactoryProcessPhase Phase) {
    // Physical effects belong to jobs, never to inspection verdicts. Repeated
    // applications and weld passes genuinely restart their deposition clocks.
    if (Phase==EDarkFactoryProcessPhase::AdhesiveApplication) {
        for (int32 I=0;I<AdhesiveDeposited.Num();++I) { AdhesiveDeposited[I]=-1.; AdhesiveProgress[I]=0.; }
        ContactStarted=-1.;
    }
    if (Phase==EDarkFactoryProcessPhase::Welding) for (auto& Time:WeldDeposited) Time=-1.;
}
void ADarkFactoryProcessScene::CompleteAgentJob(EDarkFactoryProcessPhase Phase) {
    if (Phase==EDarkFactoryProcessPhase::PanelPlacement) ContactStarted=Clock;
    if (Phase==EDarkFactoryProcessPhase::Welding && bPanelPicked && !bPanelWelded) {
        HeldPanel->AttachToComponent(Cell->GetCarriageRoot(),FAttachmentTransformRules::KeepWorldTransform);
        bPanelWelded=true;
    }
    if (Phase==EDarkFactoryProcessPhase::HolderRetract) bHolderReleased=true;
}
bool ADarkFactoryProcessScene::StartRecoveryCooling() {
    if (!bRecoveryExperiment || RecoveryStarted>=0 || CurrentPhase!=EDarkFactoryProcessPhase::Adhesive) return false;
    RecoveryStarted=Clock; return true;
}
void ADarkFactoryProcessScene::StopRecoveryCooling() { RecoveryStarted=-1.; }
void ADarkFactoryProcessScene::SetCaptureVariation(int32 Seed) {
    bCaptureVariation=true; CaptureVariationSeed=Seed;
    if (Cell) Cell->SetProcessMotorCaptureVariation(Seed);
}
double ADarkFactoryProcessScene::GetPhaseDuration(EDarkFactoryProcessPhase Phase) {
    using P=EDarkFactoryProcessPhase;
    switch (Phase) {
    case P::Arriving: case P::Departing: return CarriageTravelSeconds;
    case P::AdhesiveApplication: return ADarkFactoryWeldingCell::PanelAdhesiveSeconds;
    case P::PanelPlacement: return ADarkFactoryWeldingCell::PanelPlacementSeconds;
    case P::Welding: return ADarkFactoryWeldingCell::PanelWeldingSeconds;
    case P::HolderRetract: return ADarkFactoryWeldingCell::PanelRetractSeconds;
    default: return 0.;
    }
}
void ADarkFactoryProcessScene::ApplyAssemblyFrame(EDarkFactoryProcessPhase Phase,double StageSeconds,double ClockSeconds,double Offset,bool Show) {
    if (!bInitialized || !FMath::IsFinite(ClockSeconds)) return;
    using P=EDarkFactoryProcessPhase;
    const double Dt=FMath::Max(0.,ClockSeconds-LastClock);
    if (RecoveryStarted>=0) RecoveryCoolingAge+=Dt;
    if (RecoveryRotor) RecoveryRotor->SetRelativeRotation(FRotator(0,0,RecoveryCoolingAge*720.));
    if (RecoveryIndicator) RecoveryIndicator->SetVectorParameterValue(TEXT("BaseColor"),RecoveryStarted>=0 ? FLinearColor(.04,.5,.65):FLinearColor(.03,.08,.11));
    Clock=LastClock=ClockSeconds; LastStageSeconds=StageSeconds; CurrentPhase=Phase; bBodyVisible=Show;
    const double Target=Inspecting(Phase) ? 1.:0.;
    const double Step=Dt/(Target>InspectionExtension ? InspectionDeploySeconds:InspectionWithdrawSeconds);
    InspectionExtension=Target>InspectionExtension ? FMath::Min(Target,InspectionExtension+Step):FMath::Max(Target,InspectionExtension-Step);
    if (FMath::Abs(InspectionExtension-Target)<1.e-9) InspectionExtension=Target;
    Cell->SetPanelMotionState(Phase,StageSeconds,bPanelPicked && !bHolderReleased);
    Cell->SetProcessFrame(Phase==P::Welding ? StageSeconds:0.,Clock,Offset,Show,Phase==P::Welding,InspectionExtension);
    if (!bOfflinePrepared) {
        if (Phase==P::AdhesiveApplication) for (int32 I=0;I<AdhesiveMeshes.Num();++I) {
            AdhesiveProgress[I]=Cell->GetPanelAdhesiveProgress(I,StageSeconds);
            // Start this bead's cooling clock when extrusion begins. The same
            // arc-length parameter reveals the GPU mesh beneath the moving tip.
            if (AdhesiveProgress[I]>0. && AdhesiveDeposited[I]<0)
                AdhesiveDeposited[I]=Clock-StageSeconds+Cell->GetPanelAdhesiveStartSeconds(I);
        }
        if (Phase==P::PanelPlacement && StageSeconds>=ADarkFactoryWeldingCell::PanelPickupSeconds && !bPanelPicked) {
            bPanelPicked=true; --SupplyRemaining; Cell->SetPanelSupplyCount(FMath::Max(0,SupplyRemaining));
        }
        // Recreate the picked stock sheet once the finished carrier moves away.
        // Keep the rack depleted throughout assembly and all inspection holds.
        if (Phase==P::Departing && Offset>0. && SupplyRemaining<5) {
            SupplyRemaining=5; Cell->SetPanelSupplyCount(SupplyRemaining);
        }
        if (bPanelPicked && !bPanelWelded) HeldPanel->SetWorldTransform(Cell->GetPanelHeldTransform());
        if (Phase==P::Welding) for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
            const double Contact=ADarkFactoryWeldingCell::PanelWeldStartSeconds(I);
            if (StageSeconds>=Contact && WeldDeposited[I]<0) WeldDeposited[I]=Clock-StageSeconds+Contact;
        }
        if (!bAgentControl && bPanelPicked && !bPanelWelded && Phase==P::PanelSeated) {
            // Preserve the physically reached placement; welding fixes it to the
            // carriage before the gripper eventually releases after final G.
            HeldPanel->AttachToComponent(Cell->GetCarriageRoot(),FAttachmentTransformRules::KeepWorldTransform);
            bPanelWelded=true;
        }
        if (ContactStarted<0 && bPanelPicked && Phase!=P::PanelPlacement) ContactStarted=Clock-StageSeconds;
        UpdateTemperatureFields();
    }
    UpdatePanelVisibility();
}
void ADarkFactoryProcessScene::UpdatePanelVisibility() {
    Visible(PanelTemplate,false);
    Visible(MountingFrame,bBodyActive && bBodyVisible);
    Visible(IncomingPanel,bBodyActive && bBodyVisible && bPreassembled);
    Visible(HeldPanel,bBodyActive && bBodyVisible && bPanelPicked);
    for (int32 I=0;I<AdhesiveMeshes.Num();++I) Visible(AdhesiveMeshes[I],bBodyActive && bBodyVisible && AdhesiveDeposited[I]>=0);
    Visible(CaptureOccluder,bOfflinePrepared && bOccluded);
}
void ADarkFactoryProcessScene::UpdateTemperatureFields() {
    if (!bInitialized) return;
    for (int32 I=0;I<AdhesiveMeshes.Num();++I) {
        if (!bOfflinePrepared)
            AdhesiveC[I]=AdhesiveDeposited[I]<0 ? 20.f:20.f+(AdhesiveInitialC[I]-20.f)*FMath::Exp(-FMath::Max(0.,Clock-AdhesiveDeposited[I])/AdhesiveTau[I]);
        if (!bOfflinePrepared) {
            const double SourceAtContact=AdhesiveDeposited[I]<0 ? 20.:20.+(AdhesiveInitialC[I]-20.)*FMath::Exp(-FMath::Max(0.,ContactStarted-AdhesiveDeposited[I])/AdhesiveTau[I]);
            ContactSurfaceC[I]=ContactStarted<0 ? 23.f:FMath::Max(23.,DarkFactoryContactHeat::SurfaceC(SourceAtContact,AdhesiveTau[I],Clock-ContactStarted));
            if (RecoveryCoolingAge>0) {
                const double Cooling=FMath::Exp(-RecoveryCoolingAge/6.);
                AdhesiveC[I]=20.f+(AdhesiveC[I]-20.f)*Cooling;
                ContactSurfaceC[I]=23.f+(ContactSurfaceC[I]-23.f)*Cooling;
            }
        }
        ThermalSurfaces[2+I].TemperatureC=AdhesiveC[I];
        AdhesiveMaterials[I]->SetScalarParameterValue(TEXT("DepositProgress"),AdhesiveProgress[I]);
        if (auto* Rig=ThermalRig.Get()) Rig->SetImportedSurfaceTemperature(AdhesiveMeshes[I],AdhesiveC[I]);
    }
    for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
        if (!bOfflinePrepared) {
            const double Age=WeldDeposited[I]<0 ? 1.e9:FMath::Max(0.,Clock-WeldDeposited[I]-.5);
            WeldCoreC[I]=20.f+1580.f*FMath::Exp(-Age/ADarkFactoryWeldingCell::ProcessWeldCoreCoolingTauSeconds);
            WeldHaloC[I]=20.f+400.f*FMath::Exp(-Age/WeldHaloCoolingSeconds);
        }
    }
    // Six halo/core pairs share a bounded GPU surface-field table. Every
    // weld retains its own cooling clock and radiometry, including old spots.
    for (UStaticMeshComponent* Panel:{IncomingPanel.Get(),HeldPanel.Get()}) {
        TArray<FDarkFactoryContactHeatSegment> Segments;
        if (ContactStarted>=0) for (int32 Run=0;Run<2;++Run) {
            TArray<FVector> Path,Normals;
            if (!Cell->GetPanelAdhesivePath(Run,Path,Normals)) continue;
            for (int32 K=0;K+5<Path.Num();K+=5) {
                FDarkFactoryContactHeatSegment S;
                S.LocalStartCm=PanelTarget.InverseTransformPosition(Path[K]+FVector(0,0,ADarkFactoryWeldingCell::PanelSeatingLiftCm));
                S.LocalEndCm=PanelTarget.InverseTransformPosition(Path[K+5]+FVector(0,0,ADarkFactoryWeldingCell::PanelSeatingLiftCm));
                S.SurfaceC=ContactSurfaceC[Run]; Segments.Add(S);
            }
        }
        if (auto* Rig=ThermalRig.Get()) Rig->SetImportedContactHeat(Panel,Segments);
        TArray<FDarkFactoryWeldHeatPatch> Patches;
        for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
            const FVector Local=PanelTarget.InverseTransformPosition(SpotLocal[I]);
            FDarkFactoryWeldHeatPatch Patch; Patch.LocalPositionCm=Local; Patch.RadiusCm=12; Patch.SurfaceC=WeldHaloC[I]; Patches.Add(Patch);
        }
        for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
            FDarkFactoryWeldHeatPatch Core=Patches[I]; Core.RadiusCm=2; Core.SurfaceC=WeldCoreC[I]; Patches.Add(Core);
        }
        if (auto* Rig=ThermalRig.Get()) Rig->SetImportedWeldHeat(Panel,Patches);
        for (int32 M=0;M<Panel->GetNumMaterials();++M) if (auto* MID=Cast<UMaterialInstanceDynamic>(Panel->GetMaterial(M))) {
            MID->SetScalarParameterValue(TEXT("WeldWitnessEnabled"),1.f);
            for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
                const FVector Point=Panel->GetComponentTransform().TransformPosition(Patches[I].LocalPositionCm);
                const FVector Normal=Panel->GetComponentTransform().TransformVectorNoScale(PanelTarget.InverseTransformVectorNoScale(SpotNormals[I])).GetSafeNormal();
                MID->SetVectorParameterValue(FName(*FString::Printf(TEXT("WitnessCenter%d"),I)),FLinearColor(Point.X,Point.Y,Point.Z,1));
                MID->SetVectorParameterValue(FName(*FString::Printf(TEXT("WitnessNormal%d"),I)),FLinearColor(Normal.X,Normal.Y,Normal.Z,0));
                MID->SetScalarParameterValue(FName(*FString::Printf(TEXT("WitnessStrength%d"),I)),WeldDeposited[I]>=0 ? 1.f:0.f);
                MID->SetVectorParameterValue(FName(*FString::Printf(TEXT("WeldHotspotWorld%d"),I)),FLinearColor(Point.X,Point.Y,Point.Z,1));
                MID->SetScalarParameterValue(FName(*FString::Printf(TEXT("WeldHeatRadiusCm%d"),I)),12.f);
                const float Glow=FMath::Square(FMath::Clamp((WeldHaloC[I]-70.f)/350.f,0.f,1.f))*180.f;
                MID->SetScalarParameterValue(FName(*FString::Printf(TEXT("WeldHeatGlow%d"),I)),Glow);
                MID->SetVectorParameterValue(FName(*FString::Printf(TEXT("WeldGlowColor%d"),I)),FLinearColor(1,.08,.008,1));
            }
        }
    }
}
void ADarkFactoryProcessScene::BindThermalRig(ADarkFactoryThermalRig* Rig) {
    ThermalRig=Rig;
    // Reuse the uniform camera-housing profile: no new sensor LUT IDs.
    for (const auto& Part:CameraSupports) if (Part && CameraHousing) {
        Part->SetRenderCustomDepth(true); Part->SetCustomDepthStencilValue(CameraHousing->CustomDepthStencilValue);
    }
    UpdateTemperatureFields();
}
void ADarkFactoryProcessScene::RequestInspectionView(const FString& Stage,int32 View,double ClockSeconds,bool Snap) {
    InspectionStage=Stage; Cell->RequestInspectionSpot(View,ClockSeconds,Snap,Stage==TEXT("panel_presence") || Stage==TEXT("panel_seated"),Stage==TEXT("adhesive") || Stage==TEXT("sealant_coverage"));
    if (Snap) {
        InspectionExtension=1.; Cell->SetPanelMotionState(CurrentPhase,LastStageSeconds,bPanelPicked);
        Cell->SetProcessFrame(0.,Clock,0.,true,false,1.); UpdatePanelVisibility();
    }
}
bool ADarkFactoryProcessScene::IsInspectionViewSettled(int32 View) const {
    return InspectionExtension>=1. && Cell && Cell->IsInspectionSpotSettled(View);
}
void ADarkFactoryProcessScene::GetInspectionFramingCorners(const FString& Stage,int32 View,TArray<FVector>& Corners) const {
    Corners.Reset(); if (!bInitialized || View<0 || View>=3) return;
    const bool NIR=Stage==TEXT("panel_presence") || Stage==TEXT("panel_seated");
    const FTransform Carriage=Cell->GetCarriageRoot()->GetComponentTransform();
    const FBox PanelBounds=PanelTemplate->GetStaticMesh()->GetBoundingBox();
    const int32 BeadIndex=AdhesiveBeadForView(View);
    const auto* Strip=AdhesiveMeshes[BeadIndex].Get(); const FBox StripBounds=Strip->GetStaticMesh()->GetBoundingBox();
    for (int32 I=0;I<8;++I) {
        const FVector Sign((I&1) ? 1.:-1.,(I&2) ? 1.:-1.,(I&4) ? 1.:-1.);
        Corners.Add(NIR ? Carriage.TransformPosition(PanelTarget.TransformPosition(PanelBounds.GetCenter()+Sign*(PanelBounds.GetExtent()+FVector(10,10,3))))
            :Carriage.TransformPosition(Stage==TEXT("adhesive") ? AdhesiveLocal[BeadIndex]:SpotLocal[2*View])+Sign*14.);
        Corners.Add(Stage==TEXT("weld") ? Carriage.TransformPosition(SpotLocal[2*View+1])+Sign*14.
            :Strip->GetComponentTransform().TransformPosition(StripBounds.GetCenter()+Sign*StripBounds.GetExtent()));
    }
}
void ADarkFactoryProcessScene::GetInspectionCameraPose(const FString& Stage,int32 View,FVector& Location,FRotator& Rotation,float& FOV) const {
    if (!bInitialized || !CameraMount) { Location=FVector::ZeroVector; Rotation=FRotator::ZeroRotator; FOV=50; return; }
    Location=CameraMount->GetComponentTransform().TransformPosition(FVector(21,0,0)); Rotation=CameraMount->GetComponentRotation();
    TArray<FVector> Corners; GetInspectionFramingCorners(Stage,View,Corners);
    constexpr double Aspect=448./256.; double HalfTan=.04;
    for (const FVector& Corner:Corners) {
        const FVector V=Rotation.UnrotateVector(Corner-Location); const double Depth=FMath::Max(1.,V.X);
        HalfTan=FMath::Max(HalfTan,FMath::Max(FMath::Abs(V.Y)/Depth,Aspect*FMath::Abs(V.Z)/Depth));
    }
    FOV=FMath::Clamp(FMath::RadiansToDegrees(2.*FMath::Atan(HalfTan*1.25)),8.,110.);
    if (bOfflinePrepared && (Stage==TEXT("adhesive") || Stage==TEXT("weld")) && Cell->IsInspectionSpotSettled(View)) {
        const auto Carriage=Cell->GetCarriageRoot()->GetComponentTransform();
        const FVector Target=Carriage.TransformPosition(Stage==TEXT("adhesive") ? AdhesiveLocal[AdhesiveBeadForView(View)]:SpotLocal[2*View]);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PanelInspectionVisibility),true);
        Query.AddIgnoredComponent(CameraHousing.Get()); Query.AddIgnoredComponent(CameraLens.Get());
        FHitResult Hit; const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,Location,Target+(Target-Location).GetSafeNormal()*3.,ECC_Visibility,Query);
        auto Audit=MakeShared<FJsonObject>(); Audit->SetStringField(TEXT("stage"),Stage); Audit->SetNumberField(TEXT("view"),View);
        Audit->SetArrayField(TEXT("eye_cm"),Vec(Location)); Audit->SetArrayField(TEXT("target_cm"),Vec(Target));
        Audit->SetBoolField(TEXT("hit"),Found); Audit->SetStringField(TEXT("hit_mesh"),Hit.GetComponent() ? Hit.GetComponent()->GetName():TEXT("none"));
        Audit->SetArrayField(TEXT("hit_cm"),Vec(Hit.ImpactPoint)); Audit->SetNumberField(TEXT("target_error_cm"),Found ? FVector::Distance(Hit.ImpactPoint,Target):-1.);
        if (CaptureVisibilityRays.Num()!=3) CaptureVisibilityRays.SetNum(3);
        CaptureVisibilityRays[View]=MakeShared<FJsonValueObject>(Audit);
    }
}
void ADarkFactoryProcessScene::GetSealantSurfaceSamples(int32 View,TArray<FVector>& Points) const {
    Points.Reset(); if (!bInitialized || View<0 || View>2) return;
    TArray<FVector> Path,Normals;
    if (!Cell->GetPanelAdhesivePath(AdhesiveBeadForView(View),Path,Normals)) return;
    const FTransform Carriage=Cell->GetCarriageRoot()->GetComponentTransform();
    for (int32 I=1;I<Path.Num()-1;++I) Points.Add(Carriage.TransformPosition(Path[I]+FVector(0,0,ADarkFactoryWeldingCell::PanelSeatingLiftCm)));
}
void ADarkFactoryProcessScene::GetInspectionObserverView(FVector& Location,FRotator& Rotation) const {
    const FVector Target=CameraMount ? CameraMount->GetComponentLocation():FVector(0,0,300);
    Location=Target+FVector(-150,-120,90); Rotation=(Target-Location).Rotation();
}
bool ADarkFactoryProcessScene::PrepareCapture(const FString& Stage,int32 CaseIndex,int32 Seed,double& ClockOut) {
    if (!bInitialized || CaseIndex<0) return false;
    using P=EDarkFactoryProcessPhase;
    const bool Presence=Stage==TEXT("panel_presence"),Seated=Stage==TEXT("panel_seated"),Sealant=Stage==TEXT("adhesive"),Weld=Stage==TEXT("weld");
    if ((!Presence && !Seated && !Sealant && !Weld) || CaseIndex>(Seated ? 3:2)) return false;
    FRandomStream Random(Seed); Clock=LastClock=ClockOut=100.; bOfflinePrepared=true; bBodyActive=bBodyVisible=true;
    bPanelPicked=false; bPanelWelded=Seated || Weld; bPreassembled=!(Presence && CaseIndex==0) && !(Seated && CaseIndex==3);
    // Capture both held-panel and incoming-preassembled thermal geometries.
    if (Seated || ((Sealant || Weld) && CaptureVariationSeed%2==0)) {
        bPanelPicked=bPreassembled; bPreassembled=false;
        HeldPanel->AttachToComponent(Cell->GetCarriageRoot(),FAttachmentTransformRules::KeepRelativeTransform);
        HeldPanel->SetRelativeTransform(PanelTarget);
    }
    bPanelWelded=(Seated || Weld) && (bPreassembled || bPanelPicked);
    bMisaligned=Seated && CaseIndex==0; bOccluded=(Presence || Seated) && CaseIndex==2;
    CurrentPhase=Presence ? P::PanelPresence:Seated ? P::PanelSeated:Sealant ? P::Adhesive:P::WeldCooling;
    LastStageSeconds=0; InspectionExtension=1; IncomingPanel->SetRelativeTransform(PanelTarget);
    if (bMisaligned) {
        auto* Panel=bPanelPicked ? HeldPanel.Get():IncomingPanel.Get();
        Panel->AddRelativeLocation(FVector(Random.FRandRange(28.f,43.f),Random.FRandRange(15.f,29.f),Random.FRandRange(13.f,23.f)));
        Panel->AddRelativeRotation(FRotator(Random.FRandRange(6.f,11.f),0,0));
    }
    CaptureExpectedDecision=(Presence || Seated) ? (bOccluded ? TEXT("A"):CaseIndex==1 ? TEXT("G"):TEXT("R"))
        :CaseIndex==0 ? TEXT("R"):CaseIndex==1 ? TEXT("A"):TEXT("G");
    ContactStarted=(bPreassembled || bPanelPicked) ? Clock-10.:-1.;
    const bool NearAmbientCapture=(CaptureVariationSeed%3)==0;
    const int32 LimitingContact=Random.RandRange(0,Sealant ? 1:ADarkFactoryWeldingCell::PanelWeldCount-1); const bool AllContactsWarm=Random.RandRange(0,1)==0;
    for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
        const int32 ThermalCase=(CaseIndex<2 && (AllContactsWarm || I==LimitingContact)) ? CaseIndex:2;
        if (AdhesiveC.IsValidIndex(I)) {
            AdhesiveDeposited[I]=(Presence && CaseIndex==0) ? -1.:90.; AdhesiveProgress[I]=AdhesiveDeposited[I]>=0 ? 1.:0.;
            AdhesiveC[I]=Random.FRandRange(23.f,32.f);
            if (Sealant) AdhesiveC[I]=ThermalCase==0 ? Random.FRandRange(66.f,102.f):ThermalCase==1 ? Random.FRandRange(43.5f,46.5f):Random.FRandRange(NearAmbientCapture ? 22.4f:25.f,NearAmbientCapture ? 24.8f:39.f);
            double Low=23.,High=250.;
            for (int32 J=0;J<32;++J) { const double Mid=(Low+High)*.5; if (DarkFactoryThermal::ApparentTemperatureC(Mid,.74,20.)<AdhesiveC[I]) Low=Mid; else High=Mid; }
            ContactSurfaceC[I]=(Low+High)*.5;
        }
        WeldDeposited[I]=(Seated || Weld) ? 95.:-1.;
        const float ResidualWeldC=Random.FRandRange(23.f,32.f);
        // A held, unwelded panel has no residual weld field in the live process.
        // Preserve the random stream, but do not invent warm contact dots there.
        WeldCoreC[I]=WeldHaloC[I]=(!Sealant && (bPanelWelded || bPreassembled)) ? ResidualWeldC:20.f;
        if (Weld) {
            const float Apparent=ThermalCase==0 ? Random.FRandRange(73.f,113.f):ThermalCase==1 ? Random.FRandRange(53.5f,56.5f):Random.FRandRange(NearAmbientCapture ? 20.1f:27.f,NearAmbientCapture ? 26.f:48.f);
            // Invert the same apparent-temperature radiometry used by rendering.
            double Low=20,High=250;
            for (int32 J=0;J<32;++J) { const double Mid=(Low+High)*.5; if (DarkFactoryThermal::ApparentTemperatureC(Mid,.74,20.)<Apparent) Low=Mid; else High=Mid; }
            WeldHaloC[I]=WeldCoreC[I]=(Low+High)*.5;
        }
    }
    RequestInspectionView(Stage,0,Clock,true); UpdateTemperatureFields(); UpdatePanelVisibility(); return true;
}
float ADarkFactoryProcessScene::GetMaximumAdhesiveC() const { float Max=20; for (float T:AdhesiveC) Max=FMath::Max(Max,T); return Max; }
bool ADarkFactoryProcessScene::PrepareSealantCoverageCapture(int32 CaseIndex,int32 Seed,double& ClockOut) {
    // Offline geometry experiment only. Never add these labels to live requests.
    if (!FParse::Param(FCommandLine::Get(),TEXT("DFSealantCoverageCapture")) ||
        !FParse::Param(FCommandLine::Get(),TEXT("DFProcessCapture")) || CaseIndex<0 || CaseIndex>7 ||
        !PrepareCapture(TEXT("panel_presence"),0,Seed,ClockOut)) return false;
    FRandomStream Random(Seed);
    const double GapStart=Random.FRandRange(.25,.65);
    for (int32 I=0;I<2;++I) {
        TArray<FVector> Points,Normals; if (!Cell->GetPanelAdhesivePath(I,Points,Normals)) return false;
        const bool Gap=CaseIndex==I+1;
        const double Width=CaseIndex==3 && I==Seed%2 ? .3:1.;
        const double Offset=CaseIndex==4 && I==Seed%2 ? 5.:0.;
        FVector Origin; double Length=0;
        auto* Mesh=MakeDarkFactorySealantMesh(this,AdhesiveMaterials[I],Points,Normals,Origin,Length,Width,Gap ? GapStart:-1.,Gap ? GapStart+.14:-1.,Offset);
        if (!Mesh) return false;
        AdhesiveMeshes[I]->SetStaticMesh(Mesh); AdhesiveMeshes[I]->SetRelativeLocation(Origin);
        AdhesiveDeposited[I]=(CaseIndex==5+I) ? -1.:90.; AdhesiveProgress[I]=1.;
    }
    bOccluded=CaseIndex==7;
    CaptureExpectedDecision=CaseIndex==0 ? TEXT("G"):CaseIndex==7 ? TEXT("A"):TEXT("R");
    RequestInspectionView(TEXT("sealant_coverage"),0,Clock,true); UpdateTemperatureFields(); UpdatePanelVisibility(); return true;
}
float ADarkFactoryProcessScene::GetMaximumSealantContactApparentC() const {
    float Max=23; for (float T:ContactSurfaceC) Max=FMath::Max(Max,T);
    return DarkFactoryThermal::ApparentTemperatureC(Max,.74,20.);
}
float ADarkFactoryProcessScene::GetMaximumWeldC() const {
    float Max=20; for (float T:WeldCoreC) Max=FMath::Max(Max,T); for (float T:WeldHaloC) Max=FMath::Max(Max,T); return Max;
}
TSharedRef<FJsonObject> ADarkFactoryProcessScene::BuildMetadata() const {
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("schema"),TEXT("darkfactory-panel-process-scene-v3"));
    O->SetStringField(TEXT("scope"),TEXT("Private physical labels and authored temperatures; never model input"));
    O->SetBoolField(TEXT("cooling_recovery_experiment"),bRecoveryExperiment);
    O->SetBoolField(TEXT("cooling_recovery_active"),RecoveryStarted>=0.);
    O->SetNumberField(TEXT("cooling_recovery_age_seconds"),RecoveryCoolingAge);
    O->SetNumberField(TEXT("body_index"),BodySequence); O->SetNumberField(TEXT("clock_seconds"),Clock);
    O->SetNumberField(TEXT("body_age_seconds"),FMath::Max(0.,Clock-BodyBirthSeconds)); O->SetNumberField(TEXT("seed"),RandomSeed);
    O->SetBoolField(TEXT("preassembled"),bPreassembled); O->SetNumberField(TEXT("preassembled_probability"),.2);
    O->SetBoolField(TEXT("panel_present"),bPreassembled || bPanelPicked); O->SetBoolField(TEXT("panel_picked"),bPanelPicked);
    O->SetBoolField(TEXT("panel_welded"),bPanelWelded || bPreassembled); O->SetBoolField(TEXT("body_active"),bBodyActive);
    O->SetNumberField(TEXT("supply_remaining"),SupplyRemaining); O->SetBoolField(TEXT("offline_capture_variation"),bCaptureVariation);
    if (bCaptureVariation) O->SetNumberField(TEXT("capture_variation_seed"),CaptureVariationSeed);
    if (bOfflinePrepared) { O->SetStringField(TEXT("capture_expected_decision"),CaptureExpectedDecision); O->SetBoolField(TEXT("occluded"),bOccluded); O->SetBoolField(TEXT("misaligned"),bMisaligned); }
    O->SetNumberField(TEXT("thermal_profiles_added"),ThermalSurfaces.Num()); O->SetNumberField(TEXT("inspection_robot_index"),3);
    O->SetStringField(TEXT("mounting_frame_recipe"),TEXT("source-panel-open-frame-v1"));
    O->SetBoolField(TEXT("mounting_frame_visible"),MountingFrame && MountingFrame->IsVisible());
    O->SetNumberField(TEXT("inspection_extension"),InspectionExtension);
    O->SetObjectField(TEXT("inspection_scan"),Cell->GetInspectionScanMetadata()); O->SetObjectField(TEXT("panel_assembly"),Cell->GetPanelAssemblyMetadata());
    O->SetNumberField(TEXT("adhesive_cooling_time_scale"),AdhesiveCoolingTimeScale);
    O->SetNumberField(TEXT("inspection_deploy_seconds"),InspectionDeploySeconds); O->SetNumberField(TEXT("inspection_withdraw_seconds"),InspectionWithdrawSeconds);
    O->SetNumberField(TEXT("maximum_adhesive_c"),GetMaximumAdhesiveC()); O->SetNumberField(TEXT("maximum_adhesive_apparent_c"),GetMaximumSealantContactApparentC());
    O->SetStringField(TEXT("weld_heat_recipe"),TEXT("panel-three-edge-contact-transfer-v8"));
    O->SetNumberField(TEXT("weld_halo_cooling_tau_seconds"),WeldHaloCoolingSeconds);
    O->SetNumberField(TEXT("maximum_weld_c"),GetMaximumWeldC());
    O->SetNumberField(TEXT("maximum_weld_apparent_c"),DarkFactoryThermal::ApparentTemperatureC(GetMaximumWeldC(),.74,20.));
    O->SetArrayField(TEXT("adhesive_view_beads"),{MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(1),MakeShared<FJsonValueNumber>(0)});
    O->SetBoolField(TEXT("rear_support_present"),false);
    O->SetNumberField(TEXT("added_backing_mesh_count"),0);
    O->SetBoolField(TEXT("sealant_rear_edge"),false);
    O->SetNumberField(TEXT("panel_seating_lift_cm"),ADarkFactoryWeldingCell::PanelSeatingLiftCm);
    O->SetNumberField(TEXT("weld_contact_count"),ADarkFactoryWeldingCell::PanelWeldCount);
    TArray<TSharedPtr<FJsonValue>> WeldViewContacts;
    for (int32 View=0;View<3;++View) WeldViewContacts.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(2*View),MakeShared<FJsonValueNumber>(2*View+1)}));
    O->SetArrayField(TEXT("weld_view_contacts"),WeldViewContacts);
    O->SetStringField(TEXT("sealant_recipe"),TEXT("rounded-purple-three-edge-v3"));
    O->SetStringField(TEXT("sealant_thermal_target"),TEXT("conducted-panel-surface"));
    O->SetNumberField(TEXT("contact_response_tau_seconds"),.35);
    O->SetNumberField(TEXT("contact_heat_started_seconds"),ContactStarted);
    TArray<TSharedPtr<FJsonValue>> Strips,Welds;
    for (int32 I=0;I<AdhesiveMeshes.Num();++I) {
        auto S=MakeShared<FJsonObject>(); S->SetNumberField(TEXT("index"),I); S->SetNumberField(TEXT("sealant_surface_c"),AdhesiveC[I]); S->SetNumberField(TEXT("surface_c"),ContactSurfaceC[I]); S->SetNumberField(TEXT("apparent_c"),DarkFactoryThermal::ApparentTemperatureC(ContactSurfaceC[I],.74,20.));
        S->SetNumberField(TEXT("initial_c"),AdhesiveInitialC[I]); S->SetNumberField(TEXT("cooling_tau_seconds"),AdhesiveTau[I]); S->SetNumberField(TEXT("emissivity"),.74);
        S->SetNumberField(TEXT("deposited_seconds"),AdhesiveDeposited[I]); S->SetArrayField(TEXT("position_cm"),Vec(AdhesiveMeshes[I]->GetComponentLocation()));
        S->SetNumberField(TEXT("stencil_id"),AdhesiveMeshes[I]->CustomDepthStencilValue);
        S->SetNumberField(TEXT("arc_length_cm"),AdhesiveLengths[I]); S->SetNumberField(TEXT("deposition_progress"),AdhesiveProgress[I]);
        Strips.Add(MakeShared<FJsonValueObject>(S));
    }
    for (int32 I=0;I<ADarkFactoryWeldingCell::PanelWeldCount;++I) {
        auto W=MakeShared<FJsonObject>(); W->SetNumberField(TEXT("index"),I); W->SetNumberField(TEXT("core_c"),WeldCoreC[I]); W->SetNumberField(TEXT("halo_c"),WeldHaloC[I]);
        W->SetNumberField(TEXT("apparent_c"),DarkFactoryThermal::ApparentTemperatureC(FMath::Max(WeldCoreC[I],WeldHaloC[I]),.74,20.));
        W->SetNumberField(TEXT("deposited_seconds"),WeldDeposited[I]); W->SetArrayField(TEXT("contact_local_cm"),Vec(SpotLocal[I])); Welds.Add(MakeShared<FJsonValueObject>(W));
    }
    if (bOfflinePrepared && CaptureVisibilityRays.Num()==3 && !CaptureVisibilityRays.Contains(nullptr)) O->SetArrayField(TEXT("visibility_rays"),CaptureVisibilityRays);
    O->SetArrayField(TEXT("adhesive"),Strips); O->SetArrayField(TEXT("welds"),Welds); return O;
}
