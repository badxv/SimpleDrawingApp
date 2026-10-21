#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "AppMetrics.h"
#include "AtelierRaii.h"
#include "DrawingTools.h"
#include "UiChrome.h"

// HWND chrome, fonts, layout flags, and decorative caches.
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
