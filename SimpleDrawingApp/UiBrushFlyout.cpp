#include "UiBrushFlyout.h"
#include "BrushEngine.h"
#include "BrushGallery.h"
#include "UiControls.h"
#include "AtelierControls.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppShell.h"
#include "UiChrome.h"
#include "Resource.h"

#include <commctrl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>

using namespace Gdiplus;

namespace {

constexpr int kFlyoutW = 210;
constexpr int kFlyoutH = 338;
constexpr int kSubFlyoutW = 168;
constexpr int kSubFlyoutH = 108;
constexpr int kSubFlyoutSliderH = 88;
constexpr int kPresetCell = 58;
constexpr int kPresetCols = 3;
constexpr int kMaxFlyoutPresets = 9;

enum class BrushSubPanel { None = 0, Flow, Hardness, Pressure };

BrushSubPanel gBrushSubPanel = BrushSubPanel::None;
int gBrushSubAnchorY = 0;
int gBrushSubCmdIds[3] = {};
int gBrushPresetPage = 0;

int BrushFlyoutPageCount() {
    return (BrushPresetCount() + kMaxFlyoutPresets - 1) / kMaxFlyoutPresets;
}

int BrushFlyoutPresetIndexFromCmd(int cmdId) {
    if (cmdId < IDC_BRUSH_PRESET_BASE || cmdId >= IDC_BRUSH_PRESET_BASE + kMaxFlyoutPresets) return -1;
    const int slot = cmdId - IDC_BRUSH_PRESET_BASE;
    return gBrushPresetPage * kMaxFlyoutPresets + slot;
}

void RefreshPresetGrid() {
    const int total = BrushPresetCount();
    const int pageStart = gBrushPresetPage * kMaxFlyoutPresets;
    for (int slot = 0; slot < kMaxFlyoutPresets; ++slot) {
        HWND btn = hwndBrushPresetButtons[slot];
        if (!btn) continue;
        const int idx = pageStart + slot;
        if (idx < total) {
            ShowWindow(btn, SW_SHOW);
            const BrushPreset* preset = GetBrushPreset(idx);
            char tip[96] = {};
            if (preset) snprintf(tip, sizeof(tip), "%s\r\nClick to select", preset->name.c_str());
            InvalidateRect(btn, NULL, FALSE);
            (void)tip;
        } else {
            ShowWindow(btn, SW_HIDE);
        }
    }
    if (hwndBrushPageButtons[0]) {
        EnableWindow(hwndBrushPageButtons[0], gBrushPresetPage > 0 ? TRUE : FALSE);
    }
    if (hwndBrushPageButtons[1]) {
        EnableWindow(hwndBrushPageButtons[1],
            gBrushPresetPage + 1 < BrushFlyoutPageCount() ? TRUE : FALSE);
    }
}

void PaintBrushPresetCell(const DRAWITEMSTRUCT* dis) {
    if (!dis) return;
    const int slot = static_cast<int>(dis->CtlID) - IDC_BRUSH_PRESET_BASE;
    const int idx = gBrushPresetPage * kMaxFlyoutPresets + slot;
    const BrushPreset* preset = GetBrushPreset(idx);
    const bool selected = (GetActiveBrushIndex() == idx);
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Color fill(255, GetRValue(gTheme.chromeElevated), GetGValue(gTheme.chromeElevated),
        GetBValue(gTheme.chromeElevated));
    if (selected) {
        fill = Color(255, GetRValue(gTheme.toolSelectedBg), GetGValue(gTheme.toolSelectedBg),
            GetBValue(gTheme.toolSelectedBg));
    } else if (hot || pressed) {
        fill = Color(255, GetRValue(gTheme.chromeDeep), GetGValue(gTheme.chromeDeep),
            GetBValue(gTheme.chromeDeep));
    }

    RectF bounds(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
        static_cast<REAL>(rc.right - rc.left), static_cast<REAL>(rc.bottom - rc.top));
    Color bronze(255, GetRValue(gTheme.chromeLine), GetGValue(gTheme.chromeLine), GetBValue(gTheme.chromeLine));
    Color gilt(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    DrawHudPlate(g, bounds, fill, selected ? gilt : bronze, true);
    if (selected) {
        DrawHudCornerTicks(g, bounds, gilt, 5.0f);
    }

    RECT thumb = rc;
    thumb.bottom = thumb.top + (rc.bottom - rc.top) * 2 / 3;
    DrawBrushTipPreview(hdc, thumb, preset, false);

    if (preset) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, selected ? gTheme.accentDeep : gTheme.text);
        HFONT oldFont = nullptr;
        if (gUiFont) oldFont = static_cast<HFONT>(SelectObject(hdc, gUiFont));
        RECT textRc = rc;
        textRc.top = thumb.bottom - 2;
        char label[24] = {};
        strncpy_s(label, preset->name.c_str(), sizeof(label) - 1);
        DrawTextA(hdc, label, -1, &textRc, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (oldFont) SelectObject(hdc, oldFont);
    }

    if (dis->itemState & ODS_FOCUS) {
        RECT focus = rc;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(hdc, &focus);
    }
}

void PaintFlyoutChrome(HDC hdc, const RECT& rc, const char* title) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RectF bounds(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
        static_cast<REAL>(rc.right - rc.left), static_cast<REAL>(rc.bottom - rc.top));

    Color parchmentTop(255, GetRValue(gTheme.chromeElevated), GetGValue(gTheme.chromeElevated),
        GetBValue(gTheme.chromeElevated));
    Color parchmentBottom(255, GetRValue(gTheme.chromeDeep), GetGValue(gTheme.chromeDeep),
        GetBValue(gTheme.chromeDeep));
    Color bronze(255, GetRValue(gTheme.chromeLine), GetGValue(gTheme.chromeLine), GetBValue(gTheme.chromeLine));
    Color gilt(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));

    DrawFrescoPanel(g, bounds, parchmentTop, parchmentBottom, true);
    DrawFrescoGrain(g, bounds, Color(18, GetRValue(gTheme.ink), GetGValue(gTheme.ink), GetBValue(gTheme.ink)));
    DrawHudCornerTicks(g, bounds, gilt, 9.0f);

    // Gilt header band
    RectF header(bounds.X + 6.0f, bounds.Y + 4.0f, bounds.Width - 12.0f, 22.0f);
    SolidBrush headerFill(Color(48, GetRValue(gTheme.accentDeep), GetGValue(gTheme.accentDeep),
        GetBValue(gTheme.accentDeep)));
    g.FillRectangle(&headerFill, header);
    Pen headerLine(gilt, 1.0f);
    g.DrawRectangle(&headerLine, header.X, header.Y, header.Width, header.Height);

    if (title && gUiFont) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, gTheme.chromeElevated);
        HFONT old = static_cast<HFONT>(SelectObject(hdc, gUiFont));
        RECT tr = { static_cast<LONG>(header.X + 8), static_cast<LONG>(header.Y + 3),
            static_cast<LONG>(header.GetRight() - 8), static_cast<LONG>(header.GetBottom()) };
        DrawTextA(hdc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (old) SelectObject(hdc, old);
    }

    // Section rules
    HPEN pen = CreatePen(PS_SOLID, 1, gTheme.chromeLine);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left + 10, rc.top + 206, NULL);
    LineTo(hdc, rc.right - 10, rc.top + 206);
    MoveToEx(hdc, rc.left + 10, rc.top + 298, NULL);
    LineTo(hdc, rc.right - 10, rc.top + 298);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void CloseBrushSubFlyoutInternal() {
    gBrushSubPanel = BrushSubPanel::None;
    if (hwndBrushSubFlyout && IsWindowVisible(hwndBrushSubFlyout)) {
        ShowWindow(hwndBrushSubFlyout, SW_HIDE);
    }
}

void LayoutSubFlyoutOptions() {
    if (!hwndBrushSubFlyout) return;

    const bool useSlider = (gBrushSubPanel == BrushSubPanel::Flow || gBrushSubPanel == BrushSubPanel::Hardness);
    if (hwndBrushSubSlider) {
        ShowWindow(hwndBrushSubSlider, useSlider ? SW_SHOW : SW_HIDE);
        if (useSlider) {
            SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
            const int pos = (gBrushSubPanel == BrushSubPanel::Flow) ? brushFlow : brushHardness;
            SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, pos);
        }
    }

    if (useSlider) {
        for (HWND btn : hwndBrushSubButtons) {
            if (btn) ShowWindow(btn, SW_HIDE);
        }
        return;
    }

    char labels[3][48] = {};

    switch (gBrushSubPanel) {
    case BrushSubPanel::Flow:
        strcpy_s(labels[0], "Light  25%");
        strcpy_s(labels[1], "Medium  50%");
        strcpy_s(labels[2], "Full  100%");
        gBrushSubCmdIds[0] = IDM_FLOW_LOW;
        gBrushSubCmdIds[1] = IDM_FLOW_MED;
        gBrushSubCmdIds[2] = IDM_FLOW_FULL;
        break;
    case BrushSubPanel::Hardness:
        strcpy_s(labels[0], "Soft  35%");
        strcpy_s(labels[1], "Medium  65%");
        strcpy_s(labels[2], "Hard  100%");
        gBrushSubCmdIds[0] = IDM_HARD_SOFT;
        gBrushSubCmdIds[1] = IDM_HARD_MED;
        gBrushSubCmdIds[2] = IDM_HARD_HARD;
        break;
    case BrushSubPanel::Pressure:
        strcpy_s(labels[0], IsPenPressureEnabled() ? "On  (tablet)" : "Off");
        labels[1][0] = '\0';
        labels[2][0] = '\0';
        gBrushSubCmdIds[0] = IDM_PRESSURE_ENABLE;
        gBrushSubCmdIds[1] = 0;
        gBrushSubCmdIds[2] = 0;
        break;
    default:
        return;
    }

    for (int i = 0; i < 3; ++i) {
        HWND btn = hwndBrushSubButtons[i];
        if (!btn) continue;
        if (labels[i][0]) {
            SetWindowTextA(btn, labels[i]);
            ShowWindow(btn, SW_SHOW);
            MoveWindow(btn, 8, 8 + i * 28, kSubFlyoutW - 16, 24, TRUE);
        } else {
            ShowWindow(btn, SW_HIDE);
        }
    }
}

void OpenBrushSubFlyoutInternal(BrushSubPanel panel, int anchorY) {
    if (!hwndBrushFlyout || !hwndBrushSubFlyout) return;
    if (gBrushSubPanel == panel && IsWindowVisible(hwndBrushSubFlyout)) {
        CloseBrushSubFlyoutInternal();
        return;
    }

    gBrushSubPanel = panel;
    gBrushSubAnchorY = anchorY;
    LayoutSubFlyoutOptions();

    RECT fr = {};
    GetWindowRect(hwndBrushFlyout, &fr);
    int subH = kSubFlyoutH;
    if (gBrushSubPanel == BrushSubPanel::Pressure) subH = 44;
    else if (gBrushSubPanel == BrushSubPanel::Flow || gBrushSubPanel == BrushSubPanel::Hardness) {
        subH = kSubFlyoutSliderH;
    }
    SetWindowPos(hwndBrushSubFlyout, HWND_TOPMOST, fr.right + 4, gBrushSubAnchorY,
        kSubFlyoutW, subH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(hwndBrushSubFlyout);
}

LRESULT CALLBACK BrushSubFlyoutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            PostMessageA(hwnd, WM_APP + 8, 0, 0);
        }
        return 0;
    case WM_APP + 8: {
        POINT pt = {};
        GetCursorPos(&pt);
        HWND under = WindowFromPoint(pt);
        if (under) {
            if (under == hwnd || IsChild(hwnd, under)) return 0;
            if (hwndBrushFlyout && (under == hwndBrushFlyout || IsChild(hwndBrushFlyout, under))) return 0;
        }
        CloseBrushSubFlyoutInternal();
        return 0;
    }
    case WM_HSCROLL:
        if (hwndBrushSubSlider && reinterpret_cast<HWND>(lParam) == hwndBrushSubSlider) {
            const int pos = static_cast<int>(SendMessage(hwndBrushSubSlider, TBM_GETPOS, 0, 0));
            if (gBrushSubPanel == BrushSubPanel::Flow) {
                SetBrushFlow(pos);
            } else if (gBrushSubPanel == BrushSubPanel::Hardness) {
                SetBrushHardness(pos);
            }
            SyncBrushFlyoutChecks();
            HWND parent = GetWindow(hwnd, GW_OWNER);
            if (!parent) parent = GetParent(hwndBrushFlyout);
            if (parent) UpdateStatusBar(parent);
        }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        int cmdId = id;
        if (id == IDC_BRUSH_SUB_OPT1) cmdId = gBrushSubCmdIds[0];
        else if (id == IDC_BRUSH_SUB_OPT2) cmdId = gBrushSubCmdIds[1];
        else if (id == IDC_BRUSH_SUB_OPT3) cmdId = gBrushSubCmdIds[2];
        if (cmdId == 0) return 0;

        HWND parent = GetWindow(hwnd, GW_OWNER);
        if (!parent) parent = GetParent(hwndBrushFlyout);
        if (parent) {
            SendMessageA(parent, WM_COMMAND, MAKEWPARAM(cmdId, BN_CLICKED), lParam);
        }
        CloseBrushSubFlyoutInternal();
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
        PaintFlyoutChrome(hdc, rc, nullptr);
        return 1;
    }
    case WM_DESTROY:
        if (hwndBrushSubFlyout == hwnd) {
            hwndBrushSubFlyout = nullptr;
            for (HWND& btn : hwndBrushSubButtons) btn = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK BrushFlyoutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            PostMessageA(hwnd, WM_APP + 9, 0, 0);
        }
        return 0;
    case WM_APP + 9: {
        POINT pt = {};
        GetCursorPos(&pt);
        HWND under = WindowFromPoint(pt);
        if (under) {
            if (under == hwnd || IsChild(hwnd, under)) return 0;
            if (hwndBrushSubFlyout && (under == hwndBrushSubFlyout || IsChild(hwndBrushSubFlyout, under))) {
                return 0;
            }
            if (under == hwndToolButtons[static_cast<int>(DrawTool::Pen)]) return 0;
        }
        CloseBrushFlyout();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BRUSH_ROW_FLOW) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[0], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Flow, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_HARDNESS) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[1], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Hardness, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_PRESSURE) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[2], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Pressure, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_PAGE_PREV && gBrushPresetPage > 0) {
            --gBrushPresetPage;
            RefreshPresetGrid();
            SyncBrushFlyoutChecks();
            return 0;
        }
        if (id == IDC_BRUSH_PAGE_NEXT && gBrushPresetPage + 1 < BrushFlyoutPageCount()) {
            ++gBrushPresetPage;
            RefreshPresetGrid();
            SyncBrushFlyoutChecks();
            return 0;
        }
        HWND parent = GetParent(hwnd);
        if (!parent) parent = GetWindow(hwnd, GW_OWNER);
        if (parent) {
            SendMessageA(parent, WM_COMMAND, wParam, lParam);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlID >= IDC_BRUSH_PRESET_BASE
            && dis->CtlID < IDC_BRUSH_PRESET_BASE + kMaxFlyoutPresets) {
            PaintBrushPresetCell(dis);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)(gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
        PaintFlyoutChrome(hdc, rc, "Brush Atelier");
        return 1;
    }
    case WM_DESTROY:
        if (hwndBrushFlyout == hwnd) {
            hwndBrushFlyout = nullptr;
            for (HWND& btn : hwndBrushPresetButtons) btn = nullptr;
            for (HWND& btn : hwndBrushRowButtons) btn = nullptr;
            for (HWND& btn : hwndBrushActionButtons) btn = nullptr;
        }
        CloseBrushSubFlyoutInternal();
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void EnsureBrushSubFlyout(HWND owner) {
    if (hwndBrushSubFlyout && IsWindow(hwndBrushSubFlyout)) return;
    hwndBrushSubFlyout = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "SimpleDrawingAppBrushSubFlyout",
        "",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, kSubFlyoutW, kSubFlyoutH,
        owner, NULL, GetModuleHandle(NULL), NULL);
    if (!hwndBrushSubFlyout) return;

    for (int i = 0; i < 3; ++i) {
        const int id = IDC_BRUSH_SUB_OPT1 + i;
        hwndBrushSubButtons[i] = CreateWindowA("BUTTON", "",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            8, 8 + i * 28, kSubFlyoutW - 16, 24,
            hwndBrushSubFlyout, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndBrushSubButtons[i]);
    }
    hwndBrushSubSlider = AtelierSlider_Create(hwndBrushSubFlyout, 8, 28, kSubFlyoutW - 16, 28,
        (HMENU)(INT_PTR)IDC_BRUSH_SUB_SLIDER);
    if (hwndBrushSubSlider) ShowWindow(hwndBrushSubSlider, SW_HIDE);
}

void EnsureBrushFlyout(HWND owner) {
    if (hwndBrushFlyout && IsWindow(hwndBrushFlyout)) return;
    EnsureBrushSubFlyout(owner);

    hwndBrushFlyout = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "SimpleDrawingAppBrushFlyout",
        "",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, kFlyoutW, kFlyoutH,
        owner, NULL, GetModuleHandle(NULL), NULL);
    if (!hwndBrushFlyout) return;

    const int presetCount = BrushPresetCount();
    for (int i = 0; i < kMaxFlyoutPresets; ++i) {
        const int id = IDC_BRUSH_PRESET_BASE + i;
        if (i < presetCount || i < kMaxFlyoutPresets) {
            hwndBrushPresetButtons[i] = CreateIconButton(hwndBrushFlyout, id, "Brush preset", true);
            const int col = i % kPresetCols;
            const int row = i / kPresetCols;
            MoveWindow(hwndBrushPresetButtons[i],
                12 + col * (kPresetCell + 4),
                34 + row * (kPresetCell + 4),
                kPresetCell, kPresetCell, TRUE);
        }
    }

    hwndBrushPageButtons[0] = CreateWindowA("BUTTON", "\xE2\x80\xB9",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        12, 192, 28, 20, hwndBrushFlyout, (HMENU)(INT_PTR)IDC_BRUSH_PAGE_PREV,
        GetModuleHandle(NULL), NULL);
    hwndBrushPageButtons[1] = CreateWindowA("BUTTON", "\xE2\x80\xBA",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        kFlyoutW - 40, 192, 28, 20, hwndBrushFlyout, (HMENU)(INT_PTR)IDC_BRUSH_PAGE_NEXT,
        GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndBrushPageButtons[0]);
    ApplyUiFont(hwndBrushPageButtons[1]);

    const int rowIds[3] = { IDC_BRUSH_ROW_FLOW, IDC_BRUSH_ROW_HARDNESS, IDC_BRUSH_ROW_PRESSURE };
    const char* rowTips[3] = { "Flow — paint opacity per stamp", "Hardness — edge falloff",
        "Pen pressure — tablet dynamics" };
    for (int i = 0; i < 3; ++i) {
        hwndBrushRowButtons[i] = CreateWindowA("BUTTON", "",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            10, 214 + i * 26, kFlyoutW - 20, 22,
            hwndBrushFlyout, (HMENU)(INT_PTR)rowIds[i], GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndBrushRowButtons[i]);
        if (hwndTooltip && rowTips[i]) {
            TOOLINFOA ti = {};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwndBrushFlyout;
            ti.uId = (UINT_PTR)hwndBrushRowButtons[i];
            ti.lpszText = const_cast<char*>(rowTips[i]);
            SendMessageA(hwndTooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
        }
    }

    const int actionIds[2] = { IDC_BRUSH_FLYOUT_GALLERY, IDC_BRUSH_FLYOUT_IMPORT };
    const char* actionLabels[2] = { "Gallery...", "Import..." };
    const char* actionTips[2] = { "Open brush gallery", "Import PNG tip or ABR set" };
    for (int i = 0; i < 2; ++i) {
        hwndBrushActionButtons[i] = CreateWindowA("BUTTON", actionLabels[i],
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            10 + i * ((kFlyoutW - 20) / 2 + 2), 306, (kFlyoutW - 24) / 2, 22,
            hwndBrushFlyout, (HMENU)(INT_PTR)actionIds[i], GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndBrushActionButtons[i]);
        if (hwndTooltip) {
            TOOLINFOA ti = {};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwndBrushFlyout;
            ti.uId = (UINT_PTR)hwndBrushActionButtons[i];
            ti.lpszText = const_cast<char*>(actionTips[i]);
            SendMessageA(hwndTooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
        }
    }

    RefreshPresetGrid();
    SyncBrushFlyoutChecks();
}

} // namespace

int BrushFlyoutPresetIndexFromCmd(int cmdId) {
    if (cmdId < IDC_BRUSH_PRESET_BASE || cmdId >= IDC_BRUSH_PRESET_BASE + kMaxFlyoutPresets) return -1;
    const int slot = cmdId - IDC_BRUSH_PRESET_BASE;
    return gBrushPresetPage * kMaxFlyoutPresets + slot;
}

void RebuildBrushFlyoutPresets() {
    if (gBrushPresetPage >= BrushFlyoutPageCount()) {
        gBrushPresetPage = BrushFlyoutPageCount() - 1;
    }
    if (gBrushPresetPage < 0) gBrushPresetPage = 0;
    if (hwndBrushFlyout && IsWindow(hwndBrushFlyout)) {
        RefreshPresetGrid();
        SyncBrushFlyoutChecks();
    }
}

void SyncBrushFlyoutChecks() {
    const int total = BrushPresetCount();
    const int pageStart = gBrushPresetPage * kMaxFlyoutPresets;
    for (int slot = 0; slot < kMaxFlyoutPresets; ++slot) {
        HWND btn = hwndBrushPresetButtons[slot];
        if (!btn) continue;
        const int idx = pageStart + slot;
        const bool on = (GetActiveBrushIndex() == idx);
        SendMessageA(btn, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(btn, NULL, FALSE);
        if (idx >= total) ShowWindow(btn, SW_HIDE);
        else ShowWindow(btn, SW_SHOW);
    }

    char flowLabel[48];
    char hardLabel[48];
    char pressLabel[48];
    sprintf_s(flowLabel, "Flow  \xE2\x80\xBA  %d%%", brushFlow);
    sprintf_s(hardLabel, "Hardness  \xE2\x80\xBA  %d%%", brushHardness);
    sprintf_s(pressLabel, "Pressure  \xE2\x80\xBA  %s", penPressureEnabled ? "On" : "Off");

    if (hwndBrushRowButtons[0]) SetWindowTextA(hwndBrushRowButtons[0], flowLabel);
    if (hwndBrushRowButtons[1]) SetWindowTextA(hwndBrushRowButtons[1], hardLabel);
    if (hwndBrushRowButtons[2]) SetWindowTextA(hwndBrushRowButtons[2], pressLabel);

    if (gBrushSubPanel != BrushSubPanel::None) {
        LayoutSubFlyoutOptions();
    }
}

void CloseBrushFlyout() {
    CloseBrushSubFlyoutInternal();
    if (hwndBrushFlyout && IsWindowVisible(hwndBrushFlyout)) {
        ShowWindow(hwndBrushFlyout, SW_HIDE);
    }
}

bool RegisterBrushFlyoutClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = BrushFlyoutProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppBrushFlyout";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    const bool mainOk = RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

    WNDCLASSA sub = {};
    sub.lpfnWndProc = BrushSubFlyoutProc;
    sub.hInstance = hInstance;
    sub.lpszClassName = "SimpleDrawingAppBrushSubFlyout";
    sub.hCursor = LoadCursor(NULL, IDC_ARROW);
    sub.hbrBackground = NULL;
    sub.style = CS_DROPSHADOW;
    const bool subOk = RegisterClassA(&sub) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return mainOk && subOk;
}

void OpenBrushFlyout(HWND parent) {
    if (!parent) return;
    EnsureBrushFlyout(parent);
    if (!hwndBrushFlyout || !hwndToolButtons[static_cast<int>(DrawTool::Pen)]) return;

    RECT br = {};
    GetWindowRect(hwndToolButtons[static_cast<int>(DrawTool::Pen)], &br);
    SetWindowPos(hwndBrushFlyout, HWND_TOPMOST, br.right + 6, br.top - 4, kFlyoutW, kFlyoutH,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(hwndBrushFlyout);
    SyncBrushFlyoutChecks();
}
