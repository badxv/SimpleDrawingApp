#include "AppStroke.h"
#include "AppCanvas.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppFeatureFlags.h"
#include "DrawingTools.h"
#include "AtelierPalette.h"
#include "AtelierRaii.h"
#include <cmath>

using namespace Gdiplus;
using Atelier::MakeBitmap;
using Atelier::MakeGraphics;

void NoteDrawnColors() {
    if (!hwndPalette) return;
    if (currentTool == DrawTool::Eraser || currentTool == DrawTool::Select) return;

    if (currentTool == DrawTool::Pen
        || currentTool == DrawTool::Line
        || currentTool == DrawTool::Fill) {
        AtelierPalette_NoteColor(hwndPalette, penColor);
        return;
    }

    if (currentTool == DrawTool::Shape) {
        const ShapePaintMode mode = EffectiveShapePaintMode();
        if (mode == ShapePaintMode::Stroke || mode == ShapePaintMode::Both) {
            AtelierPalette_NoteColor(hwndPalette, penColor);
        }
        if (mode == ShapePaintMode::Fill || mode == ShapePaintMode::Both) {
            AtelierPalette_NoteColor(hwndPalette, backColor);
        }
    }
}
void DestroyStrokeLayer() {
    strokeGraphics.reset();
    strokeLayer.reset();
}

void BeginStrokeLayer() {
    DestroyStrokeLayer();
    if (!gLayers.ActiveBitmap()) return;

    const int width = gLayers.Width();
    const int height = gLayers.Height();
    strokeLayer = MakeBitmap(width, height, PixelFormat32bppARGB);
    if (!strokeLayer) {
        return;
    }
    strokeGraphics = MakeGraphics(strokeLayer.get());
    if (!strokeGraphics) {
        DestroyStrokeLayer();
        return;
    }
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    strokeGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
    strokeGraphics->SetCompositingMode(CompositingModeSourceOver);
}

void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
    if (!target) return;

    // Draw fully opaque ink onto the stroke layer; opacity is applied once when compositing.
    // Eraser on non-background layers builds an alpha coverage mask (committed as transparent holes).
    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    if (eraseTransparent) {
        // SourceOver + white ink: AA coverage accumulates in alpha (SourceCopy left speckled gaps).
        target->SetCompositingMode(CompositingModeSourceOver);
        Pen pen(Color(255, 255, 255, 255), static_cast<REAL>(penWidth));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        target->DrawLine(&pen, x0, y0, x1, y1);
        return;
    }

    COLORREF strokeColor = (currentTool == DrawTool::Eraser) ? gTheme.canvasBg : penColor;
    Pen pen(GdiplusFromColor(strokeColor, 255), static_cast<REAL>(penWidth));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    target->DrawLine(&pen, x0, y0, x1, y1);
}

static void ConstrainShapeEnd(int x0, int y0, int& x1, int& y1) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int adx = (dx < 0) ? -dx : dx;
    const int ady = (dy < 0) ? -dy : dy;

    if (currentTool == DrawTool::Line) {
        // Snap to horizontal, vertical, or 45-degree.
        if (adx * 2 < ady) {
            x1 = x0;
        }
        else if (ady * 2 < adx) {
            y1 = y0;
        }
        else {
            const int s = (adx < ady) ? adx : ady;
            x1 = x0 + ((dx >= 0) ? s : -s);
            y1 = y0 + ((dy >= 0) ? s : -s);
        }
        return;
    }

    // Square / circle: equal abs extents from start.
    const int s = (adx > ady) ? adx : ady;
    x1 = x0 + ((dx >= 0) ? s : -s);
    y1 = y0 + ((dy >= 0) ? s : -s);
}

static void BuildShapePath(GraphicsPath& path, ShapeKind kind, int left, int top, int width, int height) {
    const RectF box(
        static_cast<REAL>(left),
        static_cast<REAL>(top),
        static_cast<REAL>(MaxInt(1, width)),
        static_cast<REAL>(MaxInt(1, height)));
    const REAL cx = box.X + box.Width * 0.5f;
    const REAL cy = box.Y + box.Height * 0.5f;

    switch (kind) {
    case ShapeKind::Rectangle:
        path.AddRectangle(box);
        break;
    case ShapeKind::Ellipse:
        path.AddEllipse(box);
        break;
    case ShapeKind::Triangle: {
        PointF pts[3] = {
            { cx, box.Y },
            { box.X, box.Y + box.Height },
            { box.X + box.Width, box.Y + box.Height }
        };
        path.AddPolygon(pts, 3);
        break;
    }
    case ShapeKind::Star: {
        PointF pts[10];
        for (int i = 0; i < 10; ++i) {
            const float ang = -1.5707963f + i * 3.14159265f / 5.0f;
            const float rad = (i % 2 == 0) ? 0.5f : 0.22f;
            pts[i] = PointF(cx + cosf(ang) * box.Width * rad, cy + sinf(ang) * box.Height * rad);
        }
        path.AddPolygon(pts, 10);
        break;
    }
    case ShapeKind::Diamond: {
        PointF pts[4] = {
            { cx, box.Y },
            { box.X + box.Width, cy },
            { cx, box.Y + box.Height },
            { box.X, cy }
        };
        path.AddPolygon(pts, 4);
        break;
    }
    case ShapeKind::RoundRect: {
        REAL rr = (box.Width < box.Height ? box.Width : box.Height) * 0.18f;
        if (rr < 2.0f) rr = 2.0f;
        if (rr * 2.0f > box.Width) rr = box.Width * 0.5f;
        if (rr * 2.0f > box.Height) rr = box.Height * 0.5f;
        path.AddArc(box.X, box.Y, rr * 2, rr * 2, 180, 90);
        path.AddArc(box.X + box.Width - rr * 2, box.Y, rr * 2, rr * 2, 270, 90);
        path.AddArc(box.X + box.Width - rr * 2, box.Y + box.Height - rr * 2, rr * 2, rr * 2, 0, 90);
        path.AddArc(box.X, box.Y + box.Height - rr * 2, rr * 2, rr * 2, 90, 90);
        path.CloseFigure();
        break;
    }
    }
}

static void DrawShapeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
    if (!target) return;

    Pen pen(GdiplusFromColor(penColor, 255), static_cast<REAL>(penWidth));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    if (currentTool == DrawTool::Line) {
        target->DrawLine(&pen, x0, y0, x1, y1);
        return;
    }

    int left = (x0 < x1) ? x0 : x1;
    int top = (y0 < y1) ? y0 : y1;
    int right = (x0 > x1) ? x0 : x1;
    int bottom = (y0 > y1) ? y0 : y1;
    int width = right - left;
    int height = bottom - top;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    const ShapePaintMode paintMode = EffectiveShapePaintMode();
    const bool doFill = (paintMode == ShapePaintMode::Fill || paintMode == ShapePaintMode::Both);
    const bool doStroke = (paintMode == ShapePaintMode::Stroke || paintMode == ShapePaintMode::Both);

    GraphicsPath path;
    BuildShapePath(path, currentShape, left, top, width, height);
    if (doFill) {
        SolidBrush fill(GdiplusFromColor(backColor, 255));
        target->FillPath(&fill, &path);
    }
    if (doStroke) {
        target->DrawPath(&pen, &path);
    }
}

void RedrawShapePreview(int endX, int endY, bool shiftConstrained) {
    if (!strokeGraphics) return;
    int x1 = endX;
    int y1 = endY;
    if (shiftConstrained) {
        ConstrainShapeEnd(shapeStart.x, shapeStart.y, x1, y1);
    }
    if (IsFeatureEnabled(AppFeature::SnapToGrid)) {
        x1 = SnapCoordToGrid(x1, gGridSpacing);
        y1 = SnapCoordToGrid(y1, gGridSpacing);
    }
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    DrawShapeOnto(strokeGraphics.get(), shapeStart.x, shapeStart.y, x1, y1);
    lastPoint.x = x1;
    lastPoint.y = y1;
}

void RefreshShapePreviewIfDrawing() {
    if (!isDrawing || !strokeGraphics || !IsShapeTool(currentTool)) return;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    RedrawShapePreview(lastPoint.x, lastPoint.y, shift);
    InvalidateCanvas();
}

void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY) {
    if (!strokeLayer || !dest) return;

    const REAL alpha = static_cast<REAL>(OpacityToAlpha()) / 255.0f;
    ColorMatrix matrix = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alpha, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };

    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    const int width = static_cast<int>(strokeLayer->GetWidth());
    const int height = static_cast<int>(strokeLayer->GetHeight());
    dest->DrawImage(
        strokeLayer.get(),
        Rect(destX, destY, width, height),
        0, 0, width, height,
        UnitPixel,
        &attrs);
}

static void ApplyTransparentEraseMask(Bitmap* target, Bitmap* mask) {
    if (!target || !mask) return;

    const int width = static_cast<int>(mask->GetWidth());
    const int height = static_cast<int>(mask->GetHeight());
    if (width < 1 || height < 1) return;
    if (static_cast<int>(target->GetWidth()) < width || static_cast<int>(target->GetHeight()) < height) {
        return;
    }

    BitmapData maskData = {};
    BitmapData targetData = {};
    Rect lockRect(0, 0, width, height);
    if (mask->LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppARGB, &maskData) != Ok) return;
    if (target->LockBits(&lockRect, ImageLockModeWrite, PixelFormat32bppARGB, &targetData) != Ok) {
        mask->UnlockBits(&maskData);
        return;
    }

    auto* maskPx = static_cast<BYTE*>(maskData.Scan0);
    auto* targetPx = static_cast<BYTE*>(targetData.Scan0);
    for (int y = 0; y < height; ++y) {
        BYTE* mrow = maskPx + y * maskData.Stride;
        BYTE* trow = targetPx + y * targetData.Stride;
        for (int x = 0; x < width; ++x) {
            BYTE* m = mrow + x * 4;
            const unsigned ma = m[3];
            if (ma == 0) continue;

            BYTE* t = trow + x * 4;
            // Soft erase: scale destination by inverse mask coverage (AA fringes included).
            const unsigned inv = 255u - ma;
            t[0] = static_cast<BYTE>((t[0] * inv) / 255u);
            t[1] = static_cast<BYTE>((t[1] * inv) / 255u);
            t[2] = static_cast<BYTE>((t[2] * inv) / 255u);
            t[3] = static_cast<BYTE>((t[3] * inv) / 255u);
        }
    }

    target->UnlockBits(&targetData);
    mask->UnlockBits(&maskData);
}

Gdiplus::Bitmap* CreateErasePreviewComposite(Bitmap* eraseMask) {
    if (!eraseMask || gLayers.Width() < 1 || gLayers.Height() < 1) return nullptr;

    Bitmap* out = new Bitmap(gLayers.Width(), gLayers.Height(), PixelFormat32bppARGB);
    if (!out || out->GetLastStatus() != Ok) {
        delete out;
        return nullptr;
    }

    Graphics g(out);
    g.Clear(Color(0, 0, 0, 0));
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeNone);

    const int active = gLayers.ActiveIndex();
    Bitmap* erasedActive = nullptr;
    if (const Layer* activeLayer = gLayers.ActiveLayer()) {
        if (activeLayer->bitmap) {
            erasedActive = activeLayer->bitmap->Clone(0, 0,
                static_cast<INT>(activeLayer->bitmap->GetWidth()),
                static_cast<INT>(activeLayer->bitmap->GetHeight()),
                PixelFormat32bppARGB);
            if (erasedActive && erasedActive->GetLastStatus() == Ok) {
                ApplyTransparentEraseMask(erasedActive, eraseMask);
            } else {
                delete erasedActive;
                erasedActive = nullptr;
            }
        }
    }

    for (int i = 0; i < gLayers.Count(); ++i) {
        const Layer* layer = gLayers.At(i);
        if (!layer || !layer->visible) continue;
        Bitmap* bmp = (i == active && erasedActive) ? erasedActive : layer->bitmap;
        if (!bmp) continue;

        int opacity = layer->opacity;
        if (opacity < 1) continue;
        if (opacity > 100) opacity = 100;

        if (opacity >= 100) {
            g.DrawImage(bmp, 0, 0);
        } else {
            const REAL alpha = static_cast<REAL>(opacity) / 100.0f;
            ColorMatrix matrix = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, alpha, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 1.0f
            };
            ImageAttributes attrs;
            attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            const int w = static_cast<int>(bmp->GetWidth());
            const int h = static_cast<int>(bmp->GetHeight());
            g.DrawImage(bmp, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &attrs);
        }
    }

    delete erasedActive;
    return out;
}

void CommitStrokeLayer() {
    Graphics* ag = gLayers.ActiveGraphics();
    if (!strokeLayer || !ag) {
        DestroyStrokeLayer();
        return;
    }

    const Layer* layer = gLayers.ActiveLayer();
    const bool eraseTransparent = (currentTool == DrawTool::Eraser && layer && !layer->isBackground);
    if (eraseTransparent) {
        ApplyTransparentEraseMask(gLayers.ActiveBitmap(), strokeLayer.get());
    }
    else {
        DrawStrokeLayerWithOpacity(ag, 0, 0);
    }
    DestroyStrokeLayer();
    InvalidateComposite();
    NoteDrawnColors();
}
