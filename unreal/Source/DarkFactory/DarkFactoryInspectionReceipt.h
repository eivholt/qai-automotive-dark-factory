#pragma once
#include "CoreMinimal.h"
#include "DarkFactoryInspectionReceipt.generated.h"
class UTexture2D;

/** Observer-only receipt of an actual HTTP request. Never an inference input. */
USTRUCT()
struct FDarkFactoryInspectionReceipt {
    GENERATED_BODY()
    FString Id,Body,Stage,Decision=TEXT("A"),Action=TEXT("Request pending"),Backend;
    double CaptureMs=0,Confidence=-1,LabelMass=-1;
    float LatencyMs=0;
    bool bValid=false,bComplete=false,bProbabilityBound=false;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> Images;
};
