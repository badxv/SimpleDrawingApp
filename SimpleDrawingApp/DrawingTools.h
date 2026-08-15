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
    // Bronze & Parchment atelier × thin HUD (not purple/cyber, not Win95 gray).
    COLORREF chromeBg = RGB(232, 224, 210);      // parchment
    COLORREF chromeLine = RGB(156, 118, 62);      // bronze rule
    COLORREF chromeDeep = RGB(210, 198, 180);     // rail stone
    COLORREF chromeElevated = RGB(244, 236, 222); // plate face
    COLORREF canvasBg = RGB(252, 250, 246);
    COLORREF accent = RGB(176, 122, 48);          // gilt bronze
    COLORREF accentDeep = RGB(120, 78, 28);
    COLORREF ink = RGB(28, 24, 18);               // manuscript ink
    COLORREF text = RGB(36, 30, 22);
    COLORREF toolSelectedBg = RGB(250, 236, 200);
    COLORREF workspace = RGB(138, 128, 114);      // deeper well
    COLORREF wellRim = RGB(92, 70, 40);
};

bool FloodFillCanvas(Gdiplus::Bitmap* bitmap, int x, int y, COLORREF fillColor, BYTE alpha = 255);

COLORREF ColorFromGdiplus(Gdiplus::Color c);
Gdiplus::Color GdiplusFromColor(COLORREF c, BYTE alpha = 255);
