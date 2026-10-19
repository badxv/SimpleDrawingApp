#include "UiExpLab.h"
#include "UiBrushFlyout.h"
#include "BrushEngine.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppShell.h"
#include "UiControls.h"
#include "Resource.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr int kLabW = 230;
constexpr int kLabPad = 10;
constexpr int kLabTitleH = 28;
constexpr int kLabRowH = 22;
constexpr int kLabRowStep = 22;
constexpr int kLabBottomPad = 10;

int gLabScrollPx = 0;

struct LabRow {
    int id;
    int /*BrushSubPanel as int*/ panel;
    const char* tip;
    bool needsMaster; // hidden unless brushExperimental (except master itself)
};

void FormatLabLabel(int cmdId, char* out, size_t outChars);

constexpr int kLabIds[] = {
    IDC_BRUSH_ROW_EXPERIMENTAL,
    IDC_BRUSH_ROW_ECHODELAY,
    IDC_BRUSH_ROW_ECHOFEEDBACK,
    IDC_BRUSH_ROW_PING,
    IDC_BRUSH_ROW_BIOLUM,
    IDC_BRUSH_ROW_HEAT,
    IDC_BRUSH_ROW_CLOCK,
    IDC_BRUSH_ROW_COMPASS,
    IDC_BRUSH_ROW_AFTERIMAGE,
    IDC_BRUSH_ROW_AGING,
    IDC_BRUSH_ROW_WIND,
    IDC_BRUSH_ROW_TIDE,
    IDC_BRUSH_ROW_BLOOM,
    IDC_BRUSH_ROW_MIRAGE,
    IDC_BRUSH_ROW_RIME,
    IDC_BRUSH_ROW_ORBIT,
    IDC_BRUSH_ROW_EMBER,
};
constexpr int kLabCount = static_cast<int>(sizeof(kLabIds) / sizeof(kLabIds[0]));
static_assert(kLabCount == 17, "hwndLabRowButtons size");

bool LabIdNeedsMaster(int id) {
    return id != IDC_BRUSH_ROW_EXPERIMENTAL;
}

bool LabIdVisible(int id) {
    if (!LabIdNeedsMaster(id)) return true;
    return brushExperimental;
}

int VisibleLabCount() {
    int n = 0;
    for (int id : kLabIds) {
        if (LabIdVisible(id)) ++n;
    }
    return n;
}

int NaturalLabHeight() {
    return kLabTitleH + VisibleLabCount() * kLabRowStep + kLabBottomPad;
}

int MaxLabHeight() {
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    int h = work.bottom - work.top - 40;
    if (h < 280) h = 280;
    if (h > 560) h = 560;
    return h;
}

int LabContentHeight() {
    const int natural = NaturalLabHeight();
    const int maxH = MaxLabHeight();
    return (natural < maxH) ? natural : maxH;
}

int MaxLabScroll() {
    const int overflow = NaturalLabHeight() - LabContentHeight();
    return (overflow > 0) ? overflow : 0;
}

void ClampLabScroll() {
    const int maxS = MaxLabScroll();
    if (gLabScrollPx > maxS) gLabScrollPx = maxS;
    if (gLabScrollPx < 0) gLabScrollPx = 0;
}

void FormatLabLabel(int cmdId, char* out, size_t outChars) {
    if (!out || outChars == 0) return;
    out[0] = '\0';
    constexpr const char* kSep = " : ";
    switch (cmdId) {
    case IDC_BRUSH_ROW_EXPERIMENTAL:
        sprintf_s(out, outChars, "Experimental%s%s", kSep, brushExperimental ? "On" : "Off");
        break;
    case IDC_BRUSH_ROW_ECHODELAY:
        sprintf_s(out, outChars, "Echo Delay%s%d%%", kSep, GetBrushEchoDelay());
        break;
    case IDC_BRUSH_ROW_ECHOFEEDBACK:
        sprintf_s(out, outChars, "Echo Fdbk%s%d%%", kSep, GetBrushEchoFeedback());
        break;
    case IDC_BRUSH_ROW_PING:
        sprintf_s(out, outChars, "Ping%s%d%%", kSep, GetBrushPing());
        break;
    case IDC_BRUSH_ROW_BIOLUM:
        sprintf_s(out, outChars, "Biolum%s%d%%", kSep, GetBrushBiolum());
        break;
    case IDC_BRUSH_ROW_HEAT:
        sprintf_s(out, outChars, "Heat%s%d%%", kSep, GetBrushHeat());
        break;
    case IDC_BRUSH_ROW_CLOCK:
        sprintf_s(out, outChars, "Clock%s%d%%", kSep, GetBrushClockPhase());
        break;
    case IDC_BRUSH_ROW_COMPASS:
        sprintf_s(out, outChars, "Compass%s%d%%", kSep, GetBrushCompassLock());
        break;
    case IDC_BRUSH_ROW_AFTERIMAGE:
        sprintf_s(out, outChars, "Afterimg%s%d%%", kSep, GetBrushAfterimage());
        break;
    case IDC_BRUSH_ROW_AGING:
        sprintf_s(out, outChars, "Aging%s%d%%", kSep, GetBrushAging());
        break;
    case IDC_BRUSH_ROW_WIND:
        sprintf_s(out, outChars, "Wind%s%d%%", kSep, GetBrushWind());
        break;
    case IDC_BRUSH_ROW_TIDE:
        sprintf_s(out, outChars, "Tide%s%d%%", kSep, GetBrushTide());
        break;
    case IDC_BRUSH_ROW_BLOOM:
        sprintf_s(out, outChars, "Bloom%s%d%%", kSep, GetBrushBloom());
        break;
    case IDC_BRUSH_ROW_MIRAGE:
        sprintf_s(out, outChars, "Mirage%s%d%%", kSep, GetBrushMirage());
        break;
    case IDC_BRUSH_ROW_RIME:
        sprintf_s(out, outChars, "Rime%s%d%%", kSep, GetBrushRime());
        break;
    case IDC_BRUSH_ROW_ORBIT:
        sprintf_s(out, outChars, "Orbit%s%d%%", kSep, GetBrushOrbit());
        break;
    case IDC_BRUSH_ROW_EMBER:
        sprintf_s(out, outChars, "Ember%s%d%%", kSep, GetBrushEmber());
        break;
    default:
        break;
    }
}

void LayoutExpLabRows() {
    if (!hwndExpLabFlyout) return;
    ClampLabScroll();
    const int h = LabContentHeight();
    const int clipBottom = h - kLabBottomPad;
    int visible = 0;
    for (int i = 0; i < kLabCount; ++i) {
        HWND btn = hwndLabRowButtons[i];
        if (!btn) continue;
        if (!LabIdVisible(kLabIds[i])) {
            ShowWindow(btn, SW_HIDE);
            continue;
        }
        const int y = kLabTitleH + visible * kLabRowStep - gLabScrollPx;
        ++visible;
        if (y + kLabRowH <= kLabTitleH || y >= clipBottom) {
            ShowWindow(btn, SW_HIDE);
        } else {
            ShowWindow(btn, SW_SHOW);
            MoveWindow(btn, kLabPad, y, kLabW - kLabPad * 2, kLabRowH, TRUE);
        }
    }
    SetWindowPos(hwndExpLabFlyout, nullptr, 0, 0, kLabW, h,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SyncExpLabLabelsInternal() {
    for (int i = 0; i < kLabCount; ++i) {
        if (!hwndLabRowButtons[i]) continue;
        char label[48];
        FormatLabLabel(kLabIds[i], label, sizeof(label));
        SetWindowTextA(hwndLabRowButtons[i], label);
    }
    LayoutExpLabRows();
}

LRESULT CALLBACK ExpLabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            PostMessageA(hwnd, WM_APP + 11, 0, 0);
        }
        return 0;
    case WM_APP + 11: {
        POINT pt = {};
        GetCursorPos(&pt);
        HWND under = WindowFromPoint(pt);
        if (under) {
            if (under == hwnd || IsChild(hwnd, under)) return 0;
            if (hwndBrushFlyout && (under == hwndBrushFlyout || IsChild(hwndBrushFlyout, under))) return 0;
            if (hwndBrushSubFlyout && (under == hwndBrushSubFlyout || IsChild(hwndBrushSubFlyout, under))) return 0;
        }
        CloseExpLabFlyout();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta == 0 || MaxLabScroll() < 1) return 0;
        gLabScrollPx -= (delta / WHEEL_DELTA) * kLabRowStep;
        ClampLabScroll();
        LayoutExpLabRows();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BRUSH_ROW_EXPERIMENTAL) {
            SetBrushExperimental(!IsBrushExperimental());
            gLabScrollPx = 0; // reset — list length changed; avoid empty-space jump
            SyncExpLabLabelsInternal();
            HWND parent = GetWindow(hwnd, GW_OWNER);
            if (!parent) parent = GetParent(hwnd);
            if (parent) UpdateStatusBar(parent);
            return 0;
        }
        for (int i = 0; i < kLabCount; ++i) {
            if (id != kLabIds[i]) continue;
            if (!LabIdVisible(id)) return 0;
            RECT rr = {};
            if (hwndLabRowButtons[i]) GetWindowRect(hwndLabRowButtons[i], &rr);
            OpenBrushLabSubPanel(id, rr.top - 4);
            return 0;
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(60, 45, 30));
        HFONT old = gBrandFont ? (HFONT)SelectObject(hdc, gBrandFont) : nullptr;
        RECT title = { kLabPad, 4, kLabW - kLabPad, kLabTitleH };
        DrawTextA(hdc, "Experimental Lab", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (old) SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (hwndExpLabFlyout == hwnd) {
            hwndExpLabFlyout = nullptr;
            for (HWND& b : hwndLabRowButtons) b = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

bool RegisterExpLabClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = ExpLabProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppExpLab";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void EnsureExpLabFlyout(HWND owner) {
    if (hwndExpLabFlyout && IsWindow(hwndExpLabFlyout)) return;
    hwndExpLabFlyout = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "SimpleDrawingAppExpLab",
        "",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, kLabW, LabContentHeight(),
        owner, NULL, GetModuleHandle(NULL), NULL);
    if (!hwndExpLabFlyout) return;

    for (int i = 0; i < kLabCount; ++i) {
        hwndLabRowButtons[i] = CreateWindowA("BUTTON", "",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            kLabPad, kLabTitleH + i * kLabRowStep, kLabW - kLabPad * 2, kLabRowH,
            hwndExpLabFlyout, (HMENU)(INT_PTR)kLabIds[i], GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndLabRowButtons[i]);
    }
    SyncExpLabLabelsInternal();
}

void OpenExpLabFlyout(HWND owner) {
    if (!owner) return;
    EnsureExpLabFlyout(owner);
    if (!hwndExpLabFlyout) return;

    // Prefer anchoring beside the Lab action button / brush flyout.
    RECT br = {};
    if (hwndBrushActionButtons[1] && IsWindow(hwndBrushActionButtons[1])) {
        GetWindowRect(hwndBrushActionButtons[1], &br);
    } else if (hwndBrushFlyout && IsWindowVisible(hwndBrushFlyout)) {
        GetWindowRect(hwndBrushFlyout, &br);
    } else {
        GetWindowRect(owner, &br);
    }

    gLabScrollPx = 0;
    LayoutExpLabRows();
    const int h = LabContentHeight();
    int x = br.right + 6;
    int y = br.top;
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    if (y + h > work.bottom) y = work.bottom - h;
    if (y < work.top) y = work.top;
    if (x + kLabW > work.right) x = br.left - kLabW - 6;
    if (x < work.left) x = work.left;

    SetWindowPos(hwndExpLabFlyout, HWND_TOPMOST, x, y, kLabW, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(hwndExpLabFlyout);
    SyncExpLabLabelsInternal();
}

void CloseExpLabFlyout() {
    if (hwndExpLabFlyout && IsWindowVisible(hwndExpLabFlyout)) {
        ShowWindow(hwndExpLabFlyout, SW_HIDE);
    }
}

bool IsExpLabVisible() {
    return hwndExpLabFlyout && IsWindowVisible(hwndExpLabFlyout);
}

void SyncExpLabLabels() {
    if (!hwndExpLabFlyout || !IsWindow(hwndExpLabFlyout)) return;
    SyncExpLabLabelsInternal();
}
