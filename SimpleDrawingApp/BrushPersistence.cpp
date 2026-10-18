#include "BrushPersistence.h"
#include "BrushEngine.h"
#include "BrushPresets.h"
#include "BrushTips.h"
#include "AppPaths.h"
#include "FileManager.h"

#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

bool GetExeDir(char* path, size_t pathChars) {
    if (!path || pathChars == 0) return false;
    path[0] = '\0';
    DWORD len = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(pathChars));
    if (len == 0 || len >= pathChars) return false;
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') {
            path[i + 1] = '\0';
            return true;
        }
    }
    return false;
}

bool GetBrushesDir(char* path, size_t pathChars) {
    if (!GetExeDir(path, pathChars)) return false;
    if (strlen(path) + 8 > pathChars) return false;
    strcat_s(path, pathChars, "brushes");
    CreateDirectoryA(path, nullptr);
    return true;
}

bool GetBrushesIniPath(char* path, size_t pathChars) {
    if (!GetBrushesDir(path, pathChars)) return false;
    if (strlen(path) + 12 > pathChars) return false;
    strcat_s(path, pathChars, "\\brushes.ini");
    return true;
}

std::wstring PathToWide(const char* path) {
    if (!path || !path[0]) return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path, -1, &out[0], len);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

} // namespace

void SaveCustomBrushesToDisk() {
    char dir[MAX_PATH] = {};
    char iniPath[MAX_PATH] = {};
    if (!GetBrushesDir(dir, MAX_PATH) || !GetBrushesIniPath(iniPath, MAX_PATH)) return;

    const int total = BrushPresetCount();
    std::vector<int> customIndices;
    customIndices.reserve(8);
    for (int i = 0; i < total; ++i) {
        const BrushPreset* preset = GetBrushPreset(i);
        if (preset && !preset->builtin) customIndices.push_back(i);
    }

    char buf[32];
    sprintf_s(buf, "%d", static_cast<int>(customIndices.size()));
    WritePrivateProfileStringA("CustomBrushes", "Count", buf, iniPath);

    int slot = 0;
    for (int idx : customIndices) {
        const BrushPreset* preset = GetBrushPreset(idx);
        if (!preset || !preset->tip) continue;

        char section[32];
        sprintf_s(section, "Brush%d", slot);

        char tipPath[MAX_PATH];
        sprintf_s(tipPath, "%s\\custom_%03d.png", dir, slot + 1);
        if (!SaveCanvasToFile(preset->tip.get(), tipPath)) continue;

        WritePrivateProfileStringA(section, "Name", preset->name.c_str(), iniPath);
        sprintf_s(buf, "%.4f", preset->spacing);
        WritePrivateProfileStringA(section, "Spacing", buf, iniPath);
        sprintf_s(buf, "%d", preset->defaultSize);
        WritePrivateProfileStringA(section, "DefaultSize", buf, iniPath);

        char tipFile[32];
        sprintf_s(tipFile, "custom_%03d.png", slot + 1);
        WritePrivateProfileStringA(section, "TipFile", tipFile, iniPath);
        ++slot;
    }

    // Trim stale sections beyond current count.
    for (int stale = slot; stale < 32; ++stale) {
        char section[32];
        sprintf_s(section, "Brush%d", stale);
        WritePrivateProfileStringA(section, nullptr, nullptr, iniPath);
    }
}

void LoadCustomBrushesFromDisk() {
    char dir[MAX_PATH] = {};
    char iniPath[MAX_PATH] = {};
    if (!GetBrushesDir(dir, MAX_PATH) || !GetBrushesIniPath(iniPath, MAX_PATH)) return;

    const int count = GetPrivateProfileIntA("CustomBrushes", "Count", 0, iniPath);
    if (count < 1) return;

    for (int i = 0; i < count && i < 32; ++i) {
        if (!BrushPresetsCanAddCustom()) break;

        char section[32];
        sprintf_s(section, "Brush%d", i);

        char tipFile[64] = {};
        GetPrivateProfileStringA(section, "TipFile", "", tipFile, sizeof(tipFile), iniPath);
        if (!tipFile[0]) continue;

        char tipPath[MAX_PATH];
        sprintf_s(tipPath, "%s\\%s", dir, tipFile);

        Bitmap loaded(PathToWide(tipPath).c_str());
        if (loaded.GetLastStatus() != Ok) continue;
        Bitmap* tip = NormalizeImportedTip(&loaded);
        if (!tip) continue;

        char name[80] = "Imported";
        GetPrivateProfileStringA(section, "Name", "Imported", name, sizeof(name), iniPath);

        char spacingBuf[32] = "0.15";
        GetPrivateProfileStringA(section, "Spacing", "0.15", spacingBuf, sizeof(spacingBuf), iniPath);
        const float spacing = static_cast<float>(atof(spacingBuf));

        int defaultSize = GetPrivateProfileIntA(section, "DefaultSize", 12, iniPath);
        if (defaultSize < 1) defaultSize = 12;
        if (defaultSize > 64) defaultSize = 64;

        if (!BrushPresetsAddCustom(name, tip, spacing, defaultSize, false)) {
            delete tip;
        }
    }
}
