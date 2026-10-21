#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "AtelierRaii.h"

#include <cstdint>

constexpr int kBrushTipSize = 64;

Atelier::GdiplusBitmapPtr TipFromSampleMask(const std::uint8_t* mask, int width, int height);
Atelier::GdiplusBitmapPtr TipFromFloatAlpha(const float* alpha, int size);
Atelier::GdiplusBitmapPtr NormalizeImportedTip(Gdiplus::Bitmap* source);

Atelier::GdiplusBitmapPtr MakeRoundTip(float hardness, float rxScale = 1.0f, float ryScale = 1.0f);
Atelier::GdiplusBitmapPtr MakeCharcoalTip();
Atelier::GdiplusBitmapPtr MakeConceptTip();
Atelier::GdiplusBitmapPtr MakePencilTip();
