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
    float pressure = 1.0f) {
    if (!target || !tip || size < 1) return;

    const REAL half = static_cast<REAL>(size) * 0.5f;
    const RectF dest(static_cast<REAL>(cx) - half, static_cast<REAL>(cy) - half,
        static_cast<REAL>(size), static_cast<REAL>(size));

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
        const int w = static_cast<int>(tip->GetWidth());
        const int h = static_cast<int>(tip->GetHeight());
        target->SetCompositingMode(CompositingModeSourceOver);
        target->DrawImage(tip, dest, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h), UnitPixel, &attrs);
        return;
    }

    const REAL r = static_cast<REAL>(GetRValue(color)) / 255.0f;
    const REAL g = static_cast<REAL>(GetGValue(color)) / 255.0f;
    const REAL b = static_cast<REAL>(GetBValue(color)) / 255.0f;
    const REAL alphaScale = StampAlphaScale() * PenPressureFactor(pressure);
    ColorMatrix matrix = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alphaScale, 0.0f,
        r, g, b, 0.0f, 1.0f
    };
    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    const int w = static_cast<int>(tip->GetWidth());
    const int h = static_cast<int>(tip->GetHeight());
    target->DrawImage(tip, dest, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h), UnitPixel, &attrs);
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
    gActiveBrush = index;
    SaveBrushSettings();
    SyncBrushMenuItems();
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

    if (segLen < 0.001f) {
        const int stampW = PenPressureWidth(width, pressure1);
        int ox = 0, oy = 0;
        ScatterOffset(x1, y1, stampW, 0, ox, oy);
        DrawStamp(target, preset->tip.get(), x1 + ox, y1 + oy, color, stampW, eraseTransparent, pressure1);
        return;
    }

    float dist = gStampCarry;
    int stampIndex = 0;
    while (dist <= segLen) {
        const float t = dist / segLen;
        const float pressure = pressure0 + (pressure1 - pressure0) * t;
        const int stampW = PenPressureWidth(width, pressure);
        int sx = x0 + static_cast<int>(dx * t + (dx >= 0.0f ? 0.5f : -0.5f));
        int sy = y0 + static_cast<int>(dy * t + (dy >= 0.0f ? 0.5f : -0.5f));
        int ox = 0, oy = 0;
        ScatterOffset(sx, sy, stampW, stampIndex, ox, oy);
        DrawStamp(target, preset->tip.get(), sx + ox, sy + oy, color, stampW, eraseTransparent, pressure);
        dist += MaxFloat(1.0f, preset->spacing * static_cast<float>(MaxInt(1, stampW)));
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
    penPressureEnabled = GetPrivateProfileIntA("Features", "PenPressure", 1, iniPath) != 0;
    if (brushFlow < 1) brushFlow = 1;
    if (brushFlow > 100) brushFlow = 100;
    if (brushHardness < 1) brushHardness = 1;
    if (brushHardness > 100) brushHardness = 100;
    if (brushScatter < 0) brushScatter = 0;
    if (brushScatter > 100) brushScatter = 100;
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
