#include <windows.h>
#include <windowsx.h>
#include "framework.h"
#include "SimpleDrawingApp.h"
#include "FileManager.h"
#include "ColorPicker.h"
#include "LayerHistory.h"
#include "LayerStack.h"
#include "DrawingTools.h"
#include "UiChrome.h"
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

namespace {
const char CLASS_NAME[] = "SimpleDrawingAppWindowClass";
const char VIEWPORT_CLASS_NAME[] = "SimpleDrawingAppViewport";
constexpr int TOPBAR_HEIGHT = 44;
constexpr int TOOL_RAIL_WIDTH = 52;
constexpr int BOTTOMBAR_HEIGHT = 40;
constexpr int STATUS_HEIGHT = 24;
constexpr int LAYER_PANEL_WIDTH = 168;
constexpr int ICON_BTN = 32;
constexpr UINT_PTR IDT_UI_ANIM = 42;      // legacy tool-flash (unused; idle handles it)
constexpr UINT_PTR IDT_CHROME_REBUILD = 43;
constexpr UINT_PTR IDT_UI_IDLE = 44;      // low-rate compass + tool pulse
constexpr int DEFAULT_DOC_WIDTH = 1280;
constexpr int DEFAULT_DOC_HEIGHT = 720;
constexpr int MIN_DOC_SIZE = 1;
constexpr int MAX_DOC_SIZE = 10000;
// Fallback; runtime uses gTheme.workspace.
constexpr COLORREF WORKSPACE_COLOR = RGB(168, 158, 146);
constexpr float ZOOM_MIN = 0.25f;
constexpr float ZOOM_MAX = 8.0f;
constexpr float ZOOM_STEP = 1.25f;

const COLORREF kSwatches[8] = {
    RGB(0, 0, 0),
    RGB(255, 255, 255),
    RGB(232, 17, 35),
    RGB(247, 99, 12),
    RGB(255, 185, 0),
    RGB(16, 124, 16),
    RGB(0, 120, 212),
    RGB(136, 23, 152)
};

struct CanvasPreset {
    const char* label;
    int width;
    int height;
};

const CanvasPreset kPresets[] = {
    { "800 x 600", 800, 600 },
    { "1024 x 768", 1024, 768 },
    { "1280 x 720", 1280, 720 },
    { "1920 x 1080", 1920, 1080 },
    { "Custom", 0, 0 }
};

AppTheme gTheme;
LayerStack gLayers;
LayerHistory gHistory;
HFONT gUiFont = nullptr;
HFONT gBrandFont = nullptr;
HBRUSH gChromeBrush = nullptr;
HBRUSH gChromeDeepBrush = nullptr;
HACCEL gAccel = nullptr;
HWND hwndTooltip = nullptr;
float gUiPulse = 0.0f;            // 0..1 selected-tool breath (idle)
float gUiCompassAngle = -18.0f;   // animated overlay angle
float gToolFlash = 0.0f;          // 1 → 0 after tool switch
float gIdlePhase = 0.0f;
bool gUiSizing = false;
Bitmap* gChromeCache = nullptr;
int gChromeCacheW = 0;
int gChromeCacheH = 0;
int gChromeCacheStatusH = -1;

HWND hwndViewport = nullptr;
HWND hwndSlider = nullptr;
HWND hwndPenWidthBox = nullptr;
HWND hwndOpacitySlider = nullptr;
HWND hwndOpacityBox = nullptr;
HWND hwndSizeLabel = nullptr;
HWND hwndOpacityLabel = nullptr;
HWND hwndStatus = nullptr;
HWND hwndLayerList = nullptr;
HWND hwndLayerAdd = nullptr;
HWND hwndLayerDel = nullptr;
HWND hwndLayerUp = nullptr;
HWND hwndLayerDown = nullptr;
HWND hwndLayerVisible = nullptr;
HWND hwndLayerOpacity = nullptr;
HWND hwndToolButtons[7] = {};
HWND hwndSwatches[8] = {};
HWND hwndActionButtons[7] = {}; // 0 Color, 1 New, 2 Undo, 3 Redo, 4 Clear, 5 Save, 6 Open

POINT lastPoint = {};
POINT shapeStart = {};
bool isDrawing = false;
bool suppressEditNotify = false;
bool suppressLayerNotify = false;

int docWidth = DEFAULT_DOC_WIDTH;
int docHeight = DEFAULT_DOC_HEIGHT;
int scrollX = 0;
int scrollY = 0;
float zoomFactor = 1.0f;

struct SelectionState {
    bool hasMarquee = false;
    bool isFloating = false;
    bool creating = false;
    bool moving = false;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int anchorX = 0;
    int anchorY = 0;
    int grabDX = 0;
    int grabDY = 0;
    Bitmap* floatBmp = nullptr;
};

SelectionState gSel;
Bitmap* gClipboardBmp = nullptr;
Bitmap* compositeCache = nullptr;
bool compositeDirty = true;
Bitmap* strokeLayer = nullptr;
Graphics* strokeGraphics = nullptr;

WNDPROC gOldTrackbarProc = nullptr;
ULONG_PTR gdiplusToken = 0;
}

COLORREF penColor = RGB(0, 0, 0);
int penWidth = 5;
int penOpacity = 100; // percent 1-100
DrawTool currentTool = DrawTool::Pen;
bool documentDirty = false;

static BYTE OpacityToAlpha() {
    int pct = penOpacity;
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    return static_cast<BYTE>((pct * 255 + 50) / 100);
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
    else if (currentTool == DrawTool::Rectangle) toolName = "Rectangle";
    else if (currentTool == DrawTool::Ellipse) toolName = "Ellipse";
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

static void MarkDirty(HWND hwnd) {
    documentDirty = true;
    UpdateWindowTitle(hwnd);
    UpdateStatusBar(hwnd);
}

static void MarkClean(HWND hwnd) {
    documentDirty = false;
    UpdateWindowTitle(hwnd);
    UpdateStatusBar(hwnd);
}

static void ApplyUiFont(HWND control) {
    if (gUiFont && control) {
        SendMessageA(control, WM_SETFONT, (WPARAM)gUiFont, TRUE);
    }
}

static void SetActiveTool(DrawTool tool) {
    const bool changed = (currentTool != tool);
    currentTool = tool;
    if (changed) {
        gToolFlash = 1.0f;
    }
    for (int i = 0; i < 7; ++i) {
        if (!hwndToolButtons[i]) continue;
        const bool selected = (static_cast<int>(tool) == i);
        SendMessageA(hwndToolButtons[i], BM_SETCHECK, selected ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(hwndToolButtons[i], NULL, FALSE);
    }
}

static void InvalidateActiveToolButton() {
    const int idx = static_cast<int>(currentTool);
    if (idx >= 0 && idx < 7 && hwndToolButtons[idx]) {
        InvalidateRect(hwndToolButtons[idx], NULL, FALSE);
    }
}

static RECT BrandCompassInvalidateRect() {
    // Compass only (wordmark stays in fresco cache and is not invalidated).
    RECT r = { 8, 2, 44, TOPBAR_HEIGHT - 2 };
    return r;
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

static void DrawBrandCompassOverlay(HDC hdc, int topH) {
    const Color gold(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    const float breath = 1.0f + gUiPulse * 0.06f;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    DrawBrandCompass(g, 25.0f, static_cast<REAL>(topH) * 0.5f, 11.0f * breath, gold, gUiCompassAngle);
}

static void DestroyChromeCache() {
    delete gChromeCache;
    gChromeCache = nullptr;
    gChromeCacheW = 0;
    gChromeCacheH = 0;
    gChromeCacheStatusH = -1;
}

struct ChromeLayout {
    int topH = TOPBAR_HEIGHT;
    int railW = TOOL_RAIL_WIDTH;
    int bottomH = BOTTOMBAR_HEIGHT;
    int statusH = STATUS_HEIGHT;
    int layerW = LAYER_PANEL_WIDTH;
};

static ChromeLayout GetChromeLayout(HWND hwnd) {
    ChromeLayout layout;
    if (hwndStatus) {
        RECT sb = {};
        GetWindowRect(hwndStatus, &sb);
        layout.statusH = sb.bottom - sb.top;
        if (layout.statusH < 1) layout.statusH = STATUS_HEIGHT;
    }
    (void)hwnd;
    return layout;
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

static void DestroyStrokeLayer();
static void CommitStrokeLayer();
static void BeginStrokeLayer();
static void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1);
static void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY);
static void UpdateScrollBars();
static void SyncDocSizeFromBitmap();
static void InvalidateCanvas();
static void InvalidateComposite();
static void DestroyCompositeCache();
static Bitmap* GetCompositeBitmap();
static void LayoutViewport(HWND hwnd);
static void LayoutLayerPanel(HWND hwnd);
static void LayoutChromeControls(HWND hwnd);
static void RefreshLayerList();
static void ClearSelection(bool stampFloating);
static bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
static INT_PTR CALLBACK CanvasSizeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static int gCanvasDlgWidth = 0;
static int gCanvasDlgHeight = 0;

static int MaxInt(int a, int b) { return (a > b) ? a : b; }
static float MaxFloat(float a, float b) { return (a > b) ? a : b; }

static void SyncDocSizeFromBitmap() {
    docWidth = MaxInt(1, gLayers.Width());
    docHeight = MaxInt(1, gLayers.Height());
}

static void EnsureCanvas(HWND hwnd) {
    if (gLayers.Count() > 0) return;
    gLayers.Reset(docWidth, docHeight, gTheme.canvasBg);
    InvalidateComposite();
    (void)hwnd;
}

static void DestroyCompositeCache() {
    delete compositeCache;
    compositeCache = nullptr;
    compositeDirty = true;
}

static void InvalidateComposite() {
    compositeDirty = true;
}

static Bitmap* GetCompositeBitmap() {
    if (!compositeDirty && compositeCache) {
        return compositeCache;
    }
    delete compositeCache;
    compositeCache = gLayers.CreateComposite();
    compositeDirty = false;
    return compositeCache;
}

static void UpdateScrollBars() {
    if (!hwndViewport) return;

    RECT rc = {};
    GetClientRect(hwndViewport, &rc);
    const int viewW = rc.right - rc.left;
    const int viewH = rc.bottom - rc.top;
    const int contentW = MaxInt(1, static_cast<int>(std::lround(docWidth * zoomFactor)));
    const int contentH = MaxInt(1, static_cast<int>(std::lround(docHeight * zoomFactor)));

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;

    si.nMin = 0;
    si.nMax = (contentW > 1) ? (contentW - 1) : 0;
    si.nPage = (viewW > 0) ? static_cast<UINT>(viewW) : 1;
    if (scrollX > contentW - viewW) scrollX = (contentW > viewW) ? (contentW - viewW) : 0;
    if (scrollX < 0) scrollX = 0;
    si.nPos = scrollX;
    SetScrollInfo(hwndViewport, SB_HORZ, &si, TRUE);
    scrollX = GetScrollPos(hwndViewport, SB_HORZ);

    si.nMin = 0;
    si.nMax = (contentH > 1) ? (contentH - 1) : 0;
    si.nPage = (viewH > 0) ? static_cast<UINT>(viewH) : 1;
    if (scrollY > contentH - viewH) scrollY = (contentH > viewH) ? (contentH - viewH) : 0;
    if (scrollY < 0) scrollY = 0;
    si.nPos = scrollY;
    SetScrollInfo(hwndViewport, SB_VERT, &si, TRUE);
    scrollY = GetScrollPos(hwndViewport, SB_VERT);
}

static void LayoutViewport(HWND hwnd) {
    if (!hwndViewport) return;

    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);

    const int x = chrome.railW;
    const int y = chrome.topH;
    const int w = MaxInt(1, client.right - client.left - chrome.railW - chrome.layerW);
    int h = client.bottom - client.top - chrome.topH - chrome.bottomH - chrome.statusH;
    if (h < 1) h = 1;

    MoveWindow(hwndViewport, x, y, w, h, TRUE);
    LayoutLayerPanel(hwnd);
    LayoutChromeControls(hwnd);
    UpdateScrollBars();
}

static void LayoutLayerPanel(HWND hwnd) {
    if (!hwndLayerList) return;

    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int panelX = client.right - chrome.layerW;
    const int panelY = chrome.topH;
    const int panelH = MaxInt(1, client.bottom - client.top - chrome.topH - chrome.bottomH - chrome.statusH);

    const int btn = 28;
    const int pad = 8;
    int y = panelY + pad;

    if (hwndLayerAdd) MoveWindow(hwndLayerAdd, panelX + pad, y, btn, btn, TRUE);
    if (hwndLayerDel) MoveWindow(hwndLayerDel, panelX + pad + btn + 4, y, btn, btn, TRUE);
    if (hwndLayerUp) MoveWindow(hwndLayerUp, panelX + pad + (btn + 4) * 2, y, btn, btn, TRUE);
    if (hwndLayerDown) MoveWindow(hwndLayerDown, panelX + pad + (btn + 4) * 3, y, btn, btn, TRUE);
    y += btn + 8;

    if (hwndLayerVisible) {
        MoveWindow(hwndLayerVisible, panelX + pad, y, chrome.layerW - pad * 2, 22, TRUE);
    }
    y += 28;

    const int listH = MaxInt(60, panelH - (y - panelY) - 48);
    MoveWindow(hwndLayerList, panelX + pad, y, chrome.layerW - pad * 2, listH, TRUE);
    y += listH + 6;

    if (hwndLayerOpacity) {
        MoveWindow(hwndLayerOpacity, panelX + pad, y, chrome.layerW - pad * 2, 28, TRUE);
    }
}

static void LayoutChromeControls(HWND hwnd) {
    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int right = client.right - client.left;

    // Top bar: undo/redo left-of-center actions; doc actions on the right.
    const int topY = (chrome.topH - ICON_BTN) / 2;
    int x = 150; // leave room for brand wordmark
    if (hwndActionButtons[2]) MoveWindow(hwndActionButtons[2], x, topY, ICON_BTN, ICON_BTN, TRUE); // Undo
    x += ICON_BTN + 4;
    if (hwndActionButtons[3]) MoveWindow(hwndActionButtons[3], x, topY, ICON_BTN, ICON_BTN, TRUE); // Redo
    x += ICON_BTN + 10;
    if (hwndActionButtons[4]) MoveWindow(hwndActionButtons[4], x, topY, ICON_BTN, ICON_BTN, TRUE); // Clear

    x = right - chrome.layerW - (ICON_BTN + 4) * 3 - 12;
    if (hwndActionButtons[1]) MoveWindow(hwndActionButtons[1], x, topY, ICON_BTN, ICON_BTN, TRUE); // New
    x += ICON_BTN + 4;
    if (hwndActionButtons[6]) MoveWindow(hwndActionButtons[6], x, topY, ICON_BTN, ICON_BTN, TRUE); // Open
    x += ICON_BTN + 4;
    if (hwndActionButtons[5]) MoveWindow(hwndActionButtons[5], x, topY, ICON_BTN, ICON_BTN, TRUE); // Save

    // Left tool rail (top → bottom).
    const int railX = (chrome.railW - ICON_BTN) / 2;
    int y = chrome.topH + 10;
    const int order[7] = { 0, 1, 2, 6, 3, 4, 5 }; // Pen Eraser Fill Select Line Rect Ellipse
    for (int i = 0; i < 7; ++i) {
        const int idx = order[i];
        if (hwndToolButtons[idx]) {
            MoveWindow(hwndToolButtons[idx], railX, y, ICON_BTN, ICON_BTN, TRUE);
        }
        y += ICON_BTN + 6;
        if (i == 2 || i == 3) y += 6; // group spacing
    }

    y += 4;
    if (hwndActionButtons[0]) {
        MoveWindow(hwndActionButtons[0], railX, y, ICON_BTN, ICON_BTN, TRUE); // Color
    }
    y += ICON_BTN + 8;

    for (int i = 0; i < 8; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        if (hwndSwatches[i]) {
            MoveWindow(hwndSwatches[i],
                8 + col * 20,
                y + row * 20,
                18, 18, TRUE);
        }
    }

    // Bottom bar under canvas: size + opacity.
    const int bottomY = client.bottom - chrome.statusH - chrome.bottomH;
    const int bottomX = chrome.railW + 12;
    if (hwndSizeLabel) MoveWindow(hwndSizeLabel, bottomX, bottomY + 10, 34, 18, TRUE);
    if (hwndSlider) MoveWindow(hwndSlider, bottomX + 36, bottomY + 4, 150, 30, TRUE);
    if (hwndPenWidthBox) MoveWindow(hwndPenWidthBox, bottomX + 194, bottomY + 8, 40, 22, TRUE);
    if (hwndOpacityLabel) MoveWindow(hwndOpacityLabel, bottomX + 250, bottomY + 10, 54, 18, TRUE);
    if (hwndOpacitySlider) MoveWindow(hwndOpacitySlider, bottomX + 306, bottomY + 4, 150, 30, TRUE);
    if (hwndOpacityBox) MoveWindow(hwndOpacityBox, bottomX + 464, bottomY + 8, 40, 22, TRUE);

    (void)hwnd;
}

static void RefreshLayerList() {
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
}

static bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink) {
    if (newWidth < MIN_DOC_SIZE) newWidth = MIN_DOC_SIZE;
    if (newHeight < MIN_DOC_SIZE) newHeight = MIN_DOC_SIZE;
    if (newWidth > MAX_DOC_SIZE) newWidth = MAX_DOC_SIZE;
    if (newHeight > MAX_DOC_SIZE) newHeight = MAX_DOC_SIZE;

    EnsureCanvas(hwnd);

    if (newWidth == docWidth && newHeight == docHeight) {
        return true;
    }

    if (warnOnShrink && (newWidth < docWidth || newHeight < docHeight)) {
        const int result = MessageBoxA(
            hwnd,
            "Shrinking the canvas will crop content outside the new size. Continue?",
            "Canvas Size",
            MB_OKCANCEL | MB_ICONWARNING);
        if (result != IDOK) {
            return false;
        }
    }

    if (isDrawing) {
        isDrawing = false;
        CommitStrokeLayer();
    }
    DestroyStrokeLayer();
    ClearSelection(false);

    if (pushHistory) {
        gHistory.Push(gLayers);
    }

    gLayers.Resize(newWidth, newHeight, gTheme.canvasBg);
    docWidth = newWidth;
    docHeight = newHeight;
    scrollX = 0;
    scrollY = 0;
    InvalidateComposite();
    UpdateScrollBars();
    MarkDirty(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    RefreshLayerList();
    return true;
}

static void ClearCanvas(HWND hwnd, bool pushHistory) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    if (pushHistory) {
        gHistory.Push(gLayers);
    }
    gLayers.ClearAllContent(gTheme.canvasBg);
    InvalidateComposite();
    MarkDirty(hwnd);
    InvalidateCanvas();
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

static void InvalidateCanvas() {
    if (hwndViewport) {
        InvalidateRect(hwndViewport, NULL, FALSE);
    }
}

static int ScaledContentWidth() {
    return MaxInt(1, static_cast<int>(std::lround(docWidth * zoomFactor)));
}

static int ScaledContentHeight() {
    return MaxInt(1, static_cast<int>(std::lround(docHeight * zoomFactor)));
}

static void DestroySelFloat() {
    delete gSel.floatBmp;
    gSel.floatBmp = nullptr;
    gSel.isFloating = false;
}

static void ClearSelection(bool stampFloating) {
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
}

static void NormalizeSelRect(int x0, int y0, int x1, int y1, int& x, int& y, int& w, int& h) {
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

static bool SelectionHitTest(int docX, int docY) {
    if (!gSel.hasMarquee || gSel.w < 1 || gSel.h < 1) return false;
    return docX >= gSel.x && docY >= gSel.y
        && docX < gSel.x + gSel.w && docY < gSel.y + gSel.h;
}

static Bitmap* CloneBitmapRect(Bitmap* src, int x, int y, int w, int h) {
    if (!src || w < 1 || h < 1) return nullptr;
    Bitmap* out = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(out);
    g.Clear(Color(0, 0, 0, 0));
    g.DrawImage(src, Rect(0, 0, w, h), x, y, w, h, UnitPixel);
    return out;
}

static void LiftSelection() {
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

static void StampFloatingSelection() {
    Graphics* ag = gLayers.ActiveGraphics();
    if (!gSel.isFloating || !gSel.floatBmp || !ag) return;
    ag->DrawImage(gSel.floatBmp, gSel.x, gSel.y);
    DestroySelFloat();
    InvalidateComposite();
}

static Bitmap* CaptureSelectionPixels() {
    if (!gSel.hasMarquee || gSel.w < 1 || gSel.h < 1) return nullptr;
    if (gSel.isFloating && gSel.floatBmp) {
        return CloneBitmapRect(gSel.floatBmp, 0, 0, gSel.w, gSel.h);
    }
    return CloneBitmapRect(gLayers.ActiveBitmap(), gSel.x, gSel.y, gSel.w, gSel.h);
}

static void SetInternalClipboard(Bitmap* bmp) {
    delete gClipboardBmp;
    gClipboardBmp = bmp;
}

static bool CopyBitmapToWinClipboard(Bitmap* bmp) {
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

static Bitmap* BitmapFromWinClipboard() {
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

static void DrawSelectionOverlay(Graphics* g) {
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

    Pen dash(Color(255, 0, 0, 0), 1.0f);
    dash.SetDashStyle(DashStyleDash);
    const float selW = MaxFloat(1.0f, gSel.w * zoomFactor);
    const float selH = MaxFloat(1.0f, gSel.h * zoomFactor);
    g->DrawRectangle(
        &dash,
        RectF(
            gSel.x * zoomFactor,
            gSel.y * zoomFactor,
            selW,
            selH));

    Pen dash2(Color(255, 255, 255, 255), 1.0f);
    dash2.SetDashStyle(DashStyleDash);
    dash2.SetDashOffset(4.0f);
    g->DrawRectangle(
        &dash2,
        RectF(
            gSel.x * zoomFactor,
            gSel.y * zoomFactor,
            selW,
            selH));
}

static void DoCopy(HWND hwnd) {
    Bitmap* shot = CaptureSelectionPixels();
    if (!shot) return;
    SetInternalClipboard(CloneBitmapRect(shot, 0, 0, static_cast<int>(shot->GetWidth()), static_cast<int>(shot->GetHeight())));
    CopyBitmapToWinClipboard(shot);
    delete shot;
    (void)hwnd;
}

static void DoDeleteSelection(HWND hwnd) {
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
}

static void DoCut(HWND hwnd) {
    if (!gSel.hasMarquee) return;
    DoCopy(hwnd);
    DoDeleteSelection(hwnd);
}

static void DoPaste(HWND hwnd) {
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
}

static void DoSelectAll(HWND hwnd) {
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
}

static void EnableDarkTitleBar(HWND hwnd) {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
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

static void SyncPresetSelection(HWND hDlg) {
    HWND list = GetDlgItem(hDlg, IDC_CANVAS_PRESET);
    if (!list) return;

    char widthBuf[32] = {};
    char heightBuf[32] = {};
    GetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, widthBuf, sizeof(widthBuf));
    GetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, heightBuf, sizeof(heightBuf));
    const int w = atoi(widthBuf);
    const int h = atoi(heightBuf);

    int select = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])) - 1; // Custom
    for (int i = 0; i < select; ++i) {
        if (kPresets[i].width == w && kPresets[i].height == h) {
            select = i;
            break;
        }
    }
    SendMessageA(list, LB_SETCURSEL, select, 0);
}

static INT_PTR CALLBACK CanvasSizeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        char buf[32];
        sprintf_s(buf, "%d", docWidth);
        SetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, buf);
        sprintf_s(buf, "%d", docHeight);
        SetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, buf);

        HWND list = GetDlgItem(hDlg, IDC_CANVAS_PRESET);
        for (const CanvasPreset& preset : kPresets) {
            SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)preset.label);
        }
        SyncPresetSelection(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_CANVAS_PRESET && code == LBN_SELCHANGE) {
            HWND list = GetDlgItem(hDlg, IDC_CANVAS_PRESET);
            const int sel = static_cast<int>(SendMessageA(list, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])) &&
                kPresets[sel].width > 0) {
                char buf[32];
                sprintf_s(buf, "%d", kPresets[sel].width);
                SetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, buf);
                sprintf_s(buf, "%d", kPresets[sel].height);
                SetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, buf);
            }
            return TRUE;
        }

        if ((id == IDC_CANVAS_WIDTH || id == IDC_CANVAS_HEIGHT) && code == EN_CHANGE) {
            SyncPresetSelection(hDlg);
            return TRUE;
        }

        if (id == IDOK) {
            char widthBuf[32] = {};
            char heightBuf[32] = {};
            GetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, widthBuf, sizeof(widthBuf));
            GetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, heightBuf, sizeof(heightBuf));
            int w = atoi(widthBuf);
            int h = atoi(heightBuf);
            if (w < MIN_DOC_SIZE || h < MIN_DOC_SIZE || w > MAX_DOC_SIZE || h > MAX_DOC_SIZE) {
                MessageBoxA(
                    hDlg,
                    "Enter width and height between 1 and 10000.",
                    "Canvas Size",
                    MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            gCanvasDlgWidth = w;
            gCanvasDlgHeight = h;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static void DoCanvasSize(HWND hwnd) {
    gCanvasDlgWidth = docWidth;
    gCanvasDlgHeight = docHeight;
    if (DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_CANVAS_SIZE), hwnd, CanvasSizeDlgProc) == IDOK) {
        ResizeDocument(hwnd, gCanvasDlgWidth, gCanvasDlgHeight, true, true);
    }
}

static bool PromptSaveIfDirty(HWND hwnd) {
    if (!documentDirty) return true;
    const int result = MessageBoxA(
        hwnd,
        "You have unsaved changes. Save before continuing?",
        "Simple Drawing App",
        MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) return false;
    if (result == IDNO) return true;

    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SAVE, 0), 0);
    return !documentDirty;
}

static void DoSave(HWND hwnd) {
    EnsureCanvas(hwnd);
    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "png";

    if (GetSaveFileNameA(&ofn)) {
        ClearSelection(true);
        Bitmap* flat = GetCompositeBitmap();
        if (flat && SaveCanvasToFile(flat, filePath)) {
            MarkClean(hwnd);
        }
        else {
            MessageBoxA(hwnd, "Failed to save image.", "Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void DoOpen(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;

    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        DestroyStrokeLayer();
        isDrawing = false;
        ClearSelection(false);

        Bitmap* loaded = nullptr;
        Graphics* loadedG = nullptr;
        if (LoadImageFromFile(filePath, loaded, loadedG)) {
            delete loadedG;
            gLayers.ReplaceWithImage(loaded);
            delete loaded;
            SyncDocSizeFromBitmap();
            scrollX = 0;
            scrollY = 0;
            InvalidateComposite();
            UpdateScrollBars();
            gHistory.Clear();
            MarkClean(hwnd);
            RefreshLayerList();
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
        }
        else {
            MessageBoxA(hwnd, "Failed to load image.", "Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void DoNew(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    // Keep current document size; reset to a single background layer.
    gLayers.Reset(docWidth, docHeight, gTheme.canvasBg);
    gHistory.Clear();
    InvalidateComposite();
    MarkClean(hwnd);
    RefreshLayerList();
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}

static void DoUndo(HWND hwnd) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    if (gHistory.Undo(gLayers)) {
        SyncDocSizeFromBitmap();
        InvalidateComposite();
        UpdateScrollBars();
        MarkDirty(hwnd);
        RefreshLayerList();
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
    }
}

static void DoRedo(HWND hwnd) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    if (gHistory.Redo(gLayers)) {
        SyncDocSizeFromBitmap();
        InvalidateComposite();
        UpdateScrollBars();
        MarkDirty(hwnd);
        RefreshLayerList();
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
    }
}

static void DestroyStrokeLayer() {
    delete strokeGraphics;
    delete strokeLayer;
    strokeGraphics = nullptr;
    strokeLayer = nullptr;
}

static void BeginStrokeLayer() {
    DestroyStrokeLayer();
    if (!gLayers.ActiveBitmap()) return;

    const int width = gLayers.Width();
    const int height = gLayers.Height();
    strokeLayer = new Bitmap(width, height, PixelFormat32bppARGB);
    strokeGraphics = Graphics::FromImage(strokeLayer);
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    strokeGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
    strokeGraphics->SetCompositingMode(CompositingModeSourceOver);
}

static void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
    if (!target) return;

    // Draw fully opaque ink onto the stroke layer; opacity is applied once when compositing.
    // Eraser on non-background layers punches transparent holes via SourceCopy on commit.
    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    COLORREF strokeColor = (currentTool == DrawTool::Eraser)
        ? (eraseTransparent ? RGB(0, 0, 0) : gTheme.canvasBg)
        : penColor;
    if (eraseTransparent) {
        target->SetCompositingMode(CompositingModeSourceCopy);
        Pen pen(Color(255, 0, 0, 0), static_cast<REAL>(penWidth)); // alpha marker stored in A
        // Use opaque magenta as erase mask; commit will clear those pixels.
        pen.SetColor(Color(255, 255, 0, 255));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        target->DrawLine(&pen, x0, y0, x1, y1);
        target->SetCompositingMode(CompositingModeSourceOver);
        return;
    }

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

    Rect bounds(left, top, width, height);
    if (currentTool == DrawTool::Rectangle) {
        target->DrawRectangle(&pen, bounds);
    }
    else if (currentTool == DrawTool::Ellipse) {
        target->DrawEllipse(&pen, bounds);
    }
}

static void RedrawShapePreview(int endX, int endY, bool shiftConstrained) {
    if (!strokeGraphics) return;
    int x1 = endX;
    int y1 = endY;
    if (shiftConstrained) {
        ConstrainShapeEnd(shapeStart.x, shapeStart.y, x1, y1);
    }
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    DrawShapeOnto(strokeGraphics, shapeStart.x, shapeStart.y, x1, y1);
    lastPoint.x = x1;
    lastPoint.y = y1;
}

static void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY) {
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
        strokeLayer,
        Rect(destX, destY, width, height),
        0, 0, width, height,
        UnitPixel,
        &attrs);
}

static void ApplyTransparentEraseMask(Graphics* dest, Bitmap* mask) {
    if (!dest || !mask) return;
    Bitmap* target = gLayers.ActiveBitmap();
    if (!target) return;

    const int width = static_cast<int>(mask->GetWidth());
    const int height = static_cast<int>(mask->GetHeight());
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
            if (m[3] == 0) continue;
            // Magenta erase marker: punch to transparent.
            if (m[2] == 255 && m[1] == 0 && m[0] == 255) {
                BYTE* t = trow + x * 4;
                t[0] = t[1] = t[2] = t[3] = 0;
            }
        }
    }

    target->UnlockBits(&targetData);
    mask->UnlockBits(&maskData);
    (void)dest;
}

static void CommitStrokeLayer() {
    Graphics* ag = gLayers.ActiveGraphics();
    if (!strokeLayer || !ag) {
        DestroyStrokeLayer();
        return;
    }

    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    if (eraseTransparent) {
        ApplyTransparentEraseMask(ag, strokeLayer);
    }
    else {
        DrawStrokeLayerWithOpacity(ag, 0, 0);
    }
    DestroyStrokeLayer();
    InvalidateComposite();
}

static int ScrollByMessage(HWND hwnd, int bar, WPARAM wParam, int current, int maxScroll) {
    int pos = current;
    // SB_LINELEFT/SB_LINEUP (and similar H/V pairs) share the same numeric values.
    switch (LOWORD(wParam)) {
    case SB_LINELEFT: // also SB_LINEUP
        pos -= 16;
        break;
    case SB_LINERIGHT: // also SB_LINEDOWN
        pos += 16;
        break;
    case SB_PAGELEFT: { // also SB_PAGEUP
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_PAGE;
        GetScrollInfo(hwnd, bar, &si);
        pos -= static_cast<int>(si.nPage);
        break;
    }
    case SB_PAGERIGHT: { // also SB_PAGEDOWN
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_PAGE;
        GetScrollInfo(hwnd, bar, &si);
        pos += static_cast<int>(si.nPage);
        break;
    }
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(hwnd, bar, &si);
        pos = si.nTrackPos;
        break;
    }
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
    case WM_SIZE:
        UpdateScrollBars();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_HSCROLL: {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int contentW = ScaledContentWidth();
        const int maxScroll = (contentW > rc.right) ? (contentW - rc.right) : 0;
        const int newPos = ScrollByMessage(hwnd, SB_HORZ, wParam, scrollX, maxScroll);
        if (newPos != scrollX) {
            scrollX = newPos;
            SetScrollPos(hwnd, SB_HORZ, scrollX, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_VSCROLL: {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int contentH = ScaledContentHeight();
        const int maxScroll = (contentH > rc.bottom) ? (contentH - rc.bottom) : 0;
        const int newPos = ScrollByMessage(hwnd, SB_VERT, wParam, scrollY, maxScroll);
        if (newPos != scrollY) {
            scrollY = newPos;
            SetScrollPos(hwnd, SB_VERT, scrollY, TRUE);
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
                MarkDirty(parent);
                InvalidateCanvas();
            }
            break;
        }

        gHistory.Push(gLayers);
        BeginStrokeLayer();
        isDrawing = true;
        lastPoint.x = docX;
        lastPoint.y = docY;
        shapeStart.x = docX;
        shapeStart.y = docY;

        if (IsFreehandTool(currentTool)) {
            DrawStrokeOnto(strokeGraphics, docX, docY, docX, docY);
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
            DrawStrokeOnto(strokeGraphics, lastPoint.x, lastPoint.y, docX, docY);
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
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, viewW, viewH);
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
                        strokeLayer,
                        dest,
                        0, 0,
                        static_cast<int>(strokeLayer->GetWidth()),
                        static_cast<int>(strokeLayer->GetHeight()),
                        UnitPixel,
                        &attrs);
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

static LRESULT CALLBACK TrackbarWheelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOUSEWHEEL) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            return SendMessageA(parent, WM_MOUSEWHEEL, wParam, lParam);
        }
    }
    return CallWindowProcA(gOldTrackbarProc, hwnd, msg, wParam, lParam);
}

static void SubclassTrackbarWheel(HWND trackbar) {
    if (!trackbar) return;
    WNDPROC prev = (WNDPROC)SetWindowLongPtrA(trackbar, GWLP_WNDPROC, (LONG_PTR)TrackbarWheelProc);
    if (!gOldTrackbarProc) {
        gOldTrackbarProc = prev;
    }
}

static void LayoutStatusParts(HWND hwnd) {
    if (!hwndStatus) return;
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    int parts[4] = { width / 3, (width * 2) / 3, width - 1, -1 };
    SendMessageA(hwndStatus, SB_SETPARTS, 3, (LPARAM)parts);
}

static HWND CreateIconButton(HWND parent, int id, const char* tooltip, bool pushLike) {
    const DWORD style = WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW |
        (pushLike ? (BS_PUSHLIKE | BS_CHECKBOX) : BS_PUSHBUTTON);
    HWND btn = CreateWindowA("BUTTON", "", style,
        0, 0, ICON_BTN, ICON_BTN,
        parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    if (hwndTooltip && btn && tooltip && tooltip[0]) {
        TOOLINFOA ti = {};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = parent;
        ti.uId = (UINT_PTR)btn;
        ti.lpszText = const_cast<char*>(tooltip);
        SendMessageA(hwndTooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
    }
    return btn;
}

static void CreateToolbar(HWND hwnd) {
    hwndTooltip = CreateWindowExA(0, TOOLTIPS_CLASSA, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
    if (hwndTooltip) {
        SendMessageA(hwndTooltip, TTM_SETMAXTIPWIDTH, 0, 240);
    }

    // Tools (left rail)
    hwndToolButtons[0] = CreateIconButton(hwnd, IDC_TOOL_PEN, "Pen", true);
    hwndToolButtons[1] = CreateIconButton(hwnd, IDC_TOOL_ERASER, "Eraser", true);
    hwndToolButtons[2] = CreateIconButton(hwnd, IDC_TOOL_FILL, "Fill", true);
    hwndToolButtons[6] = CreateIconButton(hwnd, IDC_TOOL_SELECT, "Select", true);
    hwndToolButtons[3] = CreateIconButton(hwnd, IDC_TOOL_LINE, "Line", true);
    hwndToolButtons[4] = CreateIconButton(hwnd, IDC_TOOL_RECT, "Rectangle", true);
    hwndToolButtons[5] = CreateIconButton(hwnd, IDC_TOOL_ELLIPSE, "Ellipse", true);

    for (int i = 0; i < 8; ++i) {
        hwndSwatches[i] = CreateWindowA("BUTTON", "",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, 18, 18,
            hwnd, (HMENU)(INT_PTR)(IDC_SWATCH0 + i), GetModuleHandle(NULL), NULL);
    }

    // Actions
    hwndActionButtons[0] = CreateIconButton(hwnd, IDC_COLOR_BUTTON, "Color…", false);
    hwndActionButtons[1] = CreateIconButton(hwnd, IDC_NEW_BUTTON, "New (Ctrl+N)", false);
    hwndActionButtons[2] = CreateIconButton(hwnd, IDC_UNDO_BUTTON, "Undo (Ctrl+Z)", false);
    hwndActionButtons[3] = CreateIconButton(hwnd, IDC_REDO_BUTTON, "Redo (Ctrl+Y)", false);
    hwndActionButtons[4] = CreateIconButton(hwnd, IDC_CLEAR_BUTTON, "Clear canvas", false);
    hwndActionButtons[5] = CreateIconButton(hwnd, IDC_SAVE_BUTTON, "Save (Ctrl+S)", false);
    hwndActionButtons[6] = CreateIconButton(hwnd, IDC_LOAD_BUTTON, "Open (Ctrl+O)", false);

    // Bottom inspectors
    hwndSizeLabel = CreateWindowA("STATIC", "Size", WS_CHILD | WS_VISIBLE,
        0, 0, 34, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndSizeLabel);

    hwndSlider = CreateWindowExA(0, TRACKBAR_CLASSA, NULL,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        0, 0, 150, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
    SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);
    SubclassTrackbarWheel(hwndSlider);

    hwndPenWidthBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        0, 0, 40, 22, hwnd, (HMENU)(INT_PTR)IDC_WIDTH_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndPenWidthBox);

    hwndOpacityLabel = CreateWindowA("STATIC", "Opacity", WS_CHILD | WS_VISIBLE,
        0, 0, 54, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndOpacityLabel);

    hwndOpacitySlider = CreateWindowExA(0, TRACKBAR_CLASSA, NULL,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        0, 0, 150, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hwndOpacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, penOpacity);
    SubclassTrackbarWheel(hwndOpacitySlider);

    hwndOpacityBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        0, 0, 40, 22, hwnd, (HMENU)(INT_PTR)IDC_OPACITY_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndOpacityBox);

    SetActiveTool(DrawTool::Pen);
    UpdatePenWidthDisplay();
    UpdateOpacityDisplay();
}

static void PaintChromeInto(Graphics& g, int width, int height, const ChromeLayout& chrome) {
    g.SetSmoothingMode(SmoothingModeNone); // cache build: crisp fills, cheaper
    g.SetCompositingMode(CompositingModeSourceCopy);

    const Color stoneA(255, GetRValue(gTheme.chromeBg), GetGValue(gTheme.chromeBg), GetBValue(gTheme.chromeBg));
    const Color stoneB(255, 226, 216, 200);
    const Color deepA(255, GetRValue(gTheme.chromeDeep), GetGValue(gTheme.chromeDeep), GetBValue(gTheme.chromeDeep));
    const Color deepB(255, 208, 196, 178);
    const Color grain(18, 120, 92, 58);

    // Clear full client so holes behind children stay theme-colored if clipped oddly.
    SolidBrush clear(stoneA);
    g.FillRectangle(&clear, 0, 0, width, height);
    g.SetCompositingMode(CompositingModeSourceOver);

    RectF topR(0.0f, 0.0f, static_cast<REAL>(width), static_cast<REAL>(chrome.topH));
    DrawFrescoPanel(g, topR, stoneA, stoneB, true);
    DrawFrescoGrain(g, topR, grain);

    RectF railR(0.0f, static_cast<REAL>(chrome.topH), static_cast<REAL>(chrome.railW),
        static_cast<REAL>((height - chrome.statusH) - chrome.topH));
    DrawFrescoPanel(g, railR, deepA, deepB, false);
    DrawFrescoGrain(g, railR, grain);

    RectF bottomR(static_cast<REAL>(chrome.railW),
        static_cast<REAL>(height - chrome.statusH - chrome.bottomH),
        static_cast<REAL>((width - chrome.layerW) - chrome.railW),
        static_cast<REAL>(chrome.bottomH));
    DrawFrescoPanel(g, bottomR, stoneB, stoneA, true);
    DrawFrescoGrain(g, bottomR, grain);

    RectF panelR(static_cast<REAL>(width - chrome.layerW), static_cast<REAL>(chrome.topH),
        static_cast<REAL>(chrome.layerW),
        static_cast<REAL>((height - chrome.statusH) - chrome.topH));
    DrawFrescoPanel(g, panelR, stoneA, Color(255, 230, 220, 204), true);
    DrawFrescoGrain(g, panelR, grain);

    {
        LinearGradientBrush wash(
            PointF(0.0f, 0.0f), PointF(160.0f, 0.0f),
            Color(40, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)),
            Color(0, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)));
        g.FillRectangle(&wash, RectF(0.0f, 0.0f, 160.0f, static_cast<REAL>(chrome.topH)));
    }

    Pen rule(Color(255, GetRValue(gTheme.chromeLine), GetGValue(gTheme.chromeLine), GetBValue(gTheme.chromeLine)), 1.0f);
    g.DrawLine(&rule, 0.0f, static_cast<REAL>(chrome.topH) - 0.5f, static_cast<REAL>(width), static_cast<REAL>(chrome.topH) - 0.5f);
    g.DrawLine(&rule, static_cast<REAL>(chrome.railW) - 0.5f, static_cast<REAL>(chrome.topH),
        static_cast<REAL>(chrome.railW) - 0.5f, static_cast<REAL>(height - chrome.statusH));
    g.DrawLine(&rule, panelR.X, static_cast<REAL>(chrome.topH), panelR.X, static_cast<REAL>(height - chrome.statusH));
    g.DrawLine(&rule, static_cast<REAL>(chrome.railW), bottomR.Y, panelR.X, bottomR.Y);

    // Compass is NOT baked into the cache — drawn as a cheap overlay so it can
    // animate without rebuilding fresco panels.
}

static void EnsureChromeCache(int width, int height, const ChromeLayout& chrome) {
    if (width < 1 || height < 1) return;
    if (gChromeCache
        && gChromeCacheW == width
        && gChromeCacheH == height
        && gChromeCacheStatusH == chrome.statusH) {
        return;
    }

    DestroyChromeCache();
    gChromeCache = new Bitmap(width, height, PixelFormat32bppPARGB);
    if (!gChromeCache) return;
    gChromeCacheW = width;
    gChromeCacheH = height;
    gChromeCacheStatusH = chrome.statusH;

    Graphics g(gChromeCache);
    PaintChromeInto(g, width, height, chrome);

    // Bake wordmark into the same bitmap (stable; no per-frame TextOut flicker).
    HDC hdc = g.GetHDC();
    if (hdc) {
        HFONT brand = gBrandFont ? gBrandFont : gUiFont;
        HGDIOBJ oldFont = brand ? SelectObject(hdc, brand) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, gTheme.text);
        TextOutA(hdc, 44, (chrome.topH - 18) / 2, "Atelier", 7);
        if (oldFont) SelectObject(hdc, oldFont);
        g.ReleaseHDC(hdc);
    }
}

static void DrawToolbarBackgroundCheap(HDC hdc, const RECT& client, const ChromeLayout& chrome) {
    // Solid fills only — used while resizing before the fresco cache is rebuilt.
    RECT top = client; top.bottom = chrome.topH;
    FillRect(hdc, &top, gChromeBrush);
    RECT rail = client;
    rail.top = chrome.topH;
    rail.right = chrome.railW;
    rail.bottom = client.bottom - chrome.statusH;
    FillRect(hdc, &rail, gChromeDeepBrush ? gChromeDeepBrush : gChromeBrush);
    RECT bottom = client;
    bottom.top = client.bottom - chrome.statusH - chrome.bottomH;
    bottom.bottom = client.bottom - chrome.statusH;
    bottom.left = chrome.railW;
    bottom.right = client.right - chrome.layerW;
    FillRect(hdc, &bottom, gChromeBrush);
    RECT panel = client;
    panel.left = client.right - chrome.layerW;
    panel.top = chrome.topH;
    panel.bottom = client.bottom - chrome.statusH;
    FillRect(hdc, &panel, gChromeBrush);

    const Color gold(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    (void)gold;
    HFONT brand = gBrandFont ? gBrandFont : gUiFont;
    HGDIOBJ oldFont = brand ? SelectObject(hdc, brand) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, gTheme.text);
    TextOutA(hdc, 44, (chrome.topH - 18) / 2, "Atelier", 7);
    if (oldFont) SelectObject(hdc, oldFont);
    DrawBrandCompassOverlay(hdc, chrome.topH);
}

static void DrawToolbarBackground(HDC hdc, const RECT& client) {
    const ChromeLayout chrome = GetChromeLayout(nullptr);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width < 1 || height < 1) return;

    const bool cacheReady = gChromeCache
        && gChromeCacheW == width
        && gChromeCacheH == height
        && gChromeCacheStatusH == chrome.statusH;
    if (!cacheReady) {
        DrawToolbarBackgroundCheap(hdc, client, chrome);
        return;
    }

    Graphics g(hdc);
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.DrawImage(gChromeCache, 0, 0, width, height);
    // Animated compass overlay (cache blit restores the previous frame under it).
    DrawBrandCompassOverlay(hdc, chrome.topH);
}

static void CreateLayerPanel(HWND hwnd) {
    hwndLayerAdd = CreateIconButton(hwnd, IDC_LAYER_ADD, "Add layer", false);
    hwndLayerDel = CreateIconButton(hwnd, IDC_LAYER_DEL, "Delete layer", false);
    hwndLayerUp = CreateIconButton(hwnd, IDC_LAYER_UP, "Move layer up", false);
    hwndLayerDown = CreateIconButton(hwnd, IDC_LAYER_DOWN, "Move layer down", false);

    hwndLayerVisible = CreateWindowA("BUTTON", "Visible",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        0, 0, 100, 22, hwnd, (HMENU)(INT_PTR)IDC_LAYER_VISIBLE, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndLayerVisible);

    hwndLayerList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)IDC_LAYER_LIST, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndLayerList);

    hwndLayerOpacity = CreateWindowExA(0, TRACKBAR_CLASSA, NULL,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        0, 0, 100, 28, hwnd, (HMENU)(INT_PTR)IDC_LAYER_OPACITY, GetModuleHandle(NULL), NULL);
    SendMessage(hwndLayerOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndLayerOpacity, TBM_SETPOS, TRUE, 100);
    SubclassTrackbarWheel(hwndLayerOpacity);

    RefreshLayerList();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icex);

        gUiFont = CreateFontA(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        gBrandFont = CreateFontA(
            -16, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Georgia");
        gChromeBrush = CreateSolidBrush(gTheme.chromeBg);
        gChromeDeepBrush = CreateSolidBrush(gTheme.chromeDeep);

        CreateToolbar(hwnd);
        CreateLayerPanel(hwnd);

        hwndStatus = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "", hwnd, 1);
        ApplyUiFont(hwndStatus);
        LayoutStatusParts(hwnd);

        hwndViewport = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            VIEWPORT_CLASS_NAME,
            "",
            WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPCHILDREN,
            TOOL_RAIL_WIDTH, TOPBAR_HEIGHT, 100, 100,
            hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL);

        EnsureCanvas(hwnd);
        RefreshLayerList();
        LayoutViewport(hwnd);
        UpdateStatusBar(hwnd);
        UpdateWindowTitle(hwnd);
        EnableDarkTitleBar(hwnd);
        // Fresco cache once; low-rate idle motion (pauses while drawing/resizing).
        SetTimer(hwnd, IDT_CHROME_REBUILD, 1, NULL);
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

            RECT brand = BrandCompassInvalidateRect();
            InvalidateRect(hwnd, &brand, FALSE);
            InvalidateActiveToolButton();
        }
        else if (wParam == IDT_CHROME_REBUILD) {
            KillTimer(hwnd, IDT_CHROME_REBUILD);
            RECT client = {};
            GetClientRect(hwnd, &client);
            const ChromeLayout chrome = GetChromeLayout(hwnd);
            EnsureChromeCache(client.right - client.left, client.bottom - client.top, chrome);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, gTheme.chromeBg);
        SetTextColor(hdc, gTheme.text);
        return (LRESULT)gChromeBrush;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) break;
        if (dis->CtlID >= IDC_SWATCH0 && dis->CtlID <= IDC_SWATCH7) {
            const int index = dis->CtlID - IDC_SWATCH0;
            HBRUSH fill = CreateSolidBrush(kSwatches[index]);
            FillRect(dis->hDC, &dis->rcItem, fill);
            DeleteObject(fill);

            const bool selected = (penColor == kSwatches[index]);
            HPEN border = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? gTheme.accent : RGB(90, 90, 90));
            HGDIOBJ oldPen = SelectObject(dis->hDC, border);
            HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldBrush);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(border);
            return TRUE;
        }
        if (IsIconControlId(static_cast<int>(dis->CtlID))) {
            const int id = static_cast<int>(dis->CtlID);
            IconPaintOpts opts;
            opts.chromeBg = IsToolRailControlId(id) ? gTheme.chromeDeep : gTheme.chromeBg;
            opts.accent = gTheme.accent;
            opts.text = gTheme.text;
            opts.selectedBg = gTheme.toolSelectedBg;
            opts.pulse = 0.0f;
            opts.pressScale = 1.0f;
            int activeToolId = IDC_TOOL_PEN;
            switch (currentTool) {
            case DrawTool::Pen: activeToolId = IDC_TOOL_PEN; break;
            case DrawTool::Eraser: activeToolId = IDC_TOOL_ERASER; break;
            case DrawTool::Fill: activeToolId = IDC_TOOL_FILL; break;
            case DrawTool::Line: activeToolId = IDC_TOOL_LINE; break;
            case DrawTool::Rectangle: activeToolId = IDC_TOOL_RECT; break;
            case DrawTool::Ellipse: activeToolId = IDC_TOOL_ELLIPSE; break;
            case DrawTool::Select: activeToolId = IDC_TOOL_SELECT; break;
            }
            if (id == activeToolId) {
                opts.pressScale = 1.0f + gToolFlash * 0.10f;
                opts.pulse = gUiPulse;
            }
            if (id == IDC_COLOR_BUTTON) {
                opts.useColorFill = true;
                opts.colorFill = penColor;
            }
            PaintIconButton(dis, opts);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND:
        // Avoid double-paint flicker: chrome is drawn once in WM_PAINT from cache.
        return 1;
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
            SetTimer(hwnd, IDT_CHROME_REBUILD, 60, NULL);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_EXITSIZEMOVE: {
        gUiSizing = false;
        KillTimer(hwnd, IDT_CHROME_REBUILD);
        RECT client = {};
        GetClientRect(hwnd, &client);
        EnsureChromeCache(client.right - client.left, client.bottom - client.top, GetChromeLayout(hwnd));
        InvalidateRect(hwnd, NULL, FALSE);
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

        if (cmdId >= IDC_SWATCH0 && cmdId <= IDC_SWATCH7) {
            penColor = kSwatches[cmdId - IDC_SWATCH0];
            for (HWND sw : hwndSwatches) {
                InvalidateRect(sw, NULL, TRUE);
            }
            if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
            break;
        }

        switch (cmdId) {
        case IDC_TOOL_PEN:
            ClearSelection(true);
            SetActiveTool(DrawTool::Pen);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_ERASER:
            ClearSelection(true);
            SetActiveTool(DrawTool::Eraser);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_FILL:
            ClearSelection(true);
            SetActiveTool(DrawTool::Fill);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_LINE:
            ClearSelection(true);
            SetActiveTool(DrawTool::Line);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_RECT:
            ClearSelection(true);
            SetActiveTool(DrawTool::Rectangle);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_ELLIPSE:
            ClearSelection(true);
            SetActiveTool(DrawTool::Ellipse);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_SELECT:
            SetActiveTool(DrawTool::Select);
            UpdateStatusBar(hwnd);
            break;
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
            DoCut(hwnd);
            break;
        case IDM_COPY:
            DoCopy(hwnd);
            break;
        case IDM_PASTE:
            DoPaste(hwnd);
            break;
        case IDM_DELETE_SEL:
            DoDeleteSelection(hwnd);
            break;
        case IDM_SELECT_ALL:
            DoSelectAll(hwnd);
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
            for (HWND sw : hwndSwatches) {
                InvalidateRect(sw, NULL, TRUE);
            }
            if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
            break;
        }
        case IDC_NEW_BUTTON:
        case IDM_NEW:
            DoNew(hwnd);
            break;
        case IDC_CLEAR_BUTTON:
        case IDM_CLEAR:
            ClearCanvas(hwnd, true);
            UpdateStatusBar(hwnd);
            break;
        case IDC_UNDO_BUTTON:
        case IDM_UNDO:
            DoUndo(hwnd);
            break;
        case IDC_REDO_BUTTON:
        case IDM_REDO:
            DoRedo(hwnd);
            break;
        case IDC_SAVE_BUTTON:
        case IDM_SAVE:
            DoSave(hwnd);
            break;
        case IDC_LOAD_BUTTON:
        case IDM_OPEN:
            DoOpen(hwnd);
            break;
        case IDM_CANVAS_SIZE:
            DoCanvasSize(hwnd);
            break;
        case IDM_ABOUT:
            DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABOUTBOX), hwnd, AboutDlgProc);
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
    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            ClearSelection(true);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
            break;
        }
        if (wParam == VK_OEM_4) { // [
            AdjustPenWidth(hwnd, -1);
        }
        else if (wParam == VK_OEM_6) { // ]
            AdjustPenWidth(hwnd, 1);
        }
        break;
    }
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
        DrawToolbarBackground(hdc, client);
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
        ClearSelection(false);
        delete gClipboardBmp;
        gClipboardBmp = nullptr;
        gHistory.Clear();
        DestroyCompositeCache();
        DestroyChromeCache();
        gLayers.Destroy();
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

    if (!RegisterViewportClass(hInstance)) {
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
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Simple Drawing App",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    gAccel = LoadAcceleratorsA(hInstance, MAKEINTRESOURCEA(IDC_SIMPLEDRAWINGAPP));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAcceleratorA(hwnd, gAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
