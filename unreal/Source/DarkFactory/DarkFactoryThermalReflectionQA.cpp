#include "DarkFactoryThermalRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

bool ADarkFactoryThermalRig::SaveReflectionSmoke(const FString& Directory) {
    if (bImportedProfiles || !ThermalSensor || !ThermalMaterial) return false;
    const int32 Count=Surfaces.Num();
    const FTransform CameraTransform=ThermalSensor->GetComponentTransform();
    const float FOV=ThermalSensor->FOVAngle;
    const bool Fields=bSpatialFields,Exchange=bHeatTransferEnabled;
    SetHeatTransferEnabled(false); SetSpatialFieldsEnabled(false);
    auto* Plate=Fixture(TEXT("ReflectionQAPlate"),FVector(0,0,5000),FVector(600,600,2),23,.2f);
    auto* Hot=Fixture(TEXT("ReflectionQAHot"),FVector(0,100,5052),FVector(50,50,100),100,1.f);
    const FVector Eye(0,-300,5150);
    ThermalSensor->SetWorldLocationAndRotation(Eye,(FVector(0,70,5000)-Eye).Rotation()); ThermalSensor->FOVAngle=70;
    TArray<TArray<FColor>> Samples;
    bool Passed=Plate && Hot;
    for (int32 Mode=0;Mode<4 && Passed;++Mode) {
        // Low-e off/on, high-e on, source hidden: a one-bounce reflection must
        // disappear when its first visible source disappears.
        Surfaces[Count].Emissivity=Mode==2 ? 1.f:.2f;
        Hot->SetVisibility(Mode!=3);
        UpdateLUT(); ApplyMaterial(ThermalMaterial,0);
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalMinC"),0);
        ThermalMaterial->SetScalarParameterValue(TEXT("ThermalReflections"),Mode==0 ? 0:1);
        for (int32 I=0;I<3;++I) ThermalSensor->CaptureScene();
        Passed &= SaveRawForSmoke(FPaths::Combine(Directory,FString::Printf(TEXT("reflection_%d.png"),Mode)));
        TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
        Passed &= ThermalTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags); Samples.Add(MoveTemp(Pixels));
    }
    // Identify only receiver-plate pixels, so hiding the emitter cannot itself
    // count as a reflection disappearing.
    Hot->SetVisibility(true);
    ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),1);
    Passed &= SaveRawForSmoke(FPaths::Combine(Directory,TEXT("reflection_receiver_ids.png")));
    TArray<FColor> IDs; FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
    Passed &= ThermalTarget->GameThread_GetRenderTargetResource()->ReadPixels(IDs,Flags);
    int32 WarmPixels=0,HiddenWarmPixels=0; int32 MaxDelta=0,HighMin=255,HighMax=0;
    if (Passed && Samples.Num()==4) for (int32 I=0;I<IDs.Num();++I) if (IDs[I].R==Count+1) {
        const int32 Delta=int32(Samples[1][I].R)-int32(Samples[0][I].R);
        WarmPixels+=Delta>3; MaxDelta=FMath::Max(MaxDelta,Delta);
        HighMin=FMath::Min(HighMin,int32(Samples[2][I].R)); HighMax=FMath::Max(HighMax,int32(Samples[2][I].R));
        HiddenWarmPixels+=int32(Samples[3][I].R)-int32(Samples[0][I].R)>3;
    }
    Passed &= WarmPixels>20 && HiddenWarmPixels==0 && HighMax-HighMin<=1;
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Passed);
    Report->SetNumberField(TEXT("warm_receiver_pixels"),WarmPixels);Report->SetNumberField(TEXT("hidden_source_warm_pixels"),HiddenWarmPixels);
    Report->SetNumberField(TEXT("maximum_delta_codes"),MaxDelta);
    Report->SetNumberField(TEXT("high_emissivity_receiver_variation_codes"),HighMax-HighMin);
    FString JSON;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));
    FFileHelper::SaveStringToFile(JSON,*FPaths::Combine(Directory,TEXT("thermal_reflections.json")));
    if (Plate) Plate->DestroyComponent(); if (Hot) Hot->DestroyComponent(); Surfaces.SetNum(Count);
    ThermalSensor->SetWorldTransform(CameraTransform); ThermalSensor->FOVAngle=FOV;
    ThermalMaterial->SetScalarParameterValue(TEXT("ThermalDebugIds"),0);
    SetSpatialFieldsEnabled(Fields);SetHeatTransferEnabled(Exchange);UpdateLUT();ApplyMaterial(ThermalMaterial,0);
    return Passed;
}
