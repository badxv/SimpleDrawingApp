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

UiIcon UiIconFromControlId(int controlId);
bool IsIconControlId(int controlId);

void DrawUiIcon(Gdiplus::Graphics& g, UiIcon icon, const Gdiplus::RectF& bounds, Gdiplus::Color color);
void PaintIconButton(const DRAWITEMSTRUCT* dis, COLORREF chromeBg, COLORREF accent, COLORREF text, COLORREF selectedBg);
