#include "BrushEngine.h"
#include "AbrImport.h"
#include "AbrCommon.h"
#include "BrushTips.h"
#include "BrushPresets.h"
#include "AppMetrics.h"
#include "Resource.h"
#include "AppState.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

std::wstring PathToWide(const char* path) {
    if (!path || !path[0]) return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path, -1, &out[0], len);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

struct AbrImportPickerState {
    const std::vector<AbrBrush>* samples = nullptr;
    std::vector<int> selected;
};

void AbrListSelectAll(HWND list, int count, bool select) {
    for (int i = 0; i < count; ++i) {
        SendMessageA(list, LB_SETSEL, select ? TRUE : FALSE, i);
    }
}

INT_PTR CALLBACK AbrImportDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    AbrImportPickerState* state = reinterpret_cast<AbrImportPickerState*>(
        GetWindowLongPtrA(hDlg, GWLP_USERDATA));
    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<AbrImportPickerState*>(lParam);
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state || !state->samples) return TRUE;
        HWND list = GetDlgItem(hDlg, IDC_ABR_LIST);
        if (!list) return TRUE;
        const auto& samples = *state->samples;
        for (const AbrBrush& sample : samples) {
            char line[128];
            snprintf(line, sizeof(line), "%s (%dx%d)", sample.name.c_str(), sample.width, sample.height);
            SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
        }
        AbrListSelectAll(list, static_cast<int>(samples.size()), true);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ABR_SELECT_ALL:
            if (state && state->samples) {
                if (HWND list = GetDlgItem(hDlg, IDC_ABR_LIST)) {
                    AbrListSelectAll(list, static_cast<int>(state->samples->size()), true);
                }
            }
            return TRUE;
        case IDC_ABR_CLEAR_ALL:
            if (state && state->samples) {
                if (HWND list = GetDlgItem(hDlg, IDC_ABR_LIST)) {
                    AbrListSelectAll(list, static_cast<int>(state->samples->size()), false);
                }
            }
            return TRUE;
        case IDOK:
            if (state && state->samples) {
                HWND list = GetDlgItem(hDlg, IDC_ABR_LIST);
                if (!list) {
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
                }
                const int count = static_cast<int>(SendMessageA(list, LB_GETSELCOUNT, 0, 0));
                if (count <= 0) {
                    MessageBoxA(hDlg, "Select at least one brush.", "Import ABR", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                state->selected.resize(static_cast<size_t>(count));
                SendMessageA(list, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(state->selected.data()));
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool PromptAbrBrushSelection(HWND owner, const std::vector<AbrBrush>& samples, std::vector<int>& outIndices) {
    outIndices.clear();
    if (samples.empty()) return false;
    if (samples.size() == 1) {
        outIndices.push_back(0);
        return true;
    }
    AbrImportPickerState state = {};
    state.samples = &samples;
    if (DialogBoxParamA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABR_IMPORT), owner,
            AbrImportDlgProc, reinterpret_cast<LPARAM>(&state)) != IDOK) {
        return false;
    }
    outIndices = std::move(state.selected);
    return !outIndices.empty();
}

} // namespace

bool ImportBrushTipFromFile(HWND owner, const char* path) {
    if (!path || !path[0]) return false;
    if (!BrushPresetsCanAddCustom()) {
        MessageBoxA(owner, "Custom brush limit reached.", "Import Brush", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    Bitmap loaded(PathToWide(path).c_str());
    if (loaded.GetLastStatus() != Ok) return false;
    Bitmap* tip = NormalizeImportedTip(&loaded);
    if (!tip) return false;

    const std::string name = LeafNameFromPath(path, "Imported");
    if (!BrushPresetsAddCustom(name.c_str(), tip, 0.15f, 12)) {
        delete tip;
        return false;
    }
    BrushPresetsSetActive(BrushPresetCount() - 1);
    BrushPresetsSaveSettings();
    BrushPresetsSyncMenu();
    return true;
}

bool PromptImportBrushTip(HWND owner) {
    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Brush Tip Images\0*.png;*.bmp;*.jpg;*.jpeg\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (gLastBrowseDir[0]) ofn.lpstrInitialDir = gLastBrowseDir;
    if (!GetOpenFileNameA(&ofn)) return false;
    if (!ImportBrushTipFromFile(owner, filePath)) {
        MessageBoxA(owner, "Could not load brush tip image.", "Import Brush", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

bool ImportAbrBrushesFromFile(HWND owner, const char* path) {
    if (!path || !path[0]) return false;
    std::vector<AbrBrush> samples;
    std::string error;
    if (!LoadAbrBrushes(path, samples, error)) {
        if (owner && !error.empty()) {
            MessageBoxA(owner, error.c_str(), "Import ABR", MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::vector<int> pickIndices;
    if (!PromptAbrBrushSelection(owner, samples, pickIndices)) {
        return false;
    }

    int imported = 0;
    int firstIndex = -1;
    for (int sampleIdx : pickIndices) {
        if (sampleIdx < 0 || sampleIdx >= static_cast<int>(samples.size())) continue;
        if (!BrushPresetsCanAddCustom()) break;

        const AbrBrush& sample = samples[static_cast<size_t>(sampleIdx)];
        Bitmap* tip = TipFromSampleMask(sample.mask.data(), sample.width, sample.height);
        if (!tip) continue;
        const float spacing = MaxFloat(0.05f, static_cast<float>(sample.spacing) / 100.0f);
        int defaultSize = MaxInt(sample.width, sample.height) / 4;
        if (defaultSize < 4) defaultSize = 4;
        if (defaultSize > 32) defaultSize = 32;
        if (!BrushPresetsAddCustom(sample.name.c_str(), tip, spacing, defaultSize)) {
            delete tip;
            continue;
        }
        if (firstIndex < 0) firstIndex = BrushPresetCount() - 1;
        ++imported;
    }

    if (imported <= 0) {
        MessageBoxA(owner, "No brushes could be imported from this ABR file.", "Import ABR", MB_OK | MB_ICONERROR);
        return false;
    }

    BrushPresetsSetActive(firstIndex);
    BrushPresetsSaveSettings();
    BrushPresetsSyncMenu();

    char msg[128];
    sprintf_s(msg, "Imported %d brush(es). Active: %s", imported, BrushPresetsActiveName());
    MessageBoxA(owner, msg, "Import ABR", MB_OK | MB_ICONINFORMATION);
    return true;
}

bool PromptImportAbrBrushes(HWND owner) {
    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Photoshop Brushes\0*.abr\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (gLastBrowseDir[0]) ofn.lpstrInitialDir = gLastBrowseDir;
    if (!GetOpenFileNameA(&ofn)) return false;
    return ImportAbrBrushesFromFile(owner, filePath);
}
