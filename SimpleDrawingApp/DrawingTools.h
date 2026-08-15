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
    COLORREF chromeBg = RGB(243, 243, 243);
    COLORREF chromeLine = RGB(218, 218, 218);
    COLORREF canvasBg = RGB(255, 255, 255);
    COLORREF accent = RGB(0, 120, 212);
    COLORREF text = RGB(32, 32, 32);
    COLORREF toolSelectedBg = RGB(225, 239, 250);
};

// 4-connected flood fill on a 32bpp ARGB bitmap.
// alpha 0-255 blends fillColor over existing pixels.
bool FloodFillCanvas(Gdiplus::Bitmap* bitmap, int x, int y, COLORREF fillColor, BYTE alpha = 255);

COLORREF ColorFromGdiplus(Gdiplus::Color c);
Gdiplus::Color GdiplusFromColor(COLORREF c, BYTE alpha = 255);
