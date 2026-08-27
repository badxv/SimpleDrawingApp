#include "AppViewport.h"
#include "AppCanvas.h"
#include "AppStroke.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppSelection.h"
#include "AppFeatureFlags.h"
#include "SimpleDrawingApp.h"
#include "DrawingTools.h"
#include "LayerHistory.h"
#include "AtelierControls.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <cmath>

using namespace Gdiplus;

static void MaybeSnapDocumentPoint(int& docX, int& docY) {
    if (!IsFeatureEnabled(AppFeature::SnapToGrid)) return;
    docX = SnapCoordToGrid(docX);
    docY = SnapCoordToGrid(docY);
}

static bool ViewportToDocument(int localX, int localY, int& docX, int& docY) {
    if (zoomFactor <= 0.0f) return false;
    docX = static_cast<int>(std::floor((localX + scrollX) / zoomFactor));
    docY = static_cast<int>(std::floor((localY + scrollY) / zoomFactor));
    if (docX < 0 || docY < 0 || docX >= docWidth || docY >= docHeight) {
        return false;
    }
    MaybeSnapDocumentPoint(docX, docY);
    if (docX < 0) docX = 0;
    if (docY < 0) docY = 0;
    if (docX >= docWidth) docX = docWidth - 1;
    if (docY >= docHeight) docY = docHeight - 1;
    return true;
}

static void ViewportToDocumentUnclamped(int localX, int localY, int& docX, int& docY) {
    if (zoomFactor <= 0.0f) {
        docX = 0;
        docY = 0;
        return;
    }
    docX = static_cast<int>(std::floor((localX + scrollX) / zoomFactor));
    docY = static_cast<int>(std::floor((localY + scrollY) / zoomFactor));
    MaybeSnapDocumentPoint(docX, docY);
}

static void DrawCanvasGrid(Graphics* g) {
    if (!g || !IsFeatureEnabled(AppFeature::CanvasGrid)) return;
    if (docWidth < 1 || docHeight < 1 || zoomFactor <= 0.0f) return;
    if (GRID_SPACING < 1) return;

    const REAL z = zoomFactor;
    const REAL docW = static_cast<REAL>(docWidth) * z;
    const REAL docH = static_cast<REAL>(docHeight) * z;

    Pen minor(Color(55, 90, 110, 130), 1.0f);
    Pen major(Color(90, 70, 90, 110), 1.0f);
    minor.SetDashStyle(DashStyleSolid);
    major.SetDashStyle(DashStyleSolid);

    for (int x = 0; x <= docWidth; x += GRID_SPACING) {
        const bool isMajor = (x % (GRID_SPACING * GRID_MAJOR_EVERY) == 0) || x == docWidth;
        Pen& pen = isMajor ? major : minor;
        const REAL sx = static_cast<REAL>(x) * z;
        g->DrawLine(&pen, sx, 0.0f, sx, docH);
    }
    for (int y = 0; y <= docHeight; y += GRID_SPACING) {
        const bool isMajor = (y % (GRID_SPACING * GRID_MAJOR_EVERY) == 0) || y == docHeight;
        Pen& pen = isMajor ? major : minor;
        const REAL sy = static_cast<REAL>(y) * z;
        g->DrawLine(&pen, 0.0f, sy, docW, sy);
    }
}
static int ScrollByMessage(HWND scrollBar, WPARAM wParam, int current, int maxScroll) {
    int pos = current;
    const int page = scrollBar ? AtelierScroll_GetPage(scrollBar) : 32;
    switch (LOWORD(wParam)) {
    case SB_LINELEFT: // also SB_LINEUP
        pos -= 16;
        break;
    case SB_LINERIGHT: // also SB_LINEDOWN
        pos += 16;
        break;
    case SB_PAGELEFT: // also SB_PAGEUP
        pos -= page;
        break;
    case SB_PAGERIGHT: // also SB_PAGEDOWN
        pos += page;
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        pos = static_cast<int>(HIWORD(wParam));
        break;
    case SB_TOP: // also SB_LEFT
        pos = 0;
        break;
    case SB_BOTTOM: // also SB_RIGHT
        pos = maxScroll;
        break;
    default:
        break;
    }
    if (pos < 0) pos = 0;
    if (pos > maxScroll) pos = maxScroll;
    return pos;
}
static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        // Canvas often holds focus after drawing; forward keys to the frame
        // so tool shortcuts and shape Alt/Ctrl modifiers still work.
        if (HWND parent = GetParent(hwnd)) {
            return SendMessageA(parent, uMsg, wParam, lParam);
        }
        return 0;
    case WM_SIZE:
        UpdateScrollBars();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_HSCROLL: {
        if ((HWND)lParam != hwndScrollH) return 0;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        int availW = rc.right - rc.left;
        if (hwndScrollV && IsWindowVisible(hwndScrollV)) availW -= ATL_SCROLL_THICK;
        if (availW < 1) availW = 1;
        const int contentW = ScaledContentWidth();
        const int maxScroll = (contentW > availW) ? (contentW - availW) : 0;
        const int newPos = ScrollByMessage(hwndScrollH, wParam, scrollX, maxScroll);
        if (newPos != scrollX) {
            scrollX = newPos;
            AtelierScroll_SetInfo(hwndScrollH, 0, contentW > 1 ? contentW - 1 : 0, availW, scrollX, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_VSCROLL: {
        if ((HWND)lParam != hwndScrollV) return 0;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        int availH = rc.bottom - rc.top;
        if (hwndScrollH && IsWindowVisible(hwndScrollH)) availH -= ATL_SCROLL_THICK;
        if (availH < 1) availH = 1;
        const int contentH = ScaledContentHeight();
        const int maxScroll = (contentH > availH) ? (contentH - availH) : 0;
        const int newPos = ScrollByMessage(hwndScrollV, wParam, scrollY, maxScroll);
        if (newPos != scrollY) {
            scrollY = newPos;
            AtelierScroll_SetInfo(hwndScrollV, 0, contentH > 1 ? contentH - 1 : 0, availH, scrollY, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        HWND parent = GetParent(hwnd);
        if (parent) {
            return SendMessageA(parent, WM_MOUSEWHEEL, wParam, lParam);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        HWND parent = GetParent(hwnd);
        if (!parent) break;
        EnsureCanvas(parent);

        const int localX = GET_X_LPARAM(lParam);
        const int localY = GET_Y_LPARAM(lParam);
        int docX = 0, docY = 0;
        ViewportToDocumentUnclamped(localX, localY, docX, docY);

        if (currentTool == DrawTool::Select) {
            if (SelectionHitTest(docX, docY)) {
                gHistory.Push(gLayers);
                if (!gSel.isFloating) {
                    LiftSelection();
                }
                gSel.moving = true;
                gSel.creating = false;
                gSel.grabDX = docX - gSel.x;
                gSel.grabDY = docY - gSel.y;
                SetCapture(hwnd);
                MarkDirty(parent);
                InvalidateCanvas();
            }
            else {
                ClearSelection(true);
                gSel.creating = true;
                gSel.moving = false;
                gSel.hasMarquee = true;
                gSel.anchorX = docX;
                gSel.anchorY = docY;
                NormalizeSelRect(docX, docY, docX, docY, gSel.x, gSel.y, gSel.w, gSel.h);
                SetCapture(hwnd);
                InvalidateCanvas();
            }
            break;
        }

        if (!ViewportToDocument(localX, localY, docX, docY)) {
            break;
        }

        ClearSelection(true);

        if (currentTool == DrawTool::Fill) {
            gHistory.Push(gLayers);
            if (FloodFillCanvas(gLayers.ActiveBitmap(), docX, docY, penColor, OpacityToAlpha())) {
                InvalidateComposite();
                NoteDrawnColors();
                MarkDirty(parent);
                InvalidateCanvas();
            }
            break;
        }

        BeginStrokeLayer();
        if (!strokeGraphics) {
            break;
        }
        gHistory.Push(gLayers);
        isDrawing = true;
        lastPoint.x = docX;
        lastPoint.y = docY;
        shapeStart.x = docX;
        shapeStart.y = docY;

        if (IsFreehandTool(currentTool)) {
            DrawStrokeOnto(strokeGraphics.get(), docX, docY, docX, docY);
        }
        else if (IsShapeTool(currentTool)) {
            RedrawShapePreview(docX, docY, (wParam & MK_SHIFT) != 0);
        }

        SetCapture(hwnd);
        MarkDirty(parent);
        InvalidateCanvas();
        break;
    }
    case WM_MOUSEMOVE: {
        const int localX = GET_X_LPARAM(lParam);
        const int localY = GET_Y_LPARAM(lParam);
        int docX = 0, docY = 0;
        ViewportToDocumentUnclamped(localX, localY, docX, docY);

        if (gSel.creating) {
            NormalizeSelRect(gSel.anchorX, gSel.anchorY, docX, docY, gSel.x, gSel.y, gSel.w, gSel.h);
            InvalidateCanvas();
            break;
        }
        if (gSel.moving && gSel.isFloating) {
            gSel.x = docX - gSel.grabDX;
            gSel.y = docY - gSel.grabDY;
            InvalidateCanvas();
            break;
        }

        if (!isDrawing || !strokeGraphics) break;
        if (!ViewportToDocument(localX, localY, docX, docY)) break;

        if (IsFreehandTool(currentTool)) {
            DrawStrokeOnto(strokeGraphics.get(), lastPoint.x, lastPoint.y, docX, docY);
            lastPoint.x = docX;
            lastPoint.y = docY;
            InvalidateCanvas();
        }
        else if (IsShapeTool(currentTool)) {
            RedrawShapePreview(docX, docY, (wParam & MK_SHIFT) != 0);
            InvalidateCanvas();
        }
        break;
    }
    case WM_LBUTTONUP:
        if (gSel.creating) {
            gSel.creating = false;
            if (gSel.w < 2 || gSel.h < 2) {
                gSel.hasMarquee = false;
                gSel.w = gSel.h = 0;
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateCanvas();
            break;
        }
        if (gSel.moving) {
            gSel.moving = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateCanvas();
            if (HWND parent = GetParent(hwnd)) {
                UpdateStatusBar(parent);
            }
            break;
        }
        if (isDrawing) {
            isDrawing = false;
            CommitStrokeLayer();
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            InvalidateCanvas();
            if (HWND parent = GetParent(hwnd)) {
                UpdateStatusBar(parent);
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (gSel.creating) {
            gSel.creating = false;
            if (gSel.w < 2 || gSel.h < 2) {
                gSel.hasMarquee = false;
                gSel.w = gSel.h = 0;
            }
            InvalidateCanvas();
        }
        if (gSel.moving) {
            gSel.moving = false;
            InvalidateCanvas();
        }
        if (isDrawing) {
            isDrawing = false;
            CommitStrokeLayer();
            InvalidateCanvas();
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client = {};
        GetClientRect(hwnd, &client);
        const int viewW = client.right - client.left;
        const int viewH = client.bottom - client.top;

        HWND parent = GetParent(hwnd);
        if (parent) {
            EnsureCanvas(parent);
        }

        if (viewW > 0 && viewH > 0) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = memDC ? CreateCompatibleBitmap(hdc, viewW, viewH) : nullptr;
            if (!memDC || !memBmp) {
                if (memBmp) DeleteObject(memBmp);
                if (memDC) DeleteDC(memDC);
                EndPaint(hwnd, &ps);
                return 0;
            }
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            {
                Graphics g(memDC);
                g.Clear(Color(255, GetRValue(gTheme.workspace), GetGValue(gTheme.workspace), GetBValue(gTheme.workspace)));
                g.SetCompositingMode(CompositingModeSourceOver);
                g.SetInterpolationMode(InterpolationModeNearestNeighbor);
                g.SetPixelOffsetMode(PixelOffsetModeHalf);

                const int scaledW = ScaledContentWidth();
                const int scaledH = ScaledContentHeight();
                const Rect dest(-scrollX, -scrollY, scaledW, scaledH);

                Bitmap* flat = GetCompositeBitmap();
                const Layer* layer = gLayers.ActiveLayer();
                const bool erasePreview = strokeLayer
                    && currentTool == DrawTool::Eraser
                    && layer && !layer->isBackground;

                if (erasePreview) {
                    Bitmap* preview = CreateErasePreviewComposite(strokeLayer.get());
                    if (preview) {
                        g.DrawImage(preview, dest);
                        delete preview;
                    } else if (flat) {
                        g.DrawImage(flat, dest);
                    }
                } else {
                    if (flat) {
                        g.DrawImage(flat, dest);
                    }
                    if (strokeLayer) {
                        const REAL alpha = static_cast<REAL>(OpacityToAlpha()) / 255.0f;
                        ColorMatrix matrix = {
                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, alpha, 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, 1.0f
                        };
                        ImageAttributes attrs;
                        attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
                        g.DrawImage(
                            strokeLayer.get(),
                            dest,
                            0, 0,
                            static_cast<int>(strokeLayer->GetWidth()),
                            static_cast<int>(strokeLayer->GetHeight()),
                            UnitPixel,
                            &attrs);
                    }
                }

                // Selection overlay + optional grid in document space via transform.
                g.ResetTransform();
                g.TranslateTransform(static_cast<REAL>(-scrollX), static_cast<REAL>(-scrollY));
                DrawCanvasGrid(&g);
                DrawSelectionOverlay(&g);
            }

            BitBlt(hdc, 0, 0, viewW, viewH, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}
bool RegisterViewportClass(HINSTANCE hInstance) {
    WNDCLASSA vc = {};
    vc.lpfnWndProc = ViewportProc;
    vc.hInstance = hInstance;
    vc.lpszClassName = kAppViewportClassName;
    vc.hCursor = LoadCursor(NULL, IDC_ARROW);
    vc.hbrBackground = NULL;
    vc.style = CS_DBLCLKS;
    return RegisterClassA(&vc) != 0;
}
