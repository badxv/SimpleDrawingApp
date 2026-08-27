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
    Triangle,
    Star,
    Diamond,
    RoundRect,
    Shapes,
    ShapeStroke,
    ShapeFill,
    ShapeBoth,
    SwapColors,
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
    LayerDown,
    LayerDup,
    ChevronLeft,
    ChevronRight
};

struct IconPaintOpts {
    COLORREF chromeBg = RGB(210, 198, 180);
    COLORREF accent = RGB(176, 122, 48);
    COLORREF accentDeep = RGB(120, 78, 28);
    COLORREF text = RGB(36, 30, 22);
    COLORREF selectedBg = RGB(250, 236, 200);
    COLORREF elevated = RGB(244, 236, 222);
    float pulse = 0.0f;
    float pressScale = 1.0f;
    COLORREF colorFill = 0;
    bool useColorFill = false;
    // When set, idle buttons sample this fresco (never BitBlt parent — that
    // re-captures the button's own pressed pixels and sticks the press look).
    Gdiplus::Bitmap* frescoCache = nullptr;
    int frescoX = 0;
    int frescoY = 0;
    // App-owned selection (tool rail). Prefer over ODS_CHECKED when set.
    bool useAppSelected = false;
    bool appSelected = false;
};

UiIcon UiIconFromControlId(int controlId);
bool IsIconControlId(int controlId);
bool IsToolRailControlId(int controlId);

void DrawUiIcon(Gdiplus::Graphics& g, UiIcon icon, const Gdiplus::RectF& bounds, Gdiplus::Color color);
void PaintIconButton(const DRAWITEMSTRUCT* dis, const IconPaintOpts& opts);

void DrawFrescoPanel(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds,
    Gdiplus::Color top, Gdiplus::Color bottom, bool vertical);
void DrawFrescoGrain(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds, Gdiplus::Color grain);
// Quiet Renaissance × HUD linework wallpaper for chrome side panels (baked into cache).
void DrawFrescoMotifs(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds,
    Gdiplus::Color ink, bool verticalPanel);
void DrawBrandCompass(Gdiplus::Graphics& g, float cx, float cy, float radius,
    Gdiplus::Color gold, float angleDeg);

// Thin bronze HUD plate: double hairline + corner ticks (atelier × futurism).
void DrawHudPlate(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds,
    Gdiplus::Color fill, Gdiplus::Color bronze, bool filled);

void DrawHudCornerTicks(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds,
    Gdiplus::Color bronze, float tick = 7.0f);

// Minimal gilt hairline mount with Renaissance corner volutes + edge fleurons.
void DrawCanvasWell(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds,
    Gdiplus::Color rim, Gdiplus::Color gilt);
