#include "AbrCommon.h"

#include <cstring>
#include <string>

std::string NarrowPathSegment(const wchar_t* wide) {
    if (!wide || !wide[0]) return {};
    const int len = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::string LeafNameFromPath(const wchar_t* path, const char* fallback) {
    if (!path || !path[0]) return fallback ? fallback : "Imported";
    const wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    const wchar_t* base = (slash && slash[1]) ? (slash + 1) : path;
    std::wstring name = base;
    const size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name.erase(dot);
    std::string narrow = NarrowPathSegment(name.c_str());
    return narrow.empty() ? (fallback ? fallback : "Imported") : narrow;
}

std::string MakeAbrBrushName(const wchar_t* path, const std::string& leafName) {
    std::string name = leafName;
    if (!path || !path[0]) return name;
    const wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    const wchar_t* base = (slash && slash[1]) ? (slash + 1) : path;
    std::wstring file = base;
    const size_t dot = file.rfind(L'.');
    if (dot != std::wstring::npos) file.erase(dot);
    std::string fileNarrow = NarrowPathSegment(file.c_str());
    if (!fileNarrow.empty()) {
        name = fileNarrow + "-" + leafName;
    }
    return name;
}
