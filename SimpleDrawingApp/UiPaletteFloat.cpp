#include "UiPaletteFloat.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AtelierPalette.h"
#include <windowsx.h>

namespace {

void LayoutFloatPaletteContents() {
    if (!hwndPaletteFloat || !IsWindow(hwndPaletteFloat)) return;
    RECT rc = {};
    GetClientRect(hwndPaletteFloat, &rc);
    const int w = rc.right - rc.left;
    const int pad = 6;
    int y = FLOAT_DRAG_H + 4;
    if (hwndBgButton && GetParent(hwndBgButton) == hwndPaletteFloat) {
        MoveWindow(hwndBgButton, pad + 14, y + 10, 20, 20, TRUE);
    }
    if (hwndActionButtons[0] && GetParent(hwndActionButtons[0]) == hwndPaletteFloat) {
        MoveWindow(hwndActionButtons[0], pad, y, 22, 22, TRUE);
    }
    if (hwndSwapColors && GetParent(hwndSwapColors) == hwndPaletteFloat) {
        MoveWindow(hwndSwapColors, pad + 28, y - 2, 18, 18, TRUE);
    }
    y += FLOAT_CHIP_H;
    if (hwndPalette && GetParent(hwndPalette) == hwndPaletteFloat) {
        const int palW = MaxInt(72, w - pad * 2);
        const int palH = AtelierPalette_IdealHeight(palW);
        MoveWindow(hwndPalette, pad, y, palW, palH, TRUE);
    }
}

int PaletteFloatHeight(int width) {
    return FLOAT_DRAG_H + 4 + FLOAT_CHIP_H + AtelierPalette_IdealHeight(MaxInt(72, width - 12)) + 8;
}

LRESULT CALLBACK PaletteFloatProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(gTheme.chromeDeep);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        RECT drag = { 0, 0, rc.right, FLOAT_DRAG_H };
        HBRUSH bar = CreateSolidBrush(gTheme.chromeElevated);
        FillRect(hdc, &drag, bar);
        DeleteObject(bar);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, gTheme.accentDeep);
        HGDIOBJ oldFont = gUiFont ? SelectObject(hdc, gUiFont) : nullptr;
        TextOutA(hdc, 8, 4, "Color", 5);
        if (oldFont) SelectObject(hdc, oldFont);
        for (int i = 0; i < 3; ++i) {
            RECT d = { rc.right - 18, 6 + i * 4, rc.right - 10, 8 + i * 4 };
            HBRUSH dot = CreateSolidBrush(gTheme.chromeLine);
            FillRect(hdc, &d, dot);
            DeleteObject(dot);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int y = GET_Y_LPARAM(lParam);
        if (y < FLOAT_DRAG_H) {
            gFloatDragging = true;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            RECT wr = {};
            GetWindowRect(hwnd, &wr);
            gFloatDragHot.x = pt.x - wr.left;
            gFloatDragHot.y = pt.y - wr.top;
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (gFloatDragging && (wParam & MK_LBUTTON)) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            gPaletteFloatPos.x = pt.x - gFloatDragHot.x;
            gPaletteFloatPos.y = pt.y - gFloatDragHot.y;
            SetWindowPos(hwnd, nullptr, gPaletteFloatPos.x, gPaletteFloatPos.y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        gFloatDragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_COMMAND:
        if (HWND main = GetWindow(hwnd, GW_OWNER)) {
            return SendMessageA(main, msg, wParam, lParam);
        }
        break;
    case WM_DRAWITEM:
        if (HWND main = GetWindow(hwnd, GW_OWNER)) {
            return SendMessageA(main, msg, wParam, lParam);
        }
        break;
    case WM_SIZE:
        LayoutFloatPaletteContents();
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcA(hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.y < FLOAT_DRAG_H) return HTCAPTION;
        }
        return hit;
    }
    case WM_DESTROY:
        if (hwndPaletteFloat == hwnd) {
            hwndPaletteFloat = nullptr;
            gFloatDragging = false;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void EnsurePaletteFloatHost(HWND owner) {
    if (hwndPaletteFloat && IsWindow(hwndPaletteFloat)) return;
    const int w = TOOL_RAIL_WIDTH;
    const int h = PaletteFloatHeight(w);
    hwndPaletteFloat = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "AtelierPaletteFloat",
        "Color",
        WS_POPUP | WS_CLIPCHILDREN | WS_BORDER,
        gPaletteFloatPos.x, gPaletteFloatPos.y, w, h,
        owner, NULL, GetModuleHandle(NULL), NULL);
}

}  // namespace

bool RegisterPaletteFloatClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = PaletteFloatProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "AtelierPaletteFloat";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    return RegisterClassA(&wc) != 0;
}

void DockPaletteInstruments(HWND mainHwnd) {
    (void)mainHwnd;
    if (!gPaletteFloating) return;
    gPaletteFloating = false;
    if (hwndPalette) SetParent(hwndPalette, mainHwnd);
    if (hwndActionButtons[0]) SetParent(hwndActionButtons[0], mainHwnd);
    if (hwndBgButton) SetParent(hwndBgButton, mainHwnd);
    if (hwndSwapColors) SetParent(hwndSwapColors, mainHwnd);
    if (hwndPaletteFloat) ShowWindow(hwndPaletteFloat, SW_HIDE);
}

void UndockPaletteInstruments(HWND mainHwnd) {
    EnsurePaletteFloatHost(mainHwnd);
    if (!hwndPaletteFloat) return;
    gPaletteFloating = true;

    if (hwndActionButtons[0]) SetParent(hwndActionButtons[0], hwndPaletteFloat);
    if (hwndBgButton) SetParent(hwndBgButton, hwndPaletteFloat);
    if (hwndSwapColors) SetParent(hwndSwapColors, hwndPaletteFloat);
    if (hwndPalette) SetParent(hwndPalette, hwndPaletteFloat);

    if (hwndActionButtons[0]) ShowWindow(hwndActionButtons[0], SW_SHOW);
    if (hwndBgButton) ShowWindow(hwndBgButton, SW_SHOW);
    if (hwndSwapColors) ShowWindow(hwndSwapColors, SW_SHOW);
    if (hwndPalette) ShowWindow(hwndPalette, SW_SHOW);

    const int w = TOOL_RAIL_WIDTH;
    const int h = PaletteFloatHeight(w);
    SetWindowPos(hwndPaletteFloat, HWND_TOPMOST,
        gPaletteFloatPos.x, gPaletteFloatPos.y, w, h,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    LayoutFloatPaletteContents();
    if (hwndPalette) AtelierPalette_SetColors(hwndPalette, penColor, backColor);
}
