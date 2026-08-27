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
extern DrawTool currentTool;
extern ShapeKind currentShape;
extern ShapePaintMode shapePaintMode;
extern bool documentDirty;
extern char gDocumentPath[MAX_PATH];
extern char gLastBrowseDir[MAX_PATH];
extern char gLastDocumentPath[MAX_PATH];

extern int gCanvasDlgWidth;
extern int gCanvasDlgHeight;

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
