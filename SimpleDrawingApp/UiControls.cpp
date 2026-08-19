#include "UiControls.h"
#include "AppState.h"
#include "AppMetrics.h"
#include <commctrl.h>

void ApplyUiFont(HWND control) {
    if (gUiFont && control) {
        SendMessageA(control, WM_SETFONT, (WPARAM)gUiFont, TRUE);
    }
}

HWND CreateIconButton(HWND parent, int id, const char* tooltip, bool pushLike) {
    const DWORD style = WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW |
        (pushLike ? (BS_PUSHLIKE | BS_CHECKBOX) : BS_PUSHBUTTON);
    HWND btn = CreateWindowA("BUTTON", "", style,
        0, 0, ICON_BTN, ICON_BTN,
        parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    if (hwndTooltip && btn && tooltip && tooltip[0]) {
        TOOLINFOA ti = {};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = parent;
        ti.uId = (UINT_PTR)btn;
        ti.lpszText = const_cast<char*>(tooltip);
        SendMessageA(hwndTooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
    }
    return btn;
}
