#pragma once

#include "CoreMinimal.h"

class UTextureRenderTarget2D;
struct FDarkFactorySensorReadbackState;

/**
 * One nonblocking 448x256 BGRA8 sensor readback. All public methods are game-thread only.
 *
 * Call CaptureScene(), then Enqueue() while !IsBusy(). Poll() transfers a completed
 * tightly packed FColor array and its caller-supplied capture ID. A completed
 * failure/cancellation returns true with an empty array; false means not ready.
 * Keep the render target referenced by a UPROPERTY and do not resize/reinitialize
 * it while a job is pending. Its UE render-resource release follows the queued copy.
 *
 * Cancel() invalidates the current result. It does not wait or free a pending GPU
 * fence early: Poll() consumes the eventual empty result before another Enqueue().
 * Destruction abandons the result, while its render-thread job independently
 * retains and retires GPU resources once ready. No UObject/World callback survives.
 */
class FDarkFactorySensorReadback final
{
public:
    FDarkFactorySensorReadback();
    ~FDarkFactorySensorReadback();

    FDarkFactorySensorReadback(const FDarkFactorySensorReadback&) = delete;
    FDarkFactorySensorReadback& operator=(const FDarkFactorySensorReadback&) = delete;

    bool Enqueue(UTextureRenderTarget2D* Target, uint64 CaptureId);
    bool Poll(TArray<FColor>& OutPixels, uint64& OutCaptureId);
    bool IsBusy() const;
    void Cancel();

private:
    TSharedPtr<FDarkFactorySensorReadbackState, ESPMode::ThreadSafe> State;
};
