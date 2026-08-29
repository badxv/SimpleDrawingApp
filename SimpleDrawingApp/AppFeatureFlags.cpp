#include "AppFeatureFlags.h"
#include "BrushEngine.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "Resource.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

bool gFlags[static_cast<int>(AppFeature::Count)] = {};

struct FeatureMeta {
    AppFeature id;
    const char* iniKey;
    bool defaultOn;
};

const FeatureMeta kFeatures[] = {
    { AppFeature::WarnCanvasShrink, "WarnCanvasShrink", true },
    { AppFeature::PasteAtViewOrigin, "PasteAtViewOrigin", true },
    { AppFeature::SelectionExteriorVeil, "SelectionExteriorVeil", true },
    { AppFeature::ReopenLastDocument, "ReopenLastDocument", true },
    { AppFeature::AutosaveRecovery, "AutosaveRecovery", true },
    { AppFeature::CanvasGrid, "CanvasGrid", false },
    { AppFeature::SnapToGrid, "SnapToGrid", false },
};

void ApplyDefaults() {
    for (const FeatureMeta& f : kFeatures) {
        gFlags[static_cast<int>(f.id)] = f.defaultOn;
    }
}

const FeatureMeta* MetaFor(AppFeature feature) {
    for (const FeatureMeta& f : kFeatures) {
        if (f.id == feature) return &f;
    }
    return nullptr;
}

void GetFeatureFlagsIniPath(char* path, size_t pathChars) {
    if (!path || pathChars == 0) return;
    path[0] = '\0';
    DWORD len = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(pathChars));
    if (len == 0 || len >= pathChars) {
        path[0] = '\0';
        return;
    }
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') {
            path[i + 1] = '\0';
            break;
        }
    }
    const size_t used = strlen(path);
    const char suffix[] = "features.ini";
    if (used + sizeof(suffix) > pathChars) {
        path[0] = '\0';
        return;
    }
    strcat_s(path, pathChars, suffix);
}

void SetMenuCheck(HMENU menu, UINT id, bool checked) {
    if (!menu) return;
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

}  // namespace

bool FeatureDefaultEnabled(AppFeature feature) {
    if (const FeatureMeta* meta = MetaFor(feature)) {
        return meta->defaultOn;
    }
    return false;
}

bool IsFeatureEnabled(AppFeature feature) {
    const int idx = static_cast<int>(feature);
    if (idx < 0 || idx >= static_cast<int>(AppFeature::Count)) return false;
    return gFlags[idx];
}

void SetFeatureEnabled(AppFeature feature, bool enabled) {
    const int idx = static_cast<int>(feature);
    if (idx < 0 || idx >= static_cast<int>(AppFeature::Count)) return;
    gFlags[idx] = enabled;
}

bool ToggleFeatureFlag(AppFeature feature) {
    const bool next = !IsFeatureEnabled(feature);
    SetFeatureEnabled(feature, next);
    SaveFeatureFlags();
    SyncFeatureFlagMenuItems();
    return next;
}

static int ClampPenWidth(int width) {
    if (width < kPenWidthMin) return kPenWidthMin;
    if (width > kPenWidthMax) return kPenWidthMax;
    return width;
}

void LoadFeatureFlags() {
    ApplyDefaults();
    gGridSpacing = kDefaultGridSpacing;
    penWidth = kDefaultPenWidth;

    char iniPath[MAX_PATH] = {};
    GetFeatureFlagsIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;

    for (const FeatureMeta& f : kFeatures) {
        const int def = f.defaultOn ? 1 : 0;
        const int val = GetPrivateProfileIntA("Features", f.iniKey, def, iniPath);
        gFlags[static_cast<int>(f.id)] = (val != 0);
    }
    gGridSpacing = NormalizeGridSpacing(
        GetPrivateProfileIntA("Features", "GridSpacing", kDefaultGridSpacing, iniPath));
    penWidth = ClampPenWidth(
        GetPrivateProfileIntA("Features", "PenWidth", kDefaultPenWidth, iniPath));
}

void SaveFeatureFlags() {
    char iniPath[MAX_PATH] = {};
    GetFeatureFlagsIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;

    for (const FeatureMeta& f : kFeatures) {
        const char* val = IsFeatureEnabled(f.id) ? "1" : "0";
        WritePrivateProfileStringA("Features", f.iniKey, val, iniPath);
    }
    char spacingBuf[16];
    sprintf_s(spacingBuf, "%d", NormalizeGridSpacing(gGridSpacing));
    WritePrivateProfileStringA("Features", "GridSpacing", spacingBuf, iniPath);
    char widthBuf[16];
    sprintf_s(widthBuf, "%d", ClampPenWidth(penWidth));
    WritePrivateProfileStringA("Features", "PenWidth", widthBuf, iniPath);
}

int NormalizeGridSpacing(int spacing) {
    if (spacing <= 8) return 8;
    if (spacing <= 16) return 16;
    if (spacing <= 32) return 32;
    return 64;
}

void SetGridSpacing(int spacing) {
    gGridSpacing = NormalizeGridSpacing(spacing);
    SaveFeatureFlags();
    SyncFeatureFlagMenuItems();
}

void SyncFeatureFlagMenuItems() {
    if (!gAppMenu) return;
    HMENU viewMenu = GetSubMenu(gAppMenu, 3); // File, Edit, Image, View
    if (viewMenu) {
        SetMenuCheck(viewMenu, IDM_FEAT_PASTE_VIEW, IsFeatureEnabled(AppFeature::PasteAtViewOrigin));
        SetMenuCheck(viewMenu, IDM_FEAT_SEL_VEIL, IsFeatureEnabled(AppFeature::SelectionExteriorVeil));
        SetMenuCheck(viewMenu, IDM_FEAT_WARN_SHRINK, IsFeatureEnabled(AppFeature::WarnCanvasShrink));
        SetMenuCheck(viewMenu, IDM_FEAT_REOPEN_LAST, IsFeatureEnabled(AppFeature::ReopenLastDocument));
        SetMenuCheck(viewMenu, IDM_FEAT_AUTOSAVE, IsFeatureEnabled(AppFeature::AutosaveRecovery));
        SetMenuCheck(viewMenu, IDM_FEAT_GRID, IsFeatureEnabled(AppFeature::CanvasGrid));
        SetMenuCheck(viewMenu, IDM_FEAT_SNAP_GRID, IsFeatureEnabled(AppFeature::SnapToGrid));

        const int spacing = NormalizeGridSpacing(gGridSpacing);
        CheckMenuItem(viewMenu, IDM_GRID_8, MF_BYCOMMAND | (spacing == 8 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(viewMenu, IDM_GRID_16, MF_BYCOMMAND | (spacing == 16 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(viewMenu, IDM_GRID_32, MF_BYCOMMAND | (spacing == 32 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(viewMenu, IDM_GRID_64, MF_BYCOMMAND | (spacing == 64 ? MF_CHECKED : MF_UNCHECKED));
    }

    HMENU toolsMenu = GetSubMenu(gAppMenu, 4); // Tools
    if (toolsMenu) {
        const int w = ClampPenWidth(penWidth);
        CheckMenuItem(toolsMenu, IDM_BRUSH_FINE,
            MF_BYCOMMAND | (w == kBrushPresetFine ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(toolsMenu, IDM_BRUSH_MEDIUM,
            MF_BYCOMMAND | (w == kBrushPresetMedium ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(toolsMenu, IDM_BRUSH_BOLD,
            MF_BYCOMMAND | (w == kBrushPresetBold ? MF_CHECKED : MF_UNCHECKED));
    }
    SyncBrushMenuItems();
}
