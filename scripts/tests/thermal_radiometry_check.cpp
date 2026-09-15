// Standalone checks against the same radiometry source compiled by Unreal.
// Compile with g++ -std=c++17 -O2 -Wall -Wextra -pedantic, passing this file
// and unreal/Source/DarkFactory/DarkFactoryThermalRadiometry.cpp,
// then -o /tmp/radiometry-check.
// /tmp/radiometry-check
#include "../../unreal/Source/DarkFactory/DarkFactoryThermalRadiometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef DARKFACTORY_COMPARE_LEGACY_RADIOMETRY
namespace DarkFactoryThermalLegacy {
    double BandRadiance(double);
    double ApparentTemperatureC(double, double, double);
}
#endif

namespace {
// Independent midpoint integration at 8192 wavelength samples; production uses
// 64-interval Simpson integration. No production clamp or inversion is reused.
double ReferenceBand(double TemperatureC) {
    constexpr int Samples = 8192;
    constexpr double H=6.62607015e-34, C=299792458., K=1.380649e-23;
    constexpr double Step=6.e-6/Samples;
    double Sum=0.;
    for (int I=0; I<Samples; ++I) {
        const double Wavelength=8.e-6+(I+.5)*Step;
        Sum += 2.*H*C*C/(std::pow(Wavelength,5)*std::expm1(H*C/(Wavelength*K*(TemperatureC+273.15))));
    }
    return Sum*Step;
}
}

int main() {
    using namespace DarkFactoryThermal;
    int Checks=0;
    std::vector<std::string> Failed;
    auto Check=[&](const std::string& Name,bool Passed) { ++Checks; if (!Passed) Failed.push_back(Name); };
    double MaxQuadratureRelativeError=0.,MaxMixedRelativeError=0.,MaxGateFitError=0.,MaxSensorFitError=0.,MaxPeak1600SensorFitError=0.;
    Check("native_self_test",SelfTest());
    for (double T : {-100.,20.,42.,45.,48.,52.,55.,58.,120.,500.,800.,1400.,1600.,2000.}) {
        const double Reference=ReferenceBand(T);
        const double RelativeError=std::abs(BandRadiance(T)-Reference)/Reference;
        MaxQuadratureRelativeError=std::max(MaxQuadratureRelativeError,RelativeError);
        Check("independent_band_quadrature_"+std::to_string(T),RelativeError<1.e-7);
    }
    Check("high_range_has_no_500C_plateau",BandRadiance(600.)>BandRadiance(500.));
    Check("high_range_has_no_800C_plateau",BandRadiance(1600.)>BandRadiance(800.));
    Check("upper_bound",BandRadiance(2500.)==BandRadiance(2000.));
    Check("lower_bound",BandRadiance(-200.)==BandRadiance(-100.));
    Check("nan_fallback",BandRadiance(std::numeric_limits<double>::quiet_NaN())==BandRadiance(20.));
    Check("infinity_fallback",BandRadiance(std::numeric_limits<double>::infinity())==BandRadiance(20.));
    for (double E : {0.,.01,.2,.35,.74,.94,1.}) {
        double Previous=-100.;
        for (double T : {-100.,0.,20.,45.,55.,120.,500.,501.,800.,1400.,1600.,2000.}) {
            const double Apparent=ApparentTemperatureC(T,E,20.);
            const double Reference=E*ReferenceBand(T)+(1.-E)*ReferenceBand(20.);
            Check("finite_monotonic_graybody",std::isfinite(Apparent) && Apparent>=Previous &&
                Apparent>=std::min(T,20.) && Apparent<=std::max(T,20.));
            const double MixedRelativeError=std::abs(ReferenceBand(Apparent)-Reference)/Reference;
            MaxMixedRelativeError=std::max(MaxMixedRelativeError,MixedRelativeError);
            // Production keeps 24 inversion steps for identical low-range output.
            // At a 1980 C span its midpoint uncertainty is at most 0.000059 C.
            const double TemperatureHalfStep=std::abs(T-20.)/33554432.;
            const double InversionRadianceBudget=std::max(
                std::abs(ReferenceBand(Apparent+TemperatureHalfStep)-ReferenceBand(Apparent)),
                std::abs(ReferenceBand(Apparent-TemperatureHalfStep)-ReferenceBand(Apparent)));
            Check("independent_mixed_radiance",std::abs(ReferenceBand(Apparent)-Reference)<=
                InversionRadianceBudget+Reference*1.e-7);
            if (E==0.) Check("zero_emissivity_is_reflected",Apparent==20.);
            if (E==1.) Check("blackbody_matches_surface",Apparent==T);
            Previous=Apparent;
        }
    }
    Check("1600C_surface_is_not_1600C_apparent",std::abs(ApparentTemperatureC(1600.,.74,20.)-1261.81442797184)<.001);
    const auto Constant=MakeApparentTemperatureFit(55.,55.,.74,20.);
    Check("constant_fit",Constant.InvSpan==0. && EvaluateApparentTemperatureFit(Constant,1600.)==Constant.LowC);
    const auto Low=MakeApparentTemperatureFit(20.,100.,.74,20.);
    Check("fit_clamps_low",EvaluateApparentTemperatureFit(Low,-100.)==Low.LowC);
    Check("fit_clamps_high",EvaluateApparentTemperatureFit(Low,2000.)==Low.HighC);
    const auto Reverse=MakeApparentTemperatureFit(100.,20.,.74,20.);
    Check("fit_reversed_interval",Reverse.MinC==Low.MinC && Reverse.InvSpan==Low.InvSpan &&
        Reverse.LowC==Low.LowC && Reverse.MidC==Low.MidC && Reverse.HighC==Low.HighC);
    const auto LegacyWide=MakeApparentTemperatureFit(20.,1600.,.74,20.);
    Check("wide_fit_exposes_cool_region_distortion",std::abs(EvaluateApparentTemperatureFit(LegacyWide,55.)-
        ApparentTemperatureC(55.,.74,20.))>1.8);
    for (double Peak : {501.,600.,800.,1000.,1200.,1400.,1600.,1800.,2000.}) {
        const auto High=MakeApparentTemperatureFit(100.,Peak,.74,20.);
        Check("piecewise_fit_continuity",Low.HighC==High.LowC);
        double Previous=Low.LowC;
        for (double T=20.; T<=Peak; T+=.5) {
            const double Exact=ApparentTemperatureC(T,.74,20.);
            const double Fitted=EvaluateApparentTemperatureFit(T<=100. ? Low:High,T);
            Check("fit_finite_and_monotonic",std::isfinite(Fitted) && Fitted>=Previous-1.e-9);
            const double Error=std::abs(Fitted-Exact);
            if (Exact>=20. && Exact<=120.) {
                MaxSensorFitError=std::max(MaxSensorFitError,Error);
                if (Peak==1600.) MaxPeak1600SensorFitError=std::max(MaxPeak1600SensorFitError,Error);
            }
            if (Exact>=42. && Exact<=58.) MaxGateFitError=std::max(MaxGateFitError,Error);
            Previous=Fitted;
        }
    }
    Check("gate_band_piecewise_error_below_point06C",MaxGateFitError<.06);
    Check("full_sensor_band_piecewise_error_below_halfC",MaxSensorFitError<.5);
    Check("1600C_sensor_band_piecewise_error_below_point1C",MaxPeak1600SensorFitError<.1);
#ifdef DARKFACTORY_COMPARE_LEGACY_RADIOMETRY
    // Optional development audit links the saved pre-change source under its
    // renamed namespace; no retained data or second implementation is required.
    for (double T=-100.; T<=500.; T+=2.5) {
        Check("legacy_band_bit_exact",BandRadiance(T)==DarkFactoryThermalLegacy::BandRadiance(T));
        for (double E : {0.,.01,.2,.35,.74,.94,1.})
            for (double Ambient : {-20.,20.,40.})
                Check("legacy_apparent_bit_exact",ApparentTemperatureC(T,E,Ambient)==
                    DarkFactoryThermalLegacy::ApparentTemperatureC(T,E,Ambient));
    }
#endif
    std::cout<<std::setprecision(12)<<"{\"passed\":"<<(Failed.empty()?"true":"false")
        <<",\"checks\":"<<Checks<<",\"max_quadrature_relative_error\":"<<MaxQuadratureRelativeError
        <<",\"max_mixed_radiance_relative_error\":"<<MaxMixedRelativeError
        <<",\"max_gate_band_fit_error_c\":"<<MaxGateFitError
        <<",\"max_sensor_band_fit_error_c\":"<<MaxSensorFitError
        <<",\"peak1600_max_sensor_band_fit_error_c\":"<<MaxPeak1600SensorFitError<<",\"failed\":[";
    for (std::size_t I=0; I<Failed.size(); ++I) std::cout<<(I?",":"")<<'"'<<Failed[I]<<'"';
    std::cout<<"]}\n";
    return Failed.empty()?0:1;
}
