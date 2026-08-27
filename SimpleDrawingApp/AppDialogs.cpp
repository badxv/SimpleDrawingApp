#include "AppDialogs.h"
#include "AppState.h"
#include "LayerStack.h"
#include "Resource.h"

#include <cstring>

namespace {

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK ShortcutsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        const char* text =
            "Tools\r\n"
            "  B          Pen\r\n"
            "  E          Eraser\r\n"
            "  G          Fill (bucket)\r\n"
            "  M          Select\r\n"
            "  L          Line\r\n"
            "  U          Shapes\r\n"
            "  X          Swap foreground / background\r\n"
            "  Tab        Toggle tools rail (floating color when hidden)\r\n"
            "  F9         Toggle layers panel\r\n"
            "  F8         Toggle size/opacity bar\r\n"
            "\r\n"
            "While drawing a shape\r\n"
            "  Alt        Fill only (hold)\r\n"
            "  Ctrl       Stroke + Fill (hold)\r\n"
            "  Shift      Constrain proportions\r\n"
            "\r\n"
            "Brush\r\n"
            "  [ / ]      Decrease / increase size\r\n"
            "  Tools → Brush Size   Fine / Medium / Bold presets\r\n"
            "\r\n"
            "Layers\r\n"
            "  Double-click / F2   Rename active layer\r\n"
            "\r\n"
            "File & edit\r\n"
            "  Ctrl+N     New\r\n"
            "  Ctrl+O     Open\r\n"
            "  Ctrl+Shift+O  Open Last Document\r\n"
            "  Ctrl+S     Save\r\n"
            "  Ctrl+Shift+S  Save As\r\n"
            "  Ctrl+Shift+E  Export As\r\n"
            "  Ctrl+P     Print\r\n"
            "  Ctrl+Z/Y   Undo / Redo (depth limited)\r\n"
            "  Ctrl+X/C/V Cut / Copy / Paste\r\n"
            "  Ctrl+A     Select all\r\n"
            "  Del        Delete selection\r\n"
            "  Ctrl+E     Canvas size\r\n"
            "  Image → Flatten Layers   Merge visible layers\r\n"
            "  Image → Flip / Rotate    Horizontal, Vertical, 90 CW\r\n"
            "\r\n"
            "View\r\n"
            "  Ctrl++/-   Zoom in / out\r\n"
            "  Ctrl+0     Actual size\r\n"
            "  F1         This shortcuts list\r\n";
        SetDlgItemTextA(hDlg, IDC_SHORTCUTS_TEXT, text);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

struct LayerRenameState {
    char out[80];
};

static INT_PTR CALLBACK LayerRenameDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    LayerRenameState* state = reinterpret_cast<LayerRenameState*>(GetWindowLongPtrA(hDlg, GWLP_USERDATA));
    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<LayerRenameState*>(lParam);
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        const Layer* layer = gLayers.ActiveLayer();
        if (layer) {
            SetDlgItemTextA(hDlg, IDC_LAYER_NAME, layer->name.c_str());
        }
        if (HWND edit = GetDlgItem(hDlg, IDC_LAYER_NAME)) {
            SendMessageA(edit, EM_SETSEL, 0, -1);
            SetFocus(edit);
            return FALSE;
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (state) {
                GetDlgItemTextA(hDlg, IDC_LAYER_NAME, state->out, sizeof(state->out));
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

} // namespace

void ShowAboutDialog(HWND owner) {
    DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABOUTBOX), owner, AboutDlgProc);
}

void ShowShortcutsDialog(HWND owner) {
    DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_SHORTCUTS), owner, ShortcutsDlgProc);
}

bool PromptLayerRename(HWND owner, char* outName, size_t outChars) {
    if (!outName || outChars == 0 || !gLayers.ActiveLayer()) return false;
    outName[0] = '\0';

    LayerRenameState state = {};
    if (DialogBoxParamA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_LAYER_RENAME), owner,
            LayerRenameDlgProc, reinterpret_cast<LPARAM>(&state)) != IDOK) {
        return false;
    }
    strncpy(outName, state.out, outChars - 1);
    outName[outChars - 1] = '\0';
    return true;
}
