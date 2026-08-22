#include "AppFeatureFlags.h"
#include "AppState.h"
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

void LoadFeatureFlags() {
    ApplyDefaults();

    char iniPath[MAX_PATH] = {};
    GetFeatureFlagsIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;

    for (const FeatureMeta& f : kFeatures) {
        const int def = f.defaultOn ? 1 : 0;
        const int val = GetPrivateProfileIntA("Features", f.iniKey, def, iniPath);
        gFlags[static_cast<int>(f.id)] = (val != 0);
    }
}

void SaveFeatureFlags() {
    char iniPath[MAX_PATH] = {};
    GetFeatureFlagsIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;

    for (const FeatureMeta& f : kFeatures) {
        const char* val = IsFeatureEnabled(f.id) ? "1" : "0";
        WritePrivateProfileStringA("Features", f.iniKey, val, iniPath);
    }
}

void SyncFeatureFlagMenuItems() {
    if (!gAppMenu) return;
    HMENU viewMenu = GetSubMenu(gAppMenu, 3); // File, Edit, Image, View
    if (!viewMenu) return;

    SetMenuCheck(viewMenu, IDM_FEAT_PASTE_VIEW, IsFeatureEnabled(AppFeature::PasteAtViewOrigin));
    SetMenuCheck(viewMenu, IDM_FEAT_SEL_VEIL, IsFeatureEnabled(AppFeature::SelectionExteriorVeil));
    SetMenuCheck(viewMenu, IDM_FEAT_WARN_SHRINK, IsFeatureEnabled(AppFeature::WarnCanvasShrink));
}
