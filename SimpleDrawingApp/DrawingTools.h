#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

enum class DrawTool {
    Pen = 0,
    Eraser = 1,
    Fill = 2,
    Line = 3,
    Rectangle = 4,
    Ellipse = 5,
    Select = 6
};

inline bool IsFreehandTool(DrawTool tool) {
    return tool == DrawTool::Pen || tool == DrawTool::Eraser;
}

inline bool IsShapeTool(DrawTool tool) {
    return tool == DrawTool::Line
        || tool == DrawTool::Rectangle
        || tool == DrawTool::Ellipse;
}

struct AppTheme {
    // Renaissance atelier × thin bronze HUD (warm stone, not purple/cyber).
    COLORREF chromeBg = RGB(236, 230, 220);
    COLORREF chromeLine = RGB(176, 148, 108);
    COLORREF chromeDeep = RGB(220, 210, 196);
    COLORREF canvasBg = RGB(255, 255, 255);
    COLORREF accent = RGB(168, 118, 48);
    COLORREF text = RGB(42, 36, 28);
    COLORREF toolSelectedBg = RGB(248, 236, 208);
    COLORREF workspace = RGB(168, 158, 146);
};

// 4-connected flood fill on a 32bpp ARGB bitmap.
// alpha 0-255 blends fillColor over existing pixels.
bool FloodFillCanvas(Gdiplus::Bitmap* bitmap, int x, int y, COLORREF fillColor, BYTE alpha = 255);

COLORREF ColorFromGdiplus(Gdiplus::Color c);
Gdiplus::Color GdiplusFromColor(COLORREF c, BYTE alpha = 255);
