#include "AbrCommon.h"

#include <cstring>
#include <string>

std::string NarrowPathSegment(const wchar_t* wide) {
    if (!wide || !wide[0]) return {};
    // UTF-8 so imported brush names keep non-ASCII path segments.
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::string DecodeUcs2ToUtf8(const std::uint8_t* raw, std::size_t charCount) {
    if (!raw || charCount == 0) return {};
    std::wstring wide;
    wide.reserve(charCount);
    for (std::size_t i = 0; i < charCount; ++i) {
        const wchar_t ch = static_cast<wchar_t>(
            raw[i * 2] | (static_cast<unsigned>(raw[i * 2 + 1]) << 8));
        if (ch == 0) break;
        wide.push_back(ch);
    }
    if (wide.empty()) return {};
    return NarrowPathSegment(wide.c_str());
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
