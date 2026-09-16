#include "DarkFactoryWorld.h"
#include "DarkFactoryPawn.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessController.h"
#include "DarkFactoryFactoryLights.h"
#include "DarkFactoryThermalRig.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactoryCurtainPortal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

void ADarkFactoryWorld::EquipmentHeatSmokeTick(double Now) {
    // Prime the fixed capture over real rendered frames, not same-frame repeats.
    if (Sensor) Sensor->CaptureScene();
    if (Now<NextAutomation) return;
    if (EquipmentHeatStep==0 && !EquipmentHeatReport) { EquipmentHeatReport=MakeShared<FJsonObject>(); NextAutomation=Now+15.; return; }
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Observer=PC ? Cast<ADarkFactoryPawn>(PC->GetPawn()):nullptr;
    if (!Observer || !WeldingCell || !ThermalRig || !ProcessController) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Pose=[&](double Clock,double Offset=0.) { WeldingCell->SetProcessFrame(0,Clock,Offset,true,false,0); };
    auto View=[&](FVector Position,FVector Target,int32 Mode) {
        Observer->SetActorLocation(Position); Observer->SetActorRotation((Target-Position).Rotation()); Observer->SetObserverMode(Mode);
    };
    auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputRoot,Name),false,false); };
    const FVector Motor(83.8,-286.2,83.2),MotorView(175,-380,145);
    if (EquipmentHeatStep==0) {
        IFileManager::Get().MakeDirectory(*OutputRoot,true);
        EquipmentHeatReport=MakeShared<FJsonObject>(); WeldingSensorStats=MakeShared<FJsonObject>();
        if (!ProcessController->GetStatus().bPaused) ProcessController->TogglePaused();
        ProcessController->SetActorTickEnabled(false); WeldingCell->SetAnimationEnabled(false);
        if (auto* Freeze=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Test.FreezeTemporalSequences"))) Freeze->Set(1,ECVF_SetByCode);
        if (ControlCabinet) ControlCabinet->SetActorTickEnabled(false);
        if (CellMechanisms) CellMechanisms->SetActorTickEnabled(false);
        for (ADarkFactoryCurtainPortal* Portal:CurtainPortals) { Portal->SetSimulationEnabled(false); Portal->ResetSimulation(); }
        IQ9Props->SetLedTimeForQA(.1);
        IQ9Props->SetActorTickEnabled(false);
        Observer->SetCaptureAutomation(true); Observer->SetObserverMode(3); FactoryLights->SetStartupTimeForQA(3);
        Pose(0); EquipmentHeatReport->SetObjectField(TEXT("cold"),WeldingCell->BuildEquipmentHeatMetadata());
    } else if (EquipmentHeatStep==1) {
        Shot(TEXT("roof-lights.png"));
        EquipmentHeatReport->SetObjectField(TEXT("lights"),FactoryLights->BuildMetadata());
    } else if (EquipmentHeatStep==2) { View(MotorView,Motor,3); FactoryLights->SetStartupTimeForQA(3); }
    else if (EquipmentHeatStep==3) { Shot(TEXT("motor-visible.png")); }
    else if (EquipmentHeatStep==4) { View(MotorView,Motor,2); }
    else if (EquipmentHeatStep==5) {
        Shot(TEXT("motor-cold.png"));
        Sensor->CaptureScene(); Sensor->CaptureScene(); Sensor->CaptureScene();
        SaveWeldingNIRQA(TEXT("cold-nir.png"),TEXT("cold"));
    } else if (EquipmentHeatStep==6) {
        for (int32 I=1;I<=60;++I) Pose(I*.05,I*25.);
        // Publish the travelled transform before the fixture returns the body
        // to its reference position at the same clock for image comparisons.
        ThermalRig->CaptureGPU();
        Pose(3); EquipmentHeatReport->SetObjectField(TEXT("moving"),WeldingCell->BuildEquipmentHeatMetadata());
    } else if (EquipmentHeatStep==7) {
        Shot(TEXT("motor-hot.png"));
        Sensor->CaptureScene(); Sensor->CaptureScene(); Sensor->CaptureScene();
        SaveWeldingNIRQA(TEXT("hot-nir.png"),TEXT("hot"));
        EquipmentHeatReport->SetObjectField(TEXT("thermal_snapshot"),ThermalRig->GetSnapshotMetadataForQA());
        TArray<USceneCaptureComponent2D*> Cameras; ThermalRig->GetComponents(Cameras);
        if (Cameras.Num()) {
            const FTransform Original=Cameras[0]->GetComponentTransform(); Cameras[0]->SetWorldLocationAndRotation(MotorView,(Motor-MotorView).Rotation());
            Cameras[0]->bAlwaysPersistRenderingState=true;
            ThermalRig->SaveRawForSmoke(FPaths::Combine(OutputRoot,TEXT("motor-raw.png")));
            ThermalRig->SaveSurfaceIdsForQA(FPaths::Combine(OutputRoot,TEXT("motor-ids.png")));
            Cameras[0]->SetWorldTransform(Original);
        }
        for (int32 I=0;I<60;++I) Pose(3);
        EquipmentHeatReport->SetObjectField(TEXT("paused"),WeldingCell->BuildEquipmentHeatMetadata());
    } else if (EquipmentHeatStep==8) {
        Pose(63); EquipmentHeatReport->SetObjectField(TEXT("cooled"),WeldingCell->BuildEquipmentHeatMetadata());
    } else if (EquipmentHeatStep==9) { Shot(TEXT("motor-cooled.png")); }
    else if (EquipmentHeatStep==10) { View(FVector(355,-905,355),FVector(525,-715,230),2); }
    else if (EquipmentHeatStep==11) { Shot(TEXT("cabinet-thermal.png")); }
    else if (EquipmentHeatStep==12) { View(FVector(355,-905,355),FVector(525,-715,230),3); FactoryLights->SetStartupTimeForQA(3); }
    else if (EquipmentHeatStep==13) { Shot(TEXT("cabinet-visible.png")); }
    else {
        auto C=EquipmentHeatReport->GetObjectField(TEXT("cold")); auto H=EquipmentHeatReport->GetObjectField(TEXT("moving"));
        auto P=EquipmentHeatReport->GetObjectField(TEXT("paused")); auto R=EquipmentHeatReport->GetObjectField(TEXT("cooled"));
        TArray<uint8> Before,After;
        const bool NIR=FFileHelper::LoadFileToArray(Before,*FPaths::Combine(OutputRoot,TEXT("cold-nir.png")))
            && FFileHelper::LoadFileToArray(After,*FPaths::Combine(OutputRoot,TEXT("hot-nir.png"))) && Before==After;
        const bool Passed=C->GetBoolField(TEXT("enabled")) && C->GetNumberField(TEXT("groups"))==5
            && H->GetNumberField(TEXT("motor_core_c"))>C->GetNumberField(TEXT("motor_core_c"))+15
            && H->GetNumberField(TEXT("motor_core_c"))==P->GetNumberField(TEXT("motor_core_c"))
            && R->GetNumberField(TEXT("motor_core_c"))<25 && R->GetNumberField(TEXT("cabinet_core_c"))>35 && NIR;
        EquipmentHeatReport->SetBoolField(TEXT("nir_unchanged"),NIR); EquipmentHeatReport->SetBoolField(TEXT("passed"),Passed);
        FString Json; FJsonSerializer::Serialize(EquipmentHeatReport.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json,*FPaths::Combine(OutputRoot,TEXT("equipment_heat.json")));
        FPlatformMisc::RequestExitWithStatus(false,Passed ? 0:1); bEquipmentHeatQA=false;
    }
    ++EquipmentHeatStep; NextAutomation=Now+1.;
}
