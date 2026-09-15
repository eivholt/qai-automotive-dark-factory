#include "DarkFactoryThermalRadiometry.h"

#include <cmath>

namespace
{
    constexpr double MinTemperatureC = -100.0;
    constexpr double MaxTemperatureC = 2000.0;
    constexpr double DefaultTemperatureC = 20.0;
    constexpr double KelvinOffset = 273.15;
    constexpr double PlanckConstant = 6.62607015e-34; // J s, exact SI definition.
    constexpr double SpeedOfLight = 299792458.0;     // m / s, exact SI definition.
    constexpr double BoltzmannConstant = 1.380649e-23; // J / K, exact SI definition.
    constexpr double MinWavelengthM = 8.0e-6;
    constexpr double MaxWavelengthM = 14.0e-6;
    constexpr int IntegrationIntervals = 64; // Simpson integration requires even N.
    constexpr int InversionIterations = 24;

    double SafeTemperatureC(double Value)
    {
        if (!std::isfinite(Value))
        {
            return DefaultTemperatureC;
        }
        return Value < MinTemperatureC ? MinTemperatureC :
            (Value > MaxTemperatureC ? MaxTemperatureC : Value);
    }

    double SafeEmissivity(double Value)
    {
        if (!std::isfinite(Value))
        {
            return 1.0;
        }
        return Value < 0.0 ? 0.0 : (Value > 1.0 ? 1.0 : Value);
    }

    double SpectralRadiance(double WavelengthM, double TemperatureK)
    {
        const double Lambda2 = WavelengthM * WavelengthM;
        const double Lambda5 = Lambda2 * Lambda2 * WavelengthM;
        const double Exponent = PlanckConstant * SpeedOfLight /
            (WavelengthM * BoltzmannConstant * TemperatureK);
        return (2.0 * PlanckConstant * SpeedOfLight * SpeedOfLight) /
            (Lambda5 * std::expm1(Exponent));
    }
}

namespace DarkFactoryThermal
{
    double BandRadiance(double TemperatureC)
    {
        const double TemperatureK = SafeTemperatureC(TemperatureC) + KelvinOffset;
        const double StepM = (MaxWavelengthM - MinWavelengthM) / IntegrationIntervals;
        double Sum = SpectralRadiance(MinWavelengthM, TemperatureK) +
            SpectralRadiance(MaxWavelengthM, TemperatureK);
        for (int Index = 1; Index < IntegrationIntervals; ++Index)
        {
            const double Weight = (Index % 2 == 0) ? 2.0 : 4.0;
            Sum += Weight * SpectralRadiance(MinWavelengthM + Index * StepM, TemperatureK);
        }
        return Sum * StepM / 3.0;
    }

    double ApparentTemperatureC(double SurfaceC, double Emissivity, double ReflectedC)
    {
        SurfaceC = SafeTemperatureC(SurfaceC);
        ReflectedC = SafeTemperatureC(ReflectedC);
        Emissivity = SafeEmissivity(Emissivity);
        if (Emissivity == 1.0 || SurfaceC == ReflectedC)
        {
            return SurfaceC;
        }
        if (Emissivity == 0.0)
        {
            return ReflectedC;
        }

        const double TargetRadiance = Emissivity * BandRadiance(SurfaceC) +
            (1.0 - Emissivity) * BandRadiance(ReflectedC);
        double LowC = SurfaceC < ReflectedC ? SurfaceC : ReflectedC;
        double HighC = SurfaceC > ReflectedC ? SurfaceC : ReflectedC;
        for (int Iteration = 0; Iteration < InversionIterations; ++Iteration)
        {
            const double MidC = 0.5 * (LowC + HighC);
            if (BandRadiance(MidC) < TargetRadiance)
            {
                LowC = MidC;
            }
            else
            {
                HighC = MidC;
            }
        }
        return 0.5 * (LowC + HighC);
    }

    FApparentTemperatureFit MakeApparentTemperatureFit(double MinimumSurfaceC,
        double MaximumSurfaceC, double Emissivity, double ReflectedC)
    {
        MinimumSurfaceC = SafeTemperatureC(MinimumSurfaceC);
        MaximumSurfaceC = SafeTemperatureC(MaximumSurfaceC);
        if (MaximumSurfaceC < MinimumSurfaceC)
        {
            const double Swap = MinimumSurfaceC;
            MinimumSurfaceC = MaximumSurfaceC;
            MaximumSurfaceC = Swap;
        }
        FApparentTemperatureFit Fit;
        Fit.MinC = MinimumSurfaceC;
        const double Span = MaximumSurfaceC - MinimumSurfaceC;
        Fit.InvSpan = Span > 0.00001 ? 1.0 / Span : 0.0;
        Fit.LowC = ApparentTemperatureC(MinimumSurfaceC, Emissivity, ReflectedC);
        Fit.MidC = ApparentTemperatureC(0.5 * (MinimumSurfaceC + MaximumSurfaceC), Emissivity, ReflectedC);
        Fit.HighC = ApparentTemperatureC(MaximumSurfaceC, Emissivity, ReflectedC);
        return Fit;
    }

    double EvaluateApparentTemperatureFit(const FApparentTemperatureFit& Fit, double SurfaceC)
    {
        const double Coordinate = Fit.InvSpan > 0.0 ?
            (SafeTemperatureC(SurfaceC) - Fit.MinC) * Fit.InvSpan : 0.0;
        const double H = Coordinate < 0.0 ? 0.0 : (Coordinate > 1.0 ? 1.0 : Coordinate);
        const double A = 2.0 * (Fit.HighC + Fit.LowC - 2.0 * Fit.MidC);
        const double B = 4.0 * Fit.MidC - 3.0 * Fit.LowC - Fit.HighC;
        const double Result = Fit.LowC + B * H + A * H * H;
        return Result < Fit.LowC ? Fit.LowC : (Result > Fit.HighC ? Fit.HighC : Result);
    }

    bool SelfTest()
    {
        // Independent fine-midpoint quadrature reference for a 300 K blackbody
        // in the flat 8-14 um band, using the exact SI constants above.
        if (std::abs(BandRadiance(26.85) - 54.933461377128) > 0.0001)
        {
            return false;
        }

        double Previous = BandRadiance(MinTemperatureC);
        if (!std::isfinite(Previous) || Previous <= 0.0)
        {
            return false;
        }
        for (double TemperatureC = -50.0; TemperatureC <= MaxTemperatureC; TemperatureC += 50.0)
        {
            const double Current = BandRadiance(TemperatureC);
            if (!std::isfinite(Current) || Current <= Previous)
            {
                return false;
            }
            Previous = Current;
        }

        // High-temperature references use independent fine quadrature. Extending
        // the supported domain must not create a plateau at the former 500 C cap.
        if (std::abs(BandRadiance(1600.0) - 5396.372976202) > 0.001 ||
            std::abs(BandRadiance(2000.0) - 7075.161979379) > 0.001 ||
            BandRadiance(600.0) <= BandRadiance(500.0) ||
            ApparentTemperatureC(1600.0, 1.0, 20.0) != 1600.0)
        {
            return false;
        }
        const double HotApparentC = ApparentTemperatureC(1600.0, 0.74, 20.0);
        const double HotMixedRadiance = 0.74 * BandRadiance(1600.0) + 0.26 * BandRadiance(20.0);
        if (HotApparentC <= 1000.0 || HotApparentC >= 1600.0 ||
            std::abs(BandRadiance(HotApparentC) - HotMixedRadiance) > 0.001)
        {
            return false;
        }

        const double MixedC = ApparentTemperatureC(120.0, 0.35, 20.0);
        const double ReverseC = ApparentTemperatureC(20.0, 0.65, 120.0);
        const double ExpectedRadiance = 0.35 * BandRadiance(120.0) + 0.65 * BandRadiance(20.0);
        return MixedC > 20.0 && MixedC < 120.0 &&
            std::abs(MixedC - ReverseC) < 0.0001 &&
            std::abs(BandRadiance(MixedC) - ExpectedRadiance) < 0.0001 &&
            ApparentTemperatureC(120.0, 1.0, 20.0) == 120.0 &&
            ApparentTemperatureC(120.0, 0.0, 20.0) == 20.0 &&
            ApparentTemperatureC(37.0, 0.35, 37.0) == 37.0 &&
            ApparentTemperatureC(120.0, -1.0, 20.0) == 20.0 &&
            ApparentTemperatureC(120.0, 2.0, 20.0) == 120.0 &&
            BandRadiance(-200.0) == BandRadiance(MinTemperatureC) &&
            BandRadiance(2001.0) == BandRadiance(MaxTemperatureC) &&
            BandRadiance(std::nan("")) == BandRadiance(DefaultTemperatureC) &&
            ApparentTemperatureC(120.0, std::nan(""), 20.0) == 120.0;
    }
}
