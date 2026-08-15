#include "AtelierFonts.h"

#include <string>
#include <vector>

namespace {

std::wstring ModuleDirW() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return full.substr(0, slash);
}

std::wstring CandidatePath(const std::wstring& file) {
    const std::wstring dir = ModuleDirW();
    return dir + L"\\fonts\\" + file;
}

std::wstring CandidatePathAlt(const std::wstring& file) {
    const std::wstring dir = ModuleDirW();
    return dir + L"\\..\\fonts\\" + file; // when running from build-linux/
}

bool FileExistsW(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<std::wstring> gLoadedFonts;
bool gInited = false;
std::wstring gDisplayFamily = L"Georgia";
std::wstring gUiFamily = L"Segoe UI";

bool TryAddFont(const std::wstring& file, const wchar_t* familyName) {
    std::wstring path = CandidatePath(file);
    if (!FileExistsW(path)) path = CandidatePathAlt(file);
    if (!FileExistsW(path)) {
        // Dev tree: repo/fonts next to cwd
        path = L"fonts\\" + file;
        if (!FileExistsW(path)) {
            path = L"..\\fonts\\" + file;
        }
    }
    if (!FileExistsW(path)) return false;
    if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0) {
        gLoadedFonts.push_back(path);
        if (familyName && familyName[0]) {
            // Keep last successful mapping by caller.
        }
        return true;
    }
    return false;
}

HFONT MakeFont(const std::wstring& family, int heightPx, bool italic, bool bold) {
    return CreateFontW(
        -heightPx, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        family.c_str());
}

} // namespace

bool AtelierFonts_Init() {
    if (gInited) return true;
    gInited = true;

    // Variable Cinzel often registers as "Cinzel".
    if (TryAddFont(L"Cinzel-Regular.ttf", L"Cinzel")) {
        gDisplayFamily = L"Cinzel";
    }
    if (TryAddFont(L"DMSans-Regular.ttf", L"DM Sans")) {
        gUiFamily = L"DM Sans";
    }
    return true;
}

void AtelierFonts_Shutdown() {
    for (const std::wstring& path : gLoadedFonts) {
        RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    }
    gLoadedFonts.clear();
    gInited = false;
}

HFONT AtelierFonts_Display(int heightPx, bool italic) {
    HFONT font = MakeFont(gDisplayFamily, heightPx, italic, false);
    if (!font && gDisplayFamily != L"Georgia") {
        font = MakeFont(L"Georgia", heightPx, italic, false);
    }
    return font;
}

HFONT AtelierFonts_Ui(int heightPx, bool bold) {
    HFONT font = MakeFont(gUiFamily, heightPx, false, bold);
    if (!font && gUiFamily != L"Segoe UI") {
        font = MakeFont(L"Segoe UI", heightPx, false, bold);
    }
    return font;
}

const wchar_t* AtelierFonts_DisplayFamilyW() {
    return gDisplayFamily.c_str();
}

const wchar_t* AtelierFonts_UiFamilyW() {
    return gUiFamily.c_str();
}
