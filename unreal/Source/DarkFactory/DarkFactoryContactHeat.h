#pragma once
#include <algorithm>
#include <cmath>

namespace DarkFactoryContactHeat {
// Analytic metal response to a prescribed exponentially cooling sealant source.
// This local reservoir proxy is not a whole-body energy-conserving solver.
inline double SurfaceC(double SourceAtContactC,double SourceTau,double Age,double MetalTau=.35) {
    if (Age<=0.) return 23.;
    const double M=std::exp(-Age/MetalTau), S=std::exp(-Age/SourceTau);
    const double Response=std::abs(SourceTau-MetalTau)<1.e-8 ? Age/MetalTau*M : SourceTau/(SourceTau-MetalTau)*(S-M);
    return std::clamp(20.+3.*M+(SourceAtContactC-20.)*Response,20.,std::max(23.,SourceAtContactC));
}
}
