#include "DarkFactorySensorReadback.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "TickableObjectRenderThread.h"

#include <atomic>

namespace
{
constexpr int32 ReadbackWidth = 448;
constexpr int32 ReadbackHeight = 256;
}

/** The CPU mailbox has one producer (render) and one consumer (game). */
struct FDarkFactorySensorReadbackState
{
    std::atomic<bool> Busy{false};
    std::atomic<bool> Ready{false};
    std::atomic<bool> Canceled{false};
    std::atomic<bool> Abandoned{false};

    // Render thread writes these before Ready.release. Game thread takes them
    // only after Ready.acquire, then permits the next producer via Enqueue().
    TArray<FColor> Pixels;
    uint64 CaptureId = 0;

    void Complete_RenderThread(TArray<FColor>&& Result, uint64 Id)
    {
        check(IsInRenderingThread());
        Pixels = MoveTemp(Result);
        CaptureId = Id;
        Ready.store(true, std::memory_order_release);
    }
};

namespace
{
/**
 * This job self-retires on the render thread, even if the game-side owner stops
 * polling or is destroyed. Retaining the readback until IsReady is essential:
 * FRHICommandWriteGPUFence contains a raw fence pointer until RHI execution.
 * Deleting a canceled readback immediately can race that pending command.
 */
class FDarkFactorySensorReadbackJob final : public FTickableObjectRenderThread
{
public:
    FDarkFactorySensorReadbackJob(
        TSharedRef<FDarkFactorySensorReadbackState, ESPMode::ThreadSafe> InState,
        uint64 InCaptureId, FRHICommandListImmediate& RHICmdList,
        FRHITexture* Texture)
        : FTickableObjectRenderThread(true, true)
        , State(MoveTemp(InState))
        , CaptureId(InCaptureId)
        , Readback(MakeUnique<FRHIGPUTextureReadback>(TEXT("DarkFactoryFixedSensor")))
    {
        check(IsInRenderingThread());
        RHICmdList.Transition(FRHITransitionInfo(Texture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
        Readback->EnqueueCopy(RHICmdList, Texture);
        RHICmdList.Transition(FRHITransitionInfo(Texture, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
    }

    virtual ~FDarkFactorySensorReadbackJob() override
    {
        check(IsInRenderingThread());
        Readback.Reset();
    }

    virtual bool IsTickable() const override { return true; }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FDarkFactorySensorReadbackJob, STATGROUP_Tickables);
    }
    virtual bool NeedsRenderingResumedForRenderingThreadTick() const override { return true; }

    virtual void Tick(FRHICommandListImmediate& RHICmdList, float DeltaTime) override
    {
        check(IsInRenderingThread());
        if (!Readback->IsReady()) return;

        TArray<FColor> Result;
        if (!State->Canceled.load(std::memory_order_acquire)
            && !State->Abandoned.load(std::memory_order_acquire))
        {
            int32 RowPitchInPixels = 0;
            int32 BufferHeight = 0;
            const FColor* Source = static_cast<const FColor*>(Readback->Lock(RowPitchInPixels, &BufferHeight));
            if (Source)
            {
                if (RowPitchInPixels >= ReadbackWidth && BufferHeight >= ReadbackHeight)
                {
                    Result.SetNumUninitialized(ReadbackWidth * ReadbackHeight);
                    // PF_B8G8R8A8 has FColor's native BGRA layout on this Windows
                    // target. UE's ReadPixels surface conversion for this format
                    // is also a row memcpy: no gamma or range transform is added.
                    for (int32 Y = 0; Y < ReadbackHeight; ++Y)
                    {
                        FMemory::Memcpy(Result.GetData() + Y * ReadbackWidth,
                            Source + static_cast<SIZE_T>(Y) * RowPitchInPixels,
                            ReadbackWidth * sizeof(FColor));
                    }
                }
                Readback->Unlock();
            }
            // A null mapping or invalid pitch becomes an empty failed result.
            // Do not call Unlock after a null mapping (e.g. device removal).
        }

        State->Complete_RenderThread(MoveTemp(Result), CaptureId);
        // TickRenderingTickables does not access the object again after Tick.
        // Its base destructor unregisters it; every RHI member is released here.
        delete this;
    }

private:
    TSharedRef<FDarkFactorySensorReadbackState, ESPMode::ThreadSafe> State;
    const uint64 CaptureId;
    TUniquePtr<FRHIGPUTextureReadback> Readback;
};
}

FDarkFactorySensorReadback::FDarkFactorySensorReadback()
    : State(MakeShared<FDarkFactorySensorReadbackState, ESPMode::ThreadSafe>())
{
    check(IsInGameThread());
}

FDarkFactorySensorReadback::~FDarkFactorySensorReadback()
{
    check(IsInGameThread());
    State->Abandoned.store(true, std::memory_order_release);
    State->Canceled.store(true, std::memory_order_release);
    // No wait, flush, captured UObject or render-resource deletion on this thread.
    // The queued copy/job retains shared state until GPU completion.
    State.Reset();
}

bool FDarkFactorySensorReadback::IsBusy() const
{
    check(IsInGameThread());
    return State->Busy.load(std::memory_order_acquire);
}

bool FDarkFactorySensorReadback::Enqueue(UTextureRenderTarget2D* Target, uint64 CaptureId)
{
    check(IsInGameThread());
    if (!Target || IsBusy() || Target->SizeX != ReadbackWidth || Target->SizeY != ReadbackHeight
        || Target->GetFormat() != PF_B8G8R8A8)
    {
        return false;
    }
    FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
    if (!Resource) return false;

    State->Canceled.store(false, std::memory_order_release);
    State->Ready.store(false, std::memory_order_release);
    State->Busy.store(true, std::memory_order_release);
    TSharedRef<FDarkFactorySensorReadbackState, ESPMode::ThreadSafe> SharedState = State.ToSharedRef();
    ENQUEUE_RENDER_COMMAND(DarkFactoryEnqueueSensorReadback)(
        [SharedState, Resource, CaptureId](FRHICommandListImmediate& RHICmdList)
        {
            // Only this first command touches Resource. UE queues its eventual
            // render-resource release after this command; the job only owns RHI
            // staging/fence objects and never retains Resource or its UObject.
            if (SharedState->Canceled.load(std::memory_order_acquire)
                || SharedState->Abandoned.load(std::memory_order_acquire))
            {
                SharedState->Complete_RenderThread(TArray<FColor>(), CaptureId);
                return;
            }
            FRHITexture* Texture = Resource->GetRenderTargetTexture();
            if (!Texture || Texture->IsMultisampled() || Texture->GetFormat() != PF_B8G8R8A8
                || Texture->GetDesc().Extent != FIntPoint(ReadbackWidth, ReadbackHeight))
            {
                SharedState->Complete_RenderThread(TArray<FColor>(), CaptureId);
                return;
            }
            // One self-owned job per busy mailbox; no per-frame queue growth.
            new FDarkFactorySensorReadbackJob(SharedState, CaptureId, RHICmdList, Texture);
        });
    return true;
}

bool FDarkFactorySensorReadback::Poll(TArray<FColor>& OutPixels, uint64& OutCaptureId)
{
    check(IsInGameThread());
    if (!State->Busy.load(std::memory_order_acquire)
        || !State->Ready.load(std::memory_order_acquire))
    {
        return false;
    }
    OutCaptureId = State->CaptureId;
    if (State->Canceled.load(std::memory_order_acquire))
    {
        State->Pixels.Reset();
        OutPixels.Reset();
    }
    else OutPixels = MoveTemp(State->Pixels);
    State->Ready.store(false, std::memory_order_release);
    State->Busy.store(false, std::memory_order_release);
    return true;
}

void FDarkFactorySensorReadback::Cancel()
{
    check(IsInGameThread());
    if (IsBusy()) State->Canceled.store(true, std::memory_order_release);
}
