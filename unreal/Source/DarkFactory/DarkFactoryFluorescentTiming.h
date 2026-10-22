#pragma once
#include "CoreMinimal.h"

// Shared, pure startup curve for visible banks and the recorded ballast cue.
// Authored cold-start strikes, not a mains-frequency/electrical simulation.
namespace DarkFactoryFluorescent {
inline constexpr float DurationSeconds=1.5f;
inline constexpr float Delays[]={0.f,.12f,.24f,.06f,.18f,.30f};
inline float BankGain(float Elapsed,int32 Bank) {
    const float T=Elapsed-Delays[Bank];
    if (T<.055f) return 0.f;
    if (T<.105f) return .18f;
    if (T<.175f) return 0.f;
    if (T<.285f) return .85f;
    if (T<.36f) return .04f;
    if (T<.47f) return 1.f;
    if (T<.56f) return .12f;
    if (T<.67f) return .92f;
    if (T<.75f) return .30f;
    const float S=FMath::Clamp((T-.75f)/.45f,0.f,1.f);
    return .72f+.28f*S*S*(3.f-2.f*S);
}
}
