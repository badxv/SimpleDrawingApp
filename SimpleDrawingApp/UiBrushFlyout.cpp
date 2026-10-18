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
#include <windowsx.h>
#include <gdiplus.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Gdiplus;

namespace {

constexpr int kFlyoutW = 210;
constexpr int kFlyoutH = 548;
constexpr int kSubFlyoutW = 168;
constexpr int kSubFlyoutH = 108;
constexpr int kSubFlyoutSliderH = 88;
constexpr int kPresetCell = 58;
constexpr int kPresetCols = 3;
constexpr int kMaxFlyoutPresets = 9;
constexpr int kPreviewH = 44;

enum class BrushSubPanel {
    None = 0, Flow, Hardness, Pressure, Size, Spacing, Scatter, Angle, Orient, SizeJitter,
    OpacityJitter, Smooth
};
int gBrushCtxPreset = -1;

BrushSubPanel gBrushSubPanel = BrushSubPanel::None;
int gBrushSubAnchorY = 0;
int gBrushSubCmdIds[3] = {};
int gBrushPresetPage = 0;

int BrushFlyoutPageCount() {
    return (BrushPresetCount() + kMaxFlyoutPresets - 1) / kMaxFlyoutPresets;
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
            if (preset) {
                snprintf(tip, sizeof(tip), "%s\r\nClick to select%s",
                    preset->name.c_str(),
                    preset->builtin ? "" : "\r\nRight-click to rename/delete");
            }
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

    // Section rules: after presets/preview, before actions
    HPEN pen = CreatePen(PS_SOLID, 1, gTheme.chromeLine);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left + 10, rc.top + 252, NULL);
    LineTo(hdc, rc.right - 10, rc.top + 252);
    MoveToEx(hdc, rc.left + 10, rc.top + 512, NULL);
    LineTo(hdc, rc.right - 10, rc.top + 512);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void PaintStrokePreview(HDC hdc, const RECT& rc) {
    if (!hdc) return;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(255, GetRValue(gTheme.chromeDeep), GetGValue(gTheme.chromeDeep),
        GetBValue(gTheme.chromeDeep)));

    const BrushPreset* preset = GetBrushPreset(GetActiveBrushIndex());
    if (!preset || !preset->tip) return;

    const int maxStamp = (rc.bottom - rc.top) - 8;
    const int stampSize = MaxInt(4, (penWidth < maxStamp) ? penWidth : maxStamp);
    const REAL baseAlpha = static_cast<REAL>(brushFlow) / 100.0f
        * (0.45f + 0.55f * static_cast<REAL>(brushHardness) / 100.0f);

    const int tw = static_cast<int>(preset->tip->GetWidth());
    const int th = static_cast<int>(preset->tip->GetHeight());
    const float midY = (rc.top + rc.bottom) * 0.5f;
    const float left = static_cast<float>(rc.left + 10);
    const float right = static_cast<float>(rc.right - 10);
    const float spacing = MaxFloat(1.0f, preset->spacing * static_cast<float>(stampSize));
    int stampIndex = 0;
    for (float x = left; x <= right; x += spacing) {
        const float t = (x - left) / MaxFloat(1.0f, right - left);
        float y = midY + sinf(t * 6.28318f) * 6.0f;
        float sx = x;
        if (brushScatter > 0) {
            const unsigned n = (static_cast<unsigned>(stampIndex) * 2654435761u) ^ 0x9e3779b9u;
            const float rx = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
            const float ry = (static_cast<float>((n >> 8) & 255u) / 255.0f) * 2.0f - 1.0f;
            const float amp = static_cast<float>(stampSize) * 0.45f * (static_cast<float>(brushScatter) / 100.0f);
            sx += rx * amp;
            y += ry * amp;
        }
        float angleDeg = static_cast<float>(brushTipAngle);
        if (brushAngleJitter > 0) {
            const unsigned n = (static_cast<unsigned>(stampIndex) * 2246822519u) ^ 0x85ebca6bu;
            const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
            angleDeg += u * 180.0f * (static_cast<float>(brushAngleJitter) / 100.0f);
        }
        int stampDraw = stampSize;
        if (brushSizeJitter > 0) {
            const unsigned n = (static_cast<unsigned>(stampIndex) * 374761393u) ^ 0x27d4eb2du;
            const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
            const float factor = 1.0f + u * (static_cast<float>(brushSizeJitter) / 100.0f);
            stampDraw = static_cast<int>(static_cast<float>(stampSize) * factor + 0.5f);
            if (stampDraw < 1) stampDraw = 1;
        }
        REAL alphaScale = baseAlpha;
        if (brushOpacityJitter > 0) {
            const unsigned n = (static_cast<unsigned>(stampIndex) * 1103515245u) ^ 0xc2b2ae35u;
            const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
            alphaScale = baseAlpha * (1.0f + u * (static_cast<float>(brushOpacityJitter) / 100.0f));
            if (alphaScale < 0.05f) alphaScale = 0.05f;
            if (alphaScale > 1.0f) alphaScale = 1.0f;
        }
        ColorMatrix matrix = {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, alphaScale, 0.0f,
            0.18f, 0.14f, 0.10f, 0.0f, 1.0f
        };
        ImageAttributes attrs;
        attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        const REAL half = static_cast<REAL>(stampDraw) * 0.5f;
        GraphicsState state = g.Save();
        g.TranslateTransform(sx, y);
        if (angleDeg != 0.0f) g.RotateTransform(angleDeg);
        RectF dest(-half, -half, static_cast<REAL>(stampDraw), static_cast<REAL>(stampDraw));
        g.DrawImage(preset->tip.get(), dest, 0.0f, 0.0f, static_cast<REAL>(tw), static_cast<REAL>(th),
            UnitPixel, &attrs);
        g.Restore(state);
        ++stampIndex;
    }

    Color bronze(255, GetRValue(gTheme.chromeLine), GetGValue(gTheme.chromeLine), GetBValue(gTheme.chromeLine));
    Pen border(bronze, 1.0f);
    g.DrawRectangle(&border, static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
        static_cast<REAL>(rc.right - rc.left - 1), static_cast<REAL>(rc.bottom - rc.top - 1));
}

LRESULT CALLBACK BrushPreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        PaintStrokePreview(hdc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void CloseBrushSubFlyoutInternal() {
    gBrushSubPanel = BrushSubPanel::None;
    if (hwndBrushSubFlyout && IsWindowVisible(hwndBrushSubFlyout)) {
        ShowWindow(hwndBrushSubFlyout, SW_HIDE);
    }
}

void LayoutSubFlyoutOptions() {
    if (!hwndBrushSubFlyout) return;

    const bool useSlider = (gBrushSubPanel == BrushSubPanel::Flow
        || gBrushSubPanel == BrushSubPanel::Hardness
        || gBrushSubPanel == BrushSubPanel::Size
        || gBrushSubPanel == BrushSubPanel::Spacing
        || gBrushSubPanel == BrushSubPanel::Scatter
        || gBrushSubPanel == BrushSubPanel::Angle
        || gBrushSubPanel == BrushSubPanel::Orient
        || gBrushSubPanel == BrushSubPanel::SizeJitter
        || gBrushSubPanel == BrushSubPanel::OpacityJitter
        || gBrushSubPanel == BrushSubPanel::Smooth);
    if (hwndBrushSubSlider) {
        ShowWindow(hwndBrushSubSlider, useSlider ? SW_SHOW : SW_HIDE);
        if (useSlider) {
            if (gBrushSubPanel == BrushSubPanel::Size) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(kPenWidthMin, kPenWidthMax));
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, penWidth);
            } else if (gBrushSubPanel == BrushSubPanel::Spacing) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(5, 100));
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, GetBrushSpacingPercent());
            } else if (gBrushSubPanel == BrushSubPanel::Orient) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 180));
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, GetBrushTipAngle());
            } else if (gBrushSubPanel == BrushSubPanel::Scatter
                || gBrushSubPanel == BrushSubPanel::Angle
                || gBrushSubPanel == BrushSubPanel::SizeJitter
                || gBrushSubPanel == BrushSubPanel::OpacityJitter
                || gBrushSubPanel == BrushSubPanel::Smooth) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                int pos = GetBrushScatter();
                if (gBrushSubPanel == BrushSubPanel::Angle) pos = GetBrushAngleJitter();
                else if (gBrushSubPanel == BrushSubPanel::SizeJitter) pos = GetBrushSizeJitter();
                else if (gBrushSubPanel == BrushSubPanel::OpacityJitter) pos = GetBrushOpacityJitter();
                else if (gBrushSubPanel == BrushSubPanel::Smooth) pos = GetBrushSmoothing();
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, pos);
            } else {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
                const int pos = (gBrushSubPanel == BrushSubPanel::Flow) ? brushFlow : brushHardness;
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, pos);
            }
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
    else if (gBrushSubPanel == BrushSubPanel::Flow || gBrushSubPanel == BrushSubPanel::Hardness
        || gBrushSubPanel == BrushSubPanel::Size || gBrushSubPanel == BrushSubPanel::Spacing
        || gBrushSubPanel == BrushSubPanel::Scatter || gBrushSubPanel == BrushSubPanel::Angle
        || gBrushSubPanel == BrushSubPanel::Orient || gBrushSubPanel == BrushSubPanel::SizeJitter
        || gBrushSubPanel == BrushSubPanel::OpacityJitter || gBrushSubPanel == BrushSubPanel::Smooth) {
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
            } else if (gBrushSubPanel == BrushSubPanel::Size) {
                HWND parent = GetWindow(hwnd, GW_OWNER);
                if (!parent) parent = GetParent(hwndBrushFlyout);
                if (parent) ApplyPenWidth(parent, pos);
            } else if (gBrushSubPanel == BrushSubPanel::Spacing) {
                SetBrushSpacingPercent(pos);
            } else if (gBrushSubPanel == BrushSubPanel::Scatter) {
                SetBrushScatter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::Angle) {
                SetBrushAngleJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::Orient) {
                SetBrushTipAngle(pos);
            } else if (gBrushSubPanel == BrushSubPanel::SizeJitter) {
                SetBrushSizeJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::OpacityJitter) {
                SetBrushOpacityJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::Smooth) {
                SetBrushSmoothing(pos);
            }
            SyncBrushFlyoutChecks();
            if (hwndBrushPreview) InvalidateRect(hwndBrushPreview, NULL, FALSE);
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
        if (id == IDC_BRUSH_ROW_SIZE) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[0], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Size, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_SPACING) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[1], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Spacing, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_FLOW) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[2], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Flow, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_HARDNESS) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[3], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Hardness, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_SCATTER) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[4], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Scatter, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_ANGLE) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[5], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Angle, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_ORIENT) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[6], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Orient, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_SIZEJITTER) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[7], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::SizeJitter, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_OPJITTER) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[8], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::OpacityJitter, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_SMOOTH) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[9], &rr);
            OpenBrushSubFlyoutInternal(BrushSubPanel::Smooth, rr.top - 4);
            return 0;
        }
        if (id == IDC_BRUSH_ROW_PRESSURE) {
            RECT rr = {};
            GetWindowRect(hwndBrushRowButtons[10], &rr);
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
    case WM_CONTEXTMENU: {
        HWND child = reinterpret_cast<HWND>(wParam);
        int presetIdx = -1;
        for (int slot = 0; slot < kMaxFlyoutPresets; ++slot) {
            if (hwndBrushPresetButtons[slot] && hwndBrushPresetButtons[slot] == child) {
                presetIdx = gBrushPresetPage * kMaxFlyoutPresets + slot;
                break;
            }
        }
        if (presetIdx < 0 || presetIdx >= BrushPresetCount()) break;

        gBrushCtxPreset = presetIdx;
        const BrushPreset* preset = GetBrushPreset(presetIdx);
        HMENU popup = CreatePopupMenu();
        if (!popup) return 0;
        AppendMenuA(popup, MF_STRING, IDM_BRUSH_CTX_SELECT, "Select");
        if (preset && !preset->builtin) {
            AppendMenuA(popup, MF_STRING, IDM_BRUSH_CTX_RENAME, "Rename…");
            AppendMenuA(popup, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(popup, MF_STRING, IDM_BRUSH_CTX_DELETE, "Delete");
        }
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.x == -1 && pt.y == -1) {
            RECT br = {};
            GetWindowRect(child, &br);
            pt.x = br.left;
            pt.y = br.bottom;
        }
        const UINT pick = TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
            pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(popup);
        if (pick == IDM_BRUSH_CTX_SELECT || pick == IDM_BRUSH_CTX_DELETE
            || pick == IDM_BRUSH_CTX_RENAME) {
            HWND parent = GetWindow(hwnd, GW_OWNER);
            if (!parent) parent = GetParent(hwnd);
            if (parent) {
                SendMessageA(parent, WM_COMMAND, MAKEWPARAM(pick, 0), 0);
            }
        }
        return 0;
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
            hwndBrushPreview = nullptr;
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

    hwndBrushPreview = CreateWindowA("STATIC", "",
        WS_VISIBLE | WS_CHILD,
        10, 214, kFlyoutW - 20, kPreviewH,
        hwndBrushFlyout, (HMENU)(INT_PTR)IDC_BRUSH_FLYOUT_PREVIEW, GetModuleHandle(NULL), NULL);
    if (hwndBrushPreview) {
        SetWindowLongPtrA(hwndBrushPreview, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BrushPreviewProc));
    }

    const int rowIds[11] = {
        IDC_BRUSH_ROW_SIZE, IDC_BRUSH_ROW_SPACING, IDC_BRUSH_ROW_FLOW,
        IDC_BRUSH_ROW_HARDNESS, IDC_BRUSH_ROW_SCATTER, IDC_BRUSH_ROW_ANGLE,
        IDC_BRUSH_ROW_ORIENT, IDC_BRUSH_ROW_SIZEJITTER, IDC_BRUSH_ROW_OPJITTER,
        IDC_BRUSH_ROW_SMOOTH, IDC_BRUSH_ROW_PRESSURE
    };
    const char* rowTips[11] = {
        "Size — brush diameter in pixels",
        "Spacing — distance between stamps",
        "Flow — paint opacity per stamp",
        "Hardness — edge falloff",
        "Scatter — random stamp spread",
        "Angle — tip rotation jitter along stroke",
        "Orient — fixed tip angle offset in degrees",
        "Size jitter — random stamp diameter along stroke",
        "Opacity jitter — random stamp opacity along stroke",
        "Smooth — stabilize freehand stroke path",
        "Pen pressure — tablet dynamics"
    };
    for (int i = 0; i < 11; ++i) {
        hwndBrushRowButtons[i] = CreateWindowA("BUTTON", "",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            10, 264 + i * 22, kFlyoutW - 20, 20,
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

    const int actionIds[3] = {
        IDC_BRUSH_FLYOUT_GALLERY, IDC_BRUSH_FLYOUT_IMPORT, IDC_BRUSH_FLYOUT_DELETE
    };
    const char* actionLabels[3] = { "Gallery", "Import", "Delete" };
    const char* actionTips[3] = {
        "Open brush gallery",
        "Import PNG tip or ABR set",
        "Delete selected custom brush"
    };
    const int actionW = (kFlyoutW - 28) / 3;
    for (int i = 0; i < 3; ++i) {
        hwndBrushActionButtons[i] = CreateWindowA("BUTTON", actionLabels[i],
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            10 + i * (actionW + 4), 520, actionW, 22,
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

int BrushFlyoutContextPresetIndex() {
    return gBrushCtxPreset;
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

    char sizeLabel[48];
    char spacingLabel[48];
    char flowLabel[48];
    char hardLabel[48];
    char scatterLabel[48];
    char angleLabel[48];
    char orientLabel[48];
    char sizeJitterLabel[48];
    char opJitterLabel[48];
    char smoothLabel[48];
    char pressLabel[48];
    sprintf_s(sizeLabel, "Size  \xE2\x80\xBA  %d px", penWidth);
    sprintf_s(spacingLabel, "Spacing  \xE2\x80\xBA  %d%%", GetBrushSpacingPercent());
    sprintf_s(flowLabel, "Flow  \xE2\x80\xBA  %d%%", brushFlow);
    sprintf_s(hardLabel, "Hardness  \xE2\x80\xBA  %d%%", brushHardness);
    sprintf_s(scatterLabel, "Scatter  \xE2\x80\xBA  %d%%", brushScatter);
    sprintf_s(angleLabel, "Angle  \xE2\x80\xBA  %d%%", brushAngleJitter);
    sprintf_s(orientLabel, "Orient  \xE2\x80\xBA  %d deg", brushTipAngle);
    sprintf_s(sizeJitterLabel, "Sz Jitter  \xE2\x80\xBA  %d%%", brushSizeJitter);
    sprintf_s(opJitterLabel, "Op Jitter  \xE2\x80\xBA  %d%%", brushOpacityJitter);
    sprintf_s(smoothLabel, "Smooth  \xE2\x80\xBA  %d%%", brushSmoothing);
    sprintf_s(pressLabel, "Pressure  \xE2\x80\xBA  %s", penPressureEnabled ? "On" : "Off");

    if (hwndBrushRowButtons[0]) SetWindowTextA(hwndBrushRowButtons[0], sizeLabel);
    if (hwndBrushRowButtons[1]) SetWindowTextA(hwndBrushRowButtons[1], spacingLabel);
    if (hwndBrushRowButtons[2]) SetWindowTextA(hwndBrushRowButtons[2], flowLabel);
    if (hwndBrushRowButtons[3]) SetWindowTextA(hwndBrushRowButtons[3], hardLabel);
    if (hwndBrushRowButtons[4]) SetWindowTextA(hwndBrushRowButtons[4], scatterLabel);
    if (hwndBrushRowButtons[5]) SetWindowTextA(hwndBrushRowButtons[5], angleLabel);
    if (hwndBrushRowButtons[6]) SetWindowTextA(hwndBrushRowButtons[6], orientLabel);
    if (hwndBrushRowButtons[7]) SetWindowTextA(hwndBrushRowButtons[7], sizeJitterLabel);
    if (hwndBrushRowButtons[8]) SetWindowTextA(hwndBrushRowButtons[8], opJitterLabel);
    if (hwndBrushRowButtons[9]) SetWindowTextA(hwndBrushRowButtons[9], smoothLabel);
    if (hwndBrushRowButtons[10]) SetWindowTextA(hwndBrushRowButtons[10], pressLabel);

    if (hwndBrushActionButtons[2]) {
        EnableWindow(hwndBrushActionButtons[2], IsActiveBrushCustom() ? TRUE : FALSE);
    }
    if (hwndBrushPreview) InvalidateRect(hwndBrushPreview, NULL, FALSE);

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
