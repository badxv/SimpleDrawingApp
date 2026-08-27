#include "AppDialogs.h"
#include "Resource.h"

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
            "\r\n"
            "File & edit\r\n"
            "  Ctrl+N     New\r\n"
            "  Ctrl+O     Open\r\n"
            "  Ctrl+S     Save\r\n"
            "  Ctrl+Shift+S  Save As\r\n"
            "  Ctrl+Z/Y   Undo / Redo\r\n"
            "  Ctrl+X/C/V Cut / Copy / Paste\r\n"
            "  Ctrl+A     Select all\r\n"
            "  Del        Delete selection\r\n"
            "  Ctrl+E     Canvas size\r\n"
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


} // namespace

void ShowAboutDialog(HWND owner) {
    DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABOUTBOX), owner, AboutDlgProc);
}

void ShowShortcutsDialog(HWND owner) {
    DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_SHORTCUTS), owner, ShortcutsDlgProc);
}
