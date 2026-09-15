#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DarkFactoryEditorPreview.generated.h"

UENUM(BlueprintType)
enum class EDarkFactoryEditorPose : uint8 { AwaitingPanel, SealantApplied, PanelPlaced };

/** Rebuildable editor-only scene, using the same imported geometry and pose builders as Play. */
UCLASS()
class DARKFACTORY_API ADarkFactoryEditorPreview : public AActor {
    GENERATED_BODY()
public:
    ADarkFactoryEditorPreview();
    static FName GeneratedTag() { return TEXT("DarkFactory.GeneratedEditorPreview"); }
    UPROPERTY(EditAnywhere,Category="Dark Factory Preview") EDarkFactoryEditorPose PreviewPose=EDarkFactoryEditorPose::AwaitingPanel;
    UPROPERTY(VisibleAnywhere,Category="Dark Factory Preview") FString PreviewStatus;
    UFUNCTION(BlueprintCallable,Category="Dark Factory Preview") bool RefreshPreview();
    UFUNCTION(CallInEditor,Category="Dark Factory Preview",meta=(DisplayName="Refresh Preview")) void RefreshPreviewInEditor();
    UFUNCTION(BlueprintCallable,Category="Dark Factory Preview") FString GetPreviewReport() const;
    UFUNCTION(BlueprintCallable,Category="Dark Factory Preview") void InitializeEditorView(FVector Location,FRotator Rotation);
    virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
    virtual void PostRegisterAllComponents() override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void Destroyed() override;
#endif
private:
    UPROPERTY(Transient,DuplicateTransient) TArray<TObjectPtr<AActor>> PreviewActors;
    bool bRefreshPending=true;
    bool bRefreshQueued=false;
    void QueueRefresh();
    void ClearPreview();
};
