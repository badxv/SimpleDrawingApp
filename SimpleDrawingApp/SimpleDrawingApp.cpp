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

namespace {
const char CLASS_NAME[] = "SimpleDrawingAppWindowClass";
const char VIEWPORT_CLASS_NAME[] = "SimpleDrawingAppViewport";
constexpr int TOPBAR_HEIGHT = 38;       // doubles as custom titlebar height
constexpr int TOOL_RAIL_WIDTH = 100;
constexpr int PANEL_EDGE_WIDTH = 22;    // collapsed rail / layers strip
constexpr int BOTTOMBAR_HEIGHT = 34;
constexpr int STATUS_HEIGHT = 22;
constexpr int LAYER_PANEL_WIDTH = 168;
constexpr int ICON_BTN = 30;
constexpr int WELL_FRAME = 6;           // maximize canvas (was 14)
constexpr int BRAND_STRIP_W = 118;
constexpr int MENU_BTN_W = 44;
constexpr int FLOAT_DRAG_H = 22;
constexpr int FLOAT_CHIP_H = 36;
constexpr int CAPTION_BTN_W = 46;
constexpr int CAPTION_BTN_H = 30; // top-aligned; shorter than TOPBAR to avoid droop

#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92
#endif
#ifndef SM_CYPADDEDBORDER
#define SM_CYPADDEDBORDER 92
#endif

static bool IsRunningUnderWine() {
    static int cached = -1;
    if (cached < 0) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        cached = (ntdll && GetProcAddress(ntdll, "wine_get_version")) ? 1 : 0;
    }
    return cached == 1;
}
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
HBRUSH gChromeElevatedBrush = nullptr;
HACCEL gAccel = nullptr;
HWND hwndTooltip = nullptr;
HWND hwndBrand = nullptr; // dedicated child: flicker-free compass (Catch22 / clip-children pattern)
float gUiPulse = 0.0f;            // 0..1 selected-tool breath (idle)
float gUiCompassAngle = -18.0f;   // animated overlay angle
float gToolFlash = 0.0f;          // 1 → 0 after tool switch
float gIdlePhase = 0.0f;
float gSelAntOffset = 0.0f; // marching-ants dash phase (Photoshop-style)
bool gUiSizing = false;
Bitmap* gChromeCache = nullptr;
int gChromeCacheW = 0;
int gChromeCacheH = 0;
int gChromeCacheStatusH = -1;
int gChromeCacheRailW = -1;
int gChromeCacheLayerW = -1;
Bitmap* gBrandStrip = nullptr; // offscreen compose target
HBITMAP gBrandStripHbmp = nullptr; // GDI handle for single BitBlt to screen
int gBrandStripH = 0;

HWND hwndViewport = nullptr;
HWND hwndScrollH = nullptr;
HWND hwndScrollV = nullptr;
HWND hwndScrollCorner = nullptr;
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
HWND hwndToolButtons[kToolButtonCount] = {};
HWND hwndPalette = nullptr;
HWND hwndActionButtons[7] = {}; // 0 Color(FG), 1 New, 2 Undo, 3 Redo, 4 Clear, 5 Save, 6 Open
HWND hwndBgButton = nullptr;
HWND hwndSwapColors = nullptr;
HWND hwndShapeFlyout = nullptr;
HWND hwndShapeButtons[6] = {};
HWND hwndShapeModeButtons[3] = {};
HWND hwndToggleRail = nullptr;
HWND hwndToggleLayers = nullptr;
HWND hwndPaletteFloat = nullptr;
HWND hwndMenuButtons[6] = {}; // File Edit Image View Tools Help
HWND hwndCaptionMin = nullptr;
HWND hwndCaptionMax = nullptr;
HWND hwndCaptionClose = nullptr;
HWND gCaptionHot = nullptr; // which caption child is hovered
HMENU gAppMenu = nullptr;

bool gRailOpen = true;
bool gLayersOpen = true;
bool gBottomOpen = true;
bool gPaletteFloating = false;
POINT gPaletteFloatPos = { 80, 120 };
bool gFloatDragging = false;
POINT gFloatDragHot = {};

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

ULONG_PTR gdiplusToken = 0;
}

COLORREF penColor = RGB(0, 0, 0);       // foreground (stroke / brush / fill-bucket)
COLORREF backColor = RGB(255, 255, 255); // background (shape fill)
int penWidth = 5;
int penOpacity = 100; // percent 1-100
DrawTool currentTool = DrawTool::Pen;
ShapeKind currentShape = ShapeKind::Rectangle;
ShapePaintMode shapePaintMode = ShapePaintMode::Stroke;
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

static void CloseShapeFlyout();
static void OpenShapeFlyout(HWND parent);
static void SyncShapeFlyoutChecks();

static void SetActiveTool(DrawTool tool) {
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

static ShapePaintMode EffectiveShapePaintMode() {
    // Hold Alt → fill only; hold Ctrl (or Alt+Ctrl) → stroke + fill.
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) return ShapePaintMode::Both;
    if (alt) return ShapePaintMode::Fill;
    return shapePaintMode;
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

static void DestroyBrandStrip() {
    delete gBrandStrip;
    gBrandStrip = nullptr;
    if (gBrandStripHbmp) {
        DeleteObject(gBrandStripHbmp);
        gBrandStripHbmp = nullptr;
    }
    gBrandStripH = 0;
}

static void DestroyChromeCache() {
    delete gChromeCache;
    gChromeCache = nullptr;
    gChromeCacheW = 0;
    gChromeCacheH = 0;
    gChromeCacheStatusH = -1;
    gChromeCacheRailW = -1;
    gChromeCacheLayerW = -1;
    DestroyBrandStrip();
}

static void EnsureBrandStrip(int topH) {
    if (topH < 1) return;
    if (!gBrandStrip || gBrandStripH != topH) {
        DestroyBrandStrip();
        gBrandStrip = new Bitmap(BRAND_STRIP_W, topH, PixelFormat32bppPARGB);
        if (!gBrandStrip || gBrandStrip->GetLastStatus() != Ok) {
            delete gBrandStrip;
            gBrandStrip = nullptr;
            gBrandStripH = 0;
            return;
        }
        gBrandStripH = topH;
    }
    if (!gBrandStrip) return;

    Graphics g(gBrandStrip);
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);

    if (gChromeCache && gChromeCacheW >= BRAND_STRIP_W && gChromeCacheH >= topH) {
        g.DrawImage(
            gChromeCache,
            Rect(0, 0, BRAND_STRIP_W, topH),
            0, 0, BRAND_STRIP_W, topH,
            UnitPixel);
    }
    else {
        SolidBrush fill(Color(255,
            GetRValue(gTheme.chromeBg), GetGValue(gTheme.chromeBg), GetBValue(gTheme.chromeBg)));
        g.FillRectangle(&fill, 0, 0, BRAND_STRIP_W, topH);
        HDC hdc = g.GetHDC();
        if (hdc) {
            HFONT brand = gBrandFont ? gBrandFont : gUiFont;
            HGDIOBJ oldFont = brand ? SelectObject(hdc, brand) : nullptr;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, gTheme.ink);
            TextOutA(hdc, 48, (topH - 20) / 2, "ATELIER", 7);
            if (oldFont) SelectObject(hdc, oldFont);
            g.ReleaseHDC(hdc);
        }
    }

    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const Color gold(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    DrawBrandCompass(g, 25.0f, static_cast<REAL>(topH) * 0.5f, 11.0f, gold, gUiCompassAngle);

    // Refresh GDI bitmap for a single BitBlt to the brand child (no multi-step screen draws).
    if (gBrandStripHbmp) {
        DeleteObject(gBrandStripHbmp);
        gBrandStripHbmp = nullptr;
    }
    if (gBrandStrip->GetHBITMAP(
            Color(255, GetRValue(gTheme.chromeBg), GetGValue(gTheme.chromeBg), GetBValue(gTheme.chromeBg)),
            &gBrandStripHbmp) != Ok) {
        gBrandStripHbmp = nullptr;
    }
}

static void PaintBrandChild(HDC hdc, int width, int height) {
    if (width < 1 || height < 1) return;
    EnsureBrandStrip(height);
    if (gBrandStripHbmp) {
        HDC mem = CreateCompatibleDC(hdc);
        if (mem) {
            HGDIOBJ old = SelectObject(mem, gBrandStripHbmp);
            BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
            return;
        }
    }
    // Fallback if HBITMAP unavailable.
    if (gBrandStrip) {
        Graphics g(hdc);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(gBrandStrip, 0, 0, width, height);
    }
}

static void InvalidateBrandMark() {
    if (hwndBrand) {
        InvalidateRect(hwndBrand, NULL, FALSE);
    }
}

static LRESULT CALLBACK BrandProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1; // never clear-then-draw (Catch22 flicker rule)
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        PaintBrandChild(hdc, rc.right - rc.left, rc.bottom - rc.top);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        // Let clicks pass through to parent (brand is decorative).
        return HTTRANSPARENT;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

static bool RegisterBrandClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = BrandProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppBrand";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = 0; // no CS_HREDRAW/VREDRAW
    return RegisterClassA(&wc) != 0;
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
    layout.railW = gRailOpen ? TOOL_RAIL_WIDTH : PANEL_EDGE_WIDTH;
    layout.layerW = gLayersOpen ? LAYER_PANEL_WIDTH : PANEL_EDGE_WIDTH;
    layout.bottomH = gBottomOpen ? BOTTOMBAR_HEIGHT : 0;
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
static void RedrawShapePreview(int endX, int endY, bool shiftConstrained);
static void UpdateScrollBars();
static void SyncDocSizeFromBitmap();
static void InvalidateCanvas();
static void InvalidateComposite();
static void DestroyCompositeCache();
static Bitmap* GetCompositeBitmap();
static void LayoutViewport(HWND hwnd);
static void LayoutLayerPanel(HWND hwnd);
static void LayoutChromeControls(HWND hwnd);
static void LayoutCaptionButtons(HWND hwnd);
static void ApplyPanelVisibility();
static void SetRailOpen(HWND hwnd, bool open);
static void SetLayersOpen(HWND hwnd, bool open);
static void UpdateButtonTooltip(HWND parent, HWND btn, const char* text);
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
    Bitmap* next = gLayers.CreateComposite();
    if (!next) {
        // Keep previous cache if any; stay dirty so we retry.
        compositeDirty = true;
        return compositeCache;
    }
    delete compositeCache;
    compositeCache = next;
    compositeDirty = false;
    return compositeCache;
}

static void UpdateScrollBars() {
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

static void LayoutFloatPaletteContents() {
    if (!hwndPaletteFloat || !IsWindow(hwndPaletteFloat)) return;
    RECT rc = {};
    GetClientRect(hwndPaletteFloat, &rc);
    const int w = rc.right - rc.left;
    const int pad = 6;
    int y = FLOAT_DRAG_H + 4;
    if (hwndBgButton && GetParent(hwndBgButton) == hwndPaletteFloat) {
        MoveWindow(hwndBgButton, pad + 14, y + 10, 20, 20, TRUE);
    }
    if (hwndActionButtons[0] && GetParent(hwndActionButtons[0]) == hwndPaletteFloat) {
        MoveWindow(hwndActionButtons[0], pad, y, 22, 22, TRUE);
    }
    if (hwndSwapColors && GetParent(hwndSwapColors) == hwndPaletteFloat) {
        MoveWindow(hwndSwapColors, pad + 28, y - 2, 18, 18, TRUE);
    }
    y += FLOAT_CHIP_H;
    if (hwndPalette && GetParent(hwndPalette) == hwndPaletteFloat) {
        const int palW = MaxInt(72, w - pad * 2);
        const int palH = AtelierPalette_IdealHeight(palW);
        MoveWindow(hwndPalette, pad, y, palW, palH, TRUE);
    }
}

static int PaletteFloatHeight(int width) {
    return FLOAT_DRAG_H + 4 + FLOAT_CHIP_H + AtelierPalette_IdealHeight(MaxInt(72, width - 12)) + 8;
}

static LRESULT CALLBACK PaletteFloatProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(gTheme.chromeDeep);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        RECT drag = { 0, 0, rc.right, FLOAT_DRAG_H };
        HBRUSH bar = CreateSolidBrush(gTheme.chromeElevated);
        FillRect(hdc, &drag, bar);
        DeleteObject(bar);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, gTheme.accentDeep);
        HGDIOBJ oldFont = gUiFont ? SelectObject(hdc, gUiFont) : nullptr;
        TextOutA(hdc, 8, 4, "Color", 5);
        if (oldFont) SelectObject(hdc, oldFont);
        // Grip dots
        for (int i = 0; i < 3; ++i) {
            RECT d = { rc.right - 18, 6 + i * 4, rc.right - 10, 8 + i * 4 };
            HBRUSH dot = CreateSolidBrush(gTheme.chromeLine);
            FillRect(hdc, &d, dot);
            DeleteObject(dot);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int y = GET_Y_LPARAM(lParam);
        if (y < FLOAT_DRAG_H) {
            gFloatDragging = true;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            RECT wr = {};
            GetWindowRect(hwnd, &wr);
            gFloatDragHot.x = pt.x - wr.left;
            gFloatDragHot.y = pt.y - wr.top;
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (gFloatDragging && (wParam & MK_LBUTTON)) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            gPaletteFloatPos.x = pt.x - gFloatDragHot.x;
            gPaletteFloatPos.y = pt.y - gFloatDragHot.y;
            SetWindowPos(hwnd, nullptr, gPaletteFloatPos.x, gPaletteFloatPos.y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        gFloatDragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_COMMAND:
        if (HWND main = GetWindow(hwnd, GW_OWNER)) {
            return SendMessageA(main, msg, wParam, lParam);
        }
        break;
    case WM_DRAWITEM:
        if (HWND main = GetWindow(hwnd, GW_OWNER)) {
            return SendMessageA(main, msg, wParam, lParam);
        }
        break;
    case WM_SIZE:
        LayoutFloatPaletteContents();
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcA(hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.y < FLOAT_DRAG_H) return HTCAPTION; // native drag as fallback
        }
        return hit;
    }
    case WM_DESTROY:
        if (hwndPaletteFloat == hwnd) {
            hwndPaletteFloat = nullptr;
            gFloatDragging = false;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static bool RegisterPaletteFloatClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = PaletteFloatProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "AtelierPaletteFloat";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    return RegisterClassA(&wc) != 0;
}

static void EnsurePaletteFloatHost(HWND owner) {
    if (hwndPaletteFloat && IsWindow(hwndPaletteFloat)) return;
    const int w = TOOL_RAIL_WIDTH;
    const int h = PaletteFloatHeight(w);
    hwndPaletteFloat = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "AtelierPaletteFloat",
        "Color",
        WS_POPUP | WS_CLIPCHILDREN | WS_BORDER,
        gPaletteFloatPos.x, gPaletteFloatPos.y, w, h,
        owner, NULL, GetModuleHandle(NULL), NULL);
}

static void DockPaletteInstruments(HWND mainHwnd) {
    if (!gPaletteFloating) return;
    gPaletteFloating = false;
    if (hwndPalette) SetParent(hwndPalette, mainHwnd);
    if (hwndActionButtons[0]) SetParent(hwndActionButtons[0], mainHwnd);
    if (hwndBgButton) SetParent(hwndBgButton, mainHwnd);
    if (hwndSwapColors) SetParent(hwndSwapColors, mainHwnd);
    if (hwndPaletteFloat) ShowWindow(hwndPaletteFloat, SW_HIDE);
}

static void UndockPaletteInstruments(HWND mainHwnd) {
    EnsurePaletteFloatHost(mainHwnd);
    if (!hwndPaletteFloat) return;
    gPaletteFloating = true;

    if (hwndActionButtons[0]) SetParent(hwndActionButtons[0], hwndPaletteFloat);
    if (hwndBgButton) SetParent(hwndBgButton, hwndPaletteFloat);
    if (hwndSwapColors) SetParent(hwndSwapColors, hwndPaletteFloat);
    if (hwndPalette) SetParent(hwndPalette, hwndPaletteFloat);

    if (hwndActionButtons[0]) ShowWindow(hwndActionButtons[0], SW_SHOW);
    if (hwndBgButton) ShowWindow(hwndBgButton, SW_SHOW);
    if (hwndSwapColors) ShowWindow(hwndSwapColors, SW_SHOW);
    if (hwndPalette) ShowWindow(hwndPalette, SW_SHOW);

    const int w = TOOL_RAIL_WIDTH;
    const int h = PaletteFloatHeight(w);
    SetWindowPos(hwndPaletteFloat, HWND_TOPMOST,
        gPaletteFloatPos.x, gPaletteFloatPos.y, w, h,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    LayoutFloatPaletteContents();
    // Keep colors in sync after reparent.
    if (hwndPalette) AtelierPalette_SetColors(hwndPalette, penColor, backColor);
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

static void ApplyPanelVisibility() {
    const int railShow = gRailOpen ? SW_SHOW : SW_HIDE;
    for (HWND btn : hwndToolButtons) {
        if (btn) ShowWindow(btn, railShow);
    }

    if (!gPaletteFloating) {
        if (hwndActionButtons[0]) ShowWindow(hwndActionButtons[0], railShow);
        if (hwndBgButton) ShowWindow(hwndBgButton, railShow);
        if (hwndSwapColors) ShowWindow(hwndSwapColors, railShow);
        if (hwndPalette) ShowWindow(hwndPalette, railShow);
    }

    const int layerShow = gLayersOpen ? SW_SHOW : SW_HIDE;
    if (hwndLayerList) ShowWindow(hwndLayerList, layerShow);
    if (hwndLayerAdd) ShowWindow(hwndLayerAdd, layerShow);
    if (hwndLayerDel) ShowWindow(hwndLayerDel, layerShow);
    if (hwndLayerUp) ShowWindow(hwndLayerUp, layerShow);
    if (hwndLayerDown) ShowWindow(hwndLayerDown, layerShow);
    if (hwndLayerVisible) ShowWindow(hwndLayerVisible, layerShow);
    if (hwndLayerOpacity) ShowWindow(hwndLayerOpacity, layerShow);

    const int bottomShow = gBottomOpen ? SW_SHOW : SW_HIDE;
    if (hwndSizeLabel) ShowWindow(hwndSizeLabel, bottomShow);
    if (hwndSlider) ShowWindow(hwndSlider, bottomShow);
    if (hwndPenWidthBox) ShowWindow(hwndPenWidthBox, bottomShow);
    if (hwndOpacityLabel) ShowWindow(hwndOpacityLabel, bottomShow);
    if (hwndOpacitySlider) ShowWindow(hwndOpacitySlider, bottomShow);
    if (hwndOpacityBox) ShowWindow(hwndOpacityBox, bottomShow);

    if (hwndToggleRail) ShowWindow(hwndToggleRail, SW_SHOW);
    if (hwndToggleLayers) ShowWindow(hwndToggleLayers, SW_SHOW);
}

static void SetRailOpen(HWND hwnd, bool open) {
    if (gRailOpen == open) return;
    gRailOpen = open;
    if (!gRailOpen && hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
        CloseShapeFlyout();
    }
    if (gRailOpen) {
        DockPaletteInstruments(hwnd);
        if (hwndActionButtons[0]) ShowWindow(hwndActionButtons[0], SW_SHOW);
        if (hwndBgButton) ShowWindow(hwndBgButton, SW_SHOW);
        if (hwndSwapColors) ShowWindow(hwndSwapColors, SW_SHOW);
        if (hwndPalette) ShowWindow(hwndPalette, SW_SHOW);
    } else {
        for (HWND btn : hwndToolButtons) {
            if (btn) ShowWindow(btn, SW_HIDE);
        }
        UndockPaletteInstruments(hwnd);
    }
    ApplyPanelVisibility();
    UpdateButtonTooltip(hwnd, hwndToggleRail, gRailOpen ? "Hide tools (Tab)" : "Show tools (Tab)");
    DestroyChromeCache();
    LayoutViewport(hwnd);
    if (hwndToggleRail) InvalidateRect(hwndToggleRail, NULL, FALSE);
    InvalidateRect(hwnd, NULL, FALSE);
    SetTimer(hwnd, IDT_CHROME_REBUILD, 1, NULL);
}

static void SetLayersOpen(HWND hwnd, bool open) {
    if (gLayersOpen == open) return;
    gLayersOpen = open;
    ApplyPanelVisibility();
    UpdateButtonTooltip(hwnd, hwndToggleLayers, gLayersOpen ? "Hide layers (F9)" : "Show layers (F9)");
    DestroyChromeCache();
    LayoutViewport(hwnd);
    if (hwndToggleLayers) InvalidateRect(hwndToggleLayers, NULL, FALSE);
    InvalidateRect(hwnd, NULL, FALSE);
    SetTimer(hwnd, IDT_CHROME_REBUILD, 1, NULL);
}

static void SetBottomOpen(HWND hwnd, bool open) {
    if (gBottomOpen == open) return;
    gBottomOpen = open;
    ApplyPanelVisibility();
    DestroyChromeCache();
    LayoutViewport(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    SetTimer(hwnd, IDT_CHROME_REBUILD, 1, NULL);
}

static void LayoutViewport(HWND hwnd) {
    if (!hwndViewport) return;

    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);

    const int x = chrome.railW + WELL_FRAME;
    const int y = chrome.topH + WELL_FRAME;
    const int w = MaxInt(1, client.right - client.left - chrome.railW - chrome.layerW - WELL_FRAME * 2);
    int h = client.bottom - client.top - chrome.topH - chrome.bottomH - chrome.statusH - WELL_FRAME * 2;
    if (h < 1) h = 1;

    MoveWindow(hwndViewport, x, y, w, h, TRUE);
    LayoutLayerPanel(hwnd);
    LayoutChromeControls(hwnd);
    UpdateScrollBars();
}

static void LayoutLayerPanel(HWND hwnd) {
    if (!hwndLayerList && !hwndToggleLayers) return;

    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int panelX = client.right - chrome.layerW;
    const int panelY = chrome.topH;
    const int panelH = MaxInt(1, client.bottom - client.top - chrome.topH - chrome.bottomH - chrome.statusH);

    if (hwndToggleLayers) {
        const int tb = 22;
        if (gLayersOpen) {
            MoveWindow(hwndToggleLayers, panelX + chrome.layerW - tb - 6, panelY + 4, tb, tb, TRUE);
        } else {
            MoveWindow(hwndToggleLayers, panelX + (chrome.layerW - tb) / 2, panelY + 8, tb, tb, TRUE);
        }
    }

    if (!gLayersOpen) return;

    const int btn = 28;
    const int pad = 10;
    int y = panelY + 22; // room for LAYERS caption

    if (hwndLayerAdd) MoveWindow(hwndLayerAdd, panelX + pad, y, btn, btn, TRUE);
    if (hwndLayerDel) MoveWindow(hwndLayerDel, panelX + pad + btn + 4, y, btn, btn, TRUE);
    if (hwndLayerUp) MoveWindow(hwndLayerUp, panelX + pad + (btn + 4) * 2, y, btn, btn, TRUE);
    if (hwndLayerDown) MoveWindow(hwndLayerDown, panelX + pad + (btn + 4) * 3, y, btn, btn, TRUE);
    y += btn + 8;

    if (hwndLayerVisible) {
        MoveWindow(hwndLayerVisible, panelX + pad, y, chrome.layerW - pad * 2, 22, TRUE);
    }
    y += 28;

    // Reserve a quiet motif band above the opacity slider so fresco artwork reads.
    const int opacityH = 28;
    const int motifBand = 68;
    const int listH = MaxInt(60, panelH - (y - panelY) - opacityH - motifBand - 14);
    if (hwndLayerList) {
        MoveWindow(hwndLayerList, panelX + pad, y, chrome.layerW - pad * 2, listH, TRUE);
    }

    if (hwndLayerOpacity) {
        const int opacityY = panelY + panelH - opacityH - 8;
        MoveWindow(hwndLayerOpacity, panelX + pad, opacityY, chrome.layerW - pad * 2, opacityH, TRUE);
    }
}

static void LayoutChromeControls(HWND hwnd) {
    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int right = client.right - client.left;

    // Top bar: brand | File…Help menus | undo cluster | doc actions
    const int topY = (chrome.topH - ICON_BTN) / 2;
    const int menuY = (chrome.topH - 22) / 2;
    int x = BRAND_STRIP_W + 4;
    for (int i = 0; i < 6; ++i) {
        if (hwndMenuButtons[i]) {
            MoveWindow(hwndMenuButtons[i], x, menuY, MENU_BTN_W, 22, TRUE);
            x += MENU_BTN_W + 2;
        }
    }
    x += 10;
    if (hwndActionButtons[2]) MoveWindow(hwndActionButtons[2], x, topY, ICON_BTN, ICON_BTN, TRUE); // Undo
    x += ICON_BTN + 4;
    if (hwndActionButtons[3]) MoveWindow(hwndActionButtons[3], x, topY, ICON_BTN, ICON_BTN, TRUE); // Redo
    x += ICON_BTN + 8;
    if (hwndActionButtons[4]) MoveWindow(hwndActionButtons[4], x, topY, ICON_BTN, ICON_BTN, TRUE); // Clear

    x = right - (IsRunningUnderWine() ? 0 : CAPTION_BTN_W * 3) - (ICON_BTN + 4) * 3 - 12;
    if (hwndActionButtons[1]) MoveWindow(hwndActionButtons[1], x, topY, ICON_BTN, ICON_BTN, TRUE); // New
    x += ICON_BTN + 4;
    if (hwndActionButtons[6]) MoveWindow(hwndActionButtons[6], x, topY, ICON_BTN, ICON_BTN, TRUE); // Open
    x += ICON_BTN + 4;
    if (hwndActionButtons[5]) MoveWindow(hwndActionButtons[5], x, topY, ICON_BTN, ICON_BTN, TRUE); // Save

    LayoutCaptionButtons(hwnd);

    if (hwndBrand) {
        MoveWindow(hwndBrand, 0, 0, BRAND_STRIP_W, chrome.topH, TRUE);
    }

    // Panel collapse controls on panel edges.
    const int tb = 20;
    if (hwndToggleRail) {
        if (gRailOpen) {
            MoveWindow(hwndToggleRail, chrome.railW - tb - 4, chrome.topH + 4, tb, tb, TRUE);
        } else {
            MoveWindow(hwndToggleRail, (chrome.railW - tb) / 2, chrome.topH + 6, tb, tb, TRUE);
        }
    }

    // Bottom inspectors (hidden when gBottomOpen is false via ShowWindow).
    if (gBottomOpen) {
        const int bottomY = client.bottom - chrome.statusH - chrome.bottomH;
        const int bottomX = chrome.railW + 10;
        if (hwndSizeLabel) MoveWindow(hwndSizeLabel, bottomX, bottomY + 8, 34, 18, TRUE);
        if (hwndSlider) MoveWindow(hwndSlider, bottomX + 36, bottomY + 2, 140, 28, TRUE);
        if (hwndPenWidthBox) MoveWindow(hwndPenWidthBox, bottomX + 184, bottomY + 6, 40, 22, TRUE);
        if (hwndOpacityLabel) MoveWindow(hwndOpacityLabel, bottomX + 236, bottomY + 8, 54, 18, TRUE);
        if (hwndOpacitySlider) MoveWindow(hwndOpacitySlider, bottomX + 290, bottomY + 2, 140, 28, TRUE);
        if (hwndOpacityBox) MoveWindow(hwndOpacityBox, bottomX + 438, bottomY + 6, 40, 22, TRUE);
    }

    if (!gRailOpen) {
        (void)hwnd;
        return;
    }

    // Docked tool rail + palette (skipped while floating).
    const int railX = 8;
    int y = chrome.topH + 26;
    const int order[kToolButtonCount] = { 0, 1, 2, 3, 4, 5 };
    for (int i = 0; i < kToolButtonCount; ++i) {
        const int idx = order[i];
        if (hwndToolButtons[idx]) {
            MoveWindow(hwndToolButtons[idx], railX, y, ICON_BTN, ICON_BTN, TRUE);
        }
        y += ICON_BTN + 5;
        if (i == 2 || i == 3) y += 4;
    }

    if (!gPaletteFloating) {
        y += 6;
        if (hwndBgButton) MoveWindow(hwndBgButton, railX + 14, y + 10, 20, 20, TRUE);
        if (hwndActionButtons[0]) MoveWindow(hwndActionButtons[0], railX, y, 22, 22, TRUE);
        if (hwndSwapColors) MoveWindow(hwndSwapColors, railX + 28, y - 2, 18, 18, TRUE);
        y += 36;

        if (hwndPalette) {
            const int palW = chrome.railW - 10;
            const int palH = AtelierPalette_IdealHeight(palW);
            const int maxH = (client.bottom - chrome.statusH - chrome.bottomH) - y - 6;
            const int h = (palH < maxH) ? palH : (maxH > 72 ? maxH : 72);
            MoveWindow(hwndPalette, 5, y, palW, h, TRUE);
        }
    }

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
    if (!out || out->GetLastStatus() != Ok) {
        delete out;
        return nullptr;
    }
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

static void DoCopy(HWND hwnd) {
    Bitmap* shot = CaptureSelectionPixels();
    if (!shot) return;
    CopyBitmapToWinClipboard(shot);
    SetInternalClipboard(shot); // takes ownership
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

static int FrameBorderX() {
    return GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

static int FrameBorderY() {
    return GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYPADDEDBORDER);
}

static void EnableCustomTitleBar(HWND hwnd) {
    EnableDarkTitleBar(hwnd);
    // Wine still draws a WM caption; expanding the client under it hides our chrome.
    if (IsRunningUnderWine()) return;
    // Keep DWM borders; caption is drawn in-client (see WM_NCCALCSIZE).
    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void LayoutCaptionButtons(HWND hwnd) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int w = CAPTION_BTN_W;
    const int h = CAPTION_BTN_H;
    const int y = 0; // flush to top of client chrome
    if (hwndCaptionClose) {
        MoveWindow(hwndCaptionClose, rc.right - w, y, w, h, TRUE);
    }
    if (hwndCaptionMax) {
        MoveWindow(hwndCaptionMax, rc.right - w * 2, y, w, h, TRUE);
    }
    if (hwndCaptionMin) {
        MoveWindow(hwndCaptionMin, rc.right - w * 3, y, w, h, TRUE);
    }
}

static void SetCaptionHot(HWND btn) {
    if (gCaptionHot == btn) return;
    HWND prev = gCaptionHot;
    gCaptionHot = btn;
    if (prev) InvalidateRect(prev, nullptr, FALSE);
    if (btn) InvalidateRect(btn, nullptr, FALSE);
}

static void PaintOneCaptionButton(HDC hdc, HWND btn, int id) {
    RECT r = {};
    GetClientRect(btn, &r);
    const bool hot = (gCaptionHot == btn);
    const bool maximized = IsZoomed(GetParent(btn)) != FALSE;

    COLORREF bg = gTheme.chromeBg;
    if (hot) {
        bg = (id == IDC_CAPTION_CLOSE) ? RGB(196, 43, 28) : gTheme.chromeElevated;
    }
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &r, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1,
        (hot && id == IDC_CAPTION_CLOSE) ? RGB(255, 255, 255) : gTheme.ink);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const int cx = (r.left + r.right) / 2;
    const int cy = (r.top + r.bottom) / 2;
    if (id == IDC_CAPTION_MIN) {
        MoveToEx(hdc, cx - 5, cy, nullptr);
        LineTo(hdc, cx + 6, cy);
    } else if (id == IDC_CAPTION_MAX) {
        if (maximized) {
            Rectangle(hdc, cx - 3, cy - 5, cx + 5, cy + 3);
            Rectangle(hdc, cx - 5, cy - 3, cx + 3, cy + 5);
        } else {
            Rectangle(hdc, cx - 5, cy - 5, cx + 6, cy + 6);
        }
    } else if (id == IDC_CAPTION_CLOSE) {
        MoveToEx(hdc, cx - 5, cy - 5, nullptr);
        LineTo(hdc, cx + 6, cy + 6);
        MoveToEx(hdc, cx + 5, cy - 5, nullptr);
        LineTo(hdc, cx - 6, cy + 6);
    }

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static LRESULT CALLBACK CaptionBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintOneCaptionButton(hdc, hwnd, GetDlgCtrlID(hwnd));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        SetCaptionHot(hwnd);
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (gCaptionHot == hwnd) SetCaptionHot(nullptr);
        return 0;
    case WM_LBUTTONUP: {
        HWND parent = GetParent(hwnd);
        if (parent) {
            SendMessageA(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static bool RegisterCaptionBtnClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = CaptionBtnProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppCaptionBtn";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void CreateCaptionButtons(HWND hwnd) {
    if (IsRunningUnderWine()) return;
    HINSTANCE inst = GetModuleHandle(nullptr);
    auto make = [&](int id) -> HWND {
        return CreateWindowExA(0, "SimpleDrawingAppCaptionBtn", "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, CAPTION_BTN_W, CAPTION_BTN_H,
            hwnd, (HMENU)(INT_PTR)id, inst, nullptr);
    };
    hwndCaptionMin = make(IDC_CAPTION_MIN);
    hwndCaptionMax = make(IDC_CAPTION_MAX);
    hwndCaptionClose = make(IDC_CAPTION_CLOSE);
    LayoutCaptionButtons(hwnd);
}

static bool PointOverTopBarControl(HWND hwnd, POINT ptClient) {
    HWND hit = ChildWindowFromPointEx(hwnd, ptClient, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
    if (!hit || hit == hwnd) return false;
    if (hit == hwndBrand) return false; // decorative — allow window drag
    return true;
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
            if (!gLayers.ReplaceWithImage(loaded)) {
                delete loaded;
                MessageBoxA(hwnd, "Failed to create document from image.", "Error", MB_OK | MB_ICONERROR);
                return;
            }
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
    // Keep current document size; reset to Background + Layer 1 (active).
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
    if (!strokeLayer || strokeLayer->GetLastStatus() != Ok) {
        DestroyStrokeLayer();
        return;
    }
    strokeGraphics = Graphics::FromImage(strokeLayer);
    if (!strokeGraphics || strokeGraphics->GetLastStatus() != Ok) {
        DestroyStrokeLayer();
        return;
    }
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    strokeGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
    strokeGraphics->SetCompositingMode(CompositingModeSourceOver);
}

static void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
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

static void RefreshShapePreviewIfDrawing() {
    if (!isDrawing || !strokeGraphics || !IsShapeTool(currentTool)) return;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    RedrawShapePreview(lastPoint.x, lastPoint.y, shift);
    InvalidateCanvas();
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

static void CommitStrokeLayer() {
    Graphics* ag = gLayers.ActiveGraphics();
    if (!strokeLayer || !ag) {
        DestroyStrokeLayer();
        return;
    }

    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    if (eraseTransparent) {
        ApplyTransparentEraseMask(gLayers.ActiveBitmap(), strokeLayer);
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
                    Bitmap* preview = CreateErasePreviewComposite(strokeLayer);
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
                            strokeLayer,
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

static void UpdateButtonTooltip(HWND parent, HWND btn, const char* text) {
    if (!hwndTooltip || !btn || !text) return;
    TOOLINFOA ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = (UINT_PTR)btn;
    ti.lpszText = const_cast<char*>(text);
    SendMessageA(hwndTooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
}

static void SyncShapeFlyoutChecks() {
    const int shapeIds[6] = {
        IDC_SHAPE_RECT, IDC_SHAPE_ELLIPSE, IDC_SHAPE_TRIANGLE,
        IDC_SHAPE_STAR, IDC_SHAPE_DIAMOND, IDC_SHAPE_ROUNDRECT
    };
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

static void CloseShapeFlyout() {
    if (hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
        ShowWindow(hwndShapeFlyout, SW_HIDE);
    }
}

static LRESULT CALLBACK ShapeFlyoutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            // Defer hide so a click on the Shapes rail button can toggle cleanly.
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
        // Separator under shape grid.
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

static bool RegisterShapeFlyoutClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = ShapeFlyoutProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppShapeFlyout";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void EnsureShapeFlyout(HWND owner) {
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

static void OpenShapeFlyout(HWND parent) {
    if (!parent) return;
    EnsureShapeFlyout(parent);
    if (!hwndShapeFlyout || !hwndToolButtons[static_cast<int>(DrawTool::Shape)]) return;

    RECT br = {};
    GetWindowRect(hwndToolButtons[static_cast<int>(DrawTool::Shape)], &br);
    const int w = 156;
    const int h = 128;
    SetWindowPos(hwndShapeFlyout, HWND_TOPMOST, br.right + 6, br.top - 4, w, h,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    // Activate so click-away dismiss works.
    SetForegroundWindow(hwndShapeFlyout);
    SyncShapeFlyoutChecks();
}

static void CreateToolbar(HWND hwnd) {
    hwndTooltip = CreateWindowExA(0, TOOLTIPS_CLASSA, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
    if (hwndTooltip) {
        SendMessageA(hwndTooltip, TTM_SETMAXTIPWIDTH, 0, 240);
    }

    // Tools (left rail): Pen Eraser Fill | Select | Line Shapes
    hwndToolButtons[0] = CreateIconButton(hwnd, IDC_TOOL_PEN, "Pen\r\nShortcut: B", true);
    hwndToolButtons[1] = CreateIconButton(hwnd, IDC_TOOL_ERASER, "Eraser\r\nShortcut: E", true);
    hwndToolButtons[2] = CreateIconButton(hwnd, IDC_TOOL_FILL, "Fill\r\nShortcut: G", true);
    hwndToolButtons[3] = CreateIconButton(hwnd, IDC_TOOL_SELECT, "Select\r\nShortcut: M", true);
    hwndToolButtons[4] = CreateIconButton(hwnd, IDC_TOOL_LINE, "Line\r\nShortcut: L", true);
    hwndToolButtons[5] = CreateIconButton(hwnd, IDC_TOOL_SHAPES, "Shapes…\r\nShortcut: U", true);

    // FG / BG chips (Photoshop-style). FG is IDC_COLOR_BUTTON.
    hwndActionButtons[0] = CreateIconButton(hwnd, IDC_COLOR_BUTTON, "Foreground color\r\nClick to pick", false);
    hwndBgButton = CreateIconButton(hwnd, IDC_BG_BUTTON, "Background color (shape fill)\r\nClick to pick", false);
    hwndSwapColors = CreateIconButton(hwnd, IDC_SWAP_COLORS, "Swap FG/BG\r\nShortcut: X", false);

    AtelierPalette_SetTheme(&gTheme);
    hwndPalette = AtelierPalette_Create(hwnd, 0, 0, TOOL_RAIL_WIDTH - 12, 160);
    if (hwndPalette) {
        AtelierPalette_SetColors(hwndPalette, penColor, backColor);
    }

    hwndActionButtons[1] = CreateIconButton(hwnd, IDC_NEW_BUTTON, "New (Ctrl+N)", false);
    hwndActionButtons[2] = CreateIconButton(hwnd, IDC_UNDO_BUTTON, "Undo (Ctrl+Z)", false);
    hwndActionButtons[3] = CreateIconButton(hwnd, IDC_REDO_BUTTON, "Redo (Ctrl+Y)", false);
    hwndActionButtons[4] = CreateIconButton(hwnd, IDC_CLEAR_BUTTON, "Clear canvas", false);
    hwndActionButtons[5] = CreateIconButton(hwnd, IDC_SAVE_BUTTON, "Save (Ctrl+S)", false);
    hwndActionButtons[6] = CreateIconButton(hwnd, IDC_LOAD_BUTTON, "Open (Ctrl+O)", false);

    hwndToggleRail = CreateIconButton(hwnd, IDC_TOGGLE_RAIL, "Hide tools\r\nShortcut: Tab", false);
    hwndToggleLayers = CreateIconButton(hwnd, IDC_TOGGLE_LAYERS, "Hide layers\r\nShortcut: F9", false);

    static const char* kMenuLabels[6] = { "File", "Edit", "Image", "View", "Tools", "Help" };
    static const int kMenuIds[6] = {
        IDC_MENU_FILE, IDC_MENU_EDIT, IDC_MENU_IMAGE,
        IDC_MENU_VIEW, IDC_MENU_TOOLS, IDC_MENU_HELP
    };
    for (int i = 0; i < 6; ++i) {
        hwndMenuButtons[i] = CreateWindowA("BUTTON", kMenuLabels[i],
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            0, 0, MENU_BTN_W, 22,
            hwnd, (HMENU)(INT_PTR)kMenuIds[i], GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndMenuButtons[i]);
    }

    // Bottom inspectors
    hwndSizeLabel = CreateWindowA("STATIC", "Size", WS_CHILD | WS_VISIBLE,
        0, 0, 34, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndSizeLabel);

    hwndSlider = AtelierSlider_Create(hwnd, 0, 0, 150, 28, NULL);
    SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
    SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);

    hwndPenWidthBox = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
        0, 0, 40, 22, hwnd, (HMENU)(INT_PTR)IDC_WIDTH_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndPenWidthBox);

    hwndOpacityLabel = CreateWindowA("STATIC", "Opacity", WS_CHILD | WS_VISIBLE,
        0, 0, 54, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndOpacityLabel);

    hwndOpacitySlider = AtelierSlider_Create(hwnd, 0, 0, 150, 28, NULL);
    SendMessage(hwndOpacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, penOpacity);

    hwndOpacityBox = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
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
    const Color grain(26, 120, 92, 58);

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
    DrawFrescoArtwork(g, railR, true); // Renaissance×futurism sketch watermark

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
    DrawFrescoArtwork(g, panelR, false); // armillary manuscript watermark

    {
        LinearGradientBrush wash(
            PointF(0.0f, 0.0f), PointF(160.0f, 0.0f),
            Color(40, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)),
            Color(0, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)));
        g.FillRectangle(&wash, RectF(0.0f, 0.0f, 160.0f, static_cast<REAL>(chrome.topH)));
    }

    Pen rule(Color(255, GetRValue(gTheme.chromeLine), GetGValue(gTheme.chromeLine), GetBValue(gTheme.chromeLine)), 1.15f);
    g.DrawLine(&rule, 0.0f, static_cast<REAL>(chrome.topH) - 0.5f, static_cast<REAL>(width), static_cast<REAL>(chrome.topH) - 0.5f);
    g.DrawLine(&rule, static_cast<REAL>(chrome.railW) - 0.5f, static_cast<REAL>(chrome.topH),
        static_cast<REAL>(chrome.railW) - 0.5f, static_cast<REAL>(height - chrome.statusH));
    g.DrawLine(&rule, panelR.X, static_cast<REAL>(chrome.topH), panelR.X, static_cast<REAL>(height - chrome.statusH));
    g.DrawLine(&rule, static_cast<REAL>(chrome.railW), bottomR.Y, panelR.X, bottomR.Y);

    const Color bronze(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    const Color rim(255, GetRValue(gTheme.wellRim), GetGValue(gTheme.wellRim), GetBValue(gTheme.wellRim));
    const Color gilt(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));

    // Canvas mount: thin hairline + Renaissance corner/edge line-work.
    RectF well(
        static_cast<REAL>(chrome.railW),
        static_cast<REAL>(chrome.topH),
        static_cast<REAL>(width - chrome.railW - chrome.layerW),
        static_cast<REAL>(height - chrome.topH - chrome.bottomH - chrome.statusH));
    if (well.Width > 8 && well.Height > 8) {
        DrawCanvasWell(g, well, rim, gilt);
    }

    // Thin HUD plates on instrument zones.
    DrawHudCornerTicks(g, topR, bronze, 10.0f);
    DrawHudCornerTicks(g, railR, bronze, 8.0f);
    DrawHudCornerTicks(g, panelR, bronze, 8.0f);
    DrawHudCornerTicks(g, bottomR, bronze, 8.0f);

    // Section captions baked into fresco (manuscript × HUD).
    HDC hdcCaps = g.GetHDC();
    if (hdcCaps) {
        HFONT caption = gUiFont;
        HGDIOBJ old = caption ? SelectObject(hdcCaps, caption) : nullptr;
        SetBkMode(hdcCaps, TRANSPARENT);
        SetTextColor(hdcCaps, gTheme.accentDeep);
        if (chrome.layerW > 64) {
            TextOutA(hdcCaps, width - chrome.layerW + 10, chrome.topH + 4, "LAYERS", 6);
        }
        if (chrome.railW > 40) {
            TextOutA(hdcCaps, chrome.railW + 14, height - chrome.statusH - chrome.bottomH + 6, "INSTRUMENT", 10);
        }
        if (old) SelectObject(hdcCaps, old);
        g.ReleaseHDC(hdcCaps);
    }
}

static void EnsureChromeCache(int width, int height, const ChromeLayout& chrome) {
    if (width < 1 || height < 1) return;
    if (gChromeCache
        && gChromeCacheW == width
        && gChromeCacheH == height
        && gChromeCacheStatusH == chrome.statusH
        && gChromeCacheRailW == chrome.railW
        && gChromeCacheLayerW == chrome.layerW) {
        return;
    }

    DestroyChromeCache();
    gChromeCache = new Bitmap(width, height, PixelFormat32bppPARGB);
    if (!gChromeCache || gChromeCache->GetLastStatus() != Ok) {
        delete gChromeCache;
        gChromeCache = nullptr;
        return;
    }
    gChromeCacheW = width;
    gChromeCacheH = height;
    gChromeCacheStatusH = chrome.statusH;
    gChromeCacheRailW = chrome.railW;
    gChromeCacheLayerW = chrome.layerW;

    Graphics g(gChromeCache);
    PaintChromeInto(g, width, height, chrome);

    // Bake wordmark into the same bitmap (stable; no per-frame TextOut flicker).
    HDC hdc = g.GetHDC();
    if (hdc) {
        HFONT brand = gBrandFont ? gBrandFont : gUiFont;
        HGDIOBJ oldFont = brand ? SelectObject(hdc, brand) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, gTheme.ink);
        TextOutA(hdc, 48, (chrome.topH - 20) / 2, "ATELIER", 7);
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
    // Brand mark is owned by hwndBrand (WS_CLIPCHILDREN excludes it from parent paint).
}

static void DrawToolbarBackground(HDC hdc, HWND hwnd, const RECT& client) {
    const ChromeLayout chrome = GetChromeLayout(hwnd);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width < 1 || height < 1) return;

    const bool cacheReady = gChromeCache
        && gChromeCacheW == width
        && gChromeCacheH == height
        && gChromeCacheStatusH == chrome.statusH
        && gChromeCacheRailW == chrome.railW
        && gChromeCacheLayerW == chrome.layerW;
    if (!cacheReady) {
        DrawToolbarBackgroundCheap(hdc, client, chrome);
    } else {
        Graphics g(hdc);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.DrawImage(gChromeCache, 0, 0, width, height);
    }
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

    hwndLayerList = CreateWindowExA(0, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)IDC_LAYER_LIST, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndLayerList);

    hwndLayerOpacity = AtelierSlider_Create(hwnd, 0, 0, 100, 28, (HMENU)(INT_PTR)IDC_LAYER_OPACITY);
    SendMessage(hwndLayerOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndLayerOpacity, TBM_SETPOS, TRUE, 100);

    RefreshLayerList();
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

        CreateToolbar(hwnd);
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
        LayoutChromeControls(hwnd);
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
                opts.frescoCache = gChromeCache;
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
            SetTimer(hwnd, IDT_CHROME_REBUILD, 60, NULL);
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
            InvalidateColorChips();
            break;
        }
        case IDC_NEW_BUTTON:
        case IDM_NEW:
            DoNew(hwnd);
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
        ClearSelection(false);
        delete gClipboardBmp;
        gClipboardBmp = nullptr;
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
