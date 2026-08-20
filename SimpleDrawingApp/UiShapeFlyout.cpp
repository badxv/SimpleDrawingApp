#include "UiShapeFlyout.h"
#include "UiControls.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "Resource.h"

void SyncShapeFlyoutChecks() {
    const int shapeIds[6] = {
        IDC_SHAPE_RECT, IDC_SHAPE_ELLIPSE, IDC_SHAPE_TRIANGLE,
        IDC_SHAPE_STAR, IDC_SHAPE_DIAMOND, IDC_SHAPE_ROUNDRECT
    };
    (void)shapeIds;
    for (int i = 0; i < 6; ++i) {
        if (!hwndShapeButtons[i]) continue;
        const bool on = (currentTool == DrawTool::Shape && static_cast<int>(currentShape) == i);
        SendMessageA(hwndShapeButtons[i], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(hwndShapeButtons[i], NULL, FALSE);
    }
    for (int i = 0; i < 3; ++i) {
        if (!hwndShapeModeButtons[i]) continue;
        const bool on = (static_cast<int>(shapePaintMode) == i);
        SendMessageA(hwndShapeModeButtons[i], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(hwndShapeModeButtons[i], NULL, FALSE);
    }
}

void CloseShapeFlyout() {
    if (hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
        ShowWindow(hwndShapeFlyout, SW_HIDE);
    }
}

namespace {

LRESULT CALLBACK ShapeFlyoutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            PostMessageA(hwnd, WM_APP + 7, 0, 0);
        }
        return 0;
    case WM_APP + 7: {
        POINT pt = {};
        GetCursorPos(&pt);
        HWND under = WindowFromPoint(pt);
        if (under) {
            if (under == hwndShapeFlyout || IsChild(hwndShapeFlyout, under)) return 0;
            if (under == hwndToolButtons[static_cast<int>(DrawTool::Shape)]) return 0;
        }
        if (hwndShapeFlyout == hwnd) {
            CloseShapeFlyout();
        }
        return 0;
    }
    case WM_COMMAND: {
        HWND parent = GetParent(hwnd);
        if (!parent) parent = GetWindow(hwnd, GW_OWNER);
        if (parent) {
            SendMessageA(parent, WM_COMMAND, wParam, lParam);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        HWND parent = GetWindow(hwnd, GW_OWNER);
        if (parent) {
            return SendMessageA(parent, WM_DRAWITEM, wParam, lParam);
        }
        break;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)gChromeElevatedBrush;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
        HPEN pen = CreatePen(PS_SOLID, 1, gTheme.chromeLine);
        HGDIOBJ old = SelectObject(hdc, pen);
        HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        MoveToEx(hdc, 8, 84, NULL);
        LineTo(hdc, rc.right - 8, 84);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, old);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (hwndShapeFlyout == hwnd) {
            hwndShapeFlyout = nullptr;
            for (HWND& btn : hwndShapeButtons) btn = nullptr;
            for (HWND& btn : hwndShapeModeButtons) btn = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void EnsureShapeFlyout(HWND owner) {
    if (hwndShapeFlyout && IsWindow(hwndShapeFlyout)) return;
    hwndShapeFlyout = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "SimpleDrawingAppShapeFlyout",
        "",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 156, 128,
        owner, NULL, GetModuleHandle(NULL), NULL);
    if (!hwndShapeFlyout) return;

    const int ids[6] = {
        IDC_SHAPE_RECT, IDC_SHAPE_ELLIPSE, IDC_SHAPE_TRIANGLE,
        IDC_SHAPE_STAR, IDC_SHAPE_DIAMOND, IDC_SHAPE_ROUNDRECT
    };
    const char* tips[6] = {
        "Rectangle", "Ellipse", "Triangle", "Star", "Diamond", "Rounded rectangle"
    };
    for (int i = 0; i < 6; ++i) {
        hwndShapeButtons[i] = CreateIconButton(hwndShapeFlyout, ids[i], tips[i], true);
        const int col = i % 3;
        const int row = i / 3;
        MoveWindow(hwndShapeButtons[i], 10 + col * (ICON_BTN + 6), 10 + row * (ICON_BTN + 6), ICON_BTN, ICON_BTN, TRUE);
    }

    const int modeIds[3] = { IDC_SHAPE_MODE_STROKE, IDC_SHAPE_MODE_FILL, IDC_SHAPE_MODE_BOTH };
    const char* modeTips[3] = {
        "Stroke only (foreground)",
        "Fill only (background)",
        "Fill + stroke (BG fill, FG stroke)"
    };
    for (int i = 0; i < 3; ++i) {
        hwndShapeModeButtons[i] = CreateIconButton(hwndShapeFlyout, modeIds[i], modeTips[i], true);
        MoveWindow(hwndShapeModeButtons[i], 10 + i * (ICON_BTN + 6), 92, ICON_BTN, ICON_BTN, TRUE);
    }
    SyncShapeFlyoutChecks();
}

}  // namespace

bool RegisterShapeFlyoutClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = ShapeFlyoutProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppShapeFlyout";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void OpenShapeFlyout(HWND parent) {
    if (!parent) return;
    EnsureShapeFlyout(parent);
    if (!hwndShapeFlyout || !hwndToolButtons[static_cast<int>(DrawTool::Shape)]) return;

    RECT br = {};
    GetWindowRect(hwndToolButtons[static_cast<int>(DrawTool::Shape)], &br);
    const int w = 156;
    const int h = 128;
    SetWindowPos(hwndShapeFlyout, HWND_TOPMOST, br.right + 6, br.top - 4, w, h,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(hwndShapeFlyout);
    SyncShapeFlyoutChecks();
}
