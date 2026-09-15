#pragma once
#include "CoreMinimal.h"
class USceneCaptureComponent2D;

// The observer may use temporal upscaling and indirect lighting. Scientific
// capture targets retain their independently specified raster/PNG contract.
namespace DarkFactoryGraphics {
    DARKFACTORY_API void ConfigureSensorCapture(USceneCaptureComponent2D* Capture);
    DARKFACTORY_API bool HardwareRayTracingSupported();
    DARKFACTORY_API bool SoftwareLumenSupported();
}
