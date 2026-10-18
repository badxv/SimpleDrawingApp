#include "BrushEngine.h"
#include "BrushTips.h"
#include "BrushPresets.h"
#include "BrushPersistence.h"
#include "UiBrushFlyout.h"
#include "AppPaths.h"
#include "AppMetrics.h"
#include "DrawingTools.h"
#include "Resource.h"
#include "AppState.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace Gdiplus;

namespace {

constexpr int kMaxCustomBrushes = 8;
constexpr int kMaxTotalBrushes = 24;

std::vector<BrushPreset> gPresets;
int gActiveBrush = 0;
float gStampCarry = 0.0f;
float gSmoothX = 0.0f;
float gSmoothY = 0.0f;
bool gSmoothInit = false;

// Cached wet-edges remapped tip (invalidated when tip/wet% change).
Bitmap* gWetTipCache = nullptr;
Bitmap* gWetTipSource = nullptr;
int gWetTipPct = -1;

void InvalidateWetTipCache() {
    delete gWetTipCache;
    gWetTipCache = nullptr;
    gWetTipSource = nullptr;
    gWetTipPct = -1;
}

Bitmap* BuildWetEdgesTip(Bitmap* src, int wetPct) {
    if (!src || wetPct < 1) return nullptr;
    const int w = static_cast<int>(src->GetWidth());
    const int h = static_cast<int>(src->GetHeight());
    if (w < 1 || h < 1) return nullptr;
    Bitmap* dst = new Bitmap(w, h, PixelFormat32bppARGB);
    if (!dst || dst->GetLastStatus() != Ok) {
        delete dst;
        return nullptr;
    }
    const float amount = static_cast<float>(wetPct) / 100.0f;
    const float cx = (static_cast<float>(w) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(h) - 1.0f) * 0.5f;
    const float invR = 1.0f / MaxFloat(1.0f, MaxFloat(cx, cy));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color c;
            src->GetPixel(x, y, &c);
            const float a = static_cast<float>(c.GetA()) / 255.0f;
            if (a <= 0.0f) {
                dst->SetPixel(x, y, Color(0, 255, 255, 255));
                continue;
            }
            const float dx = (static_cast<float>(x) - cx) * invR;
            const float dy = (static_cast<float>(y) - cy) * invR;
            float r = sqrtf(dx * dx + dy * dy);
            if (r > 1.0f) r = 1.0f;
            // Soft-tip ring (peaks at mid-alpha) + outer radial gate (hollow center).
            const float softRing = 4.0f * a * (1.0f - a);
            float edgeGate = 0.0f;
            if (r > 0.42f) {
                edgeGate = (r - 0.42f) / 0.58f;
                edgeGate = edgeGate * edgeGate; // bias paint toward the rim
            }
            float target = a * edgeGate;
            if (softRing > target) target = softRing;
            float out = a * (1.0f - amount) + target * amount;
            if (out < 0.0f) out = 0.0f;
            if (out > 1.0f) out = 1.0f;
            const BYTE ba = static_cast<BYTE>(out * 255.0f + 0.5f);
            dst->SetPixel(x, y, Color(ba, 255, 255, 255));
        }
    }
    return dst;
}

Bitmap* TipWithWetEdges(Bitmap* tip) {
    int wet = brushWetEdges;
    if (wet < 1 || !tip) return tip;
    if (wet > 100) wet = 100;
    if (gWetTipCache && gWetTipSource == tip && gWetTipPct == wet) {
        return gWetTipCache;
    }
    InvalidateWetTipCache();
    gWetTipCache = BuildWetEdgesTip(tip, wet);
    if (!gWetTipCache) return tip;
    gWetTipSource = tip;
    gWetTipPct = wet;
    return gWetTipCache;
}

BrushPreset* ActivePresetMutable() {
    if (gActiveBrush < 0 || gActiveBrush >= static_cast<int>(gPresets.size())) return nullptr;
    return &gPresets[static_cast<size_t>(gActiveBrush)];
}

const BrushPreset* ActivePreset() {
    return ActivePresetMutable();
}

void AddBuiltinPreset(const char* name, Bitmap* tip, float spacing, int defaultSize) {
    if (!tip) return;
    BrushPreset preset;
    preset.name = name;
    preset.tip.reset(tip);
    preset.spacing = spacing;
    preset.defaultSize = defaultSize;
    preset.builtin = true;
    gPresets.push_back(std::move(preset));
}

int CustomBrushCount() {
    int count = 0;
    for (const BrushPreset& p : gPresets) {
        if (!p.builtin) ++count;
    }
    return count;
}

bool AddCustomPreset(const char* name, Bitmap* tip, float spacing, int defaultSize) {
    if (!tip) return false;
    if (CustomBrushCount() >= kMaxCustomBrushes || static_cast<int>(gPresets.size()) >= kMaxTotalBrushes) {
        return false;
    }
    BrushPreset preset;
    preset.name = name ? name : "Imported";
    preset.tip.reset(tip);
    preset.spacing = spacing;
    preset.defaultSize = defaultSize;
    preset.builtin = false;
    gPresets.push_back(std::move(preset));
    return true;
}

REAL StampAlphaScale() {
    int flow = brushFlow;
    int hard = brushHardness;
    if (flow < 1) flow = 1;
    if (flow > 100) flow = 100;
    if (hard < 1) hard = 1;
    if (hard > 100) hard = 100;
    return static_cast<REAL>(flow) / 100.0f * (0.45f + 0.55f * static_cast<REAL>(hard) / 100.0f);
}

void DrawRoundLine(Graphics* target, int x0, int y0, int x1, int y1, COLORREF color, int width, bool eraseTransparent) {
    if (!target) return;
    if (eraseTransparent) {
        target->SetCompositingMode(CompositingModeSourceOver);
        Pen pen(Color(255, 255, 255, 255), static_cast<REAL>(width));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        target->DrawLine(&pen, x0, y0, x1, y1);
        return;
    }
    Pen pen(GdiplusFromColor(color, 255), static_cast<REAL>(width));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    target->DrawLine(&pen, x0, y0, x1, y1);
}

void DrawStamp(Graphics* target, Bitmap* tip, int cx, int cy, COLORREF color, int size, bool eraseTransparent,
    float pressure = 1.0f, float angleDeg = 0.0f, int roundnessOverride = -1,
    bool flipX = false, bool flipY = false) {
    if (!target || !tip || size < 1) return;

    tip = TipWithWetEdges(tip);
    if (!tip) return;

    const REAL half = static_cast<REAL>(size) * 0.5f;
    const int w = static_cast<int>(tip->GetWidth());
    const int h = static_cast<int>(tip->GetHeight());

    int roundness = (roundnessOverride >= 0) ? roundnessOverride : brushRoundness;
    if (roundness < 1) roundness = 1;
    if (roundness > 100) roundness = 100;
    const REAL height = static_cast<REAL>(size) * (static_cast<REAL>(roundness) / 100.0f);
    const REAL halfH = height * 0.5f;

    // Mirror oriented elliptical tips by reflecting angle; also mirror tip pixels via dest.
    if (flipX) angleDeg = -angleDeg;
    if (flipY) angleDeg = 180.0f - angleDeg;

    GraphicsState state = target->Save();
    target->TranslateTransform(static_cast<REAL>(cx), static_cast<REAL>(cy));
    if (angleDeg != 0.0f) {
        target->RotateTransform(angleDeg);
    }
    // Negative dest width/height mirrors tip texture (Charcoal grain, etc.).
    const REAL destX = flipX ? half : -half;
    const REAL destY = flipY ? halfH : -halfH;
    const REAL destW = flipX ? -static_cast<REAL>(size) : static_cast<REAL>(size);
    const REAL destH = flipY ? -height : height;
    const RectF dest(destX, destY, destW, destH);

    if (eraseTransparent) {
        ColorMatrix matrix = {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 0.0f, 1.0f
        };
        ImageAttributes attrs;
        attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        target->SetCompositingMode(CompositingModeSourceOver);
        target->DrawImage(tip, dest, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h), UnitPixel, &attrs);
        target->Restore(state);
        return;
    }

    const REAL r = static_cast<REAL>(GetRValue(color)) / 255.0f;
    const REAL g = static_cast<REAL>(GetGValue(color)) / 255.0f;
    const REAL b = static_cast<REAL>(GetBValue(color)) / 255.0f;
    // When tablet pressure is off, still honor the pressure arg (opacity jitter uses it).
    float press = pressure;
    if (press < 0.0f) press = 0.0f;
    if (press > 1.0f) press = 1.0f;
    const REAL pressScale = penPressureEnabled ? PenPressureFactor(press) : static_cast<REAL>(press);
    REAL alphaScale = StampAlphaScale() * pressScale;
    if (brushFlowJitter >= 1) {
        int jitter = brushFlowJitter;
        if (jitter > 100) jitter = 100;
        const unsigned n = (static_cast<unsigned>(cx) * 2246822519u)
            ^ (static_cast<unsigned>(cy) * 3266489917u)
            ^ (static_cast<unsigned>(size) * 668265263u);
        const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
        alphaScale *= (1.0f + u * (static_cast<float>(jitter) / 100.0f));
        if (alphaScale < 0.05f) alphaScale = 0.05f;
        if (alphaScale > 1.0f) alphaScale = 1.0f;
    }
    ColorMatrix matrix = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alphaScale, 0.0f,
        r, g, b, 0.0f, 1.0f
    };
    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    target->DrawImage(tip, dest, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h), UnitPixel, &attrs);
    target->Restore(state);
}

} // namespace

bool BrushPresetsCanAddCustom() {
    return CustomBrushCount() < kMaxCustomBrushes && static_cast<int>(gPresets.size()) < kMaxTotalBrushes;
}

bool BrushPresetsAddCustom(const char* name, Bitmap* tip, float spacing, int defaultSize, bool persist) {
    if (!AddCustomPreset(name, tip, spacing, defaultSize)) return false;
    if (persist) SaveCustomBrushesToDisk();
    RebuildBrushFlyoutPresets();
    return true;
}

bool BrushPresetsRemove(int index, bool persist) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return false;
    if (gPresets[static_cast<size_t>(index)].builtin) return false;
    gPresets.erase(gPresets.begin() + index);
    if (gActiveBrush >= static_cast<int>(gPresets.size())) {
        gActiveBrush = static_cast<int>(gPresets.size()) - 1;
    }
    if (gActiveBrush < 0) gActiveBrush = 0;
    else if (gActiveBrush > index) {
        --gActiveBrush;
    }
    if (persist) SaveCustomBrushesToDisk();
    SaveBrushSettings();
    RebuildBrushFlyoutPresets();
    SyncBrushMenuItems();
    return true;
}

bool BrushPresetsRename(int index, const char* name, bool persist) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return false;
    BrushPreset& preset = gPresets[static_cast<size_t>(index)];
    if (preset.builtin) return false;
    if (!name) return false;
    // Trim leading/trailing spaces
    while (*name == ' ' || *name == '\t') ++name;
    if (!name[0]) return false;
    char cleaned[80] = {};
    strncpy_s(cleaned, name, _TRUNCATE);
    size_t len = strlen(cleaned);
    while (len > 0 && (cleaned[len - 1] == ' ' || cleaned[len - 1] == '\t')) {
        cleaned[--len] = '\0';
    }
    if (!cleaned[0]) return false;
    preset.name = cleaned;
    if (persist) SaveCustomBrushesToDisk();
    RebuildBrushFlyoutPresets();
    SyncBrushMenuItems();
    return true;
}

bool BrushPresetsSetDefaultSize(int index, int size, bool persist) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return false;
    BrushPreset& preset = gPresets[static_cast<size_t>(index)];
    if (size < kPenWidthMin) size = kPenWidthMin;
    if (size > kPenWidthMax) size = kPenWidthMax;
    if (preset.defaultSize == size) return true;
    preset.defaultSize = size;
    if (persist && !preset.builtin) SaveCustomBrushesToDisk();
    return true;
}

void BrushPresetsSetActive(int index) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return;
    if (gActiveBrush != index) InvalidateWetTipCache();
    gActiveBrush = index;
}

const char* BrushPresetsActiveName() {
    const BrushPreset* preset = ActivePreset();
    return preset ? preset->name.c_str() : "Round Soft";
}

void BrushPresetsSaveSettings() {
    SaveBrushSettings();
}

void BrushPresetsSyncMenu() {
    SyncBrushMenuItems();
}

void InitBrushEngine() {
    if (!gPresets.empty()) return;
    AddBuiltinPreset("Round Soft", MakeRoundTip(0.15f), 0.18f, 8);
    AddBuiltinPreset("Round Hard", MakeRoundTip(0.85f), 0.12f, 6);
    AddBuiltinPreset("Charcoal", MakeCharcoalTip(), 0.10f, 14);
    AddBuiltinPreset("Flat Oil", MakeRoundTip(0.35f, 1.0f, 0.35f), 0.16f, 16);
    AddBuiltinPreset("Concept Soft", MakeConceptTip(), 0.20f, 20);
    AddBuiltinPreset("Pencil", MakePencilTip(), 0.08f, 4);
    LoadCustomBrushesFromDisk();
    LoadBrushSettings();
}

void ShutdownBrushEngine() {
    SaveCustomBrushesToDisk();
    InvalidateWetTipCache();
    gPresets.clear();
    gActiveBrush = 0;
    gStampCarry = 0.0f;
}

int BrushPresetCount() {
    return static_cast<int>(gPresets.size());
}

const BrushPreset* GetBrushPreset(int index) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return nullptr;
    return &gPresets[static_cast<size_t>(index)];
}

int GetActiveBrushIndex() {
    return gActiveBrush;
}

void SetActiveBrushIndex(int index) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return;
    if (gActiveBrush != index) InvalidateWetTipCache();
    gActiveBrush = index;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

Bitmap* BrushTipForDraw(Bitmap* tip) {
    return TipWithWetEdges(tip);
}

const char* GetActiveBrushName() {
    return BrushPresetsActiveName();
}

bool IsActiveBrushCustom() {
    const BrushPreset* preset = ActivePreset();
    return preset && !preset->builtin;
}

bool DeleteBrushPreset(int index) {
    return BrushPresetsRemove(index, true);
}

bool RenameBrushPreset(int index, const char* name) {
    return BrushPresetsRename(index, name, true);
}

void PersistActiveBrushDefaultSize(int size) {
    BrushPresetsSetDefaultSize(gActiveBrush, size, true);
}

void ResetBrushStrokeState() {
    gStampCarry = 0.0f;
    gSmoothInit = false;
    gSmoothX = 0.0f;
    gSmoothY = 0.0f;
}

void InitBrushStrokeSmoothing(int x, int y) {
    gSmoothX = static_cast<float>(x);
    gSmoothY = static_cast<float>(y);
    gSmoothInit = true;
}

bool SmoothBrushStrokePoint(int rawX, int rawY, int& outX, int& outY) {
    if (!gSmoothInit) {
        InitBrushStrokeSmoothing(rawX, rawY);
    }
    int amount = brushSmoothing;
    if (amount < 0) amount = 0;
    if (amount > 100) amount = 100;
    // 0% → follow cursor exactly; 100% → strong lag (~8% pull per sample).
    const float pull = 1.0f - 0.92f * (static_cast<float>(amount) / 100.0f);
    gSmoothX += (static_cast<float>(rawX) - gSmoothX) * pull;
    gSmoothY += (static_cast<float>(rawY) - gSmoothY) * pull;
    outX = static_cast<int>(gSmoothX + (gSmoothX >= 0.0f ? 0.5f : -0.5f));
    outY = static_cast<int>(gSmoothY + (gSmoothY >= 0.0f ? 0.5f : -0.5f));
    return true;
}

bool IsPenPressureEnabled() {
    return penPressureEnabled;
}

void SetPenPressureEnabled(bool enabled) {
    penPressureEnabled = enabled;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

float PenPressureFactor(float pressure) {
    if (!penPressureEnabled) return 1.0f;
    if (pressure < 0.0f) pressure = 0.0f;
    if (pressure > 1.0f) pressure = 1.0f;
    constexpr float kMin = 0.15f;
    return kMin + (1.0f - kMin) * pressure;
}

int PenPressureWidth(int baseWidth, float pressure) {
    const float factor = PenPressureFactor(pressure);
    int width = static_cast<int>(static_cast<float>(baseWidth) * factor + 0.5f);
    if (width < 1) width = 1;
    if (width > kPenWidthMax) width = kPenWidthMax;
    return width;
}

void DrawBrushStrokeSegment(Graphics* target, int x0, int y0, int x1, int y1,
    COLORREF color, int width, bool eraseTransparent, bool eraseOpaque,
    float pressure0, float pressure1) {
    if (!target) return;

    const float avgPressure = (pressure0 + pressure1) * 0.5f;
    const int effectiveWidth = PenPressureWidth(width, avgPressure);

    if (eraseOpaque) {
        DrawRoundLine(target, x0, y0, x1, y1, gTheme.canvasBg, effectiveWidth, false);
        return;
    }

    const BrushPreset* preset = ActivePreset();
    if (!preset || !preset->tip || currentTool != DrawTool::Pen) {
        DrawRoundLine(target, x0, y0, x1, y1, color, effectiveWidth, eraseTransparent);
        return;
    }

    const float dx = static_cast<float>(x1 - x0);
    const float dy = static_cast<float>(y1 - y0);
    const float segLen = sqrtf(dx * dx + dy * dy);

    auto ScatterOffset = [](int x, int y, int stampW, int salt, int& outDx, int& outDy) {
        outDx = 0;
        outDy = 0;
        int scatter = brushScatter;
        if (scatter < 1) return;
        if (scatter > 100) scatter = 100;
        const unsigned n = (static_cast<unsigned>(x) * 73856093u)
            ^ (static_cast<unsigned>(y) * 19349663u)
            ^ (static_cast<unsigned>(salt) * 83492791u);
        const float rx = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
        const float ry = (static_cast<float>((n >> 8) & 255u) / 255.0f) * 2.0f - 1.0f;
        const float amp = static_cast<float>(stampW) * 0.55f * (static_cast<float>(scatter) / 100.0f);
        outDx = static_cast<int>(rx * amp + (rx >= 0.0f ? 0.5f : -0.5f));
        outDy = static_cast<int>(ry * amp + (ry >= 0.0f ? 0.5f : -0.5f));
    };

    auto AngleJitter = [](int x, int y, int salt, float baseDeg) -> float {
        int jitter = brushAngleJitter;
        if (jitter < 1) return baseDeg;
        if (jitter > 100) jitter = 100;
        const unsigned n = (static_cast<unsigned>(x) * 83492791u)
            ^ (static_cast<unsigned>(y) * 2654435761u)
            ^ (static_cast<unsigned>(salt) * 2246822519u);
        const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
        return baseDeg + u * 180.0f * (static_cast<float>(jitter) / 100.0f);
    };

    // tipW = brush diameter before pressure; floor is % of tip (Photoshop-style Minimum Diameter).
    auto SizeJitter = [](int x, int y, int salt, int baseW, int tipW) -> int {
        int jitter = brushSizeJitter;
        int w = baseW;
        if (jitter >= 1) {
            if (jitter > 100) jitter = 100;
            const unsigned n = (static_cast<unsigned>(x) * 374761393u)
                ^ (static_cast<unsigned>(y) * 668265263u)
                ^ (static_cast<unsigned>(salt) * 2147483647u);
            const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
            const float factor = 1.0f + u * (static_cast<float>(jitter) / 100.0f);
            w = static_cast<int>(static_cast<float>(baseW) * factor + 0.5f);
        }
        int minPct = brushMinDiameter;
        if (minPct < 0) minPct = 0;
        if (minPct > 100) minPct = 100;
        if (minPct > 0) {
            int floorW = static_cast<int>(static_cast<float>(tipW) * (static_cast<float>(minPct) / 100.0f) + 0.5f);
            if (floorW < 1) floorW = 1;
            if (w < floorW) w = floorW;
        }
        if (w < 1) w = 1;
        return w;
    };

    auto OpacityJitter = [](int x, int y, int salt, float basePressure) -> float {
        int jitter = brushOpacityJitter;
        if (jitter < 1) return basePressure;
        if (jitter > 100) jitter = 100;
        const unsigned n = (static_cast<unsigned>(x) * 1103515245u)
            ^ (static_cast<unsigned>(y) * 12345u)
            ^ (static_cast<unsigned>(salt) * 1664525u);
        const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
        float p = basePressure * (1.0f + u * (static_cast<float>(jitter) / 100.0f));
        if (p < 0.05f) p = 0.05f;
        if (p > 1.0f) p = 1.0f;
        return p;
    };

    auto RoundnessJitter = [](int x, int y, int salt, int baseR) -> int {
        int jitter = brushRoundnessJitter;
        int r = baseR;
        if (jitter >= 1) {
            if (jitter > 100) jitter = 100;
            const unsigned n = (static_cast<unsigned>(x) * 2654435761u)
                ^ (static_cast<unsigned>(y) * 2246822519u)
                ^ (static_cast<unsigned>(salt) * 3266489917u);
            const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
            const float factor = 1.0f + u * (static_cast<float>(jitter) / 100.0f);
            r = static_cast<int>(static_cast<float>(baseR) * factor + 0.5f);
        }
        if (r < 1) r = 1;
        if (r > 100) r = 100;
        return r;
    };

    auto FlipChance = [](int x, int y, int salt, int chance, unsigned mix) -> bool {
        if (chance < 1) return false;
        if (chance > 100) chance = 100;
        const unsigned n = (static_cast<unsigned>(x) * 1597334677u)
            ^ (static_cast<unsigned>(y) * 3812015801u)
            ^ (static_cast<unsigned>(salt) * mix);
        return static_cast<int>(n % 100u) < chance;
    };

    const float strokeAngleDeg = (segLen >= 0.001f)
        ? (atan2f(dy, dx) * (180.0f / 3.14159265f))
        : 0.0f;
    const float baseTipAngleDeg = strokeAngleDeg + static_cast<float>(brushTipAngle);

    int count = brushStampCount;
    if (count < 1) count = 1;
    if (count > 8) count = 8;

    auto PlaceStamps = [&](int sx, int sy, int baseSalt, float pressure) {
        const int baseW = PenPressureWidth(width, pressure);
        int spacingW = baseW;
        for (int c = 0; c < count; ++c) {
            const int salt = baseSalt * 8 + c;
            const int stampW = SizeJitter(sx, sy, salt, baseW, width);
            if (c == 0) spacingW = stampW;
            const float stampP = OpacityJitter(sx, sy, salt, pressure);
            int ox = 0, oy = 0;
            ScatterOffset(sx, sy, stampW, salt, ox, oy);
            // Count polish: if Scatter is off, fan secondary stamps so Count is visible.
            if (brushScatter < 1 && count > 1 && c > 0) {
                const unsigned n = (static_cast<unsigned>(sx) * 73856093u)
                    ^ (static_cast<unsigned>(sy) * 19349663u)
                    ^ (static_cast<unsigned>(salt) * 83492791u);
                const float rx = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                const float ry = (static_cast<float>((n >> 8) & 255u) / 255.0f) * 2.0f - 1.0f;
                const float amp = static_cast<float>(stampW) * 0.40f;
                ox = static_cast<int>(rx * amp + (rx >= 0.0f ? 0.5f : -0.5f));
                oy = static_cast<int>(ry * amp + (ry >= 0.0f ? 0.5f : -0.5f));
            }
            const float ang = AngleJitter(sx, sy, salt, baseTipAngleDeg);
            const int stampRd = RoundnessJitter(sx, sy, salt, brushRoundness);
            const bool fx = FlipChance(sx, sy, salt, brushFlipX, 0x85ebca6bu);
            const bool fy = FlipChance(sx, sy, salt, brushFlipY, 0xc2b2ae35u);
            DrawStamp(target, preset->tip.get(), sx + ox, sy + oy, color, stampW, eraseTransparent, stampP, ang, stampRd, fx, fy);
        }
        return spacingW;
    };

    if (segLen < 0.001f) {
        PlaceStamps(x1, y1, 0, pressure1);
        return;
    }

    float dist = gStampCarry;
    int stampIndex = 0;
    while (dist <= segLen) {
        const float t = dist / segLen;
        const float pressure = pressure0 + (pressure1 - pressure0) * t;
        int sx = x0 + static_cast<int>(dx * t + (dx >= 0.0f ? 0.5f : -0.5f));
        int sy = y0 + static_cast<int>(dy * t + (dy >= 0.0f ? 0.5f : -0.5f));
        const int spacingW = PlaceStamps(sx, sy, stampIndex, pressure);
        dist += MaxFloat(1.0f, preset->spacing * static_cast<float>(MaxInt(1, spacingW)));
        ++stampIndex;
    }
    gStampCarry = dist - segLen;
}

int GetBrushFlow() {
    return brushFlow;
}

int GetBrushHardness() {
    return brushHardness;
}

int GetBrushScatter() {
    return brushScatter;
}

int GetBrushAngleJitter() {
    return brushAngleJitter;
}

int GetBrushTipAngle() {
    return brushTipAngle;
}

int GetBrushSizeJitter() {
    return brushSizeJitter;
}

int GetBrushOpacityJitter() {
    return brushOpacityJitter;
}

int GetBrushSmoothing() {
    return brushSmoothing;
}

int GetBrushStampCount() {
    return brushStampCount;
}

int GetBrushRoundness() {
    return brushRoundness;
}

int GetBrushMinDiameter() {
    return brushMinDiameter;
}

int GetBrushWetEdges() {
    return brushWetEdges;
}

int GetBrushRoundnessJitter() {
    return brushRoundnessJitter;
}

int GetBrushFlipX() {
    return brushFlipX;
}

int GetBrushFlipY() {
    return brushFlipY;
}

int GetBrushFlowJitter() {
    return brushFlowJitter;
}

void SetBrushFlow(int flow) {
    if (flow < 1) flow = 1;
    if (flow > 100) flow = 100;
    brushFlow = flow;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

void SetBrushHardness(int hardness) {
    if (hardness < 1) hardness = 1;
    if (hardness > 100) hardness = 100;
    brushHardness = hardness;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

void SetBrushScatter(int scatter) {
    if (scatter < 0) scatter = 0;
    if (scatter > 100) scatter = 100;
    brushScatter = scatter;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushAngleJitter(int jitter) {
    if (jitter < 0) jitter = 0;
    if (jitter > 100) jitter = 100;
    brushAngleJitter = jitter;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushTipAngle(int degrees) {
    if (degrees < 0) degrees = 0;
    if (degrees > 180) degrees = 180;
    brushTipAngle = degrees;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushSizeJitter(int jitter) {
    if (jitter < 0) jitter = 0;
    if (jitter > 100) jitter = 100;
    brushSizeJitter = jitter;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushOpacityJitter(int jitter) {
    if (jitter < 0) jitter = 0;
    if (jitter > 100) jitter = 100;
    brushOpacityJitter = jitter;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushSmoothing(int amount) {
    if (amount < 0) amount = 0;
    if (amount > 100) amount = 100;
    brushSmoothing = amount;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushStampCount(int count) {
    if (count < 1) count = 1;
    if (count > 8) count = 8;
    brushStampCount = count;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushRoundness(int percent) {
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    brushRoundness = percent;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushMinDiameter(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    brushMinDiameter = percent;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushWetEdges(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (brushWetEdges != percent) InvalidateWetTipCache();
    brushWetEdges = percent;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushRoundnessJitter(int jitter) {
    if (jitter < 0) jitter = 0;
    if (jitter > 100) jitter = 100;
    brushRoundnessJitter = jitter;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushFlipX(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    brushFlipX = percent;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushFlipY(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    brushFlipY = percent;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

void SetBrushFlowJitter(int jitter) {
    if (jitter < 0) jitter = 0;
    if (jitter > 100) jitter = 100;
    brushFlowJitter = jitter;
    SaveBrushSettings();
    SyncBrushFlyoutChecks();
}

int GetBrushSpacingPercent() {
    const BrushPreset* preset = ActivePreset();
    if (!preset) return 20;
    int pct = static_cast<int>(preset->spacing * 100.0f + 0.5f);
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    return pct;
}

void SetBrushSpacingPercent(int percent) {
    BrushPreset* preset = ActivePresetMutable();
    if (!preset) return;
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    preset->spacing = static_cast<float>(percent) / 100.0f;
    if (!preset->builtin) {
        SaveCustomBrushesToDisk();
    }
    SyncBrushFlyoutChecks();
}

void LoadBrushSettings() {
    char iniPath[MAX_PATH] = {};
    GetFeaturesIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    const int idx = GetPrivateProfileIntA("Features", "ActiveBrush", 0, iniPath);
    if (idx >= 0 && idx < static_cast<int>(gPresets.size())) {
        gActiveBrush = idx;
    }
    brushFlow = GetPrivateProfileIntA("Features", "BrushFlow", 100, iniPath);
    brushHardness = GetPrivateProfileIntA("Features", "BrushHardness", 100, iniPath);
    brushScatter = GetPrivateProfileIntA("Features", "BrushScatter", 0, iniPath);
    brushAngleJitter = GetPrivateProfileIntA("Features", "BrushAngleJitter", 0, iniPath);
    brushTipAngle = GetPrivateProfileIntA("Features", "BrushTipAngle", 0, iniPath);
    brushSizeJitter = GetPrivateProfileIntA("Features", "BrushSizeJitter", 0, iniPath);
    brushOpacityJitter = GetPrivateProfileIntA("Features", "BrushOpacityJitter", 0, iniPath);
    brushSmoothing = GetPrivateProfileIntA("Features", "BrushSmoothing", 0, iniPath);
    brushStampCount = GetPrivateProfileIntA("Features", "BrushStampCount", 1, iniPath);
    brushRoundness = GetPrivateProfileIntA("Features", "BrushRoundness", 100, iniPath);
    brushMinDiameter = GetPrivateProfileIntA("Features", "BrushMinDiameter", 0, iniPath);
    brushWetEdges = GetPrivateProfileIntA("Features", "BrushWetEdges", 0, iniPath);
    brushRoundnessJitter = GetPrivateProfileIntA("Features", "BrushRoundnessJitter", 0, iniPath);
    brushFlipX = GetPrivateProfileIntA("Features", "BrushFlipX", 0, iniPath);
    brushFlipY = GetPrivateProfileIntA("Features", "BrushFlipY", 0, iniPath);
    brushFlowJitter = GetPrivateProfileIntA("Features", "BrushFlowJitter", 0, iniPath);
    penPressureEnabled = GetPrivateProfileIntA("Features", "PenPressure", 1, iniPath) != 0;
    const int spacingPct = GetPrivateProfileIntA("Features", "BrushSpacing", -1, iniPath);
    if (spacingPct >= 5) {
        if (BrushPreset* preset = ActivePresetMutable()) {
            int pct = spacingPct;
            if (pct > 100) pct = 100;
            preset->spacing = static_cast<float>(pct) / 100.0f;
        }
    }
    if (brushFlow < 1) brushFlow = 1;
    if (brushFlow > 100) brushFlow = 100;
    if (brushHardness < 1) brushHardness = 1;
    if (brushHardness > 100) brushHardness = 100;
    if (brushScatter < 0) brushScatter = 0;
    if (brushScatter > 100) brushScatter = 100;
    if (brushAngleJitter < 0) brushAngleJitter = 0;
    if (brushAngleJitter > 100) brushAngleJitter = 100;
    if (brushTipAngle < 0) brushTipAngle = 0;
    if (brushTipAngle > 180) brushTipAngle = 180;
    if (brushSizeJitter < 0) brushSizeJitter = 0;
    if (brushSizeJitter > 100) brushSizeJitter = 100;
    if (brushOpacityJitter < 0) brushOpacityJitter = 0;
    if (brushOpacityJitter > 100) brushOpacityJitter = 100;
    if (brushSmoothing < 0) brushSmoothing = 0;
    if (brushSmoothing > 100) brushSmoothing = 100;
    if (brushStampCount < 1) brushStampCount = 1;
    if (brushStampCount > 8) brushStampCount = 8;
    if (brushRoundness < 1) brushRoundness = 1;
    if (brushRoundness > 100) brushRoundness = 100;
    if (brushMinDiameter < 0) brushMinDiameter = 0;
    if (brushMinDiameter > 100) brushMinDiameter = 100;
    if (brushWetEdges < 0) brushWetEdges = 0;
    if (brushWetEdges > 100) brushWetEdges = 100;
    if (brushRoundnessJitter < 0) brushRoundnessJitter = 0;
    if (brushRoundnessJitter > 100) brushRoundnessJitter = 100;
    if (brushFlipX < 0) brushFlipX = 0;
    if (brushFlipX > 100) brushFlipX = 100;
    if (brushFlipY < 0) brushFlipY = 0;
    if (brushFlipY > 100) brushFlipY = 100;
    if (brushFlowJitter < 0) brushFlowJitter = 0;
    if (brushFlowJitter > 100) brushFlowJitter = 100;
}

void SaveBrushSettings() {
    char iniPath[MAX_PATH] = {};
    GetFeaturesIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    char buf[16];
    sprintf_s(buf, "%d", gActiveBrush);
    WritePrivateProfileStringA("Features", "ActiveBrush", buf, iniPath);
    sprintf_s(buf, "%d", brushFlow);
    WritePrivateProfileStringA("Features", "BrushFlow", buf, iniPath);
    sprintf_s(buf, "%d", brushHardness);
    WritePrivateProfileStringA("Features", "BrushHardness", buf, iniPath);
    sprintf_s(buf, "%d", brushScatter);
    WritePrivateProfileStringA("Features", "BrushScatter", buf, iniPath);
    sprintf_s(buf, "%d", brushAngleJitter);
    WritePrivateProfileStringA("Features", "BrushAngleJitter", buf, iniPath);
    sprintf_s(buf, "%d", brushTipAngle);
    WritePrivateProfileStringA("Features", "BrushTipAngle", buf, iniPath);
    sprintf_s(buf, "%d", brushSizeJitter);
    WritePrivateProfileStringA("Features", "BrushSizeJitter", buf, iniPath);
    sprintf_s(buf, "%d", brushOpacityJitter);
    WritePrivateProfileStringA("Features", "BrushOpacityJitter", buf, iniPath);
    sprintf_s(buf, "%d", brushSmoothing);
    WritePrivateProfileStringA("Features", "BrushSmoothing", buf, iniPath);
    sprintf_s(buf, "%d", brushStampCount);
    WritePrivateProfileStringA("Features", "BrushStampCount", buf, iniPath);
    sprintf_s(buf, "%d", brushRoundness);
    WritePrivateProfileStringA("Features", "BrushRoundness", buf, iniPath);
    sprintf_s(buf, "%d", brushMinDiameter);
    WritePrivateProfileStringA("Features", "BrushMinDiameter", buf, iniPath);
    sprintf_s(buf, "%d", brushWetEdges);
    WritePrivateProfileStringA("Features", "BrushWetEdges", buf, iniPath);
    sprintf_s(buf, "%d", brushRoundnessJitter);
    WritePrivateProfileStringA("Features", "BrushRoundnessJitter", buf, iniPath);
    sprintf_s(buf, "%d", brushFlipX);
    WritePrivateProfileStringA("Features", "BrushFlipX", buf, iniPath);
    sprintf_s(buf, "%d", brushFlipY);
    WritePrivateProfileStringA("Features", "BrushFlipY", buf, iniPath);
    sprintf_s(buf, "%d", brushFlowJitter);
    WritePrivateProfileStringA("Features", "BrushFlowJitter", buf, iniPath);
    WritePrivateProfileStringA("Features", "PenPressure", penPressureEnabled ? "1" : "0", iniPath);
}

namespace {

bool MenuContainsCommand(HMENU menu, UINT commandId) {
    if (!menu) return false;
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        const UINT id = GetMenuItemID(menu, i);
        if (id == commandId) return true;
        HMENU sub = GetSubMenu(menu, i);
        if (sub && MenuContainsCommand(sub, commandId)) return true;
    }
    return false;
}

HMENU FindToolsSubmenuContaining(UINT commandId) {
    if (!gAppMenu) return nullptr;
    HMENU toolsMenu = GetSubMenu(gAppMenu, 4);
    if (!toolsMenu) return nullptr;
    const int count = GetMenuItemCount(toolsMenu);
    for (int i = 0; i < count; ++i) {
        HMENU sub = GetSubMenu(toolsMenu, i);
        if (sub && MenuContainsCommand(sub, commandId)) return sub;
    }
    return nullptr;
}

} // namespace

void SyncBrushMenuItems() {
    if (!gAppMenu) return;
    HMENU presetMenu = FindToolsSubmenuContaining(IDM_BRUSH_ROUND);
    if (presetMenu) {
        for (int i = IDM_BRUSH_ROUND; i <= IDM_BRUSH_PENCIL; ++i) {
            CheckMenuItem(presetMenu, i, MF_BYCOMMAND | MF_UNCHECKED);
        }
        if (gActiveBrush >= 0 && gActiveBrush <= IDM_BRUSH_PENCIL - IDM_BRUSH_ROUND) {
            CheckMenuItem(presetMenu, IDM_BRUSH_ROUND + gActiveBrush, MF_BYCOMMAND | MF_CHECKED);
        }
    }

    HMENU dynamicsMenu = FindToolsSubmenuContaining(IDM_FLOW_LOW);
    if (!dynamicsMenu) return;
    CheckMenuItem(dynamicsMenu, IDM_FLOW_LOW, MF_BYCOMMAND | (brushFlow == 25 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_FLOW_MED, MF_BYCOMMAND | (brushFlow == 50 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_FLOW_FULL, MF_BYCOMMAND | (brushFlow == 100 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_HARD_SOFT, MF_BYCOMMAND | (brushHardness == 35 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_HARD_MED, MF_BYCOMMAND | (brushHardness == 65 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_HARD_HARD, MF_BYCOMMAND | (brushHardness == 100 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_PRESSURE_ENABLE,
        MF_BYCOMMAND | (penPressureEnabled ? MF_CHECKED : MF_UNCHECKED));
    SyncBrushFlyoutChecks();
}
