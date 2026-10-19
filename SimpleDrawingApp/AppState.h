#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "AppMetrics.h"
#include "AtelierRaii.h"
#include "DrawingTools.h"
#include "LayerHistory.h"
#include "LayerStack.h"
#include "UiChrome.h"

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
    Atelier::GdiplusBitmapPtr floatBmp;
};

extern AppTheme gTheme;
extern LayerStack gLayers;
extern LayerHistory gHistory;

extern HFONT gUiFont;
extern HFONT gBrandFont;
extern HBRUSH gChromeBrush;
extern HBRUSH gChromeDeepBrush;
extern HBRUSH gChromeElevatedBrush;
extern HACCEL gAccel;

extern HWND hwndTooltip;
extern HWND hwndBrand;
extern HWND hwndViewport;
extern HWND hwndScrollH;
extern HWND hwndScrollV;
extern HWND hwndScrollCorner;
extern HWND hwndSlider;
extern HWND hwndPenWidthBox;
extern HWND hwndOpacitySlider;
extern HWND hwndOpacityBox;
extern HWND hwndSizeLabel;
extern HWND hwndOpacityLabel;
extern HWND hwndStatus;
extern HWND hwndLayerList;
extern HWND hwndLayerAdd;
extern HWND hwndLayerDel;
extern HWND hwndLayerUp;
extern HWND hwndLayerDown;
extern HWND hwndLayerDup;
extern HWND hwndLayerVisible;
extern HWND hwndLayerOpacity;
extern HWND hwndToolButtons[kToolButtonCount];
extern HWND hwndPalette;
extern HWND hwndActionButtons[7];
extern HWND hwndBgButton;
extern HWND hwndSwapColors;
extern HWND hwndShapeFlyout;
extern HWND hwndShapeButtons[6];
extern HWND hwndShapeModeButtons[3];
extern HWND hwndBrushFlyout;
extern HWND hwndBrushSubFlyout;
extern HWND hwndBrushPresetButtons[9];
extern HWND hwndBrushRowButtons[40];
extern HWND hwndBrushActionButtons[3];
extern HWND hwndBrushSubButtons[3];
extern HWND hwndBrushPageButtons[2];
extern HWND hwndBrushSubSlider;
extern HWND hwndBrushPreview;
extern HWND hwndToggleRail;
extern HWND hwndToggleLayers;
extern HWND hwndPaletteFloat;
extern HWND hwndMenuButtons[6];
extern HWND hwndCaptionMin;
extern HWND hwndCaptionMax;
extern HWND hwndCaptionClose;
extern HWND gCaptionHot;
extern HMENU gAppMenu;

extern float gUiPulse;
extern float gUiCompassAngle;
extern float gToolFlash;
extern float gIdlePhase;
extern float gSelAntOffset;
extern bool gUiSizing;

extern Atelier::GdiplusBitmapPtr gChromeCache;
extern int gChromeCacheW;
extern int gChromeCacheH;
extern int gChromeCacheStatusH;
extern int gChromeCacheRailW;
extern int gChromeCacheLayerW;
extern Atelier::GdiplusBitmapPtr gBrandStrip;
extern Atelier::WinBitmapHandle gBrandStripHbmp;
extern int gBrandStripH;

extern bool gRailOpen;
extern bool gLayersOpen;
extern bool gBottomOpen;
extern bool gPaletteFloating;
extern POINT gPaletteFloatPos;
extern bool gFloatDragging;
extern POINT gFloatDragHot;

extern POINT lastPoint;
extern POINT shapeStart;
extern bool isDrawing;
extern bool strokeAwaitingEcho; // hold strokeLayer until temporal echo queue drains
extern bool suppressEditNotify;
extern bool suppressLayerNotify;

extern int docWidth;
extern int docHeight;
extern int scrollX;
extern int scrollY;
extern float zoomFactor;

extern SelectionState gSel;
extern Atelier::GdiplusBitmapPtr gClipboardBmp;
extern Atelier::GdiplusBitmapPtr compositeCache;
extern bool compositeDirty;
extern Atelier::GdiplusBitmapPtr strokeLayer;
extern Atelier::GdiplusGraphicsPtr strokeGraphics;

extern COLORREF penColor;
extern COLORREF backColor;
extern int penWidth;
extern int penOpacity;
extern int brushFlow;
extern int brushHardness;
extern int brushScatter; // 0..100 — stamp position jitter
extern int brushAngleJitter; // 0..100 — stamp rotation jitter
extern int brushTipAngle;    // 0..180 — fixed tip orientation offset (degrees)
extern int brushSizeJitter;  // 0..100 — stamp diameter jitter
extern int brushOpacityJitter; // 0..100 — stamp opacity jitter
extern int brushSmoothing;     // 0..100 — freehand stroke stabilization
extern int brushStampCount;    // 1..8 — stamps per spacing step
extern int brushRoundness;     // 1..100 — tip height/width (100 = round)
extern int brushMinDiameter;   // 0..100 — floor for size jitter (% of tip)
extern int brushWetEdges;      // 0..100 — watercolor edge buildup
extern int brushRoundnessJitter; // 0..100 — tip roundness jitter per stamp
extern int brushFlipX;           // 0..100 — chance to mirror tip horizontally
extern int brushFlipY;           // 0..100 — chance to mirror tip vertically
extern int brushFlowJitter;      // 0..100 — stamp flow/alpha jitter
extern int brushColorJitter;     // 0..100 — blend toward backColor per stamp
extern int brushHueJitter;       // 0..100 — hue shift range per stamp
extern int brushSatJitter;       // 0..100 — saturation jitter per stamp
extern int brushBrightJitter;    // 0..100 — brightness jitter per stamp
extern bool brushExperimental;   // opt-in Atelier Experimental section (default off)
extern int brushEchoDelay;       // 0..100 — echo delay (maps to ms); 0 = off
extern int brushEchoFeedback;    // 0..100 — echo decay / hop count
extern int brushPing;            // 0..100 — sonar ping ring on stroke start
extern int brushBiolum;          // 0..100 — proximity reveal veil radius
extern int brushHeat;            // 0..100 — visit heat ledger strength / overlay
extern int brushClockPhase;      // 0..100 — tip angle driven by clock seconds
extern int brushCompassLock;     // 0..100 — blend tip angle toward UI compass
extern int brushAfterimage;      // 0..100 — inverted ghost stamps after erase
extern int brushAging;           // 0..100 — wet→dry pigment over stroke duration
extern int brushWind;            // 0..100 — scatter bias toward nearest canvas edge
extern int brushTide;            // 0..100 — sinusoidal size breathe along stroke
extern int brushBloom;           // 0..100 — soft halo stamp around each mark
extern int brushMirage;          // 0..100 — delayed mirror ghost across canvas midline
extern int brushRime;            // 0..100 — cool frost fringe along stroke edges
extern int brushOrbit;           // 0..100 — satellite stamps on a rotating ring
extern int brushEmber;           // 0..100 — warm sparks that drift upward over time
extern bool penPressureEnabled;
extern float lastPenPressure;
extern DrawTool currentTool;
extern ShapeKind currentShape;
extern ShapePaintMode shapePaintMode;
extern bool documentDirty;
extern char gDocumentPath[MAX_PATH];
extern char gLastBrowseDir[MAX_PATH];
extern char gLastDocumentPath[MAX_PATH];
constexpr int kMaxRecentDocuments = 5;
extern char gRecentDocuments[kMaxRecentDocuments][MAX_PATH];
extern int gRecentDocumentCount;

extern int gCanvasDlgWidth;
extern int gCanvasDlgHeight;

// Document coords under cursor in the viewport; -1 when not hovering the canvas.
extern int gStatusHoverDocX;
extern int gStatusHoverDocY;
extern int gGridSpacing; // 8 / 16 / 32 / 64 document pixels


inline BYTE OpacityToAlpha() {
    int pct = penOpacity;
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    return static_cast<BYTE>((pct * 255 + 50) / 100);
}

inline ShapePaintMode EffectiveShapePaintMode() {
    // Hold Alt → fill only; hold Ctrl (or Alt+Ctrl) → stroke + fill.
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) return ShapePaintMode::Both;
    if (alt) return ShapePaintMode::Fill;
    return shapePaintMode;
}

#include "AppCanvas.h"
#include "AppStroke.h"
#include "AppShell.h"
