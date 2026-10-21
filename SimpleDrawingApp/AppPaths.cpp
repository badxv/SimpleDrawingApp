#include "AppPaths.h"

#include <cstring>

namespace {

bool NarrowToWide(const char* narrow, wchar_t* wide, size_t wideChars) {
    if (!wide || wideChars == 0) return false;
    wide[0] = L'\0';
    if (!narrow) return true;
    const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, static_cast<int>(wideChars));
    if (n > 0) return true;
    return MultiByteToWideChar(CP_ACP, 0, narrow, -1, wide, static_cast<int>(wideChars)) > 0;
}

} // namespace

void GetModuleSiblingPathW(wchar_t* path, size_t pathChars, const wchar_t* fileName) {
    if (!path || pathChars == 0) return;
    path[0] = L'\0';
    if (!fileName || !fileName[0]) return;
    DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(pathChars));
    if (len == 0 || len >= pathChars) {
        path[0] = L'\0';
        return;
    }
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            path[i + 1] = L'\0';
            break;
        }
    }
    if (wcslen(path) + wcslen(fileName) + 1 > pathChars) {
        path[0] = L'\0';
        return;
    }
    wcscat_s(path, pathChars, fileName);
}

void GetFeaturesIniPathW(wchar_t* path, size_t pathChars) {
    GetModuleSiblingPathW(path, pathChars, L"features.ini");
}

bool FeaturesIniWrite(const wchar_t* key, const wchar_t* value) {
    if (!key || !key[0]) return false;
    wchar_t iniPath[MAX_PATH] = {};
    GetFeaturesIniPathW(iniPath, MAX_PATH);
    if (!iniPath[0]) return false;
    return WritePrivateProfileStringW(L"Features", key, value ? value : L"", iniPath) != 0;
}

bool FeaturesIniWriteA(const char* key, const char* value) {
    wchar_t keyW[128] = {};
    wchar_t valueW[64] = {};
    if (!NarrowToWide(key, keyW, 128)) return false;
    if (!NarrowToWide(value ? value : "", valueW, 64)) return false;
    return FeaturesIniWrite(keyW, valueW);
}

int FeaturesIniReadInt(const wchar_t* key, int defaultValue) {
    if (!key || !key[0]) return defaultValue;
    wchar_t iniPath[MAX_PATH] = {};
    GetFeaturesIniPathW(iniPath, MAX_PATH);
    if (!iniPath[0]) return defaultValue;
    return GetPrivateProfileIntW(L"Features", key, defaultValue, iniPath);
}

int FeaturesIniReadIntA(const char* key, int defaultValue) {
    wchar_t keyW[128] = {};
    if (!NarrowToWide(key, keyW, 128)) return defaultValue;
    return FeaturesIniReadInt(keyW, defaultValue);
}
