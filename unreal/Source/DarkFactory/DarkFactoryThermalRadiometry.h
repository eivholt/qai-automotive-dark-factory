#pragma once

// CPU helpers for profile changes, not per-pixel shading. This is a flat-response
// 8-14 micrometre, opaque diffuse-surface approximation. It omits atmospheric
// attenuation/emission, optical transmission and reflection direction/geometry.
namespace DarkFactoryThermal
{
    // Band-integrated blackbody radiance in W / (m^2 sr), with temperature in C.
    // Temperatures are clamped to [-100, 500] C; non-finite inputs use 20 C.
    double BandRadiance(double TemperatureC);

    // Returns the blackbody-equivalent temperature of
    // epsilon * L(SurfaceC) + (1 - epsilon) * L(ReflectedC).
    // ReflectedC represents an effective uniform surrounding temperature.
    // Emissivity is clamped to [0, 1]; non-finite emissivity uses 1.
    // This apparent temperature is not necessarily the surface temperature.
    double ApparentTemperatureC(double SurfaceC, double Emissivity, double ReflectedC);

    // Small opt-in numerical/physical invariant check; caller owns reporting.
    bool SelfTest();
}
