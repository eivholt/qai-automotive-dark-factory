#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryEnvironmentDetails.generated.h"

class ADarkFactoryWeldingCell;
class FJsonObject;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

/** Three static visual factory additions; no simulation, lights, capture passes or thermal IDs. */
UCLASS()
class DARKFACTORY_API ADarkFactoryEnvironmentDetails : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryEnvironmentDetails();
    bool Initialize(ADarkFactoryWeldingCell* Cell);
    void SetEnabled(bool Enabled);
    bool IsEnabled() const { return bEnabled; }
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> ContextBatches;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    enum class EFinish : uint8 { Lane,White,Yellow,Steel,Dark,Blue,Cable,Count };
    struct FBatch { EFinish Finish; bool Round=false; int32 Component=0; };
    struct FFeature { FString Name; FBox Bounds=FBox(ForceInit); int32 Instances=0; };
    TArray<FBatch> BatchKeys;
    TArray<FFeature> Features;
    TArray<FVector> StationCenters,HoodCenters;
    bool bInitialized=false,bEnabled=false,bFinite=true;
    int32 TotalInstances=0;
    UInstancedStaticMeshComponent* Batch(EFinish Finish,bool Round);
    void Box(int32 Feature,EFinish Finish,FVector Center,FVector Size,FRotator Rotation=FRotator::ZeroRotator);
    void Bar(int32 Feature,EFinish Finish,FVector A,FVector B,float Width,float Depth);
    void Pipe(int32 Feature,EFinish Finish,FVector A,FVector B,float Diameter);
    void Instance(int32 Feature,EFinish Finish,const FTransform& Transform,bool Round);
    void StationSign(int32 Number,FVector Center,FVector Normal);
    void BuildFloor();
    void BuildServices();
    void BuildExtraction();
    void BuildFactoryContext();
};
