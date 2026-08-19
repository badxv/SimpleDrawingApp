#include "AppSelection.h"
#include "AppState.h"
#include "SimpleDrawingApp.h"
#include "Resource.h"
#include "AtelierRaii.h"
#include "EventBus.h"
#include "AtelierEvents.h"
#include <windowsx.h>
#include <cmath>

using namespace Gdiplus;
using Atelier::MakeBitmap;

namespace {

void PublishSelectionChanged(HWND hwnd) {
    EventPayload payload{};
    payload.type = AtelierEvent::SelectionChanged;
    payload.hwnd = hwnd;
    payload.hasSelection = gSel.hasMarquee && gSel.w >= 1 && gSel.h >= 1;
    AppEventBus().Publish(payload);
}

}  // namespace

void DestroySelFloat() {
    delete gSel.floatBmp;
    gSel.floatBmp = nullptr;
    gSel.isFloating = false;
}

void ClearSelection(bool stampFloating) {
    Graphics* ag = gLayers.ActiveGraphics();
    if (stampFloating && gSel.isFloating && gSel.floatBmp && ag) {
        ag->DrawImage(gSel.floatBmp, gSel.x, gSel.y);
        InvalidateComposite();
    }
    DestroySelFloat();
    gSel.hasMarquee = false;
    gSel.creating = false;
    gSel.moving = false;
    gSel.x = gSel.y = gSel.w = gSel.h = 0;
    PublishSelectionChanged(nullptr);
}

void NormalizeSelRect(int x0, int y0, int x1, int y1, int& x, int& y, int& w, int& h) {
    int left = (x0 < x1) ? x0 : x1;
    int top = (y0 < y1) ? y0 : y1;
    int right = (x0 > x1) ? x0 : x1;
    int bottom = (y0 > y1) ? y0 : y1;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > docWidth) right = docWidth;
    if (bottom > docHeight) bottom = docHeight;
    x = left;
    y = top;
    w = right - left;
    h = bottom - top;
}

bool SelectionHitTest(int docX, int docY) {
    if (!gSel.hasMarquee || gSel.w < 1 || gSel.h < 1) return false;
    return docX >= gSel.x && docY >= gSel.y
        && docX < gSel.x + gSel.w && docY < gSel.y + gSel.h;
}

Bitmap* CloneBitmapRect(Bitmap* src, int x, int y, int w, int h) {
    if (!src || w < 1 || h < 1) return nullptr;
    Atelier::GdiplusBitmapPtr owned = MakeBitmap(w, h, PixelFormat32bppARGB);
    if (!owned) return nullptr;
    Graphics g(owned.get());
    g.Clear(Color(0, 0, 0, 0));
    g.DrawImage(src, Rect(0, 0, w, h), x, y, w, h, UnitPixel);
    return owned.release();
}

void LiftSelection() {
    Bitmap* ab = gLayers.ActiveBitmap();
    Graphics* ag = gLayers.ActiveGraphics();
    if (!gSel.hasMarquee || gSel.isFloating || !ab || !ag) return;
    if (gSel.w < 1 || gSel.h < 1) return;

    DestroySelFloat();
    gSel.floatBmp = CloneBitmapRect(ab, gSel.x, gSel.y, gSel.w, gSel.h);
    if (!gSel.floatBmp) return;

    const Layer* layer = gLayers.ActiveLayer();
    if (layer && layer->isBackground) {
        SolidBrush brush(GdiplusFromColor(gTheme.canvasBg));
        ag->FillRectangle(&brush, gSel.x, gSel.y, gSel.w, gSel.h);
    } else {
        ag->SetCompositingMode(CompositingModeSourceCopy);
        SolidBrush brush(Color(0, 0, 0, 0));
        ag->FillRectangle(&brush, gSel.x, gSel.y, gSel.w, gSel.h);
        ag->SetCompositingMode(CompositingModeSourceOver);
    }
    gSel.isFloating = true;
    InvalidateComposite();
}

void StampFloatingSelection() {
    Graphics* ag = gLayers.ActiveGraphics();
    if (!gSel.isFloating || !gSel.floatBmp || !ag) return;
    ag->DrawImage(gSel.floatBmp, gSel.x, gSel.y);
    DestroySelFloat();
    InvalidateComposite();
}

Bitmap* CaptureSelectionPixels() {
    if (!gSel.hasMarquee || gSel.w < 1 || gSel.h < 1) return nullptr;
    if (gSel.isFloating && gSel.floatBmp) {
        return CloneBitmapRect(gSel.floatBmp, 0, 0, gSel.w, gSel.h);
    }
    return CloneBitmapRect(gLayers.ActiveBitmap(), gSel.x, gSel.y, gSel.w, gSel.h);
}

void SetInternalClipboard(Bitmap* bmp) {
    delete gClipboardBmp;
    gClipboardBmp = bmp;
}

bool CopyBitmapToWinClipboard(Bitmap* bmp) {
    if (!bmp) return false;
    HBITMAP hbm = nullptr;
    if (bmp->GetHBITMAP(Color(255, 255, 255, 255), &hbm) != Ok || !hbm) {
        return false;
    }
    if (!OpenClipboard(nullptr)) {
        DeleteObject(hbm);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_BITMAP, hbm)) {
        DeleteObject(hbm);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

Bitmap* BitmapFromWinClipboard() {
    if (!OpenClipboard(nullptr)) return nullptr;
    HANDLE handle = GetClipboardData(CF_BITMAP);
    Bitmap* out = nullptr;
    if (handle) {
        out = new Bitmap(static_cast<HBITMAP>(handle), nullptr);
        if (out && out->GetLastStatus() != Ok) {
            delete out;
            out = nullptr;
        }
    }
    CloseClipboard();
    return out;
}

void DrawSelectionOverlay(Graphics* g) {
    if (!g) return;
    if (!gSel.hasMarquee && !gSel.creating) return;
    if (gSel.w < 1 || gSel.h < 1) return;

    if (gSel.isFloating && gSel.floatBmp) {
        g->DrawImage(
            gSel.floatBmp,
            RectF(
                gSel.x * zoomFactor,
                gSel.y * zoomFactor,
                gSel.w * zoomFactor,
                gSel.h * zoomFactor));
    }

    const float x = gSel.x * zoomFactor;
    const float y = gSel.y * zoomFactor;
    const float selW = MaxFloat(1.0f, gSel.w * zoomFactor);
    const float selH = MaxFloat(1.0f, gSel.h * zoomFactor);
    const RectF box(x, y, selW, selH);

    // Soft exterior veil (PS-like focus) — warm atelier ink, not cold gray.
    {
        const float docW = static_cast<float>(ScaledContentWidth());
        const float docH = static_cast<float>(ScaledContentHeight());
        SolidBrush veil(Color(58, 36, 28, 20));
        if (y > 0.0f) {
            g->FillRectangle(&veil, RectF(0.0f, 0.0f, docW, y));
        }
        if (y + selH < docH) {
            g->FillRectangle(&veil, RectF(0.0f, y + selH, docW, docH - (y + selH)));
        }
        if (x > 0.0f) {
            g->FillRectangle(&veil, RectF(0.0f, y, x, selH));
        }
        if (x + selW < docW) {
            g->FillRectangle(&veil, RectF(x + selW, y, docW - (x + selW), selH));
        }
    }

    // Crisp under-rule so ants read on any canvas tone.
    Pen under(Color(200, GetRValue(gTheme.ink), GetGValue(gTheme.ink), GetBValue(gTheme.ink)), 1.35f);
    g->DrawRectangle(&under, box);

    // Marching ants: ink + parchment (high contrast), gilt hairline for atelier.
    REAL dashPattern[2] = { 5.0f, 4.0f };
    Pen antDark(Color(255, GetRValue(gTheme.ink), GetGValue(gTheme.ink), GetBValue(gTheme.ink)), 1.15f);
    antDark.SetDashStyle(DashStyleCustom);
    antDark.SetDashPattern(dashPattern, 2);
    antDark.SetDashOffset(gSelAntOffset);

    Pen antLight(Color(255, 250, 244, 230), 1.15f);
    antLight.SetDashStyle(DashStyleCustom);
    antLight.SetDashPattern(dashPattern, 2);
    antLight.SetDashOffset(gSelAntOffset + dashPattern[0]);

    g->DrawRectangle(&antDark, box);
    g->DrawRectangle(&antLight, box);

    Pen gilt(Color(170, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)), 1.0f);
    g->DrawRectangle(&gilt, RectF(box.X - 0.5f, box.Y - 0.5f, box.Width + 1.0f, box.Height + 1.0f));

    // Corner handles — small gilt plates (transform affordance, atelier-scaled).
    const float hs = 3.6f;
    const PointF corners[4] = {
        { box.X, box.Y },
        { box.X + box.Width, box.Y },
        { box.X, box.Y + box.Height },
        { box.X + box.Width, box.Y + box.Height }
    };
    SolidBrush handleFill(Color(235, GetRValue(gTheme.chromeElevated), GetGValue(gTheme.chromeElevated), GetBValue(gTheme.chromeElevated)));
    Pen handleRim(Color(230, GetRValue(gTheme.accentDeep), GetGValue(gTheme.accentDeep), GetBValue(gTheme.accentDeep)), 1.1f);
    for (const PointF& c : corners) {
        RectF h(c.X - hs, c.Y - hs, hs * 2.0f, hs * 2.0f);
        g->FillRectangle(&handleFill, h);
        g->DrawRectangle(&handleRim, h);
    }
}

void DoCopy(HWND hwnd) {
    Bitmap* shot = CaptureSelectionPixels();
    if (!shot) return;
    CopyBitmapToWinClipboard(shot);
    SetInternalClipboard(shot); // takes ownership
    (void)hwnd;
}

void DoDeleteSelection(HWND hwnd) {
    if (!gSel.hasMarquee) return;
    EnsureCanvas(hwnd);
    gHistory.Push(gLayers);
    if (gSel.isFloating) {
        DestroySelFloat();
        gSel.hasMarquee = false;
        gSel.w = gSel.h = 0;
    }
    else {
        Graphics* ag = gLayers.ActiveGraphics();
        if (ag) {
            const Layer* layer = gLayers.ActiveLayer();
            if (layer && layer->isBackground) {
                SolidBrush brush(GdiplusFromColor(gTheme.canvasBg));
                ag->FillRectangle(&brush, gSel.x, gSel.y, gSel.w, gSel.h);
            } else {
                ag->SetCompositingMode(CompositingModeSourceCopy);
                SolidBrush brush(Color(0, 0, 0, 0));
                ag->FillRectangle(&brush, gSel.x, gSel.y, gSel.w, gSel.h);
                ag->SetCompositingMode(CompositingModeSourceOver);
            }
        }
        gSel.hasMarquee = false;
        gSel.w = gSel.h = 0;
        InvalidateComposite();
    }
    MarkDirty(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    PublishSelectionChanged(hwnd);
}

void DoCut(HWND hwnd) {
    if (!gSel.hasMarquee) return;
    DoCopy(hwnd);
    DoDeleteSelection(hwnd);
}

void DoPaste(HWND hwnd) {
    EnsureCanvas(hwnd);
    Bitmap* src = nullptr;
    if (gClipboardBmp) {
        src = CloneBitmapRect(
            gClipboardBmp, 0, 0,
            static_cast<int>(gClipboardBmp->GetWidth()),
            static_cast<int>(gClipboardBmp->GetHeight()));
    }
    if (!src) {
        src = BitmapFromWinClipboard();
    }
    if (!src) return;

    ClearSelection(true);
    SetActiveTool(DrawTool::Select);

    const int w = static_cast<int>(src->GetWidth());
    const int h = static_cast<int>(src->GetHeight());
    int pasteX = static_cast<int>(std::floor(scrollX / zoomFactor));
    int pasteY = static_cast<int>(std::floor(scrollY / zoomFactor));
    if (pasteX < 0) pasteX = 0;
    if (pasteY < 0) pasteY = 0;

    gHistory.Push(gLayers);
    gSel.hasMarquee = true;
    gSel.isFloating = true;
    gSel.floatBmp = src;
    gSel.x = pasteX;
    gSel.y = pasteY;
    gSel.w = w;
    gSel.h = h;
    MarkDirty(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    PublishSelectionChanged(hwnd);
}

void DoSelectAll(HWND hwnd) {
    EnsureCanvas(hwnd);
    ClearSelection(true);
    SetActiveTool(DrawTool::Select);
    gSel.hasMarquee = true;
    gSel.x = 0;
    gSel.y = 0;
    gSel.w = docWidth;
    gSel.h = docHeight;
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    PublishSelectionChanged(hwnd);
}


void Selection_Shutdown() {
    ClearSelection(false);
    delete gClipboardBmp;
    gClipboardBmp = nullptr;
}
