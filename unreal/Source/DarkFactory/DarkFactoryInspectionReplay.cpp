#include "DarkFactoryProcessController.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"

void ADarkFactoryProcessController::RetainInspectionReceipt() {
    // At most eight requests / 24 textures (~10.5 MiB RGBA), decoded only once.
    if (!DarkFactoryEnhancements::Enabled() || BatchImages.Num()!=3) return;
    FDarkFactoryInspectionReceipt Receipt;
    Receipt.Id=ActiveRequestId; Receipt.Body=CaptureBodyId; Receipt.Stage=CaptureStage; Receipt.CaptureMs=CapturedAtMs;
    for (const auto& PNG:BatchImages) {
        auto* Image=FImageUtils::ImportBufferAsTexture2D(PNG);
        if (!Image || Image->GetSizeX()!=448 || Image->GetSizeY()!=256) return;
        Image->Filter=TF_Bilinear;
        Receipt.Images.Add(Image);
    }
    if (InspectionReceipts.Num()==8) InspectionReceipts.RemoveAt(0);
    InspectionReceipts.Add(MoveTemp(Receipt));
}
void ADarkFactoryProcessController::CompleteInspectionReceipt(const FString& Id,bool Valid,const FString& Code,float Latency,double Confidence,double Mass,bool Bound) {
    for (auto& Receipt:InspectionReceipts) if (Receipt.Id==Id) {
        Receipt.bComplete=true; Receipt.bValid=Valid; Receipt.Decision=Valid ? Code:TEXT("A");
        Receipt.Confidence=Valid ? Confidence:-1; Receipt.LabelMass=Valid ? Mass:-1; Receipt.bProbabilityBound=Bound;
        Receipt.LatencyMs=Latency; Receipt.Backend=State.Backend;
        // State after ApplyDecision records the actual resulting action/hold.
        Receipt.Action=State.Message;
        return;
    }
}
const FDarkFactoryInspectionReceipt* ADarkFactoryProcessController::GetInspectionReceipt() const {
    if (!ReplaySelection.IsEmpty()) for (const auto& Receipt:InspectionReceipts) if (Receipt.Id==ReplaySelection) return &Receipt;
    return InspectionReceipts.IsEmpty() ? nullptr:&InspectionReceipts.Last();
}
void ADarkFactoryProcessController::StepInspectionReplay(int32 Direction) {
    if (InspectionReceipts.IsEmpty()) return;
    bInspectionReplay=true;
    int32 Index=InspectionReceipts.Num()-1;
    for (int32 I=0;I<InspectionReceipts.Num();++I) if (InspectionReceipts[I].Id==ReplaySelection) Index=I;
    ReplaySelection=InspectionReceipts[FMath::Clamp(Index+Direction,0,InspectionReceipts.Num()-1)].Id;
}
