#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "AppMetrics.h"
#include "AtelierRaii.h"
#include "DrawingTools.h"
#include "LayerHistory.h"
#include "LayerStack.h"

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

// Document / canvas / tool model (not HWND chrome).
extern AppTheme gTheme;
extern LayerStack gLayers;
extern LayerHistory gHistory;

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
extern int brushFlow;
extern int brushHardness;
extern bool penPressureEnabled;
extern float lastPenPressure;
extern DrawTool currentTool;
extern ShapeKind currentShape;
extern ShapePaintMode shapePaintMode;
extern bool documentDirty;

// Wide document paths (Unicode-safe open/save/recent).
extern wchar_t gDocumentPath[MAX_PATH];
extern wchar_t gLastBrowseDir[MAX_PATH];
extern wchar_t gLastDocumentPath[MAX_PATH];
constexpr int kMaxRecentDocuments = 5;
extern wchar_t gRecentDocuments[kMaxRecentDocuments][MAX_PATH];
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
