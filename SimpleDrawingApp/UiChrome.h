#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

enum class UiIcon {
    Pen = 0,
    Eraser,
    Fill,
    Select,
    Line,
    Rect,
    Ellipse,
    NewDoc,
    Open,
    Save,
    Undo,
    Redo,
    Clear,
    Color,
    LayerAdd,
    LayerDel,
    LayerUp,
    LayerDown
};

struct IconPaintOpts {
    COLORREF chromeBg = RGB(220, 210, 196);
    COLORREF accent = RGB(168, 118, 48);
    COLORREF text = RGB(42, 36, 28);
    COLORREF selectedBg = RGB(248, 236, 208);
    float pulse = 0.0f;          // 0..1 selected border breath
    float pressScale = 1.0f;     // brief tool-switch pop
    COLORREF colorFill = 0;      // Color icon fill (pen color)
    bool useColorFill = false;
};

UiIcon UiIconFromControlId(int controlId);
bool IsIconControlId(int controlId);
bool IsToolRailControlId(int controlId);

void DrawUiIcon(Gdiplus::Graphics& g, UiIcon icon, const Gdiplus::RectF& bounds, Gdiplus::Color color);
void PaintIconButton(const DRAWITEMSTRUCT* dis, const IconPaintOpts& opts);

void DrawFrescoPanel(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds,
    Gdiplus::Color top, Gdiplus::Color bottom, bool vertical);
void DrawFrescoGrain(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds, Gdiplus::Color grain);
void DrawBrandCompass(Gdiplus::Graphics& g, float cx, float cy, float radius,
    Gdiplus::Color gold, float angleDeg);
