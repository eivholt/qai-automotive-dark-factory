#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryProcessTypes.h"
#include "DarkFactoryCellMechanisms.generated.h"

class ADarkFactoryWeldingCell;
class FJsonObject;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class USceneCaptureComponent2D;

/** Authored fixture motion and geometric presence indicators; no inspection or safety controller. */
UCLASS()
class DARKFACTORY_API ADarkFactoryCellMechanisms : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryCellMechanisms();
    bool Initialize(ADarkFactoryWeldingCell* Cell);
    void SetEnabled(bool Enabled);
    void SetProcessStatus(const FDarkFactoryProcessStatus& Status);
    void HideProcessIndicatorsFrom(USceneCaptureComponent2D* Capture) const;
    bool IsEnabled() const { return bEnabled; }
    // Call after a direct cell time seek and before diagnostic captures.
    void UpdateFromCell();
    virtual void Tick(float DeltaSeconds) override;
    TSharedRef<FJsonObject> BuildMetadata() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GreenMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AmberMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RedMaterial;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> ProcessTowerParts;
    FDarkFactoryProcessStatus ProcessStatus;
    bool bRed=false;
    TWeakObjectPtr<ADarkFactoryWeldingCell> SourceCell;
    struct FFixture {
        FString Id;
        FVector Pivot=FVector::ZeroVector,ActuatorBase=FVector::ZeroVector,PadPosition=FVector::ZeroVector;
        double Side=1,BodySideX=0,LengthCm=42,SurfaceGapCm=2;
        bool bSurfaceBound=false;
        FString ContactMesh;
        FVector SurfaceContact=FVector::ZeroVector;
        TWeakObjectPtr<UStaticMeshComponent> Arm,Pad,Rod,Toggle;
    };
    struct FBeamBox {
        FString Name;
        bool bEnabled=false;
        FTransform WorldTransform=FTransform::Identity;
        FVector StartLocal=FVector::ZeroVector,EndLocal=FVector::ZeroVector,HalfExtent=FVector::ZeroVector;
    };
    struct FPhotoeye {
        FString Id;
        FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
        bool Occupied=false;
        TArray<FString> BlockingColliders;
        TArray<FBeamBox> TestedBoxes;
        TWeakObjectPtr<UMaterialInstanceDynamic> Indicator;
    };
    TArray<FFixture> Fixtures;
    TArray<FPhotoeye> Photoeyes;
    UStaticMeshComponent* AddPart(const FString& Name,UStaticMesh* Shape,UMaterialInterface* Material,
        const FVector& Position,const FVector& Size,const FRotator& Rotation=FRotator::ZeroRotator,bool Shadow=true);
    UMaterialInstanceDynamic* MakePaint(UMaterialInterface* Parent,const FLinearColor& Color,float Roughness);
    UMaterialInstanceDynamic* MakeIndicator(UMaterialInterface* Parent,const FLinearColor& Color);
    void SetSegment(UStaticMeshComponent* Part,const FVector& A,const FVector& B,double DiameterCm);
    bool bInitialized=false,bEnabled=true,bGreen=false,bAmber=false,bFinite=true,bRandomized=true;
    double AppliedSeconds=0,CycleSeconds=0,CycleDuration=21,Closure=0,ArmAngleDegrees=75;
    double OpenStart=15.8,OpenEnd=16.2,DepartureStart=16.3,FirstApproach=3.7,LatestWithdrawal=15.8;
    double BodyMinX=0,BodyMaxX=0;
};
