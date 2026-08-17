#include "CaptionBar.h"
#include "AppState.h"
#include "Resource.h"
#include <windowsx.h>
#include <dwmapi.h>

using namespace Gdiplus;

#pragma comment(lib, "Dwmapi.lib")

void EnableDarkTitleBar(HWND hwnd) {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
}

int FrameBorderX() {
    return GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

int FrameBorderY() {
    return GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYPADDEDBORDER);
}

void EnableCustomTitleBar(HWND hwnd) {
    EnableDarkTitleBar(hwnd);
    // Wine still draws a WM caption; expanding the client under it hides our chrome.
    if (IsRunningUnderWine()) return;
    // Keep DWM borders; caption is drawn in-client (see WM_NCCALCSIZE).
    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayoutCaptionButtons(HWND hwnd) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int w = CAPTION_BTN_W;
    const int h = CAPTION_BTN_H;
    const int y = 0; // flush to top of client chrome
    if (hwndCaptionClose) {
        MoveWindow(hwndCaptionClose, rc.right - w, y, w, h, TRUE);
    }
    if (hwndCaptionMax) {
        MoveWindow(hwndCaptionMax, rc.right - w * 2, y, w, h, TRUE);
    }
    if (hwndCaptionMin) {
        MoveWindow(hwndCaptionMin, rc.right - w * 3, y, w, h, TRUE);
    }
}

void SetCaptionHot(HWND btn) {
    if (gCaptionHot == btn) return;
    HWND prev = gCaptionHot;
    gCaptionHot = btn;
    if (prev) InvalidateRect(prev, nullptr, FALSE);
    if (btn) InvalidateRect(btn, nullptr, FALSE);
}

void PaintOneCaptionButton(HDC hdc, HWND btn, int id) {
    RECT r = {};
    GetClientRect(btn, &r);
    const bool hot = (gCaptionHot == btn);
    const bool maximized = IsZoomed(GetParent(btn)) != FALSE;

    COLORREF bg = gTheme.chromeBg;
    if (hot) {
        bg = (id == IDC_CAPTION_CLOSE) ? RGB(196, 43, 28) : gTheme.chromeElevated;
    }
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &r, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1,
        (hot && id == IDC_CAPTION_CLOSE) ? RGB(255, 255, 255) : gTheme.ink);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const int cx = (r.left + r.right) / 2;
    const int cy = (r.top + r.bottom) / 2;
    if (id == IDC_CAPTION_MIN) {
        MoveToEx(hdc, cx - 5, cy, nullptr);
        LineTo(hdc, cx + 6, cy);
    } else if (id == IDC_CAPTION_MAX) {
        if (maximized) {
            Rectangle(hdc, cx - 3, cy - 5, cx + 5, cy + 3);
            Rectangle(hdc, cx - 5, cy - 3, cx + 3, cy + 5);
        } else {
            Rectangle(hdc, cx - 5, cy - 5, cx + 6, cy + 6);
        }
    } else if (id == IDC_CAPTION_CLOSE) {
        MoveToEx(hdc, cx - 5, cy - 5, nullptr);
        LineTo(hdc, cx + 6, cy + 6);
        MoveToEx(hdc, cx + 5, cy - 5, nullptr);
        LineTo(hdc, cx - 6, cy + 6);
    }

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK CaptionBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintOneCaptionButton(hdc, hwnd, GetDlgCtrlID(hwnd));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        SetCaptionHot(hwnd);
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (gCaptionHot == hwnd) SetCaptionHot(nullptr);
        return 0;
    case WM_LBUTTONUP: {
        HWND parent = GetParent(hwnd);
        if (parent) {
            SendMessageA(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool RegisterCaptionBtnClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = CaptionBtnProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppCaptionBtn";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void CreateCaptionButtons(HWND hwnd) {
    if (IsRunningUnderWine()) return;
    HINSTANCE inst = GetModuleHandle(nullptr);
    auto make = [&](int id) -> HWND {
        return CreateWindowExA(0, "SimpleDrawingAppCaptionBtn", "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, CAPTION_BTN_W, CAPTION_BTN_H,
            hwnd, (HMENU)(INT_PTR)id, inst, nullptr);
    };
    hwndCaptionMin = make(IDC_CAPTION_MIN);
    hwndCaptionMax = make(IDC_CAPTION_MAX);
    hwndCaptionClose = make(IDC_CAPTION_CLOSE);
    LayoutCaptionButtons(hwnd);
}

bool PointOverTopBarControl(HWND hwnd, POINT ptClient) {
    HWND hit = ChildWindowFromPointEx(hwnd, ptClient, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
    if (!hit || hit == hwnd) return false;
    if (hit == hwndBrand) return false; // decorative — allow window drag
    return true;
}
