#pragma once
#include "CoreMinimal.h"
class UStaticMesh;
class UMaterialInterface;
// Private source-derived mesh. Builds once; all subsequent rendering is static GPU work.
UStaticMesh* MakeDarkFactoryPanelFrame(UObject* Outer,const FString& Path,
    UMaterialInterface* Material,FVector& Origin);
