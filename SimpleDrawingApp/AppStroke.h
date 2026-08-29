#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

void DestroyStrokeLayer();
void BeginStrokeLayer();
void DrawStrokeOnto(Gdiplus::Graphics* target, int x0, int y0, int x1, int y1,
    float pressure0 = 1.0f, float pressure1 = 1.0f);
void DrawStrokeLayerWithOpacity(Gdiplus::Graphics* dest, int destX, int destY);
void RedrawShapePreview(int endX, int endY, bool shiftConstrained);
void RefreshShapePreviewIfDrawing();
void CommitStrokeLayer();
Gdiplus::Bitmap* CreateErasePreviewComposite(Gdiplus::Bitmap* eraseMask);
void NoteDrawnColors();
