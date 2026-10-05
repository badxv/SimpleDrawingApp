#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>

constexpr int kBrushTipSize = 64;

Gdiplus::Bitmap* TipFromSampleMask(const std::uint8_t* mask, int width, int height);
Gdiplus::Bitmap* TipFromFloatAlpha(const float* alpha, int size);
Gdiplus::Bitmap* NormalizeImportedTip(Gdiplus::Bitmap* source);

Gdiplus::Bitmap* MakeRoundTip(float hardness, float rxScale = 1.0f, float ryScale = 1.0f);
Gdiplus::Bitmap* MakeCharcoalTip();
Gdiplus::Bitmap* MakeConceptTip();
Gdiplus::Bitmap* MakePencilTip();
