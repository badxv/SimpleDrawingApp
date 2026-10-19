#include "BrushTips.h"
#include "AbrComputed.h"
#include "AppMetrics.h"

#include <cmath>
#include <vector>

using namespace Gdiplus;

namespace {

void SetTipPixel(Bitmap* bmp, int x, int y, BYTE alpha) {
    if (!bmp || x < 0 || y < 0 || x >= static_cast<int>(bmp->GetWidth()) || y >= static_cast<int>(bmp->GetHeight())) {
        return;
    }
    bmp->SetPixel(x, y, Color(alpha, 255, 255, 255));
}

float RadialAlpha(int x, int y, int cx, int cy, int rx, int ry, float hardness) {
    const float dx = static_cast<float>(x - cx) / static_cast<float>(MaxInt(1, rx));
    const float dy = static_cast<float>(y - cy) / static_cast<float>(MaxInt(1, ry));
    const float d = sqrtf(dx * dx + dy * dy);
    if (d >= 1.0f) return 0.0f;
    const float h = hardness;
    if (d <= h) return 1.0f;
    return 1.0f - (d - h) / MaxFloat(0.001f, 1.0f - h);
}

} // namespace

Bitmap* TipFromFloatAlpha(const float* alpha, int size) {
    if (!alpha || size < 1) return nullptr;
    Bitmap* bmp = new Bitmap(size, size, PixelFormat32bppARGB);
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float a = alpha[y * size + x];
            const BYTE ba = static_cast<BYTE>(MaxInt(0, (a * 255.0f + 0.5f) > 255.0f ? 255 : static_cast<int>(a * 255.0f + 0.5f)));
            SetTipPixel(bmp, x, y, ba);
        }
    }
    return bmp;
}

Bitmap* MakeRoundTip(float hardness, float rxScale, float ryScale) {
    AbrComputedParams params = {};
    params.hardness = hardness;
    params.roundness = MaxFloat(0.05f, ryScale / MaxFloat(0.05f, rxScale));
    std::vector<std::uint8_t> mask;
    if (!RasterizeComputedMask(params, kBrushTipSize, mask)) return nullptr;
    return TipFromSampleMask(mask.data(), kBrushTipSize, kBrushTipSize);
}

Bitmap* MakeCharcoalTip() {
    float alpha[kBrushTipSize * kBrushTipSize];
    const int cx = kBrushTipSize / 2;
    const int cy = kBrushTipSize / 2;
    for (int y = 0; y < kBrushTipSize; ++y) {
        for (int x = 0; x < kBrushTipSize; ++x) {
            float base = RadialAlpha(x, y, cx, cy, kBrushTipSize / 2 - 2, kBrushTipSize / 2 - 2, 0.35f);
            const unsigned noise = (static_cast<unsigned>(x) * 73856093u) ^ (static_cast<unsigned>(y) * 19349663u);
            const float grain = 0.65f + 0.35f * (static_cast<float>(noise & 255) / 255.0f);
            alpha[y * kBrushTipSize + x] = base * grain;
        }
    }
    return TipFromFloatAlpha(alpha, kBrushTipSize);
}

Bitmap* MakeConceptTip() {
    float alpha[kBrushTipSize * kBrushTipSize];
    const int cx = kBrushTipSize / 2;
    const int cy = kBrushTipSize / 2;
    for (int y = 0; y < kBrushTipSize; ++y) {
        for (int x = 0; x < kBrushTipSize; ++x) {
            float base = RadialAlpha(x, y, cx, cy, kBrushTipSize / 2 - 1, kBrushTipSize / 2 - 1, 0.55f);
            const unsigned n = ((static_cast<unsigned>(x + y * 3)) * 2654435761u) & 255u;
            const float tex = 0.82f + 0.18f * (static_cast<float>(n) / 255.0f);
            alpha[y * kBrushTipSize + x] = base * tex;
        }
    }
    return TipFromFloatAlpha(alpha, kBrushTipSize);
}

Bitmap* MakePencilTip() {
    float alpha[kBrushTipSize * kBrushTipSize];
    const int cx = kBrushTipSize / 2;
    const int cy = kBrushTipSize / 2;
    for (int y = 0; y < kBrushTipSize; ++y) {
        for (int x = 0; x < kBrushTipSize; ++x) {
            float base = RadialAlpha(x, y, cx, cy, kBrushTipSize / 4, kBrushTipSize / 4, 0.75f);
            const unsigned n = (static_cast<unsigned>(x) * 92837111u) ^ (static_cast<unsigned>(y) * 689287499u);
            const float grain = 0.55f + 0.45f * (static_cast<float>(n & 255) / 255.0f);
            alpha[y * kBrushTipSize + x] = base * grain;
        }
    }
    return TipFromFloatAlpha(alpha, kBrushTipSize);
}

Bitmap* NormalizeImportedTip(Bitmap* source) {
    if (!source) return nullptr;
    const int w = static_cast<int>(source->GetWidth());
    const int h = static_cast<int>(source->GetHeight());
    if (w < 1 || h < 1) return nullptr;

    Bitmap* out = new Bitmap(kBrushTipSize, kBrushTipSize, PixelFormat32bppARGB);
    if (!out || out->GetLastStatus() != Ok) {
        delete out;
        return nullptr;
    }
    Graphics g(out);
    g.Clear(Color(0, 0, 0, 0));
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(source, Rect(0, 0, kBrushTipSize, kBrushTipSize));

    BitmapData data = {};
    Rect lock(0, 0, kBrushTipSize, kBrushTipSize);
    if (out->LockBits(&lock, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) {
        delete out;
        return nullptr;
    }
    auto* px = static_cast<BYTE*>(data.Scan0);
    for (int y = 0; y < kBrushTipSize; ++y) {
        BYTE* row = px + y * data.Stride;
        for (int x = 0; x < kBrushTipSize; ++x) {
            BYTE* p = row + x * 4;
            const BYTE b = p[0];
            const BYTE gch = p[1];
            const BYTE r = p[2];
            const BYTE a = p[3];
            const int lum = (r * 77 + gch * 150 + b * 29) >> 8;
            const BYTE alpha = static_cast<BYTE>((lum * a + 127) / 255);
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = alpha;
        }
    }
    out->UnlockBits(&data);
    return out;
}

Bitmap* TipFromSampleMask(const std::uint8_t* mask, int width, int height) {
    if (!mask || width < 1 || height < 1) return nullptr;
    Bitmap* bmp = new Bitmap(kBrushTipSize, kBrushTipSize, PixelFormat32bppARGB);
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }
    BitmapData data = {};
    Rect lock(0, 0, kBrushTipSize, kBrushTipSize);
    if (bmp->LockBits(&lock, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) {
        delete bmp;
        return nullptr;
    }
    auto* px = static_cast<BYTE*>(data.Scan0);
    for (int y = 0; y < kBrushTipSize; ++y) {
        BYTE* row = px + y * data.Stride;
        for (int x = 0; x < kBrushTipSize; ++x) {
            const int sx = x * width / kBrushTipSize;
            const int sy = y * height / kBrushTipSize;
            const BYTE lum = mask[sy * width + sx];
            BYTE* p = row + x * 4;
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = lum;
        }
    }
    bmp->UnlockBits(&data);
    return bmp;
}
