#include "DarkFactoryGameMode.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace {
float EnlargedTextScale(const UFont* Font,float Scale) {
    const float BaseSize=Font->LegacyFontSize>0 ? Font->LegacyFontSize:Font->GetMaxCharHeight();
    return Scale+1.f/FMath::Max(1.f,BaseSize);
}
}

void ADarkFactoryHUD::DrawHUDText(const FString& Text,FLinearColor Color,float X,float Y,UFont* Font,float Scale,bool ScalePosition) {
    if (ScalePosition) { X*=Scale; Y*=Scale; }
    AHUD::DrawText(Text,Color,X,Y,Font,EnlargedTextScale(Font,Scale),false);
}

void ADarkFactoryHUD::MeasureHUDText(const FString& Text,float& Width,float& Height,UFont* Font,float Scale) const {
    AHUD::GetTextSize(Text,Width,Height,Font,EnlargedTextScale(Font,Scale));
}

void ADarkFactoryHUD::DrawHUDControls(const FString& Text,float X,float Y,float Scale) {
    auto* Font=GEngine->GetSmallFont();
    const FLinearColor KeyColor(1.f,.85f,.10f);
    int32 Start=0;
    bool Key=false;
    for (int32 I=0;I<=Text.Len();++I) {
        if (I<Text.Len() && Text[I]!=TEXT('[') && Text[I]!=TEXT(']')) continue;
        const FString Span=Text.Mid(Start,I-Start);
        if (!Span.IsEmpty()) {
            DrawHUDText(Span,Key ? KeyColor:FLinearColor::White,X,Y,Font,Scale);
            float Width=0,Height=0; MeasureHUDText(Span,Width,Height,Font,Scale); X+=Width;
        }
        Key=I<Text.Len() && Text[I]==TEXT('[');
        Start=I+1;
    }
}
