#include "DarkFactoryThermalRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraTypes.h"
#include "Kismet/GameplayStatics.h"
#include "SceneView.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool ADarkFactoryThermalRig::SaveTransferSmoke(const FString& Directory) {
    if (!TransferSource || !TransferReceiver || !TransferBlocker || !ThermalSensor || !ThermalMaterial) return false;
    IFileManager::Get().MakeDirectory(*Directory,true);
    const int32 OriginalCase=CaseIndex;
    const float OriginalAge=CoolingAgeSeconds,OriginalFOV=ThermalSensor->FOVAngle;
    const bool OriginalTransfer=bHeatTransferEnabled,OriginalSpatial=bSpatialFields,OriginalFrozen=bFrozen;
    const FTransform OriginalCamera=ThermalSensor->GetComponentTransform();
    const FTransform OriginalSource=TransferSource->GetRelativeTransform();
    const FTransform OriginalReceiver=TransferReceiver->GetRelativeTransform();
    const FTransform OriginalBlocker=TransferBlocker->GetRelativeTransform();
    const auto OriginalSurfaces=Surfaces;
    const auto OriginalExchange=HeatExchange;
    TArray<TPair<TWeakObjectPtr<UStaticMeshComponent>,bool>> OriginalVisibility;
    for (const auto& Surface:Surfaces) if (auto* Mesh=Surface.Mesh.Get()) OriginalVisibility.Add({Mesh,Mesh->IsVisible()});
    if (DepthTestCover) OriginalVisibility.Add({DepthTestCover.Get(),DepthTestCover->IsVisible()});
    float OriginalDebugIds=0;
    ThermalMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ThermalDebugIds")),OriginalDebugIds);
    ON_SCOPE_EXIT {
        // Restore retained heat as well as its nominal age; do not re-simulate a
        // possibly different movement history while leaving this offline test.
        TransferSource->SetRelativeTransform(OriginalSource);
        TransferReceiver->SetRelativeTransform(OriginalReceiver);
        TransferBlocker->SetRelativeTransform(OriginalBlocker);
        for (const auto& Visibility:OriginalVisibility) if (auto* Mesh=Visibility.Key.Get()) Mesh->SetVisibility(Visibility.Value,true);
        CaseIndex=OriginalCase; CoolingAgeSeconds=OriginalAge;
        bHeatTransferEnabled=OriginalTransfer; bSpatialFields=OriginalSpatial; bFrozen=OriginalFrozen;
        Surfaces=OriginalSurfaces; HeatExchange=OriginalExchange;
        ThermalSensor->SetWorldTransform(OriginalCamera); ThermalSensor->FOVAngle=OriginalFOV;
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),OriginalDebugIds);
        UpdateLUT(); CaptureGPU();
    };

    bFrozen=true;
    SetCase(5); SetSpatialFieldsEnabled(true); SetHeatTransferEnabled(true);
    // Clear the overhead beam even for the 100 cm tall shielding plate.
    constexpr double FixtureZ=350;
    const FVector ReceiverPosition(0,54,FixtureZ),CameraPosition(320,-100,FixtureZ+75);
    TransferReceiver->SetRelativeLocation(ReceiverPosition);
    TransferReceiver->SetRelativeRotation(FRotator::ZeroRotator);
    TransferSource->SetRelativeRotation(FRotator::ZeroRotator);
    TransferBlocker->SetRelativeLocation(FVector(0,27,FixtureZ));
    TransferBlocker->SetRelativeRotation(FRotator::ZeroRotator);
    ThermalSensor->SetWorldLocation(CameraPosition);
    ThermalSensor->SetWorldRotation((TransferReceiver->GetComponentLocation()-CameraPosition).Rotation());
    ThermalSensor->FOVAngle=35;
    ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),0);

    auto ReceiverRegions=[&]() {
        FMinimalViewInfo Info; ThermalSensor->GetCameraView(0,Info);
        Info.AspectRatio=448.f/256.f;
        FMatrix View,Projection,VP;
        UGameplayStatics::CalculateViewProjectionMatricesFromMinimalView(Info,TOptional<FMatrix>(),View,Projection,VP);
        auto QA=MakeShared<FJsonObject>(),Faces=MakeShared<FJsonObject>();
        static const TCHAR* FaceNames[]={TEXT("+X"),TEXT("-X"),TEXT("+Y"),TEXT("-Y"),TEXT("+Z"),TEXT("-Z")};
        for (int32 Index=0;Index<Surfaces.Num();++Index) {
            if (Surfaces[Index].Mesh.Get()!=TransferReceiver.Get()) continue;
            const FBox Bounds=Surfaces[Index].LocalBounds;
            const FTransform Transform=TransferReceiver->GetComponentTransform();
            QA->SetNumberField(TEXT("receiver_stencil_id"),Index+1);
            QA->SetStringField(TEXT("receiver_surface"),TransferReceiver->GetName());
            QA->SetStringField(TEXT("donor_facing_face"),TEXT("-Y"));
            for (int32 Face=0;Face<6;++Face) {
                FVector Axis=FVector::ZeroVector; Axis[Face/2]=(Face%2) ? -1:1;
                const FVector LocalPoint=Bounds.GetCenter()+Axis*(Bounds.GetExtent()+FVector(.001));
                const FVector WorldPoint=Transform.TransformPosition(LocalPoint);
                FVector2D Pixel;
                const bool Projected=FSceneView::ProjectWorldToScreen(WorldPoint,FIntRect(0,0,448,256),VP,Pixel);
                auto Region=MakeShared<FJsonObject>();
                Region->SetNumberField(TEXT("stencil_id"),Index+1);
                Region->SetNumberField(TEXT("face_index"),Face);
                Region->SetBoolField(TEXT("projected"),Projected);
                Region->SetBoolField(TEXT("camera_facing"),FVector::DotProduct(Transform.TransformVectorNoScale(Axis),CameraPosition-WorldPoint)>0);
                Region->SetArrayField(TEXT("normalized_face_point"),{MakeShared<FJsonValueNumber>(Axis.X),MakeShared<FJsonValueNumber>(Axis.Y),MakeShared<FJsonValueNumber>(Axis.Z)});
                if (Projected) {
                    const int32 X=FMath::RoundToInt(Pixel.X),Y=FMath::RoundToInt(Pixel.Y);
                    Region->SetArrayField(TEXT("box"),{MakeShared<FJsonValueNumber>(FMath::Clamp(X-2,0,447)),MakeShared<FJsonValueNumber>(FMath::Clamp(Y-2,0,255)),MakeShared<FJsonValueNumber>(FMath::Clamp(X+3,1,448)),MakeShared<FJsonValueNumber>(FMath::Clamp(Y+3,1,256))});
                    Region->SetArrayField(TEXT("pixel_center"),{MakeShared<FJsonValueNumber>(Pixel.X),MakeShared<FJsonValueNumber>(Pixel.Y)});
                }
                Faces->SetObjectField(FaceNames[Face],Region);
            }
            break;
        }
        QA->SetObjectField(TEXT("receiver_faces"),Faces);
        QA->SetStringField(TEXT("roi_scope"),TEXT("Projected face centers; stencil masks reject other objects. Back-facing/occluded centers are not guarantees of face visibility."));
        return QA;
    };
    auto States=MakeShared<FJsonObject>();
    bool Passed=true;
    auto SaveState=[&](const TCHAR* Name,double ExposureGap,bool Retracted=false,bool Retained=false) {
        UpdateTransforms();
        auto Metadata=BuildSnapshotMetadata();
        const FString Stem=FString(TEXT("transfer_"))+Name;
        const FString Image=Stem+TEXT(".png"),Ids=Stem+TEXT("_ids.png");
        Passed &= SaveRawForSmoke(FPaths::Combine(Directory,Image));
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),1);
        Passed &= SaveRawForSmoke(FPaths::Combine(Directory,Ids));
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),0);
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalPalette"),2);
        Passed &= SaveRawForSmoke(FPaths::Combine(Directory,Stem+TEXT("_display_iron.png")));
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalPalette"),0);
        Passed &= HeatExchange.Valid;
        Metadata->SetStringField(TEXT("image"),Image);
        Metadata->SetStringField(TEXT("id_image"),Ids);
        Metadata->SetNumberField(TEXT("exposure_gap_cm"),ExposureGap);
        Metadata->SetNumberField(TEXT("fixture_gap_cm"),50-TransferSource->GetRelativeLocation().Y);
        Metadata->SetBoolField(TEXT("donor_retracted_for_surface_inspection"),Retracted);
        Metadata->SetBoolField(TEXT("donor_relocated_after_heating"),Retained);
        Metadata->SetBoolField(TEXT("donor_visible"),TransferSource->IsVisible());
        Metadata->SetBoolField(TEXT("blocker_visible"),TransferBlocker->IsVisible());
        Metadata->SetNumberField(TEXT("initial_temperature_c"),24);
        Metadata->SetNumberField(TEXT("source_temperature_c"),110);
        Metadata->SetObjectField(TEXT("transfer_qa_regions"),ReceiverRegions());
        if (Retracted) Metadata->SetStringField(TEXT("inspection_note"),TEXT("Donor moved from exposure position to 100 cm gap immediately before inspection; no elapsed simulation time and no heat reset. Contacting opaque donor otherwise hides the heated face."));
        States->SetObjectField(Name,Metadata);
    };
    auto PrepareExposure=[&](double Gap,bool Blocked,double Age) {
        TransferSource->SetRelativeLocation(FVector(0,50-Gap,FixtureZ));
        TransferSource->SetVisibility(true,true); TransferReceiver->SetVisibility(true,true);
        TransferBlocker->SetVisibility(Blocked,true);
        CoolingAgeSeconds=0; ResetExchange(); SetCoolingAge(static_cast<float>(Age));
        NotifySceneChanged();
        Passed &= FMath::IsNearlyEqual(CoolingAgeSeconds,static_cast<float>(Age));
    };
    auto RetractWithoutCooling=[&]() {
        TransferSource->SetRelativeLocation(FVector(0,-50,FixtureZ));
        // A zero-duration refresh rebuilds geometry links without changing heat,
        // integration step count or elapsed time. It never calls ResetExchange.
        SetCoolingAge(CoolingAgeSeconds); NotifySceneChanged();
    };

    PrepareExposure(50,false,0); SaveState(TEXT("initial"),50);
    PrepareExposure(50,false,600); SaveState(TEXT("near"),50);
    PrepareExposure(100,false,600); SaveState(TEXT("far"),100);
    PrepareExposure(50,true,600); SaveState(TEXT("blocked"),50);
    PrepareExposure(0,false,600); RetractWithoutCooling(); SaveState(TEXT("contact"),0,true);
    PrepareExposure(2,false,600); RetractWithoutCooling(); SaveState(TEXT("gap"),2,true);
    PrepareExposure(50,false,600); RetractWithoutCooling(); SaveState(TEXT("moved"),50,false,true);
    TransferSource->SetVisibility(false,true);
    SetCoolingAge(1200); NotifySceneChanged(); SaveState(TEXT("cooled"),50,false,true);

    auto Report=MakeShared<FJsonObject>();
    Report->SetObjectField(TEXT("states"),States);
    Report->SetBoolField(TEXT("passed"),Passed);
    Report->SetStringField(TEXT("scope"),TEXT("Offline deterministic transfer QA. Independent exposures reset heat; moved/cooled preserve the near exposure. ID masks are diagnostics and never enter inference or training exports."));
    Report->SetStringField(TEXT("contact_inspection"),TEXT("Contact and 2 cm-gap donors are retracted to 100 cm with zero time advancement, exposing the stored receiver surface temperature."));
    FString Json;
    Passed &= FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    Passed &= FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,TEXT("transfer_states.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return Passed;
}
