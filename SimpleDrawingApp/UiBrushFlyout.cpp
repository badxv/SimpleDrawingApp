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
constexpr int kSubFlyoutW = 168;
constexpr int kSubFlyoutH = 108;
constexpr int kSubFlyoutSliderH = 88;
constexpr int kPresetCell = 58;
constexpr int kPresetCols = 3;
constexpr int kMaxFlyoutPresets = 9;
constexpr int kPreviewH = 44;
constexpr int kRowTop = 264;
constexpr int kRowH = 20;      // button height
constexpr int kRowStep = 20;    // tighter than 22 so tall dynamics list fits more screens
constexpr int kActionH = 22;
constexpr int kActionGap = 8;

enum class BrushSubPanel {
    None = 0, Flow, Hardness, Pressure, Size, Spacing, Scatter, Angle, Orient, SizeJitter,
    OpacityJitter, Smooth, Count, Roundness, MinDiameter, WetEdges, RoundnessJitter, FlipX, FlipY,
    FlowJitter, ColorJitter, HueJitter, SatJitter, BrightJitter,
    Experimental, EchoDelay, EchoFeedback
};

struct BrushRowDef {
    int id;
    BrushSubPanel panel;
    const char* tip;
    bool experimental; // hidden unless brushExperimental (toggle row itself is always shown)
};

// Single source of truth for Atelier rows (order = button index).
constexpr BrushRowDef kBrushRows[] = {
    { IDC_BRUSH_ROW_SIZE, BrushSubPanel::Size, "Size — brush diameter in pixels", false },
    { IDC_BRUSH_ROW_SPACING, BrushSubPanel::Spacing, "Spacing — distance between stamps", false },
    { IDC_BRUSH_ROW_FLOW, BrushSubPanel::Flow, "Flow — paint opacity per stamp", false },
    { IDC_BRUSH_ROW_HARDNESS, BrushSubPanel::Hardness, "Hardness — edge falloff", false },
    { IDC_BRUSH_ROW_SCATTER, BrushSubPanel::Scatter, "Scatter — random stamp spread", false },
    { IDC_BRUSH_ROW_ANGLE, BrushSubPanel::Angle, "Angle — tip rotation jitter along stroke", false },
    { IDC_BRUSH_ROW_ORIENT, BrushSubPanel::Orient, "Orient — fixed tip angle offset in degrees", false },
    { IDC_BRUSH_ROW_SIZEJITTER, BrushSubPanel::SizeJitter, "Size jitter — random stamp diameter along stroke", false },
    { IDC_BRUSH_ROW_OPJITTER, BrushSubPanel::OpacityJitter, "Opacity jitter — random stamp opacity along stroke", false },
    { IDC_BRUSH_ROW_SMOOTH, BrushSubPanel::Smooth, "Smooth — stabilize freehand stroke path", false },
    { IDC_BRUSH_ROW_COUNT, BrushSubPanel::Count, "Count — stamps per spacing step (fans when Scatter is 0)", false },
    { IDC_BRUSH_ROW_ROUNDNESS, BrushSubPanel::Roundness, "Roundness — tip height as percent of width", false },
    { IDC_BRUSH_ROW_MINDIAM, BrushSubPanel::MinDiameter, "Min diameter — floor for size jitter as percent of size", false },
    { IDC_BRUSH_ROW_WETEDGES, BrushSubPanel::WetEdges, "Wet edges — watercolor buildup toward tip edges", false },
    { IDC_BRUSH_ROW_RDJITTER, BrushSubPanel::RoundnessJitter, "Roundness jitter — random tip squash along stroke", false },
    { IDC_BRUSH_ROW_FLIPX, BrushSubPanel::FlipX, "Flip X — chance to mirror tip horizontally", false },
    { IDC_BRUSH_ROW_FLIPY, BrushSubPanel::FlipY, "Flip Y — chance to mirror tip vertically", false },
    { IDC_BRUSH_ROW_FLOWJITTER, BrushSubPanel::FlowJitter, "Flow jitter — random stamp flow/alpha along stroke", false },
    { IDC_BRUSH_ROW_COLORJITTER, BrushSubPanel::ColorJitter, "Color jitter — blend toward background color per stamp", false },
    { IDC_BRUSH_ROW_HUEJITTER, BrushSubPanel::HueJitter, "Hue jitter — random hue shift per stamp", false },
    { IDC_BRUSH_ROW_SATJITTER, BrushSubPanel::SatJitter, "Sat jitter — random saturation per stamp", false },
    { IDC_BRUSH_ROW_BRIJITTER, BrushSubPanel::BrightJitter, "Bright jitter — random brightness per stamp", false },
    { IDC_BRUSH_ROW_PRESSURE, BrushSubPanel::Pressure, "Pen pressure — tablet dynamics", false },
    { IDC_BRUSH_ROW_EXPERIMENTAL, BrushSubPanel::Experimental,
      "Experimental — opt-in atelier lab (echo and future weird tools). Off by default.", false },
    { IDC_BRUSH_ROW_ECHODELAY, BrushSubPanel::EchoDelay,
      "Echo delay — postponed ghost stamps (ms scaled). Requires Experimental On.", true },
    { IDC_BRUSH_ROW_ECHOFEEDBACK, BrushSubPanel::EchoFeedback,
      "Echo feedback — how many decaying repeats follow each stamp", true },
};
constexpr int kBrushRowCount = static_cast<int>(sizeof(kBrushRows) / sizeof(kBrushRows[0]));

static_assert(kBrushRowCount == 26, "hwndBrushRowButtons size must match kBrushRows");

bool RowVisible(const BrushRowDef& row) {
    return !row.experimental || brushExperimental;
}

void LayoutBrushFlyoutRows(); // defined below; used by flyout proc / ensure

int VisibleBrushRowCount() {
    int n = 0;
    for (const BrushRowDef& row : kBrushRows) {
        if (RowVisible(row)) ++n;
    }
    return n;
}

int FlyoutContentHeight() {
    return kRowTop + VisibleBrushRowCount() * kRowStep + kActionGap + kActionH + 12;
}

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
    const int ruleY = kRowTop + VisibleBrushRowCount() * kRowStep + 2;
    MoveToEx(hdc, rc.left + 10, rc.top + ruleY, NULL);
    LineTo(hdc, rc.right - 10, rc.top + ruleY);
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
    Bitmap* drawTip = BrushTipForDraw(preset->tip.get());
    if (!drawTip) drawTip = preset->tip.get();

    const int maxStamp = (rc.bottom - rc.top) - 8;
    const int stampSize = MaxInt(4, (penWidth < maxStamp) ? penWidth : maxStamp);
    const REAL baseAlpha = static_cast<REAL>(brushFlow) / 100.0f
        * (0.45f + 0.55f * static_cast<REAL>(brushHardness) / 100.0f);

    const int tw = static_cast<int>(drawTip->GetWidth());
    const int th = static_cast<int>(drawTip->GetHeight());
    const float midY = (rc.top + rc.bottom) * 0.5f;
    const float left = static_cast<float>(rc.left + 10);
    const float right = static_cast<float>(rc.right - 10);
    const float spacing = MaxFloat(1.0f, preset->spacing * static_cast<float>(stampSize));
    int stampIndex = 0;
    for (float x = left; x <= right; x += spacing) {
        const float t = (x - left) / MaxFloat(1.0f, right - left);
        float y = midY + sinf(t * 6.28318f) * 6.0f;
        float sx = x;
        int count = brushStampCount;
        if (count < 1) count = 1;
        if (count > 8) count = 8;
        for (int c = 0; c < count; ++c) {
            float px = sx;
            float py = y;
            const int salt = stampIndex * 8 + c;
            if (brushScatter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 2654435761u) ^ 0x9e3779b9u;
                const float rx = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                const float ry = (static_cast<float>((n >> 8) & 255u) / 255.0f) * 2.0f - 1.0f;
                const float amp = static_cast<float>(stampSize) * 0.45f * (static_cast<float>(brushScatter) / 100.0f);
                px += rx * amp;
                py += ry * amp;
            } else if (count > 1 && c > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 73856093u) ^ 0x85ebca6bu;
                const float rx = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                const float ry = (static_cast<float>((n >> 8) & 255u) / 255.0f) * 2.0f - 1.0f;
                const float amp = static_cast<float>(stampSize) * 0.40f;
                px += rx * amp;
                py += ry * amp;
            }
            float angleDeg = static_cast<float>(brushTipAngle);
            if (brushAngleJitter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 2246822519u) ^ 0x85ebca6bu;
                const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                angleDeg += u * 180.0f * (static_cast<float>(brushAngleJitter) / 100.0f);
            }
            int stampDraw = stampSize;
            if (brushSizeJitter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 374761393u) ^ 0x27d4eb2du;
                const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                const float factor = 1.0f + u * (static_cast<float>(brushSizeJitter) / 100.0f);
                stampDraw = static_cast<int>(static_cast<float>(stampSize) * factor + 0.5f);
            }
            if (brushMinDiameter > 0) {
                int floorW = static_cast<int>(static_cast<float>(stampSize)
                    * (static_cast<float>(brushMinDiameter) / 100.0f) + 0.5f);
                if (floorW < 1) floorW = 1;
                if (stampDraw < floorW) stampDraw = floorW;
            }
            if (stampDraw < 1) stampDraw = 1;
            REAL alphaScale = baseAlpha;
            if (brushOpacityJitter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 1103515245u) ^ 0xc2b2ae35u;
                const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                alphaScale = baseAlpha * (1.0f + u * (static_cast<float>(brushOpacityJitter) / 100.0f));
                if (alphaScale < 0.05f) alphaScale = 0.05f;
                if (alphaScale > 1.0f) alphaScale = 1.0f;
            }
            if (brushFlowJitter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 2246822519u) ^ 0x27d4eb2du;
                const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                alphaScale *= (1.0f + u * (static_cast<float>(brushFlowJitter) / 100.0f));
                if (alphaScale < 0.05f) alphaScale = 0.05f;
                if (alphaScale > 1.0f) alphaScale = 1.0f;
            }
            REAL cr = static_cast<float>(GetRValue(penColor)) / 255.0f;
            REAL cg = static_cast<float>(GetGValue(penColor)) / 255.0f;
            REAL cb = static_cast<float>(GetBValue(penColor)) / 255.0f;
            if (brushColorJitter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 374761393u) ^ 0x9e3779b9u;
                const float u = static_cast<float>(n & 255u) / 255.0f;
                const float mix = u * (static_cast<float>(brushColorJitter) / 100.0f);
                const float br = static_cast<float>(GetRValue(backColor)) / 255.0f;
                const float bg = static_cast<float>(GetGValue(backColor)) / 255.0f;
                const float bb = static_cast<float>(GetBValue(backColor)) / 255.0f;
                cr = cr + (br - cr) * mix;
                cg = cg + (bg - cg) * mix;
                cb = cb + (bb - cb) * mix;
            }
            if (brushHueJitter > 0 || brushSatJitter > 0 || brushBrightJitter > 0) {
                COLORREF rgb = RGB(
                    ClampInt(static_cast<int>(cr * 255.0f + 0.5f), 0, 255),
                    ClampInt(static_cast<int>(cg * 255.0f + 0.5f), 0, 255),
                    ClampInt(static_cast<int>(cb * 255.0f + 0.5f), 0, 255));
                const float r0 = GetRValue(rgb) / 255.0f;
                const float g0 = GetGValue(rgb) / 255.0f;
                const float b0 = GetBValue(rgb) / 255.0f;
                const float maxc = (r0 > g0) ? ((r0 > b0) ? r0 : b0) : ((g0 > b0) ? g0 : b0);
                const float minc = (r0 < g0) ? ((r0 < b0) ? r0 : b0) : ((g0 < b0) ? g0 : b0);
                const float d = maxc - minc;
                float h = 0.0f, s = (maxc <= 0.0001f) ? 0.0f : (d / maxc), v = maxc;
                if (d > 0.0001f) {
                    if (maxc == r0) h = 60.0f * fmodf((g0 - b0) / d, 6.0f);
                    else if (maxc == g0) h = 60.0f * ((b0 - r0) / d + 2.0f);
                    else h = 60.0f * ((r0 - g0) / d + 4.0f);
                    if (h < 0.0f) h += 360.0f;
                }
                if (brushHueJitter > 0) {
                    const unsigned n = (static_cast<unsigned>(salt) * 0x85ebca6bu) ^ 0x27d4eb2du;
                    const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                    h += u * 180.0f * (static_cast<float>(brushHueJitter) / 100.0f);
                }
                if (brushSatJitter > 0) {
                    const unsigned n = (static_cast<unsigned>(salt) * 0xc2b2ae35u) ^ 0x9e3779b9u;
                    const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                    s *= (1.0f + u * (static_cast<float>(brushSatJitter) / 100.0f));
                    if (s < 0.0f) s = 0.0f;
                    if (s > 1.0f) s = 1.0f;
                }
                if (brushBrightJitter > 0) {
                    const unsigned n = (static_cast<unsigned>(salt) * 0x27d4eb2du) ^ 0x85ebca6bu;
                    const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                    v *= (1.0f + u * (static_cast<float>(brushBrightJitter) / 100.0f));
                    if (v < 0.05f) v = 0.05f;
                    if (v > 1.0f) v = 1.0f;
                }
                if (brushHueJitter > 0 && s < 0.15f) s = 0.55f;
                while (h < 0.0f) h += 360.0f;
                while (h >= 360.0f) h -= 360.0f;
                const float c = v * s;
                const float hp = h / 60.0f;
                const float xh = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
                float rr = 0, gg = 0, bb = 0;
                if (hp < 1.0f) { rr = c; gg = xh; }
                else if (hp < 2.0f) { rr = xh; gg = c; }
                else if (hp < 3.0f) { gg = c; bb = xh; }
                else if (hp < 4.0f) { gg = xh; bb = c; }
                else if (hp < 5.0f) { rr = xh; bb = c; }
                else { rr = c; bb = xh; }
                const float m = v - c;
                cr = rr + m;
                cg = gg + m;
                cb = bb + m;
            }
            ColorMatrix matrix = {
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, alphaScale, 0.0f,
                cr, cg, cb, 0.0f, 1.0f
            };
            ImageAttributes attrs;
            attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            int roundness = brushRoundness;
            if (brushRoundnessJitter > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 2654435761u) ^ 0x27d4eb2du;
                const float u = (static_cast<float>(n & 255u) / 255.0f) * 2.0f - 1.0f;
                const float factor = 1.0f + u * (static_cast<float>(brushRoundnessJitter) / 100.0f);
                roundness = static_cast<int>(static_cast<float>(brushRoundness) * factor + 0.5f);
            }
            if (roundness < 1) roundness = 1;
            if (roundness > 100) roundness = 100;
            const REAL half = static_cast<REAL>(stampDraw) * 0.5f;
            const REAL height = static_cast<REAL>(stampDraw) * (static_cast<REAL>(roundness) / 100.0f);
            const REAL halfH = height * 0.5f;
            bool flipX = false;
            bool flipY = false;
            if (brushFlipX > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 1597334677u) ^ 0x85ebca6bu;
                flipX = static_cast<int>(n % 100u) < brushFlipX;
            }
            if (brushFlipY > 0) {
                const unsigned n = (static_cast<unsigned>(salt) * 3812015801u) ^ 0xc2b2ae35u;
                flipY = static_cast<int>(n % 100u) < brushFlipY;
            }
            if (flipX) angleDeg = -angleDeg;
            if (flipY) angleDeg = 180.0f - angleDeg;
            GraphicsState state = g.Save();
            g.TranslateTransform(px, py);
            if (angleDeg != 0.0f) g.RotateTransform(angleDeg);
            const REAL destX = flipX ? half : -half;
            const REAL destY = flipY ? halfH : -halfH;
            const REAL destW = flipX ? -static_cast<REAL>(stampDraw) : static_cast<REAL>(stampDraw);
            const REAL destH = flipY ? -height : height;
            RectF dest(destX, destY, destW, destH);
            g.DrawImage(drawTip, dest, 0.0f, 0.0f, static_cast<REAL>(tw), static_cast<REAL>(th),
                UnitPixel, &attrs);
            g.Restore(state);
        }
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
        || gBrushSubPanel == BrushSubPanel::Smooth
        || gBrushSubPanel == BrushSubPanel::Count
        || gBrushSubPanel == BrushSubPanel::Roundness
        || gBrushSubPanel == BrushSubPanel::MinDiameter
        || gBrushSubPanel == BrushSubPanel::WetEdges
        || gBrushSubPanel == BrushSubPanel::RoundnessJitter
        || gBrushSubPanel == BrushSubPanel::FlipX
        || gBrushSubPanel == BrushSubPanel::FlipY
        || gBrushSubPanel == BrushSubPanel::FlowJitter
        || gBrushSubPanel == BrushSubPanel::ColorJitter
        || gBrushSubPanel == BrushSubPanel::HueJitter
        || gBrushSubPanel == BrushSubPanel::SatJitter
        || gBrushSubPanel == BrushSubPanel::BrightJitter
        || gBrushSubPanel == BrushSubPanel::EchoDelay
        || gBrushSubPanel == BrushSubPanel::EchoFeedback);
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
            } else if (gBrushSubPanel == BrushSubPanel::Count) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 8));
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, GetBrushStampCount());
            } else if (gBrushSubPanel == BrushSubPanel::Roundness) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
                SendMessage(hwndBrushSubSlider, TBM_SETPOS, TRUE, GetBrushRoundness());
            } else if (gBrushSubPanel == BrushSubPanel::Scatter
                || gBrushSubPanel == BrushSubPanel::Angle
                || gBrushSubPanel == BrushSubPanel::SizeJitter
                || gBrushSubPanel == BrushSubPanel::OpacityJitter
                || gBrushSubPanel == BrushSubPanel::Smooth
                || gBrushSubPanel == BrushSubPanel::MinDiameter
                || gBrushSubPanel == BrushSubPanel::WetEdges
                || gBrushSubPanel == BrushSubPanel::RoundnessJitter
                || gBrushSubPanel == BrushSubPanel::FlipX
                || gBrushSubPanel == BrushSubPanel::FlipY
                || gBrushSubPanel == BrushSubPanel::FlowJitter
                || gBrushSubPanel == BrushSubPanel::ColorJitter
                || gBrushSubPanel == BrushSubPanel::HueJitter
                || gBrushSubPanel == BrushSubPanel::SatJitter
                || gBrushSubPanel == BrushSubPanel::BrightJitter
                || gBrushSubPanel == BrushSubPanel::EchoDelay
                || gBrushSubPanel == BrushSubPanel::EchoFeedback) {
                SendMessage(hwndBrushSubSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                int pos = GetBrushScatter();
                if (gBrushSubPanel == BrushSubPanel::Angle) pos = GetBrushAngleJitter();
                else if (gBrushSubPanel == BrushSubPanel::SizeJitter) pos = GetBrushSizeJitter();
                else if (gBrushSubPanel == BrushSubPanel::OpacityJitter) pos = GetBrushOpacityJitter();
                else if (gBrushSubPanel == BrushSubPanel::Smooth) pos = GetBrushSmoothing();
                else if (gBrushSubPanel == BrushSubPanel::MinDiameter) pos = GetBrushMinDiameter();
                else if (gBrushSubPanel == BrushSubPanel::WetEdges) pos = GetBrushWetEdges();
                else if (gBrushSubPanel == BrushSubPanel::RoundnessJitter) pos = GetBrushRoundnessJitter();
                else if (gBrushSubPanel == BrushSubPanel::FlipX) pos = GetBrushFlipX();
                else if (gBrushSubPanel == BrushSubPanel::FlipY) pos = GetBrushFlipY();
                else if (gBrushSubPanel == BrushSubPanel::FlowJitter) pos = GetBrushFlowJitter();
                else if (gBrushSubPanel == BrushSubPanel::ColorJitter) pos = GetBrushColorJitter();
                else if (gBrushSubPanel == BrushSubPanel::HueJitter) pos = GetBrushHueJitter();
                else if (gBrushSubPanel == BrushSubPanel::SatJitter) pos = GetBrushSatJitter();
                else if (gBrushSubPanel == BrushSubPanel::BrightJitter) pos = GetBrushBrightJitter();
                else if (gBrushSubPanel == BrushSubPanel::EchoDelay) pos = GetBrushEchoDelay();
                else if (gBrushSubPanel == BrushSubPanel::EchoFeedback) pos = GetBrushEchoFeedback();
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
        || gBrushSubPanel == BrushSubPanel::OpacityJitter || gBrushSubPanel == BrushSubPanel::Smooth
        || gBrushSubPanel == BrushSubPanel::Count || gBrushSubPanel == BrushSubPanel::Roundness
        || gBrushSubPanel == BrushSubPanel::MinDiameter || gBrushSubPanel == BrushSubPanel::WetEdges
        || gBrushSubPanel == BrushSubPanel::RoundnessJitter || gBrushSubPanel == BrushSubPanel::FlipX
        || gBrushSubPanel == BrushSubPanel::FlipY || gBrushSubPanel == BrushSubPanel::FlowJitter
        || gBrushSubPanel == BrushSubPanel::ColorJitter || gBrushSubPanel == BrushSubPanel::HueJitter
        || gBrushSubPanel == BrushSubPanel::SatJitter || gBrushSubPanel == BrushSubPanel::BrightJitter
        || gBrushSubPanel == BrushSubPanel::EchoDelay || gBrushSubPanel == BrushSubPanel::EchoFeedback) {
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
            } else if (gBrushSubPanel == BrushSubPanel::Count) {
                SetBrushStampCount(pos);
            } else if (gBrushSubPanel == BrushSubPanel::Roundness) {
                SetBrushRoundness(pos);
            } else if (gBrushSubPanel == BrushSubPanel::MinDiameter) {
                SetBrushMinDiameter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::WetEdges) {
                SetBrushWetEdges(pos);
            } else if (gBrushSubPanel == BrushSubPanel::RoundnessJitter) {
                SetBrushRoundnessJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::FlipX) {
                SetBrushFlipX(pos);
            } else if (gBrushSubPanel == BrushSubPanel::FlipY) {
                SetBrushFlipY(pos);
            } else if (gBrushSubPanel == BrushSubPanel::FlowJitter) {
                SetBrushFlowJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::ColorJitter) {
                SetBrushColorJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::HueJitter) {
                SetBrushHueJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::SatJitter) {
                SetBrushSatJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::BrightJitter) {
                SetBrushBrightJitter(pos);
            } else if (gBrushSubPanel == BrushSubPanel::EchoDelay) {
                SetBrushEchoDelay(pos);
            } else if (gBrushSubPanel == BrushSubPanel::EchoFeedback) {
                SetBrushEchoFeedback(pos);
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
        for (int i = 0; i < kBrushRowCount; ++i) {
            if (id == kBrushRows[i].id) {
                if (kBrushRows[i].panel == BrushSubPanel::Experimental) {
                    SetBrushExperimental(!IsBrushExperimental());
                    LayoutBrushFlyoutRows();
                    SyncBrushFlyoutChecks();
                    HWND parent = GetWindow(hwnd, GW_OWNER);
                    if (!parent) parent = GetParent(hwnd);
                    if (parent) UpdateStatusBar(parent);
                    return 0;
                }
                if (!RowVisible(kBrushRows[i])) return 0;
                RECT rr = {};
                if (hwndBrushRowButtons[i]) GetWindowRect(hwndBrushRowButtons[i], &rr);
                OpenBrushSubFlyoutInternal(kBrushRows[i].panel, rr.top - 4);
                return 0;
            }
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
        0, 0, kFlyoutW, FlyoutContentHeight(),
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

    for (int i = 0; i < kBrushRowCount; ++i) {
        hwndBrushRowButtons[i] = CreateWindowA("BUTTON", "",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            10, kRowTop + i * kRowStep, kFlyoutW - 20, kRowH,
            hwndBrushFlyout, (HMENU)(INT_PTR)kBrushRows[i].id, GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndBrushRowButtons[i]);
        if (hwndTooltip && kBrushRows[i].tip) {
            TOOLINFOA ti = {};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwndBrushFlyout;
            ti.uId = (UINT_PTR)hwndBrushRowButtons[i];
            ti.lpszText = const_cast<char*>(kBrushRows[i].tip);
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
    const int actionY = kRowTop + VisibleBrushRowCount() * kRowStep + kActionGap;
    for (int i = 0; i < 3; ++i) {
        hwndBrushActionButtons[i] = CreateWindowA("BUTTON", actionLabels[i],
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            10 + i * (actionW + 4), actionY, actionW, kActionH,
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

    LayoutBrushFlyoutRows();
    RefreshPresetGrid();
    SyncBrushFlyoutChecks();
}

void FormatBrushRowLabel(BrushSubPanel panel, char* out, size_t outChars) {
    if (!out || outChars == 0) return;
    out[0] = '\0';
    // UTF-8 › (U+203A) matches existing Atelier row labels.
    constexpr const char* kSep = "  \xE2\x80\xBA  ";
    switch (panel) {
    case BrushSubPanel::Size: sprintf_s(out, outChars, "Size%s%d px", kSep, penWidth); break;
    case BrushSubPanel::Spacing: sprintf_s(out, outChars, "Spacing%s%d%%", kSep, GetBrushSpacingPercent()); break;
    case BrushSubPanel::Flow: sprintf_s(out, outChars, "Flow%s%d%%", kSep, brushFlow); break;
    case BrushSubPanel::Hardness: sprintf_s(out, outChars, "Hardness%s%d%%", kSep, brushHardness); break;
    case BrushSubPanel::Scatter: sprintf_s(out, outChars, "Scatter%s%d%%", kSep, brushScatter); break;
    case BrushSubPanel::Angle: sprintf_s(out, outChars, "Angle%s%d%%", kSep, brushAngleJitter); break;
    case BrushSubPanel::Orient: sprintf_s(out, outChars, "Orient%s%d deg", kSep, brushTipAngle); break;
    case BrushSubPanel::SizeJitter: sprintf_s(out, outChars, "Sz Jitter%s%d%%", kSep, brushSizeJitter); break;
    case BrushSubPanel::OpacityJitter: sprintf_s(out, outChars, "Op Jitter%s%d%%", kSep, brushOpacityJitter); break;
    case BrushSubPanel::Smooth: sprintf_s(out, outChars, "Smooth%s%d%%", kSep, brushSmoothing); break;
    case BrushSubPanel::Count: sprintf_s(out, outChars, "Count%s%d", kSep, brushStampCount); break;
    case BrushSubPanel::Roundness: sprintf_s(out, outChars, "Roundness%s%d%%", kSep, brushRoundness); break;
    case BrushSubPanel::MinDiameter: sprintf_s(out, outChars, "Min Diam%s%d%%", kSep, brushMinDiameter); break;
    case BrushSubPanel::WetEdges: sprintf_s(out, outChars, "Wet Edges%s%d%%", kSep, brushWetEdges); break;
    case BrushSubPanel::RoundnessJitter: sprintf_s(out, outChars, "Rd Jitter%s%d%%", kSep, brushRoundnessJitter); break;
    case BrushSubPanel::FlipX: sprintf_s(out, outChars, "Flip X%s%d%%", kSep, brushFlipX); break;
    case BrushSubPanel::FlipY: sprintf_s(out, outChars, "Flip Y%s%d%%", kSep, brushFlipY); break;
    case BrushSubPanel::FlowJitter: sprintf_s(out, outChars, "Fl Jitter%s%d%%", kSep, brushFlowJitter); break;
    case BrushSubPanel::ColorJitter: sprintf_s(out, outChars, "Color Jit%s%d%%", kSep, brushColorJitter); break;
    case BrushSubPanel::HueJitter: sprintf_s(out, outChars, "Hue Jit%s%d%%", kSep, brushHueJitter); break;
    case BrushSubPanel::SatJitter: sprintf_s(out, outChars, "Sat Jit%s%d%%", kSep, brushSatJitter); break;
    case BrushSubPanel::BrightJitter: sprintf_s(out, outChars, "Bri Jit%s%d%%", kSep, brushBrightJitter); break;
    case BrushSubPanel::Experimental:
        sprintf_s(out, outChars, "Experimental%s%s", kSep, brushExperimental ? "On" : "Off");
        break;
    case BrushSubPanel::EchoDelay: {
        const int ms = brushEchoDelay * 8;
        sprintf_s(out, outChars, "Echo Delay%s%d ms", kSep, ms);
        break;
    }
    case BrushSubPanel::EchoFeedback: sprintf_s(out, outChars, "Echo Fdbk%s%d%%", kSep, brushEchoFeedback); break;
    case BrushSubPanel::Pressure:
        sprintf_s(out, outChars, "Pressure%s%s", kSep, penPressureEnabled ? "On" : "Off");
        break;
    default: break;
    }
}

void LayoutBrushFlyoutRows() {
    if (!hwndBrushFlyout) return;
    int visible = 0;
    for (int i = 0; i < kBrushRowCount; ++i) {
        HWND btn = hwndBrushRowButtons[i];
        if (!btn) continue;
        if (RowVisible(kBrushRows[i])) {
            ShowWindow(btn, SW_SHOW);
            MoveWindow(btn, 10, kRowTop + visible * kRowStep, kFlyoutW - 20, kRowH, TRUE);
            ++visible;
        } else {
            ShowWindow(btn, SW_HIDE);
        }
    }
    const int actionY = kRowTop + visible * kRowStep + kActionGap;
    const int actionW = (kFlyoutW - 28) / 3;
    for (int i = 0; i < 3; ++i) {
        if (!hwndBrushActionButtons[i]) continue;
        MoveWindow(hwndBrushActionButtons[i], 10 + i * (actionW + 4), actionY, actionW, kActionH, TRUE);
    }
    const int h = FlyoutContentHeight();
    RECT wr = {};
    GetWindowRect(hwndBrushFlyout, &wr);
    SetWindowPos(hwndBrushFlyout, nullptr, 0, 0, kFlyoutW, h,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SyncBrushRowButtonLabels() {
    for (int i = 0; i < kBrushRowCount; ++i) {
        if (!hwndBrushRowButtons[i]) continue;
        char label[48];
        FormatBrushRowLabel(kBrushRows[i].panel, label, sizeof(label));
        SetWindowTextA(hwndBrushRowButtons[i], label);
    }
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

    LayoutBrushFlyoutRows();
    SyncBrushRowButtonLabels();

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
    int x = br.right + 6;
    int y = br.top - 4;

    LayoutBrushFlyoutRows();
    const int flyoutH = FlyoutContentHeight();

    // Keep the tall dynamics list on-screen (Wine/Xvfb and short monitors).
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    if (y + flyoutH > work.bottom) y = work.bottom - flyoutH;
    if (y < work.top) y = work.top;
    if (x + kFlyoutW > work.right) x = work.right - kFlyoutW;
    if (x < work.left) x = work.left;

    SetWindowPos(hwndBrushFlyout, HWND_TOPMOST, x, y, kFlyoutW, flyoutH,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(hwndBrushFlyout);
    SyncBrushFlyoutChecks();
}
