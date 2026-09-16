#pragma once
#include "CoreMinimal.h"
class UStaticMesh;
class UMaterialInterface;
/** Rounded extruded bead, generated once; UV.x follows deposited arc length. */
UStaticMesh* MakeDarkFactorySealantMesh(UObject* Outer,UMaterialInterface* Material,
    const TArray<FVector>& Points,const TArray<FVector>& Normals,FVector& Origin,double& ArcLength,
    double WidthScale=1.,double GapStart=-1.,double GapEnd=-1.,double LateralOffset=0.);
