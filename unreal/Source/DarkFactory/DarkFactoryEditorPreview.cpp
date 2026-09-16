#include "DarkFactoryEditorPreview.h"
#include "DarkFactoryWeldingCell.h"
#include "DarkFactoryProcessScene.h"
#include "DarkFactoryIQ9Props.h"
#include "DarkFactoryControlCabinet.h"
#include "DarkFactoryEnvironmentDetails.h"
#include "DarkFactoryCellMechanisms.h"
#include "DarkFactoryCurtainPortal.h"
#include "DarkFactoryFactoryLights.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/LightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"

ADarkFactoryEditorPreview::ADarkFactoryEditorPreview() {
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
    PrimaryActorTick.bCanEverTick=true;
    bIsEditorOnlyActor=true;
    SetActorHiddenInGame(true);
}
void ADarkFactoryEditorPreview::ClearPreview() {
    for (int32 I=PreviewActors.Num()-1;I>=0;--I) if (IsValid(PreviewActors[I])) PreviewActors[I]->Destroy();
    PreviewActors.Reset();
}
void ADarkFactoryEditorPreview::InitializeEditorView(FVector Location,FRotator Rotation) {
#if WITH_EDITORONLY_DATA
    if (!GetWorld() || GetWorld()->WorldType!=EWorldType::Editor) return;
    auto& Views=GetWorld()->EditorViews;
    if (Views.Num()<8) Views.SetNum(8);
    // LVT_Perspective, without an UnrealEd dependency in the runtime module.
    if (Views[3].CamPosition.IsNearlyZero()) {
        Views[3]=FLevelViewportInfo(Location,Rotation,DEFAULT_ORTHOZOOM);
        Views[3].CamUpdated=true;
    }
#endif
}
#if WITH_EDITOR
void ADarkFactoryEditorPreview::PostRegisterAllComponents() {
    Super::PostRegisterAllComponents(); QueueRefresh();
}
void ADarkFactoryEditorPreview::PostEditChangeProperty(FPropertyChangedEvent& Event) {
    Super::PostEditChangeProperty(Event); QueueRefresh();
}
void ADarkFactoryEditorPreview::Destroyed() { ClearPreview(); Super::Destroyed(); }
#endif
void ADarkFactoryEditorPreview::QueueRefresh() {
#if WITH_EDITOR
    bRefreshPending=true;
    if (bRefreshQueued || IsRunningCommandlet() || !GetWorld() || GetWorld()->WorldType!=EWorldType::Editor) return;
    bRefreshQueued=true;
    // Map loading must finish first. A core tick also works with viewport realtime off.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float) {
        bRefreshQueued=false;
        if (bRefreshPending) RefreshPreview();
        return false;
    }));
#endif
}
void ADarkFactoryEditorPreview::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if (bRefreshPending && GetWorld() && GetWorld()->WorldType==EWorldType::Editor) RefreshPreview();
#endif
}
void ADarkFactoryEditorPreview::RefreshPreviewInEditor() { RefreshPreview(); }
bool ADarkFactoryEditorPreview::RefreshPreview() {
#if WITH_EDITOR
    if (!GetWorld() || GetWorld()->WorldType!=EWorldType::Editor) return false;
    bRefreshPending=false; ClearPreview();
    auto Spawn=[&](UClass* Class,const TCHAR* Label) {
        FActorSpawnParameters Params; Params.Owner=this;
        Params.ObjectFlags=RF_Transient; // Save only the generator; never stale scene copies.
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* Actor=GetWorld()->SpawnActor<AActor>(Class,FTransform::Identity,Params);
        if (Actor) {
            // RF_Transient excludes these from saved/cooked maps. Do not mark
            // children editor-only: that flag hides them in the editor's G view.
            // The game mode removes tagged actors before runtime BeginPlay.
            Actor->Tags.Add(GeneratedTag()); Actor->bIsEditorOnlyActor=false;
            Actor->SetActorLabel(Label,false); Actor->SetFolderPath(TEXT("DarkFactory/Generated Preview"));
            Actor->SetActorTickEnabled(false); PreviewActors.Add(Actor);
        }
        return Actor;
    };
    auto Fail=[&](const FString& Message) {
        ClearPreview(); PreviewStatus=Message;
        UE_LOG(LogTemp,Warning,TEXT("DF_EDITOR_PREVIEW_FAILED %s"),*Message); return false;
    };
    auto* Cell=Cast<ADarkFactoryWeldingCell>(Spawn(ADarkFactoryWeldingCell::StaticClass(),TEXT("Robots, body and imported cell")));
    FString Error;
    const FString Manifest=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),TEXT("../assets/derived/welding_cell/scene.json")));
    if (!Cell || !Cell->LoadScene(Manifest,Error)) return Fail(TEXT("Prepare/import the local welding assets, then Refresh Preview. ")+Error);
    Cell->SetAnimationEnabled(false);
    auto* Props=Cast<ADarkFactoryIQ9Props>(Spawn(ADarkFactoryIQ9Props::StaticClass(),TEXT("IQ9 EVK props")));
    if (!Props || !Props->Initialize(Cell)) return Fail(TEXT("IQ9 prop initialization failed"));
    Props->SetLedTimeForQA(0.);
    auto* Process=Cast<ADarkFactoryProcessScene>(Spawn(ADarkFactoryProcessScene::StaticClass(),TEXT("Panel frame, sealant and inspection camera")));
    if (!Process || !Process->Initialize(Cell,42)) return Fail(TEXT("Current panel assets are missing; rebuild/import content"));
    // This is an illustrative static pose only; it never makes a model decision.
    Process->BeginBody(1,0.,0);
    using P=EDarkFactoryProcessPhase;
    Process->ApplyAssemblyFrame(P::Arriving,ADarkFactoryProcessScene::CarriageTravelSeconds,4.,0.,true);
    if (PreviewPose!=EDarkFactoryEditorPose::AwaitingPanel)
        Process->ApplyAssemblyFrame(P::AdhesiveApplication,ADarkFactoryWeldingCell::PanelAdhesiveSeconds,18.,0.,true);
    if (PreviewPose==EDarkFactoryEditorPose::PanelPlaced)
        Process->ApplyAssemblyFrame(P::PanelPlacement,ADarkFactoryWeldingCell::PanelPlacementSeconds,24.,0.,true);
    auto* Cabinet=Cast<ADarkFactoryControlCabinet>(Spawn(ADarkFactoryControlCabinet::StaticClass(),TEXT("Control cabinet display")));
    auto* Details=Cast<ADarkFactoryEnvironmentDetails>(Spawn(ADarkFactoryEnvironmentDetails::StaticClass(),TEXT("Floor markings and factory details")));
    auto* Mechanisms=Cast<ADarkFactoryCellMechanisms>(Spawn(ADarkFactoryCellMechanisms::StaticClass(),TEXT("Clamps, photoeyes and stack lights")));
    if (!Cabinet || !Cabinet->Initialize(Cell) || !Details || !Details->Initialize(Cell) || !Mechanisms || !Mechanisms->Initialize(Cell))
        return Fail(TEXT("Cabinet or realism content initialization failed"));
    FDarkFactoryProcessStatus Status; Status.bEnabled=true; Status.bPaused=true;
    Status.Backend=TEXT("EDITOR PREVIEW / NO INFERENCE"); Status.Message=TEXT("Press Play to run camera gates");
    Cabinet->SetEnabled(true); Cabinet->SetProcessStatus(Status); Details->SetEnabled(true); Mechanisms->SetEnabled(true); Mechanisms->SetProcessStatus(Status);
    for (double Y:{-1204.08264,1219.15393}) {
        auto* Portal=Cast<ADarkFactoryCurtainPortal>(Spawn(ADarkFactoryCurtainPortal::StaticClass(),Y<0 ? TEXT("Entry portal"):TEXT("Exit portal")));
        if (!Portal) return Fail(TEXT("Portal spawn failed"));
        Portal->SetActorTransform(FTransform(FRotator(0,Y<0 ? 180:0,0),FVector(4.505665,Y,6.3477106)));
        if (!Portal->Initialize(420,390,ADarkFactoryProcessScene::TunnelLengthCm,14,3)) return Fail(TEXT("Portal initialization failed"));
        Portal->SetSimulationEnabled(false);
    }
    auto* Lights=Cast<ADarkFactoryFactoryLights>(Spawn(ADarkFactoryFactoryLights::StaticClass(),TEXT("Fluorescent roof lights")));
    if (!Lights || !Lights->Initialize(true)) return Fail(TEXT("Factory light materials missing"));
    Lights->SetEnabled(true); Lights->SetStartupTimeForQA(2.f);
    auto* Exposure=Cast<APostProcessVolume>(Spawn(APostProcessVolume::StaticClass(),TEXT("Editor preview exposure")));
    if (!Exposure) return Fail(TEXT("Preview exposure spawn failed"));
    Exposure->bUnbound=true;
    auto& PP=Exposure->Settings;
    PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=-.7f;
    PP.bOverride_UserFlags=true; PP.UserFlags=256;
    for (AActor* Actor:PreviewActors) if (Actor) {
        Actor->SetActorTickEnabled(false);
        TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
        for (auto* Part:Primitives) { Part->SetSimulatePhysics(false); Part->SetCastHiddenShadow(false); }
    }
    PreviewStatus=TEXT("Current panel assembly preview. Press Play for live camera gates. Refresh Preview after reimporting assets.");
    UE_LOG(LogTemp,Display,TEXT("DF_EDITOR_PREVIEW_READY %s"),*GetPreviewReport());
    return true;
#else
    return false;
#endif
}
FString ADarkFactoryEditorPreview::GetPreviewReport() const {
    auto Result=MakeShared<FJsonObject>(); int32 Actors=0,Meshes=0,Captures=0,Simulated=0;
    for (AActor* Actor:PreviewActors) if (IsValid(Actor)) {
        ++Actors;
        TInlineComponentArray<UStaticMeshComponent*> Parts(Actor); Meshes+=Parts.Num();
        for (auto* Part:Parts) if (Part->IsSimulatingPhysics()) ++Simulated;
        TInlineComponentArray<USceneCaptureComponent2D*> Sensors(Actor); Captures+=Sensors.Num();
    }
    Result->SetNumberField(TEXT("actors"),Actors); Result->SetNumberField(TEXT("mesh_components"),Meshes);
    Result->SetNumberField(TEXT("scene_captures"),Captures); Result->SetNumberField(TEXT("simulated_bodies"),Simulated);
    Result->SetStringField(TEXT("status"),PreviewStatus);
    FString Text; auto Writer=TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(Result,Writer); return Text;
}
