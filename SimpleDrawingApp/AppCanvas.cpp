#include "AppCanvas.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "SimpleDrawingApp.h"
#include "AtelierControls.h"
#include <cmath>

using namespace Gdiplus;

void SyncDocSizeFromBitmap() {
    docWidth = MaxInt(1, gLayers.Width());
    docHeight = MaxInt(1, gLayers.Height());
}

void EnsureCanvas(HWND hwnd) {
    if (gLayers.Count() > 0) return;
    gLayers.Reset(docWidth, docHeight, gTheme.canvasBg);
    InvalidateComposite();
    (void)hwnd;
}

void DestroyCompositeCache() {
    compositeCache.reset();
    compositeDirty = true;
}

void InvalidateComposite() {
    compositeDirty = true;
}

Bitmap* GetCompositeBitmap() {
    if (!compositeDirty && compositeCache) {
        return compositeCache.get();
    }
    Bitmap* next = gLayers.CreateComposite();
    if (!next) {
        compositeDirty = true;
        return compositeCache.get();
    }
    compositeCache.reset(next);
    compositeDirty = false;
    return compositeCache.get();
}

void UpdateScrollBars() {
    if (!hwndViewport) return;

    RECT rc = {};
    GetClientRect(hwndViewport, &rc);
    const int clientW = rc.right - rc.left;
    const int clientH = rc.bottom - rc.top;
    const int contentW = MaxInt(1, static_cast<int>(std::lround(docWidth * zoomFactor)));
    const int contentH = MaxInt(1, static_cast<int>(std::lround(docHeight * zoomFactor)));

    int availW = clientW;
    int availH = clientH;
    bool needV = contentH > availH;
    bool needH = contentW > availW;
    if (needV) availW = MaxInt(1, clientW - ATL_SCROLL_THICK);
    if (needH) availH = MaxInt(1, clientH - ATL_SCROLL_THICK);
    needV = contentH > availH;
    needH = contentW > availW;
    availW = needV ? MaxInt(1, clientW - ATL_SCROLL_THICK) : clientW;
    availH = needH ? MaxInt(1, clientH - ATL_SCROLL_THICK) : clientH;

    const int maxX = (contentW > availW) ? (contentW - availW) : 0;
    const int maxY = (contentH > availH) ? (contentH - availH) : 0;
    if (scrollX > maxX) scrollX = maxX;
    if (scrollX < 0) scrollX = 0;
    if (scrollY > maxY) scrollY = maxY;
    if (scrollY < 0) scrollY = 0;

    if (hwndScrollH) {
        ShowWindow(hwndScrollH, needH ? SW_SHOW : SW_HIDE);
        if (needH) {
            MoveWindow(hwndScrollH, 0, clientH - ATL_SCROLL_THICK,
                needV ? (clientW - ATL_SCROLL_THICK) : clientW, ATL_SCROLL_THICK, TRUE);
            AtelierScroll_SetInfo(hwndScrollH, 0, contentW > 1 ? contentW - 1 : 0,
                availW > 0 ? availW : 1, scrollX, TRUE);
            scrollX = AtelierScroll_GetPos(hwndScrollH);
        }
    }
    if (hwndScrollV) {
        ShowWindow(hwndScrollV, needV ? SW_SHOW : SW_HIDE);
        if (needV) {
            MoveWindow(hwndScrollV, clientW - ATL_SCROLL_THICK, 0,
                ATL_SCROLL_THICK, needH ? (clientH - ATL_SCROLL_THICK) : clientH, TRUE);
            AtelierScroll_SetInfo(hwndScrollV, 0, contentH > 1 ? contentH - 1 : 0,
                availH > 0 ? availH : 1, scrollY, TRUE);
            scrollY = AtelierScroll_GetPos(hwndScrollV);
        }
    }
    if (hwndScrollCorner) {
        const bool both = needH && needV;
        ShowWindow(hwndScrollCorner, both ? SW_SHOW : SW_HIDE);
        if (both) {
            MoveWindow(hwndScrollCorner, clientW - ATL_SCROLL_THICK, clientH - ATL_SCROLL_THICK,
                ATL_SCROLL_THICK, ATL_SCROLL_THICK, TRUE);
            AtelierScroll_SetInfo(hwndScrollCorner, 0, 0, 1, 0, TRUE);
        }
    }
}

void InvalidateCanvas() {
    if (hwndViewport) {
        InvalidateRect(hwndViewport, NULL, FALSE);
    }
}

int ScaledContentWidth() {
    return MaxInt(1, static_cast<int>(std::lround(docWidth * zoomFactor)));
}

int ScaledContentHeight() {
    return MaxInt(1, static_cast<int>(std::lround(docHeight * zoomFactor)));
}

void SetZoomAtViewportPoint(HWND hwnd, float newZoom, int localX, int localY) {
    if (newZoom < ZOOM_MIN) newZoom = ZOOM_MIN;
    if (newZoom > ZOOM_MAX) newZoom = ZOOM_MAX;
    if (std::fabs(newZoom - zoomFactor) < 0.0001f) return;

    const float docX = (localX + scrollX) / zoomFactor;
    const float docY = (localY + scrollY) / zoomFactor;
    zoomFactor = newZoom;
    scrollX = static_cast<int>(std::lround(docX * zoomFactor)) - localX;
    scrollY = static_cast<int>(std::lround(docY * zoomFactor)) - localY;
    UpdateScrollBars();
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}

void ZoomByFactor(HWND hwnd, float factor) {
    if (!hwndViewport) {
        zoomFactor *= factor;
        if (zoomFactor < ZOOM_MIN) zoomFactor = ZOOM_MIN;
        if (zoomFactor > ZOOM_MAX) zoomFactor = ZOOM_MAX;
        UpdateScrollBars();
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
        return;
    }
    RECT rc = {};
    GetClientRect(hwndViewport, &rc);
    const int cx = (rc.right - rc.left) / 2;
    const int cy = (rc.bottom - rc.top) / 2;
    SetZoomAtViewportPoint(hwnd, zoomFactor * factor, cx, cy);
}

void ZoomToActual(HWND hwnd) {
    if (!hwndViewport) {
        zoomFactor = 1.0f;
        UpdateScrollBars();
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
        return;
    }
    RECT rc = {};
    GetClientRect(hwndViewport, &rc);
    SetZoomAtViewportPoint(hwnd, 1.0f, (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2);
}

void ZoomToFit(HWND hwnd) {
    if (!hwndViewport || docWidth < 1 || docHeight < 1) return;
    RECT rc = {};
    GetClientRect(hwndViewport, &rc);
    const int viewW = MaxInt(1, static_cast<int>(rc.right - rc.left));
    const int viewH = MaxInt(1, static_cast<int>(rc.bottom - rc.top));
    const float zx = static_cast<float>(viewW) / static_cast<float>(docWidth);
    const float zy = static_cast<float>(viewH) / static_cast<float>(docHeight);
    float z = (zx < zy) ? zx : zy;
    if (z < ZOOM_MIN) z = ZOOM_MIN;
    if (z > ZOOM_MAX) z = ZOOM_MAX;
    zoomFactor = z;
    scrollX = 0;
    scrollY = 0;
    UpdateScrollBars();
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}
