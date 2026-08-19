#include <windows.h>
#include <windowsx.h>
#include "framework.h"
#include "SimpleDrawingApp.h"
#include "AppMetrics.h"
#include "AppState.h"
#include "CaptionBar.h"
#include "AppSelection.h"
#include "AppDocument.h"
#include "AppApi.h"
#include "UiChromeLayout.h"
#include "UiChromeRender.h"
#include "UiControls.h"
#include "UiPaletteFloat.h"
#include "UiShapeFlyout.h"
#include "UiToolbar.h"
#include "EventBus.h"
#include "AtelierEvents.h"
#include "AtelierRaii.h"
#include "FileManager.h"
#include "ColorPicker.h"
#include "LayerHistory.h"
#include "LayerStack.h"
#include "DrawingTools.h"
#include "UiChrome.h"
#include "AtelierFonts.h"
#include "AtelierControls.h"
#include "AtelierArtwork.h"
#include "AtelierPalette.h"
#include "Resource.h"

#include <commctrl.h>
#include <objidl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Dwmapi.lib")

using namespace Gdiplus;
using Atelier::MakeBitmap;
using Atelier::MakeGraphics;

namespace {
const char CLASS_NAME[] = "SimpleDrawingAppWindowClass";
const char VIEWPORT_CLASS_NAME[] = "SimpleDrawingAppViewport";
ULONG_PTR gdiplusToken = 0;
}




void UpdatePenWidthDisplay() {
    if (!hwndPenWidthBox) return;
    char buf[16];
    sprintf_s(buf, "%d", penWidth);
    suppressEditNotify = true;
    SetWindowTextA(hwndPenWidthBox, buf);
    suppressEditNotify = false;
}

void UpdateOpacityDisplay() {
    if (!hwndOpacityBox) return;
    char buf[16];
    sprintf_s(buf, "%d", penOpacity);
    suppressEditNotify = true;
    SetWindowTextA(hwndOpacityBox, buf);
    suppressEditNotify = false;
}

static void AdjustPenWidth(HWND hwnd, int delta) {
    int next = penWidth + delta;
    if (next < 1) next = 1;
    if (next > 50) next = 50;
    if (next == penWidth) return;

    penWidth = next;
    if (hwndSlider) {
        SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);
    }
    UpdatePenWidthDisplay();
    UpdateStatusBar(hwnd);
}

static void AdjustOpacity(HWND hwnd, int delta) {
    int next = penOpacity + delta;
    if (next < 1) next = 1;
    if (next > 100) next = 100;
    if (next == penOpacity) return;

    penOpacity = next;
    if (hwndOpacitySlider) {
        SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, penOpacity);
    }
    UpdateOpacityDisplay();
    UpdateStatusBar(hwnd);
}

void UpdateWindowTitle(HWND hwnd) {
    char title[128];
    sprintf_s(title, "Simple Drawing App%s", documentDirty ? " *" : "");
    SetWindowTextA(hwnd, title);
}

void UpdateStatusBar(HWND hwnd) {
    if (!hwndStatus) return;

    const char* toolName = "Pen";
    if (currentTool == DrawTool::Eraser) toolName = "Eraser";
    else if (currentTool == DrawTool::Fill) toolName = "Fill";
    else if (currentTool == DrawTool::Line) toolName = "Line";
    else if (currentTool == DrawTool::Shape) {
        switch (currentShape) {
        case ShapeKind::Rectangle: toolName = "Rectangle"; break;
        case ShapeKind::Ellipse: toolName = "Ellipse"; break;
        case ShapeKind::Triangle: toolName = "Triangle"; break;
        case ShapeKind::Star: toolName = "Star"; break;
        case ShapeKind::Diamond: toolName = "Diamond"; break;
        case ShapeKind::RoundRect: toolName = "Round rect"; break;
        }
    }
    else if (currentTool == DrawTool::Select) toolName = "Select";

    const int zoomPct = static_cast<int>(zoomFactor * 100.0f + 0.5f);

    char part0[64];
    char part1[64];
    char part2[80];
    sprintf_s(part0, "Tool: %s", toolName);
    if (const Layer* layer = gLayers.ActiveLayer()) {
        sprintf_s(part1, "%s | %d x %d  %d%%", layer->name.c_str(), docWidth, docHeight, zoomPct);
    }
    else {
        sprintf_s(part1, "Size: %d x %d  %d%%", docWidth, docHeight, zoomPct);
    }
    sprintf_s(part2, "W:%d  Op:%d%%  %s", penWidth, penOpacity, documentDirty ? "Modified" : "Saved");

    SendMessageA(hwndStatus, SB_SETTEXTA, 0, (LPARAM)part0);
    SendMessageA(hwndStatus, SB_SETTEXTA, 1, (LPARAM)part1);
    SendMessageA(hwndStatus, SB_SETTEXTA, 2, (LPARAM)part2);
    (void)hwnd;
}

void MarkDirty(HWND hwnd) {
    documentDirty = true;
    EventPayload payload{};
    payload.type = AtelierEvent::DocumentDirtyChanged;
    payload.hwnd = hwnd;
    payload.documentDirty = true;
    AppEventBus().Publish(payload);
}

void MarkClean(HWND hwnd) {
    documentDirty = false;
    EventPayload payload{};
    payload.type = AtelierEvent::DocumentDirtyChanged;
    payload.hwnd = hwnd;
    payload.documentDirty = false;
    AppEventBus().Publish(payload);
}

void SetActiveTool(DrawTool tool) {
    const bool changed = (currentTool != tool);
    currentTool = tool;
    if (changed) {
        gToolFlash = 1.0f;
        if (tool != DrawTool::Shape) {
            CloseShapeFlyout();
        }
    }
    for (int i = 0; i < kToolButtonCount; ++i) {
        if (!hwndToolButtons[i]) continue;
        const bool selected = (static_cast<int>(tool) == i);
        SendMessageA(hwndToolButtons[i], BM_SETCHECK, selected ? BST_CHECKED : BST_UNCHECKED, 0);
        RedrawWindow(hwndToolButtons[i], NULL, NULL,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
    SyncShapeFlyoutChecks();
    if (changed) {
        EventPayload payload{};
        payload.type = AtelierEvent::ActiveToolChanged;
        payload.tool = tool;
        AppEventBus().Publish(payload);
    }
}

static void InvalidateActiveToolButton() {
    const int idx = static_cast<int>(currentTool);
    if (idx >= 0 && idx < kToolButtonCount && hwndToolButtons[idx]) {
        InvalidateRect(hwndToolButtons[idx], NULL, FALSE);
    }
}

static void SyncPaletteFromApp() {
    if (hwndPalette) {
        AtelierPalette_SetColors(hwndPalette, penColor, backColor);
    }
}

static void NoteDrawnColors() {
    if (!hwndPalette) return;
    if (currentTool == DrawTool::Eraser || currentTool == DrawTool::Select) return;

    if (currentTool == DrawTool::Pen
        || currentTool == DrawTool::Line
        || currentTool == DrawTool::Fill) {
        AtelierPalette_NoteColor(hwndPalette, penColor);
        return;
    }

    if (currentTool == DrawTool::Shape) {
        const ShapePaintMode mode = EffectiveShapePaintMode();
        if (mode == ShapePaintMode::Stroke || mode == ShapePaintMode::Both) {
            AtelierPalette_NoteColor(hwndPalette, penColor);
        }
        if (mode == ShapePaintMode::Fill || mode == ShapePaintMode::Both) {
            AtelierPalette_NoteColor(hwndPalette, backColor);
        }
    }
}

static bool IsTypingInEdit() {
    HWND focus = GetFocus();
    if (!focus) return false;
    char cls[32] = {};
    GetClassNameA(focus, cls, static_cast<int>(sizeof(cls)));
    return _stricmp(cls, "Edit") == 0;
}

static void InvalidateColorChips() {
    if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
    if (hwndBgButton) InvalidateRect(hwndBgButton, NULL, FALSE);
    SyncPaletteFromApp();
}

static bool ShouldRunIdleMotion(HWND hwnd) {
    if (!hwnd || gUiSizing) return false;
    if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (isDrawing) return false;
    if (gSel.creating || gSel.moving) return false;
    const HWND cap = GetCapture();
    if (cap == hwnd || (hwndViewport && cap == hwndViewport)) return false;
    return true;
}

static void GetChromeMetrics(HWND hwnd, int& toolbarH, int& statusH) {
    const ChromeLayout layout = GetChromeLayout(hwnd);
    toolbarH = layout.topH;
    statusH = layout.statusH + layout.bottomH;
}

static void ConfigureCanvasGraphics(Graphics* g) {
    if (!g) return;
    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetCompositingMode(CompositingModeSourceOver);
}

void DestroyStrokeLayer();
void CommitStrokeLayer();
void BeginStrokeLayer();
void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1);
void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY);
void RedrawShapePreview(int endX, int endY, bool shiftConstrained);
void UpdateScrollBars();
void SyncDocSizeFromBitmap();
void InvalidateCanvas();
void InvalidateComposite();
void DestroyCompositeCache();
Bitmap* GetCompositeBitmap();
void RefreshLayerList();
static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);


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

static void PopupAppMenu(HWND hwnd, int menuIndex, HWND anchorBtn) {
    if (!gAppMenu || menuIndex < 0) return;
    HMENU sub = GetSubMenu(gAppMenu, menuIndex);
    if (!sub) return;
    RECT br = {};
    if (anchorBtn) GetWindowRect(anchorBtn, &br);
    else GetWindowRect(hwnd, &br);
    TrackPopupMenu(sub, TPM_LEFTALIGN | TPM_TOPALIGN, br.left, br.bottom, 0, hwnd, NULL);
}

void RefreshLayerList() {
    if (!hwndLayerList) return;
    suppressLayerNotify = true;
    SendMessageA(hwndLayerList, LB_RESETCONTENT, 0, 0);

    // Show topmost layer first in the list.
    for (int i = gLayers.Count() - 1; i >= 0; --i) {
        const Layer* layer = gLayers.At(i);
        if (!layer) continue;
        char label[96];
        sprintf_s(label, "%s%s  %d%%",
            layer->visible ? "" : "[H] ",
            layer->name.c_str(),
            layer->opacity);
        const int idx = static_cast<int>(SendMessageA(hwndLayerList, LB_ADDSTRING, 0, (LPARAM)label));
        SendMessageA(hwndLayerList, LB_SETITEMDATA, idx, i);
    }

    // Select the row that matches active layer.
    const int count = static_cast<int>(SendMessageA(hwndLayerList, LB_GETCOUNT, 0, 0));
    for (int row = 0; row < count; ++row) {
        const int layerIndex = static_cast<int>(SendMessageA(hwndLayerList, LB_GETITEMDATA, row, 0));
        if (layerIndex == gLayers.ActiveIndex()) {
            SendMessageA(hwndLayerList, LB_SETCURSEL, row, 0);
            break;
        }
    }

    if (hwndLayerVisible) {
        const Layer* active = gLayers.ActiveLayer();
        const bool visible = active ? active->visible : true;
        SendMessageA(hwndLayerVisible, BM_SETCHECK, visible ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (hwndLayerOpacity) {
        const Layer* active = gLayers.ActiveLayer();
        const int opacity = active ? active->opacity : 100;
        SendMessage(hwndLayerOpacity, TBM_SETPOS, TRUE, opacity);
    }
    suppressLayerNotify = false;

    HWND mainWnd = hwndLayerList ? GetAncestor(hwndLayerList, GA_ROOT) : nullptr;
    PublishLayerListChanged(mainWnd);
}

static bool ViewportToDocument(int localX, int localY, int& docX, int& docY) {
    if (zoomFactor <= 0.0f) return false;
    docX = static_cast<int>(std::floor((localX + scrollX) / zoomFactor));
    docY = static_cast<int>(std::floor((localY + scrollY) / zoomFactor));
    if (docX < 0 || docY < 0 || docX >= docWidth || docY >= docHeight) {
        return false;
    }
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

static void SetZoomAtViewportPoint(HWND hwnd, float newZoom, int localX, int localY) {
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

static void ZoomByFactor(HWND hwnd, float factor) {
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

static void ZoomToActual(HWND hwnd) {
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

static void ZoomToFit(HWND hwnd) {
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

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK ShortcutsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        const char* text =
            "Tools\r\n"
            "  B          Pen\r\n"
            "  E          Eraser\r\n"
            "  G          Fill (bucket)\r\n"
            "  M          Select\r\n"
            "  L          Line\r\n"
            "  U          Shapes\r\n"
            "  X          Swap foreground / background\r\n"
            "  Tab        Toggle tools rail (floating color when hidden)\r\n"
            "  F9         Toggle layers panel\r\n"
            "  F8         Toggle size/opacity bar\r\n"
            "\r\n"
            "While drawing a shape\r\n"
            "  Alt        Fill only (hold)\r\n"
            "  Ctrl       Stroke + Fill (hold)\r\n"
            "  Shift      Constrain proportions\r\n"
            "\r\n"
            "Brush\r\n"
            "  [ / ]      Decrease / increase size\r\n"
            "\r\n"
            "File & edit\r\n"
            "  Ctrl+N     New\r\n"
            "  Ctrl+O     Open\r\n"
            "  Ctrl+S     Save\r\n"
            "  Ctrl+Z/Y   Undo / Redo\r\n"
            "  Ctrl+X/C/V Cut / Copy / Paste\r\n"
            "  Ctrl+A     Select all\r\n"
            "  Del        Delete selection\r\n"
            "  Ctrl+E     Canvas size\r\n"
            "\r\n"
            "View\r\n"
            "  Ctrl++/-   Zoom in / out\r\n"
            "  Ctrl+0     Actual size\r\n"
            "  F1         This shortcuts list\r\n";
        SetDlgItemTextA(hDlg, IDC_SHORTCUTS_TEXT, text);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void DestroyStrokeLayer() {
    strokeGraphics.reset();
    strokeLayer.reset();
}

void BeginStrokeLayer() {
    DestroyStrokeLayer();
    if (!gLayers.ActiveBitmap()) return;

    const int width = gLayers.Width();
    const int height = gLayers.Height();
    strokeLayer = MakeBitmap(width, height, PixelFormat32bppARGB);
    if (!strokeLayer) {
        return;
    }
    strokeGraphics = MakeGraphics(strokeLayer.get());
    if (!strokeGraphics) {
        DestroyStrokeLayer();
        return;
    }
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    strokeGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
    strokeGraphics->SetCompositingMode(CompositingModeSourceOver);
}

void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
    if (!target) return;

    // Draw fully opaque ink onto the stroke layer; opacity is applied once when compositing.
    // Eraser on non-background layers builds an alpha coverage mask (committed as transparent holes).
    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    if (eraseTransparent) {
        // SourceOver + white ink: AA coverage accumulates in alpha (SourceCopy left speckled gaps).
        target->SetCompositingMode(CompositingModeSourceOver);
        Pen pen(Color(255, 255, 255, 255), static_cast<REAL>(penWidth));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        target->DrawLine(&pen, x0, y0, x1, y1);
        return;
    }

    COLORREF strokeColor = (currentTool == DrawTool::Eraser) ? gTheme.canvasBg : penColor;
    Pen pen(GdiplusFromColor(strokeColor, 255), static_cast<REAL>(penWidth));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    target->DrawLine(&pen, x0, y0, x1, y1);
}

static void ConstrainShapeEnd(int x0, int y0, int& x1, int& y1) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int adx = (dx < 0) ? -dx : dx;
    const int ady = (dy < 0) ? -dy : dy;

    if (currentTool == DrawTool::Line) {
        // Snap to horizontal, vertical, or 45-degree.
        if (adx * 2 < ady) {
            x1 = x0;
        }
        else if (ady * 2 < adx) {
            y1 = y0;
        }
        else {
            const int s = (adx < ady) ? adx : ady;
            x1 = x0 + ((dx >= 0) ? s : -s);
            y1 = y0 + ((dy >= 0) ? s : -s);
        }
        return;
    }

    // Square / circle: equal abs extents from start.
    const int s = (adx > ady) ? adx : ady;
    x1 = x0 + ((dx >= 0) ? s : -s);
    y1 = y0 + ((dy >= 0) ? s : -s);
}

static void BuildShapePath(GraphicsPath& path, ShapeKind kind, int left, int top, int width, int height) {
    const RectF box(
        static_cast<REAL>(left),
        static_cast<REAL>(top),
        static_cast<REAL>(MaxInt(1, width)),
        static_cast<REAL>(MaxInt(1, height)));
    const REAL cx = box.X + box.Width * 0.5f;
    const REAL cy = box.Y + box.Height * 0.5f;

    switch (kind) {
    case ShapeKind::Rectangle:
        path.AddRectangle(box);
        break;
    case ShapeKind::Ellipse:
        path.AddEllipse(box);
        break;
    case ShapeKind::Triangle: {
        PointF pts[3] = {
            { cx, box.Y },
            { box.X, box.Y + box.Height },
            { box.X + box.Width, box.Y + box.Height }
        };
        path.AddPolygon(pts, 3);
        break;
    }
    case ShapeKind::Star: {
        PointF pts[10];
        for (int i = 0; i < 10; ++i) {
            const float ang = -1.5707963f + i * 3.14159265f / 5.0f;
            const float rad = (i % 2 == 0) ? 0.5f : 0.22f;
            pts[i] = PointF(cx + cosf(ang) * box.Width * rad, cy + sinf(ang) * box.Height * rad);
        }
        path.AddPolygon(pts, 10);
        break;
    }
    case ShapeKind::Diamond: {
        PointF pts[4] = {
            { cx, box.Y },
            { box.X + box.Width, cy },
            { cx, box.Y + box.Height },
            { box.X, cy }
        };
        path.AddPolygon(pts, 4);
        break;
    }
    case ShapeKind::RoundRect: {
        REAL rr = (box.Width < box.Height ? box.Width : box.Height) * 0.18f;
        if (rr < 2.0f) rr = 2.0f;
        if (rr * 2.0f > box.Width) rr = box.Width * 0.5f;
        if (rr * 2.0f > box.Height) rr = box.Height * 0.5f;
        path.AddArc(box.X, box.Y, rr * 2, rr * 2, 180, 90);
        path.AddArc(box.X + box.Width - rr * 2, box.Y, rr * 2, rr * 2, 270, 90);
        path.AddArc(box.X + box.Width - rr * 2, box.Y + box.Height - rr * 2, rr * 2, rr * 2, 0, 90);
        path.AddArc(box.X, box.Y + box.Height - rr * 2, rr * 2, rr * 2, 90, 90);
        path.CloseFigure();
        break;
    }
    }
}

static void DrawShapeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
    if (!target) return;

    Pen pen(GdiplusFromColor(penColor, 255), static_cast<REAL>(penWidth));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    if (currentTool == DrawTool::Line) {
        target->DrawLine(&pen, x0, y0, x1, y1);
        return;
    }

    int left = (x0 < x1) ? x0 : x1;
    int top = (y0 < y1) ? y0 : y1;
    int right = (x0 > x1) ? x0 : x1;
    int bottom = (y0 > y1) ? y0 : y1;
    int width = right - left;
    int height = bottom - top;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    const ShapePaintMode paintMode = EffectiveShapePaintMode();
    const bool doFill = (paintMode == ShapePaintMode::Fill || paintMode == ShapePaintMode::Both);
    const bool doStroke = (paintMode == ShapePaintMode::Stroke || paintMode == ShapePaintMode::Both);

    GraphicsPath path;
    BuildShapePath(path, currentShape, left, top, width, height);
    if (doFill) {
        SolidBrush fill(GdiplusFromColor(backColor, 255));
        target->FillPath(&fill, &path);
    }
    if (doStroke) {
        target->DrawPath(&pen, &path);
    }
}

void RedrawShapePreview(int endX, int endY, bool shiftConstrained) {
    if (!strokeGraphics) return;
    int x1 = endX;
    int y1 = endY;
    if (shiftConstrained) {
        ConstrainShapeEnd(shapeStart.x, shapeStart.y, x1, y1);
    }
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    DrawShapeOnto(strokeGraphics.get(), shapeStart.x, shapeStart.y, x1, y1);
    lastPoint.x = x1;
    lastPoint.y = y1;
}

static void RefreshShapePreviewIfDrawing() {
    if (!isDrawing || !strokeGraphics || !IsShapeTool(currentTool)) return;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    RedrawShapePreview(lastPoint.x, lastPoint.y, shift);
    InvalidateCanvas();
}

void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY) {
    if (!strokeLayer || !dest) return;

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

    const int width = static_cast<int>(strokeLayer->GetWidth());
    const int height = static_cast<int>(strokeLayer->GetHeight());
    dest->DrawImage(
        strokeLayer.get(),
        Rect(destX, destY, width, height),
        0, 0, width, height,
        UnitPixel,
        &attrs);
}

static void ApplyTransparentEraseMask(Bitmap* target, Bitmap* mask) {
    if (!target || !mask) return;

    const int width = static_cast<int>(mask->GetWidth());
    const int height = static_cast<int>(mask->GetHeight());
    if (width < 1 || height < 1) return;
    if (static_cast<int>(target->GetWidth()) < width || static_cast<int>(target->GetHeight()) < height) {
        return;
    }

    BitmapData maskData = {};
    BitmapData targetData = {};
    Rect lockRect(0, 0, width, height);
    if (mask->LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppARGB, &maskData) != Ok) return;
    if (target->LockBits(&lockRect, ImageLockModeWrite, PixelFormat32bppARGB, &targetData) != Ok) {
        mask->UnlockBits(&maskData);
        return;
    }

    auto* maskPx = static_cast<BYTE*>(maskData.Scan0);
    auto* targetPx = static_cast<BYTE*>(targetData.Scan0);
    for (int y = 0; y < height; ++y) {
        BYTE* mrow = maskPx + y * maskData.Stride;
        BYTE* trow = targetPx + y * targetData.Stride;
        for (int x = 0; x < width; ++x) {
            BYTE* m = mrow + x * 4;
            const unsigned ma = m[3];
            if (ma == 0) continue;

            BYTE* t = trow + x * 4;
            // Soft erase: scale destination by inverse mask coverage (AA fringes included).
            const unsigned inv = 255u - ma;
            t[0] = static_cast<BYTE>((t[0] * inv) / 255u);
            t[1] = static_cast<BYTE>((t[1] * inv) / 255u);
            t[2] = static_cast<BYTE>((t[2] * inv) / 255u);
            t[3] = static_cast<BYTE>((t[3] * inv) / 255u);
        }
    }

    target->UnlockBits(&targetData);
    mask->UnlockBits(&maskData);
}

static Bitmap* CreateErasePreviewComposite(Bitmap* eraseMask) {
    if (!eraseMask || gLayers.Width() < 1 || gLayers.Height() < 1) return nullptr;

    Bitmap* out = new Bitmap(gLayers.Width(), gLayers.Height(), PixelFormat32bppARGB);
    if (!out || out->GetLastStatus() != Ok) {
        delete out;
        return nullptr;
    }

    Graphics g(out);
    g.Clear(Color(0, 0, 0, 0));
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeNone);

    const int active = gLayers.ActiveIndex();
    Bitmap* erasedActive = nullptr;
    if (const Layer* activeLayer = gLayers.ActiveLayer()) {
        if (activeLayer->bitmap) {
            erasedActive = activeLayer->bitmap->Clone(0, 0,
                static_cast<INT>(activeLayer->bitmap->GetWidth()),
                static_cast<INT>(activeLayer->bitmap->GetHeight()),
                PixelFormat32bppARGB);
            if (erasedActive && erasedActive->GetLastStatus() == Ok) {
                ApplyTransparentEraseMask(erasedActive, eraseMask);
            } else {
                delete erasedActive;
                erasedActive = nullptr;
            }
        }
    }

    for (int i = 0; i < gLayers.Count(); ++i) {
        const Layer* layer = gLayers.At(i);
        if (!layer || !layer->visible) continue;
        Bitmap* bmp = (i == active && erasedActive) ? erasedActive : layer->bitmap;
        if (!bmp) continue;

        int opacity = layer->opacity;
        if (opacity < 1) continue;
        if (opacity > 100) opacity = 100;

        if (opacity >= 100) {
            g.DrawImage(bmp, 0, 0);
        } else {
            const REAL alpha = static_cast<REAL>(opacity) / 100.0f;
            ColorMatrix matrix = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, alpha, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 1.0f
            };
            ImageAttributes attrs;
            attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            const int w = static_cast<int>(bmp->GetWidth());
            const int h = static_cast<int>(bmp->GetHeight());
            g.DrawImage(bmp, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &attrs);
        }
    }

    delete erasedActive;
    return out;
}

void CommitStrokeLayer() {
    Graphics* ag = gLayers.ActiveGraphics();
    if (!strokeLayer || !ag) {
        DestroyStrokeLayer();
        return;
    }

    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    if (eraseTransparent) {
        ApplyTransparentEraseMask(gLayers.ActiveBitmap(), strokeLayer.get());
    }
    else {
        DrawStrokeLayerWithOpacity(ag, 0, 0);
    }
    DestroyStrokeLayer();
    InvalidateComposite();
    NoteDrawnColors();
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

                // Selection overlay in document space via transform.
                g.ResetTransform();
                g.TranslateTransform(static_cast<REAL>(-scrollX), static_cast<REAL>(-scrollY));
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

static void LayoutStatusParts(HWND hwnd) {
    if (!hwndStatus) return;
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    int parts[4] = { width / 3, (width * 2) / 3, width - 1, -1 };
    SendMessageA(hwndStatus, SB_SETPARTS, 3, (LPARAM)parts);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icex);

        gUiFont = AtelierFonts_Ui(13, false);
        gBrandFont = AtelierFonts_Display(18, false);
        if (!gUiFont) {
            gUiFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        }
        if (!gBrandFont) {
            gBrandFont = CreateFontA(-18, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Georgia");
        }
        gChromeBrush = CreateSolidBrush(gTheme.chromeBg);
        gChromeDeepBrush = CreateSolidBrush(gTheme.chromeDeep);
        gChromeElevatedBrush = CreateSolidBrush(gTheme.chromeElevated);
        AtelierControls_SetTheme(&gTheme);
        AtelierPalette_SetTheme(&gTheme);

        hwndBrand = CreateWindowExA(
            0,
            "SimpleDrawingAppBrand",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, BRAND_STRIP_W, TOPBAR_HEIGHT,
            hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL);

        CreateAppToolbar(hwnd);
        CreateLayerPanel(hwnd);

        hwndStatus = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "", hwnd, 1);
        ApplyUiFont(hwndStatus);
        LayoutStatusParts(hwnd);

        hwndViewport = CreateWindowExA(
            0, // no CLIENTEDGE — custom bronze well frame instead
            VIEWPORT_CLASS_NAME,
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            TOOL_RAIL_WIDTH + WELL_FRAME, TOPBAR_HEIGHT + WELL_FRAME, 100, 100,
            hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL);
        hwndScrollH = AtelierScroll_Create(hwndViewport, false, 0, 0, 40, ATL_SCROLL_THICK);
        hwndScrollV = AtelierScroll_Create(hwndViewport, true, 0, 0, ATL_SCROLL_THICK, 40);
        hwndScrollCorner = AtelierScroll_Create(hwndViewport, false, 0, 0, ATL_SCROLL_THICK, ATL_SCROLL_THICK);
        ShowWindow(hwndScrollH, SW_HIDE);
        ShowWindow(hwndScrollV, SW_HIDE);
        ShowWindow(hwndScrollCorner, SW_HIDE);

        EnsureCanvas(hwnd);
        RefreshLayerList();
        LayoutViewport(hwnd);
        UpdateStatusBar(hwnd);
        UpdateWindowTitle(hwnd);
        EnableCustomTitleBar(hwnd);
        CreateCaptionButtons(hwnd);
        // Fresco cache once; low-rate idle motion (pauses while drawing/resizing).
        RequestChromeRebuild(hwnd, 1);
        SetTimer(hwnd, IDT_UI_IDLE, 100, NULL); // ~10fps overlay only
        break;
    }
    case WM_TIMER:
        if (wParam == IDT_UI_IDLE) {
            if (!ShouldRunIdleMotion(hwnd)) break;

            gIdlePhase += 0.14f;
            if (gIdlePhase > 6.2831853f) gIdlePhase -= 6.2831853f;
            const float breath = 0.5f + 0.5f * sinf(gIdlePhase);
            gUiCompassAngle += 2.2f; // ~22°/s at 10fps
            if (gUiCompassAngle >= 360.0f) gUiCompassAngle -= 360.0f;

            if (gToolFlash > 0.0f) {
                gToolFlash -= 0.12f;
                if (gToolFlash < 0.0f) gToolFlash = 0.0f;
            }
            gUiPulse = breath * 0.65f + gToolFlash * 0.35f;

            // Professional pattern: compose offscreen, invalidate only the brand child.
            EnsureBrandStrip(TOPBAR_HEIGHT);
            InvalidateBrandMark();
            InvalidateActiveToolButton();

            // Photoshop-style marching ants while a selection sits idle.
            if (gSel.hasMarquee && !gSel.creating && !gSel.moving) {
                gSelAntOffset += 1.25f;
                if (gSelAntOffset >= 9.0f) gSelAntOffset -= 9.0f;
                InvalidateCanvas();
            }
        }
        else if (wParam == IDT_CHROME_REBUILD) {
            KillTimer(hwnd, IDT_CHROME_REBUILD);
            RECT client = {};
            GetClientRect(hwnd, &client);
            const ChromeLayout chrome = GetChromeLayout(hwnd);
            EnsureChromeCache(client.right - client.left, client.bottom - client.top, chrome);
            EnsureBrandStrip(chrome.topH);
            InvalidateRect(hwnd, NULL, FALSE);
            InvalidateBrandMark();
        }
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, gTheme.chromeBg);
        SetTextColor(hdc, gTheme.ink);
        return (LRESULT)gChromeBrush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, gTheme.chromeElevated);
        SetTextColor(hdc, gTheme.ink);
        return (LRESULT)(gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) break;
        if (IsIconControlId(static_cast<int>(dis->CtlID))) {
            const int id = static_cast<int>(dis->CtlID);
            IconPaintOpts opts;
            opts.chromeBg = IsToolRailControlId(id) ? gTheme.chromeDeep : gTheme.chromeBg;
            opts.accent = gTheme.accent;
            opts.accentDeep = gTheme.accentDeep;
            opts.text = gTheme.ink;
            opts.selectedBg = gTheme.toolSelectedBg;
            opts.elevated = gTheme.chromeElevated;
            opts.pulse = 0.0f;
            opts.pressScale = 1.0f;
            int activeToolId = IDC_TOOL_PEN;
            switch (currentTool) {
            case DrawTool::Pen: activeToolId = IDC_TOOL_PEN; break;
            case DrawTool::Eraser: activeToolId = IDC_TOOL_ERASER; break;
            case DrawTool::Fill: activeToolId = IDC_TOOL_FILL; break;
            case DrawTool::Select: activeToolId = IDC_TOOL_SELECT; break;
            case DrawTool::Line: activeToolId = IDC_TOOL_LINE; break;
            case DrawTool::Shape: activeToolId = IDC_TOOL_SHAPES; break;
            }
            if (IsToolRailControlId(id) && id != IDC_COLOR_BUTTON && id != IDC_BG_BUTTON && id != IDC_SWAP_COLORS) {
                opts.useAppSelected = true;
                opts.appSelected = (id == activeToolId);
            }
            // Shape flyout selection / paint mode.
            if (id >= IDC_SHAPE_RECT && id <= IDC_SHAPE_ROUNDRECT) {
                opts.useAppSelected = true;
                opts.appSelected = (currentTool == DrawTool::Shape
                    && static_cast<int>(currentShape) == (id - IDC_SHAPE_RECT));
                opts.chromeBg = gTheme.chromeElevated;
            }
            if (id >= IDC_SHAPE_MODE_STROKE && id <= IDC_SHAPE_MODE_BOTH) {
                opts.useAppSelected = true;
                opts.appSelected = (static_cast<int>(shapePaintMode) == (id - IDC_SHAPE_MODE_STROKE));
                opts.chromeBg = gTheme.chromeElevated;
            }
            if (id == activeToolId) {
                opts.pressScale = 1.0f + gToolFlash * 0.10f;
                opts.pulse = gUiPulse;
            }
            if (id == IDC_COLOR_BUTTON) {
                opts.useColorFill = true;
                opts.colorFill = penColor;
            }
            if (id == IDC_BG_BUTTON) {
                opts.useColorFill = true;
                opts.colorFill = backColor;
            }
            if (id == IDC_TOGGLE_RAIL) {
                opts.useAppSelected = true;
                opts.appSelected = gRailOpen;
                opts.chromeBg = gTheme.chromeDeep;
            }
            if (id == IDC_TOGGLE_LAYERS) {
                opts.useAppSelected = true;
                opts.appSelected = gLayersOpen;
            }
            if (gChromeCache && dis->hwndItem) {
                POINT pt = { dis->rcItem.left, dis->rcItem.top };
                MapWindowPoints(dis->hwndItem, hwnd, &pt, 1);
                opts.frescoCache = gChromeCache.get();
                opts.frescoX = pt.x;
                opts.frescoY = pt.y;
            }
            PaintIconButton(dis, opts);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND:
        // Avoid double-paint flicker: chrome is drawn once in WM_PAINT from cache.
        return 1;

    case WM_NCCALCSIZE: {
        // Replace the OS caption with our in-client top bar; keep resize borders.
        // Skip on Wine — its window manager still draws a caption and our expanded
        // client would sit underneath it, clipping brand/menus.
        if (wParam == TRUE && !IsRunningUnderWine()) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const int frameX = FrameBorderX();
            const int frameY = FrameBorderY();
            params->rgrc[0].left += frameX;
            params->rgrc[0].right -= frameX;
            params->rgrc[0].bottom -= frameY;
            if (IsZoomed(hwnd)) {
                params->rgrc[0].top += frameY;
            }
            return 0;
        }
        break;
    }

    case WM_NCHITTEST: {
        if (IsRunningUnderWine()) break;

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT wr = {};
        GetWindowRect(hwnd, &wr);
        const int frameX = FrameBorderX();
        const int frameY = FrameBorderY();

        if (pt.y >= wr.top && pt.y < wr.top + frameY) {
            if (pt.x < wr.left + frameX) return HTTOPLEFT;
            if (pt.x >= wr.right - frameX) return HTTOPRIGHT;
            return HTTOP;
        }
        if (pt.y >= wr.bottom - frameY && pt.y < wr.bottom) {
            if (pt.x < wr.left + frameX) return HTBOTTOMLEFT;
            if (pt.x >= wr.right - frameX) return HTBOTTOMRIGHT;
            return HTBOTTOM;
        }
        if (pt.x >= wr.left && pt.x < wr.left + frameX) return HTLEFT;
        if (pt.x >= wr.right - frameX && pt.x < wr.right) return HTRIGHT;

        POINT clientPt = pt;
        ScreenToClient(hwnd, &clientPt);
        RECT client = {};
        GetClientRect(hwnd, &client);

        if (clientPt.y >= 0 && clientPt.y < TOPBAR_HEIGHT) {
            if (PointOverTopBarControl(hwnd, clientPt)) return HTCLIENT;
            return HTCAPTION;
        }

        if (PtInRect(&client, clientPt)) return HTCLIENT;
        return HTNOWHERE;
    }

    case WM_NCACTIVATE:
        if (IsRunningUnderWine()) break;
        // Keep custom chrome painted; skip default caption redraw flash.
        InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;

    case WM_ENTERSIZEMOVE:
        gUiSizing = true;
        break;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            SendMessageA(hwndStatus, WM_SIZE, 0, 0);
            LayoutStatusParts(hwnd);
            LayoutViewport(hwnd);
            UpdateStatusBar(hwnd);
            // Drop stale fresco bitmap during live resize (avoids leak-like growth + hitch).
            DestroyChromeCache();
            RequestChromeRebuild(hwnd, 60);
            InvalidateRect(hwnd, NULL, FALSE);
            if (hwndCaptionMax) InvalidateRect(hwndCaptionMax, nullptr, FALSE);
        }
        break;
    case WM_EXITSIZEMOVE: {
        gUiSizing = false;
        KillTimer(hwnd, IDT_CHROME_REBUILD);
        RECT client = {};
        GetClientRect(hwnd, &client);
        EnsureChromeCache(client.right - client.left, client.bottom - client.top, GetChromeLayout(hwnd));
        EnsureBrandStrip(TOPBAR_HEIGHT);
        InvalidateRect(hwnd, NULL, FALSE);
        InvalidateBrandMark();
        break;
    }
    case WM_HSCROLL: {
        if ((HWND)lParam == hwndSlider) {
            penWidth = static_cast<int>(SendMessage(hwndSlider, TBM_GETPOS, 0, 0));
            UpdatePenWidthDisplay();
            UpdateStatusBar(hwnd);
        }
        else if ((HWND)lParam == hwndOpacitySlider) {
            penOpacity = static_cast<int>(SendMessage(hwndOpacitySlider, TBM_GETPOS, 0, 0));
            UpdateOpacityDisplay();
            UpdateStatusBar(hwnd);
        }
        else if ((HWND)lParam == hwndLayerOpacity) {
            if (suppressLayerNotify) break;
            const int opacity = static_cast<int>(SendMessage(hwndLayerOpacity, TBM_GETPOS, 0, 0));
            gLayers.SetActiveOpacity(opacity);
            InvalidateComposite();
            RefreshLayerList();
            MarkDirty(hwnd);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
        }
        break;
    }
    case WM_COMMAND: {
        const int cmdId = LOWORD(wParam);
        const int notifyCode = HIWORD(wParam);

        if (cmdId == IDC_WIDTH_EDIT && notifyCode == EN_CHANGE) {
            if (suppressEditNotify) break;
            char buf[16];
            GetWindowTextA(hwndPenWidthBox, buf, sizeof(buf));
            int val = atoi(buf);
            if (val >= 1 && val <= 50) {
                penWidth = val;
                SendMessage(hwndSlider, TBM_SETPOS, TRUE, val);
                UpdateStatusBar(hwnd);
            }
            break;
        }

        if (cmdId == IDC_OPACITY_EDIT && notifyCode == EN_CHANGE) {
            if (suppressEditNotify) break;
            char buf[16];
            GetWindowTextA(hwndOpacityBox, buf, sizeof(buf));
            int val = atoi(buf);
            if (val >= 1 && val <= 100) {
                penOpacity = val;
                SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, val);
                UpdateStatusBar(hwnd);
            }
            break;
        }

        if (cmdId == ID_PALETTE) {
            COLORREF fg = penColor;
            COLORREF bg = backColor;
            AtelierPalette_GetColors(hwndPalette, &fg, &bg);
            if (notifyCode == 1) {
                penColor = fg;
                if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
            } else if (notifyCode == 2) {
                backColor = bg;
                if (hwndBgButton) InvalidateRect(hwndBgButton, NULL, FALSE);
            } else             if (notifyCode == 3) {
                COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
                penColor = newColor;
                InvalidateColorChips();
            } else if (notifyCode == 4) {
                COLORREF newColor = ColorPicker::PickColor(hwnd, backColor);
                backColor = newColor;
                InvalidateColorChips();
            }
            UpdateStatusBar(hwnd);
            break;
        }

        if (cmdId >= IDC_SHAPE_RECT && cmdId <= IDC_SHAPE_ROUNDRECT) {
            currentShape = static_cast<ShapeKind>(cmdId - IDC_SHAPE_RECT);
            SetActiveTool(DrawTool::Shape);
            CloseShapeFlyout();
            UpdateStatusBar(hwnd);
            break;
        }
        if (cmdId >= IDC_SHAPE_MODE_STROKE && cmdId <= IDC_SHAPE_MODE_BOTH) {
            shapePaintMode = static_cast<ShapePaintMode>(cmdId - IDC_SHAPE_MODE_STROKE);
            SyncShapeFlyoutChecks();
            if (currentTool != DrawTool::Shape) {
                SetActiveTool(DrawTool::Shape);
            }
            UpdateStatusBar(hwnd);
            break;
        }

        switch (cmdId) {
        case IDC_TOOL_PEN:
        case IDM_TOOL_PEN:
            ClearSelection(true);
            SetActiveTool(DrawTool::Pen);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_ERASER:
        case IDM_TOOL_ERASER:
            ClearSelection(true);
            SetActiveTool(DrawTool::Eraser);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_FILL:
        case IDM_TOOL_FILL:
            ClearSelection(true);
            SetActiveTool(DrawTool::Fill);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_LINE:
        case IDM_TOOL_LINE:
            ClearSelection(true);
            SetActiveTool(DrawTool::Line);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_SHAPES:
        case IDM_TOOL_SHAPES:
            ClearSelection(true);
            if (cmdId == IDC_TOOL_SHAPES
                && currentTool == DrawTool::Shape && hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
                CloseShapeFlyout();
            } else {
                SetActiveTool(DrawTool::Shape);
                OpenShapeFlyout(hwnd);
            }
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_SELECT:
        case IDM_TOOL_SELECT:
            SetActiveTool(DrawTool::Select);
            UpdateStatusBar(hwnd);
            break;
        case IDC_SWAP_COLORS:
        case IDM_SWAP_COLORS: {
            const COLORREF tmp = penColor;
            penColor = backColor;
            backColor = tmp;
            InvalidateColorChips();
            break;
        }
        case IDC_TOGGLE_RAIL:
            SetRailOpen(hwnd, !gRailOpen);
            break;
        case IDC_TOGGLE_LAYERS:
            SetLayersOpen(hwnd, !gLayersOpen);
            break;
        case IDC_TOGGLE_BOTTOM:
            SetBottomOpen(hwnd, !gBottomOpen);
            break;
        case IDC_MENU_FILE:
            PopupAppMenu(hwnd, 0, hwndMenuButtons[0]);
            break;
        case IDC_MENU_EDIT:
            PopupAppMenu(hwnd, 1, hwndMenuButtons[1]);
            break;
        case IDC_MENU_IMAGE:
            PopupAppMenu(hwnd, 2, hwndMenuButtons[2]);
            break;
        case IDC_MENU_VIEW:
            PopupAppMenu(hwnd, 3, hwndMenuButtons[3]);
            break;
        case IDC_MENU_TOOLS:
            PopupAppMenu(hwnd, 4, hwndMenuButtons[4]);
            break;
        case IDC_MENU_HELP:
            PopupAppMenu(hwnd, 5, hwndMenuButtons[5]);
            break;
        case IDC_BG_BUTTON: {
            COLORREF newColor = ColorPicker::PickColor(hwnd, backColor);
            backColor = newColor;
            InvalidateColorChips();
            break;
        }
        case IDC_LAYER_ADD:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.AddLayer()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
                UpdateStatusBar(hwnd);
            }
            break;
        case IDC_LAYER_DEL:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.DeleteActiveLayer()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
                UpdateStatusBar(hwnd);
            }
            break;
        case IDC_LAYER_UP:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.MoveActiveUp()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
            }
            break;
        case IDC_LAYER_DOWN:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.MoveActiveDown()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
            }
            break;
        case IDC_LAYER_VISIBLE:
            if (suppressLayerNotify) break;
            {
                const bool checked = SendMessageA(hwndLayerVisible, BM_GETCHECK, 0, 0) == BST_CHECKED;
                gHistory.Push(gLayers);
                gLayers.SetActiveVisible(checked);
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
            }
            break;
        case IDC_LAYER_LIST:
            if (suppressLayerNotify) break;
            if (notifyCode == LBN_SELCHANGE) {
                const int row = static_cast<int>(SendMessageA(hwndLayerList, LB_GETCURSEL, 0, 0));
                if (row >= 0) {
                    const int layerIndex = static_cast<int>(SendMessageA(hwndLayerList, LB_GETITEMDATA, row, 0));
                    ClearSelection(true);
                    gLayers.SetActiveIndex(layerIndex);
                    RefreshLayerList();
                    UpdateStatusBar(hwnd);
                }
            }
            break;
        case IDM_CUT:
            CutSelection(hwnd);
            break;
        case IDM_COPY:
            CopySelection(hwnd);
            break;
        case IDM_PASTE:
            PasteSelection(hwnd);
            break;
        case IDM_DELETE_SEL:
            DeleteSelection(hwnd);
            break;
        case IDM_SELECT_ALL:
            SelectAll(hwnd);
            break;
        case IDM_ZOOM_IN:
            ZoomByFactor(hwnd, ZOOM_STEP);
            break;
        case IDM_ZOOM_OUT:
            ZoomByFactor(hwnd, 1.0f / ZOOM_STEP);
            break;
        case IDM_ZOOM_100:
            ZoomToActual(hwnd);
            break;
        case IDM_ZOOM_FIT:
            ZoomToFit(hwnd);
            break;
        case IDC_COLOR_BUTTON: {
            COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
            penColor = newColor;
            InvalidateColorChips();
            break;
        }
        case IDC_NEW_BUTTON:
        case IDM_NEW:
            NewDocument(hwnd);
            break;
        case IDC_CAPTION_MIN:
            SendMessageA(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case IDC_CAPTION_MAX:
            SendMessageA(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
            break;
        case IDC_CAPTION_CLOSE:
            SendMessageA(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
        case IDC_CLEAR_BUTTON:
        case IDM_CLEAR:
            ClearCanvas(hwnd, true);
            UpdateStatusBar(hwnd);
            break;
        case IDC_UNDO_BUTTON:
        case IDM_UNDO:
            UndoDocument(hwnd);
            break;
        case IDC_REDO_BUTTON:
        case IDM_REDO:
            RedoDocument(hwnd);
            break;
        case IDC_SAVE_BUTTON:
        case IDM_SAVE:
            SaveDocument(hwnd);
            break;
        case IDC_LOAD_BUTTON:
        case IDM_OPEN:
            OpenDocument(hwnd);
            break;
        case IDM_CANVAS_SIZE:
            ResizeCanvas(hwnd);
            break;
        case IDM_ABOUT:
            DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABOUTBOX), hwnd, AboutDlgProc);
            break;
        case IDM_SHORTCUTS:
            DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_SHORTCUTS), hwnd, ShortcutsDlgProc);
            break;
        case IDM_EXIT:
            SendMessageA(hwnd, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int steps = wheelDelta / WHEEL_DELTA;
        if (steps != 0) {
            const bool ctrlDown = ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0)
                || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0)
                || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrlDown) {
                // Zoom toward cursor when possible.
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (hwndViewport) {
                    ScreenToClient(hwndViewport, &pt);
                    float factor = 1.0f;
                    for (int i = 0; i < (steps > 0 ? steps : -steps); ++i) {
                        factor *= (steps > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP);
                    }
                    SetZoomAtViewportPoint(hwnd, zoomFactor * factor, pt.x, pt.y);
                }
                else {
                    ZoomByFactor(hwnd, (steps > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP));
                }
            }
            else if (shiftDown) {
                AdjustOpacity(hwnd, steps * 5);
            }
            else {
                AdjustPenWidth(hwnd, steps);
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (wParam == VK_ESCAPE) {
            if (hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
                CloseShapeFlyout();
                break;
            }
            ClearSelection(true);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
            break;
        }

        // Live shape fill/stroke modifiers while dragging.
        if (wParam == VK_MENU || wParam == VK_CONTROL || wParam == VK_SHIFT) {
            RefreshShapePreviewIfDrawing();
        }

        if (!IsTypingInEdit()
            && (GetKeyState(VK_CONTROL) & 0x8000) == 0
            && (GetKeyState(VK_MENU) & 0x8000) == 0) {
            const WPARAM key = (wParam >= 'a' && wParam <= 'z') ? (wParam - 'a' + 'A') : wParam;
            switch (key) {
            case 'B': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_PEN, 0), 0); break;
            case 'E': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_ERASER, 0), 0); break;
            case 'G': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_FILL, 0), 0); break;
            case 'M': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_SELECT, 0), 0); break;
            case 'L': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_LINE, 0), 0); break;
            case 'U': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_SHAPES, 0), 0); break;
            case 'X': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SWAP_COLORS, 0), 0); break;
            case VK_TAB:
                SetRailOpen(hwnd, !gRailOpen);
                break;
            case VK_F9:
                SetLayersOpen(hwnd, !gLayersOpen);
                break;
            case VK_F8:
                SetBottomOpen(hwnd, !gBottomOpen);
                break;
            default: break;
            }
        }

        if (wParam == VK_OEM_4) { // [
            AdjustPenWidth(hwnd, -1);
        }
        else if (wParam == VK_OEM_6) { // ]
            AdjustPenWidth(hwnd, 1);
        }
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam == VK_MENU || wParam == VK_CONTROL || wParam == VK_SHIFT) {
            RefreshShapePreviewIfDrawing();
        }
        break;
    case WM_CLOSE:
        if (!PromptSaveIfDirty(hwnd)) {
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client = {};
        GetClientRect(hwnd, &client);
        DrawToolbarBackground(hdc, hwnd, client);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd, IDT_UI_ANIM);
        KillTimer(hwnd, IDT_UI_IDLE);
        KillTimer(hwnd, IDT_CHROME_REBUILD);
        if (GetCapture() == hwnd || (hwndViewport && GetCapture() == hwndViewport)) {
            ReleaseCapture();
        }
        DestroyStrokeLayer();
        Selection_Shutdown();
        ShutdownAppEventHandlers();
        gHistory.Clear();
        DestroyCompositeCache();
        DestroyChromeCache();
        gLayers.Destroy();
        if (hwndShapeFlyout && IsWindow(hwndShapeFlyout)) {
            DestroyWindow(hwndShapeFlyout);
        }
        hwndShapeFlyout = nullptr;
        if (hwndPalette && IsWindow(hwndPalette)) {
            AtelierPalette_Save(hwndPalette);
        }
        hwndPalette = nullptr;
        if (hwndPaletteFloat && IsWindow(hwndPaletteFloat)) {
            DestroyWindow(hwndPaletteFloat);
        }
        hwndPaletteFloat = nullptr;
        gAppMenu = nullptr;
        hwndBrand = nullptr;
        hwndCaptionMin = nullptr;
        hwndCaptionMax = nullptr;
        hwndCaptionClose = nullptr;
        gCaptionHot = nullptr;
        hwndViewport = nullptr;
        if (gUiFont) {
            DeleteObject(gUiFont);
            gUiFont = nullptr;
        }
        if (gBrandFont) {
            DeleteObject(gBrandFont);
            gBrandFont = nullptr;
        }
        if (gChromeBrush) {
            DeleteObject(gChromeBrush);
            gChromeBrush = nullptr;
        }
        if (gChromeDeepBrush) {
            DeleteObject(gChromeDeepBrush);
            gChromeDeepBrush = nullptr;
        }
        if (gChromeElevatedBrush) {
            DeleteObject(gChromeElevatedBrush);
            gChromeElevatedBrush = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static bool RegisterViewportClass(HINSTANCE hInstance) {
    WNDCLASSA vc = {};
    vc.lpfnWndProc = ViewportProc;
    vc.hInstance = hInstance;
    vc.lpszClassName = VIEWPORT_CLASS_NAME;
    vc.hCursor = LoadCursor(NULL, IDC_ARROW);
    vc.hbrBackground = NULL;
    vc.style = CS_DBLCLKS;
    return RegisterClassA(&vc) != 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    AtelierFonts_Init();
    AtelierArtwork_Init();
    AtelierControls_SetTheme(&gTheme);
    if (!AtelierControls_Register()) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    AtelierPalette_SetTheme(&gTheme);
    if (!AtelierPalette_Register()) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    if (!RegisterViewportClass(hInstance)) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    if (!RegisterBrandClass(hInstance)) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    if (!RegisterCaptionBtnClass(hInstance)) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    if (!RegisterShapeFlyoutClass(hInstance)) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    if (!RegisterPaletteFloatClass(hInstance)) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // painted manually
    wc.lpszMenuName = MAKEINTRESOURCEA(IDC_SIMPLEDRAWINGAPP);
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_SIMPLEDRAWINGAPP));
    wc.style = CS_DBLCLKS;

    if (!RegisterClassA(&wc)) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Simple Drawing App",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        AtelierArtwork_Shutdown();
        AtelierFonts_Shutdown();
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    // Move classic menubar into the top chrome (Firefox-like); keep HMENU for popups.
    gAppMenu = GetMenu(hwnd);
    SetMenu(hwnd, NULL);
    DrawMenuBar(hwnd);

    gAccel = LoadAcceleratorsA(hInstance, MAKEINTRESOURCEA(IDC_SIMPLEDRAWINGAPP));

    InitAppEventHandlers();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Tab toggles the tools rail (don't let it cycle child focus).
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && !IsTypingInEdit()
            && (GetKeyState(VK_CONTROL) & 0x8000) == 0
            && (GetKeyState(VK_MENU) & 0x8000) == 0) {
            SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDC_TOGGLE_RAIL, 0), 0);
            continue;
        }
        const bool keyMsg = (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN);
        const bool typing = keyMsg && IsTypingInEdit();
        const bool ctrlOrAlt = (GetKeyState(VK_CONTROL) & 0x8000) != 0
            || (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool tryAccel = !typing || ctrlOrAlt;
        if (tryAccel && TranslateAcceleratorA(hwnd, gAccel, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    AtelierArtwork_Shutdown();
    AtelierFonts_Shutdown();
    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
