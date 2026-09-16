#pragma once
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace DarkFactoryEnhancements {
inline bool Enabled() { return FParse::Param(FCommandLine::Get(),TEXT("DFEnhancements")); }
inline double MotionEase(double T) {
    T=FMath::Clamp(T,0.,1.);
    return T*T*T*(10.+T*(-15.+6.*T)); // Zero velocity and acceleration at each endpoint.
}
/** Recovery actuates cooling only. It cannot accept a gate or release a body. */
struct FCoolingRecovery {
    int32 ConsecutiveRejects=0;
    bool Observe(bool Valid,const FString& Code,bool IsSealant) {
        if (!Valid || !IsSealant || Code!=TEXT("R")) { ConsecutiveRejects=0; return false; }
        return ++ConsecutiveRejects==2;
    }
    void Reset() { ConsecutiveRejects=0; }
};
}
