#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryThermalRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void ADarkFactoryWorld::CaptureModalitiesTick(double Now) {
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !ThermalRig || !ThermalRig->IsReady()) {
        UE_LOG(LogTemp,Error,TEXT("DF_MODALITY_SCREENSHOTS passed=0 missing observer or thermal materials"));
        bModalityScreenshots=false;
        if (Observer) { Observer->SetCaptureAutomation(false); Observer->SetNightVisionSmokeFrame(-1); }
        if (ThermalRig) ThermalRig->SetFrozen(false);
        return;
    }
    static const TCHAR* Files[]={TEXT("01_visible_rooflight.png"),TEXT("02_nir_green.png"),TEXT("03_nir_monochrome.png"),TEXT("04_thermal_white_hot.png"),TEXT("05_thermal_black_hot.png"),TEXT("06_thermal_iron.png")};
    static const TCHAR* Modes[]={TEXT("visible_dim_roof_window_light"),TEXT("active_nir_green"),TEXT("active_nir_monochrome"),TEXT("thermal_white_hot"),TEXT("thermal_black_hot"),TEXT("thermal_iron")};
    IFileManager::Get().MakeDirectory(*OutputRoot,true);
    if (SmokeStep==0) {
        bInferenceEnabled=false;
        Seed=BaseSeed; SetScenario(0,false);
        Hold(TEXT("HOLD: inference paused for modality screenshots"));
        Observer->SetCaptureAutomation(true);
        Observer->SetActorLocation(FVector(-300,-780,380));
        Observer->SetActorRotation((FVector(0,25,95)-Observer->GetActorLocation()).Rotation());
        Observer->SetNightVisionSmokeFrame(12);
        ThermalRig->SetFrozen(true);
        ThermalRig->SetHeatTransferEnabled(true);
        ThermalRig->SetSpatialFieldsEnabled(true);
        ThermalRig->SetCase(1); // Overheated module, selected simulated age 15 s.
        // Offline evidence that visible roof light does not change either sensor.
        // Both NIR captures use identical augmentation state; previews stay on GPU.
        const float RoofLumens=GetRoofLightIntensity();
        const int32 ReferenceFrame=CaptureIndex;
        SetRoofLightIntensity(0); CaptureSensor();
        const auto NIROff=LatestPng;
        bRoofLightSensorParity=FFileHelper::SaveArrayToFile(NIROff,*FPaths::Combine(OutputRoot,TEXT("sensor_nir_rooflight_off.png")));
        bRoofLightSensorParity &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("sensor_thermal_rooflight_off.png")));
        SetRoofLightIntensity(RoofLumens); CaptureIndex=ReferenceFrame; CaptureSensor();
        bRoofLightSensorParity &= LatestPng==NIROff && !LatestPng.IsEmpty();
        bRoofLightSensorParity &= FFileHelper::SaveArrayToFile(LatestPng,*FPaths::Combine(OutputRoot,TEXT("sensor_nir_rooflight_on.png")));
        bRoofLightSensorParity &= ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("sensor_thermal_rooflight_on.png")));
        TArray<uint8> ThermalOff,ThermalOn;
        bRoofLightSensorParity &= FFileHelper::LoadFileToArray(ThermalOff,*FPaths::Combine(OutputRoot,TEXT("sensor_thermal_rooflight_off.png")))
            && FFileHelper::LoadFileToArray(ThermalOn,*FPaths::Combine(OutputRoot,TEXT("sensor_thermal_rooflight_on.png")))
            && !ThermalOff.IsEmpty() && ThermalOff==ThermalOn;
        // Camera previews remain GPU-only and independent of inference.
        Sensor->CaptureScene(); ++GPUPreviewCaptures;
        PreviewEpoch=Epoch; PreviewCapturedAt=Now;
    }
    if (SmokeStep<12) {
        const int32 Mode=SmokeStep/2;
        if (SmokeStep%2==0) {
            // Set mode on one frame, then allow a full second of settled rendering.
            bGreenDisplay=Mode!=2;
            Observer->SetObserverMode(Mode==0 ? 0:Mode<=2 ? 1:2);
            if (Mode>=3) Observer->SetThermalPalette(Mode-3);
            ThermalRig->CaptureGPU();
        } else {
            // Canvas HUD is part of the game viewport. Exclude unrelated Slate UI.
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Files[Mode]),false,false);
        }
        ++SmokeStep; NextAutomation=Now+1.0;
        return;
    }

    // The last screenshot was submitted on the previous stage, never this frame.
    int32 Width=0,Height=0; PC->GetViewportSize(Width,Height);
    bool Passed=bRoofLightSensorParity && Width==1600 && Height==900 && Observer->HasNightVisionMaterial() && Observer->HasThermalMaterial();
    auto Report=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Captures;
    for (int32 I=0;I<6;++I) {
        const int64 Bytes=IFileManager::Get().FileSize(*FPaths::Combine(OutputRoot,Files[I]));
        Passed &= Bytes>0;
        auto Capture=MakeShared<FJsonObject>();
        Capture->SetStringField(TEXT("image"),Files[I]); Capture->SetStringField(TEXT("mode"),Modes[I]);
        Capture->SetNumberField(TEXT("bytes"),Bytes);
        Capture->SetBoolField(TEXT("nir_green_display"),I!=2);
        if (I>=3) Capture->SetNumberField(TEXT("thermal_palette"),I-3);
        Captures.Add(MakeShared<FJsonValueObject>(Capture));
    }
    Report->SetBoolField(TEXT("passed"),Passed);
    Report->SetArrayField(TEXT("screenshots"),Captures);
    Report->SetNumberField(TEXT("width"),Width); Report->SetNumberField(TEXT("height"),Height);
    Report->SetNumberField(TEXT("seed"),Seed);
    Report->SetStringField(TEXT("nir_scenario"),GetScenarioName());
    Report->SetStringField(TEXT("thermal_case"),ThermalRig->GetCaseName());
    Report->SetBoolField(TEXT("heat_transfer_enabled"),ThermalRig->HasHeatTransfer());
    Report->SetBoolField(TEXT("spatial_fields_enabled"),ThermalRig->HasSpatialFields());
    Report->SetBoolField(TEXT("inference_enabled"),false);
    Report->SetBoolField(TEXT("hud_included"),true);
    Report->SetNumberField(TEXT("roof_window_lumens_each"),GetRoofLightIntensity());
    Report->SetBoolField(TEXT("sensor_pngs_unchanged_by_visible_rooflight"),bRoofLightSensorParity);
    Report->SetBoolField(TEXT("night_vision_quality"),Observer->IsNightVisionQuality());
    Report->SetNumberField(TEXT("night_vision_frame"),12);
    Report->SetStringField(TEXT("night_vision_settings"),Observer->GetNightVisionStatus());
    Report->SetObjectField(TEXT("thermal_encoding"),ThermalRig->GetEncodingMetadata());
    const FVector Position=Observer->GetActorLocation();
    const FRotator Rotation=Observer->GetActorRotation();
    auto Camera=MakeShared<FJsonObject>();
    Camera->SetArrayField(TEXT("position_cm"),{MakeShared<FJsonValueNumber>(Position.X),MakeShared<FJsonValueNumber>(Position.Y),MakeShared<FJsonValueNumber>(Position.Z)});
    Camera->SetArrayField(TEXT("rotation_pitch_yaw_roll_deg"),{MakeShared<FJsonValueNumber>(Rotation.Pitch),MakeShared<FJsonValueNumber>(Rotation.Yaw),MakeShared<FJsonValueNumber>(Rotation.Roll)});
    Camera->SetNumberField(TEXT("horizontal_fov_deg"),72);
    Report->SetObjectField(TEXT("observer_camera"),Camera);
    Report->SetStringField(TEXT("scope"),TEXT("Same scene and observer pose across six implemented modalities. The visible observer uses dim roof-window light; fixed NIR and thermal sensor HUDs remain active. Night vision grain is frozen at frame 12. Inference remains paused."));
    FString Json;
    const bool Serialized=FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    const bool Saved=Serialized && FFileHelper::SaveStringToFile(Json,*FPaths::Combine(OutputRoot,TEXT("modalities.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Passed &= Saved;
    UE_LOG(LogTemp,Display,TEXT("DF_MODALITY_SCREENSHOTS passed=%d directory=%s leave_running=1"),Passed,*OutputRoot);
    bModalityScreenshots=false;
    bGreenDisplay=true;
    Observer->SetObserverMode(2); Observer->SetThermalPalette(2);
    Observer->SetCaptureAutomation(false); Observer->SetNightVisionSmokeFrame(-1);
    ThermalRig->SetFrozen(false);
    Hold(TEXT("HOLD: inference paused; press P to enable"));
    NextCapture=0;
}
