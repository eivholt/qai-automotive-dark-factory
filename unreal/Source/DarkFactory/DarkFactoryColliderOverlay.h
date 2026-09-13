#pragma once
#include "CoreMinimal.h"
class UPrimitiveComponent;
// Observer HUD data only: never register scene primitives or alter collision.
struct FDarkFactoryColliderBox {
    TWeakObjectPtr<UPrimitiveComponent> Component;
    FBox LocalBounds=FBox(ForceInit);
    FTransform Transform=FTransform::Identity;
    int32 Instance=-1;
    uint8 Kind=0; // static, kinematic, configured dynamic body, query-only
    uint8 Geometry=0; // exact UBox, simple aggregate, complex mesh, fallback bounds
};
