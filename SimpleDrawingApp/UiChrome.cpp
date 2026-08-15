#include "UiChrome.h"
#include "Resource.h"
#include <cmath>

using namespace Gdiplus;

UiIcon UiIconFromControlId(int controlId) {
    switch (controlId) {
    case IDC_TOOL_PEN: return UiIcon::Pen;
    case IDC_TOOL_ERASER: return UiIcon::Eraser;
    case IDC_TOOL_FILL: return UiIcon::Fill;
    case IDC_TOOL_SELECT: return UiIcon::Select;
    case IDC_TOOL_LINE: return UiIcon::Line;
    case IDC_TOOL_RECT: return UiIcon::Rect;
    case IDC_TOOL_ELLIPSE: return UiIcon::Ellipse;
    case IDC_NEW_BUTTON: return UiIcon::NewDoc;
    case IDC_LOAD_BUTTON: return UiIcon::Open;
    case IDC_SAVE_BUTTON: return UiIcon::Save;
    case IDC_UNDO_BUTTON: return UiIcon::Undo;
    case IDC_REDO_BUTTON: return UiIcon::Redo;
    case IDC_CLEAR_BUTTON: return UiIcon::Clear;
    case IDC_COLOR_BUTTON: return UiIcon::Color;
    case IDC_LAYER_ADD: return UiIcon::LayerAdd;
    case IDC_LAYER_DEL: return UiIcon::LayerDel;
    case IDC_LAYER_UP: return UiIcon::LayerUp;
    case IDC_LAYER_DOWN: return UiIcon::LayerDown;
    default: return UiIcon::Pen;
    }
}

bool IsIconControlId(int controlId) {
    switch (controlId) {
    case IDC_TOOL_PEN:
    case IDC_TOOL_ERASER:
    case IDC_TOOL_FILL:
    case IDC_TOOL_SELECT:
    case IDC_TOOL_LINE:
    case IDC_TOOL_RECT:
    case IDC_TOOL_ELLIPSE:
    case IDC_NEW_BUTTON:
    case IDC_LOAD_BUTTON:
    case IDC_SAVE_BUTTON:
    case IDC_UNDO_BUTTON:
    case IDC_REDO_BUTTON:
    case IDC_CLEAR_BUTTON:
    case IDC_COLOR_BUTTON:
    case IDC_LAYER_ADD:
    case IDC_LAYER_DEL:
    case IDC_LAYER_UP:
    case IDC_LAYER_DOWN:
        return true;
    default:
        return false;
    }
}

bool IsToolRailControlId(int controlId) {
    switch (controlId) {
    case IDC_TOOL_PEN:
    case IDC_TOOL_ERASER:
    case IDC_TOOL_FILL:
    case IDC_TOOL_SELECT:
    case IDC_TOOL_LINE:
    case IDC_TOOL_RECT:
    case IDC_TOOL_ELLIPSE:
    case IDC_COLOR_BUTTON:
        return true;
    default:
        return false;
    }
}

static void StrokeStyle(Pen& pen) {
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
}

void DrawFrescoPanel(Graphics& g, const RectF& bounds, Color top, Color bottom, bool vertical) {
    if (bounds.Width < 1.0f || bounds.Height < 1.0f) return;
    LinearGradientBrush brush(
        bounds,
        top,
        bottom,
        vertical ? LinearGradientModeVertical : LinearGradientModeHorizontal);
    g.FillRectangle(&brush, bounds);

    // Soft cross-wash (first-approach warmth) — still only when baking the cache.
    const Color tip(
        28,
        (top.GetR() + bottom.GetR()) / 2,
        (top.GetG() + bottom.GetG()) / 2,
        (top.GetB() + bottom.GetB()) / 2);
    LinearGradientBrush cross(
        bounds,
        Color(0, tip.GetR(), tip.GetG(), tip.GetB()),
        tip,
        vertical ? LinearGradientModeHorizontal : LinearGradientModeVertical);
    g.FillRectangle(&cross, bounds);
}

void DrawFrescoGrain(Graphics& g, const RectF& bounds, Color grain) {
    if (bounds.Width < 8.0f || bounds.Height < 8.0f) return;

    const REAL x0 = bounds.X;
    const REAL y0 = bounds.Y;
    const REAL x1 = bounds.X + bounds.Width;
    const REAL y1 = bounds.Y + bounds.Height;
    auto clampByte = [](int v) -> BYTE {
        if (v < 1) return 1;
        if (v > 255) return 255;
        return static_cast<BYTE>(v);
    };

    // Primary diagonal hatch (closer to the first fresco look).
    {
        Pen pen(grain, 1.0f);
        const REAL step = 17.0f;
        for (REAL t = -bounds.Height; t < bounds.Width + bounds.Height; t += step) {
            g.DrawLine(&pen, x0 + t, y0, x0 + t + bounds.Height, y1);
        }
    }
    // Softer counter-hatch.
    {
        Pen pen(Color(clampByte(static_cast<int>(grain.GetA()) * 2 / 3),
            grain.GetR(), grain.GetG(), grain.GetB()), 1.0f);
        const REAL step = 29.0f;
        for (REAL t = -bounds.Height; t < bounds.Width + bounds.Height; t += step) {
            g.DrawLine(&pen, x1 - t, y0, x1 - t - bounds.Height, y1);
        }
    }
    // Speckle (deterministic, no RNG allocation).
    {
        SolidBrush dot(Color(clampByte(static_cast<int>(grain.GetA()) / 2),
            grain.GetR(), grain.GetG(), grain.GetB()));
        for (int y = 0; y < static_cast<int>(bounds.Height); y += 7) {
            for (int x = 0; x < static_cast<int>(bounds.Width); x += 9) {
                const unsigned h = static_cast<unsigned>((x * 73856093u) ^ (y * 19349663u));
                if ((h & 7u) == 0u) {
                    g.FillRectangle(&dot, x0 + static_cast<REAL>(x), y0 + static_cast<REAL>(y), 1.0f, 1.0f);
                }
            }
        }
    }
    // Edge vignette.
    SolidBrush wash(Color(clampByte(static_cast<int>(grain.GetA())),
        grain.GetR(), grain.GetG(), grain.GetB()));
    g.FillRectangle(&wash, RectF(x0, y0, bounds.Width, 5.0f));
    g.FillRectangle(&wash, RectF(x0, y1 - 5.0f, bounds.Width, 5.0f));
    g.FillRectangle(&wash, RectF(x0, y0, 4.0f, bounds.Height));
    g.FillRectangle(&wash, RectF(x1 - 4.0f, y0, 4.0f, bounds.Height));
}

void DrawBrandCompass(Graphics& g, float cx, float cy, float radius, Color gold, float angleDeg) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen ring(gold, 1.5f);
    StrokeStyle(ring);
    g.DrawEllipse(&ring, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // Inner tick ring.
    Pen soft(Color(140, gold.GetR(), gold.GetG(), gold.GetB()), 1.0f);
    g.DrawEllipse(&soft, cx - radius * 0.62f, cy - radius * 0.62f, radius * 1.24f, radius * 1.24f);

    const REAL rad = angleDeg * 3.14159265f / 180.0f;
    const REAL c = cosf(rad);
    const REAL s = sinf(rad);
    auto rot = [&](float x, float y) -> PointF {
        return PointF(cx + x * c - y * s, cy + x * s + y * c);
    };

    Pen needle(gold, 1.6f);
    StrokeStyle(needle);
    PointF n0 = rot(0.0f, -radius * 0.78f);
    PointF n1 = rot(0.0f, radius * 0.55f);
    PointF e0 = rot(-radius * 0.55f, 0.0f);
    PointF e1 = rot(radius * 0.55f, 0.0f);
    g.DrawLine(&needle, n0, n1);
    g.DrawLine(&needle, e0, e1);

    SolidBrush hub(gold);
    g.FillEllipse(&hub, cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
}

void DrawUiIcon(Graphics& g, UiIcon icon, const RectF& b, Color color) {
    const REAL pad = 6.0f;
    RectF r(b.X + pad, b.Y + pad, b.Width - pad * 2, b.Height - pad * 2);
    if (r.Width < 4 || r.Height < 4) return;

    Pen pen(color, 1.7f);
    StrokeStyle(pen);
    SolidBrush brush(color);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    const REAL cx = r.X + r.Width * 0.5f;
    const REAL cy = r.Y + r.Height * 0.5f;
    const REAL right = r.X + r.Width;
    const REAL bottom = r.Y + r.Height;

    switch (icon) {
    case UiIcon::Pen: {
        PointF tip(r.X + r.Width * 0.22f, r.Y + r.Height * 0.78f);
        PointF end(r.X + r.Width * 0.78f, r.Y + r.Height * 0.22f);
        g.DrawLine(&pen, tip, end);
        g.FillEllipse(&brush, tip.X - 1.5f, tip.Y - 1.5f, 3.0f, 3.0f);
        break;
    }
    case UiIcon::Eraser: {
        RectF body(r.X + r.Width * 0.2f, r.Y + r.Height * 0.28f, r.Width * 0.55f, r.Height * 0.42f);
        g.DrawRectangle(&pen, body);
        g.DrawLine(&pen, body.X, body.Y + body.Height * 0.55f, body.X + body.Width, body.Y + body.Height * 0.55f);
        break;
    }
    case UiIcon::Fill: {
        PointF pts[4] = {
            { r.X + r.Width * 0.28f, r.Y + r.Height * 0.22f },
            { r.X + r.Width * 0.72f, r.Y + r.Height * 0.22f },
            { r.X + r.Width * 0.62f, r.Y + r.Height * 0.72f },
            { r.X + r.Width * 0.38f, r.Y + r.Height * 0.72f }
        };
        g.DrawPolygon(&pen, pts, 4);
        g.FillEllipse(&brush, cx - 2.0f, r.Y + r.Height * 0.78f, 4.0f, 4.0f);
        break;
    }
    case UiIcon::Select: {
        Pen dash(color, 1.4f);
        StrokeStyle(dash);
        dash.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&dash, RectF(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2));
        break;
    }
    case UiIcon::Line:
        g.DrawLine(&pen, r.X + 2, bottom - 2, right - 2, r.Y + 2);
        break;
    case UiIcon::Rect:
        g.DrawRectangle(&pen, RectF(r.X + 2, r.Y + 3, r.Width - 4, r.Height - 6));
        break;
    case UiIcon::Ellipse:
        g.DrawEllipse(&pen, RectF(r.X + 2, r.Y + 3, r.Width - 4, r.Height - 6));
        break;
    case UiIcon::NewDoc: {
        RectF page(r.X + 3, r.Y + 2, r.Width - 6, r.Height - 4);
        g.DrawRectangle(&pen, page);
        g.DrawLine(&pen, page.X + 3, page.Y + page.Height * 0.35f, page.X + page.Width - 3, page.Y + page.Height * 0.35f);
        g.DrawLine(&pen, page.X + 3, page.Y + page.Height * 0.55f, page.X + page.Width - 5, page.Y + page.Height * 0.55f);
        break;
    }
    case UiIcon::Open: {
        PointF pts[6] = {
            { r.X + 2, r.Y + r.Height * 0.35f },
            { r.X + 2, r.Y + r.Height * 0.82f },
            { right - 2, r.Y + r.Height * 0.82f },
            { right - 2, r.Y + r.Height * 0.42f },
            { r.X + r.Width * 0.55f, r.Y + r.Height * 0.42f },
            { r.X + r.Width * 0.42f, r.Y + r.Height * 0.28f }
        };
        g.DrawLines(&pen, pts, 6);
        g.DrawLine(&pen, r.X + 2, r.Y + r.Height * 0.35f, r.X + r.Width * 0.38f, r.Y + r.Height * 0.35f);
        break;
    }
    case UiIcon::Save: {
        RectF body(r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4);
        g.DrawRectangle(&pen, body);
        g.DrawRectangle(&pen, RectF(r.X + r.Width * 0.28f, r.Y + 2, r.Width * 0.44f, r.Height * 0.28f));
        g.DrawRectangle(&pen, RectF(r.X + r.Width * 0.25f, r.Y + r.Height * 0.48f, r.Width * 0.5f, r.Height * 0.34f));
        break;
    }
    case UiIcon::Undo: {
        g.DrawArc(&pen, RectF(r.X + 2, r.Y + 3, r.Width - 4, r.Height - 6), 40.0f, 220.0f);
        PointF a(r.X + 3, cy);
        g.DrawLine(&pen, a.X, a.Y, a.X + 5, a.Y - 5);
        g.DrawLine(&pen, a.X, a.Y, a.X + 5, a.Y + 5);
        break;
    }
    case UiIcon::Redo: {
        g.DrawArc(&pen, RectF(r.X + 2, r.Y + 3, r.Width - 4, r.Height - 6), 280.0f, 220.0f);
        PointF a(right - 3, cy);
        g.DrawLine(&pen, a.X, a.Y, a.X - 5, a.Y - 5);
        g.DrawLine(&pen, a.X, a.Y, a.X - 5, a.Y + 5);
        break;
    }
    case UiIcon::Clear: {
        g.DrawEllipse(&pen, RectF(r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4));
        g.DrawLine(&pen, r.X + 4, r.Y + 4, right - 4, bottom - 4);
        break;
    }
    case UiIcon::Color: {
        g.FillEllipse(&brush, RectF(r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6));
        Pen ring(Color(255, 40, 34, 28), 1.2f);
        StrokeStyle(ring);
        g.DrawEllipse(&ring, RectF(r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6));
        break;
    }
    case UiIcon::LayerAdd: {
        g.DrawLine(&pen, cx, r.Y + 3, cx, bottom - 3);
        g.DrawLine(&pen, r.X + 3, cy, right - 3, cy);
        break;
    }
    case UiIcon::LayerDel: {
        g.DrawLine(&pen, r.X + 4, cy, right - 4, cy);
        break;
    }
    case UiIcon::LayerUp: {
        PointF pts[3] = {
            { cx, r.Y + 4 },
            { r.X + 4, bottom - 5 },
            { right - 4, bottom - 5 }
        };
        g.DrawPolygon(&pen, pts, 3);
        break;
    }
    case UiIcon::LayerDown: {
        PointF pts[3] = {
            { cx, bottom - 4 },
            { r.X + 4, r.Y + 5 },
            { right - 4, r.Y + 5 }
        };
        g.DrawPolygon(&pen, pts, 3);
        break;
    }
    }
}

void DrawHudCornerTicks(Graphics& g, const RectF& bounds, Color bronze, float tick) {
    Pen pen(bronze, 1.2f);
    StrokeStyle(pen);
    const REAL x0 = bounds.X;
    const REAL y0 = bounds.Y;
    const REAL x1 = bounds.X + bounds.Width;
    const REAL y1 = bounds.Y + bounds.Height;
    g.DrawLine(&pen, x0, y0, x0 + tick, y0);
    g.DrawLine(&pen, x0, y0, x0, y0 + tick);
    g.DrawLine(&pen, x1, y0, x1 - tick, y0);
    g.DrawLine(&pen, x1, y0, x1, y0 + tick);
    g.DrawLine(&pen, x0, y1, x0 + tick, y1);
    g.DrawLine(&pen, x0, y1, x0, y1 - tick);
    g.DrawLine(&pen, x1, y1, x1 - tick, y1);
    g.DrawLine(&pen, x1, y1, x1, y1 - tick);
}

void DrawHudPlate(Graphics& g, const RectF& bounds, Color fill, Color bronze, bool filled) {
    if (filled) {
        SolidBrush brush(fill);
        g.FillRectangle(&brush, bounds);
    }
    Pen outer(bronze, 1.15f);
    Pen inner(Color(90, bronze.GetR(), bronze.GetG(), bronze.GetB()), 1.0f);
    g.DrawRectangle(&outer, bounds);
    if (bounds.Width > 6 && bounds.Height > 6) {
        g.DrawRectangle(&inner, RectF(bounds.X + 2.0f, bounds.Y + 2.0f, bounds.Width - 4.0f, bounds.Height - 4.0f));
    }
    DrawHudCornerTicks(g, bounds, bronze, 6.0f);
}

namespace {

void StrokeRound(Pen& pen) {
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
}

// Corner volute: thin L-rule + inward scroll (manuscript picture-frame).
void DrawCornerVolute(Graphics& g, float cx, float cy, float sx, float sy, Color gilt) {
    Pen arm(Color(210, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.05f);
    StrokeRound(arm);
    const float reach = 14.0f;
    g.DrawLine(&arm, cx + sx * 0.5f, cy, cx + sx * reach, cy);
    g.DrawLine(&arm, cx, cy + sy * 0.5f, cx, cy + sy * reach);

    Pen scroll(Color(185, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.15f);
    StrokeRound(scroll);

    // Primary volute (acanthus-like curl into the mount).
    GraphicsPath path;
    const float x0 = cx + sx * 2.5f;
    const float y0 = cy + sy * 2.5f;
    path.AddBezier(
        PointF(x0, y0),
        PointF(x0 + sx * 1.0f, y0 + sy * 6.5f),
        PointF(x0 + sx * 7.0f, y0 + sy * 7.5f),
        PointF(x0 + sx * 8.0f, y0 + sy * 3.0f));
    path.AddBezier(
        PointF(x0 + sx * 8.0f, y0 + sy * 3.0f),
        PointF(x0 + sx * 8.5f, y0 + sy * 0.5f),
        PointF(x0 + sx * 5.5f, y0 + sy * 0.2f),
        PointF(x0 + sx * 4.2f, y0 + sy * 2.8f));
    g.DrawPath(&scroll, &path);

    // Inner eye of the scroll.
    Pen eye(Color(160, gilt.GetR(), gilt.GetG(), gilt.GetB()), 0.9f);
    StrokeRound(eye);
    g.DrawEllipse(&eye, RectF(x0 + sx * 4.6f - 1.6f, y0 + sy * 3.2f - 1.6f, 3.2f, 3.2f));

    // Small leaf tip off the arm.
    GraphicsPath leaf;
    leaf.AddBezier(
        PointF(cx + sx * 9.0f, cy + sy * 0.8f),
        PointF(cx + sx * 11.5f, cy + sy * 0.2f),
        PointF(cx + sx * 12.5f, cy + sy * 2.5f),
        PointF(cx + sx * 10.0f, cy + sy * 3.2f));
    Pen leafPen(Color(150, gilt.GetR(), gilt.GetG(), gilt.GetB()), 0.95f);
    StrokeRound(leafPen);
    g.DrawPath(&leafPen, &leaf);
}

// Mid-edge fleuron: lozenge + tiny side curls.
void DrawEdgeFleuron(Graphics& g, float cx, float cy, bool horizontal, Color gilt) {
    Pen pen(Color(175, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.0f);
    StrokeRound(pen);
    const float a = 4.5f;
    PointF diamond[4] = {
        PointF(cx, cy - a),
        PointF(cx + a, cy),
        PointF(cx, cy + a),
        PointF(cx - a, cy)
    };
    g.DrawPolygon(&pen, diamond, 4);

    Pen curl(Color(140, gilt.GetR(), gilt.GetG(), gilt.GetB()), 0.9f);
    StrokeRound(curl);
    if (horizontal) {
        g.DrawLine(&curl, cx - a - 5.0f, cy, cx - a - 1.0f, cy);
        g.DrawLine(&curl, cx + a + 1.0f, cy, cx + a + 5.0f, cy);
        g.DrawEllipse(&curl, RectF(cx - a - 7.5f, cy - 1.4f, 2.8f, 2.8f));
        g.DrawEllipse(&curl, RectF(cx + a + 4.7f, cy - 1.4f, 2.8f, 2.8f));
    } else {
        g.DrawLine(&curl, cx, cy - a - 5.0f, cx, cy - a - 1.0f);
        g.DrawLine(&curl, cx, cy + a + 1.0f, cx, cy + a + 5.0f);
        g.DrawEllipse(&curl, RectF(cx - 1.4f, cy - a - 7.5f, 2.8f, 2.8f));
        g.DrawEllipse(&curl, RectF(cx - 1.4f, cy + a + 4.7f, 2.8f, 2.8f));
    }
}

} // namespace

void DrawCanvasWell(Graphics& g, const RectF& bounds, Color rim, Color gilt) {
    if (bounds.Width < 24.0f || bounds.Height < 24.0f) return;

    const SmoothingMode prevSmooth = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Quiet mount fill (no heavy shadow slab).
    SolidBrush mount(Color(28, rim.GetR(), rim.GetG(), rim.GetB()));
    g.FillRectangle(&mount, bounds);

    // Single outer hairline — minimal frame.
    Pen outer(Color(200, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.0f);
    StrokeRound(outer);
    g.DrawRectangle(&outer, RectF(bounds.X + 0.5f, bounds.Y + 0.5f,
        bounds.Width - 1.0f, bounds.Height - 1.0f));

    // Whisper-thin inner rule, set in from the mount edge.
    const float inset = 6.5f;
    if (bounds.Width > inset * 2 + 8.0f && bounds.Height > inset * 2 + 8.0f) {
        Pen inner(Color(110, rim.GetR(), rim.GetG(), rim.GetB()), 0.85f);
        StrokeRound(inner);
        g.DrawRectangle(&inner, RectF(
            bounds.X + inset, bounds.Y + inset,
            bounds.Width - inset * 2.0f, bounds.Height - inset * 2.0f));
    }

    const float x0 = bounds.X + 1.0f;
    const float y0 = bounds.Y + 1.0f;
    const float x1 = bounds.X + bounds.Width - 1.0f;
    const float y1 = bounds.Y + bounds.Height - 1.0f;

    DrawCornerVolute(g, x0, y0, 1.0f, 1.0f, gilt);
    DrawCornerVolute(g, x1, y0, -1.0f, 1.0f, gilt);
    DrawCornerVolute(g, x0, y1, 1.0f, -1.0f, gilt);
    DrawCornerVolute(g, x1, y1, -1.0f, -1.0f, gilt);

    const float mx = bounds.X + bounds.Width * 0.5f;
    const float my = bounds.Y + bounds.Height * 0.5f;
    DrawEdgeFleuron(g, mx, y0 + 5.0f, true, gilt);
    DrawEdgeFleuron(g, mx, y1 - 5.0f, true, gilt);
    DrawEdgeFleuron(g, x0 + 5.0f, my, false, gilt);
    DrawEdgeFleuron(g, x1 - 5.0f, my, false, gilt);

    g.SetSmoothingMode(prevSmooth);
}

void PaintIconButton(const DRAWITEMSTRUCT* dis, const IconPaintOpts& opts) {
    if (!dis) return;

    bool checked = (dis->itemState & ODS_CHECKED) != 0;
    if (dis->hwndItem && (SendMessageA(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED)) {
        checked = true;
    }
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool selected = pressed || checked;
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    RectF bounds(
        static_cast<REAL>(dis->rcItem.left),
        static_cast<REAL>(dis->rcItem.top),
        static_cast<REAL>(dis->rcItem.right - dis->rcItem.left),
        static_cast<REAL>(dis->rcItem.bottom - dis->rcItem.top));

    Graphics g(dis->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetCompositingMode(CompositingModeSourceCopy);

    // Always paint chrome behind so we never flash system button face (90s look).
    {
        SolidBrush base(Color(255, GetRValue(opts.chromeBg), GetGValue(opts.chromeBg), GetBValue(opts.chromeBg)));
        g.FillRectangle(&base, bounds);
    }
    g.SetCompositingMode(CompositingModeSourceOver);

    if (selected || hot || pressed) {
        const Color top = selected
            ? Color(255, GetRValue(opts.selectedBg), GetGValue(opts.selectedBg), GetBValue(opts.selectedBg))
            : Color(255, GetRValue(opts.elevated), GetGValue(opts.elevated), GetBValue(opts.elevated));
        const Color bot = selected
            ? Color(255,
                (GetRValue(opts.selectedBg) * 3 + GetRValue(opts.accent)) / 4,
                (GetGValue(opts.selectedBg) * 3 + GetGValue(opts.accent)) / 4,
                (GetBValue(opts.selectedBg) * 3 + GetBValue(opts.accent)) / 4)
            : Color(255, GetRValue(opts.chromeBg), GetGValue(opts.chromeBg), GetBValue(opts.chromeBg));
        LinearGradientBrush wash(bounds, top, bot, LinearGradientModeVertical);
        g.FillRectangle(&wash, bounds);

        const Color bronze(255, GetRValue(opts.accent), GetGValue(opts.accent), GetBValue(opts.accent));
        const Color deep(255, GetRValue(opts.accentDeep), GetGValue(opts.accentDeep), GetBValue(opts.accentDeep));
        Pen rim(selected ? deep : bronze, selected ? 1.5f : 1.1f);
        g.DrawRectangle(&rim, RectF(bounds.X + 0.5f, bounds.Y + 0.5f, bounds.Width - 1.0f, bounds.Height - 1.0f));
        if (selected) {
            Pen inner(Color(static_cast<BYTE>(100 + static_cast<int>(opts.pulse * 80)), bronze.GetR(), bronze.GetG(), bronze.GetB()), 1.0f);
            g.DrawRectangle(&inner, RectF(bounds.X + 2.0f, bounds.Y + 2.0f, bounds.Width - 4.0f, bounds.Height - 4.0f));
        }
    }

    float scale = opts.pressScale;
    if (scale < 0.88f) scale = 0.88f;
    if (scale > 1.10f) scale = 1.10f;
    if (scale != 1.0f) {
        const REAL cx = bounds.X + bounds.Width * 0.5f;
        const REAL cy = bounds.Y + bounds.Height * 0.5f;
        Matrix m;
        m.Translate(cx, cy);
        m.Scale(scale, scale);
        m.Translate(-cx, -cy);
        g.SetTransform(&m);
    }

    const UiIcon icon = UiIconFromControlId(static_cast<int>(dis->CtlID));
    Color ink = disabled
        ? Color(255, 150, 140, 125)
        : Color(255, GetRValue(opts.text), GetGValue(opts.text), GetBValue(opts.text));
    if (selected) {
        ink = Color(255, GetRValue(opts.accentDeep), GetGValue(opts.accentDeep), GetBValue(opts.accentDeep));
    }
    if (icon == UiIcon::Color && opts.useColorFill) {
        ink = Color(255, GetRValue(opts.colorFill), GetGValue(opts.colorFill), GetBValue(opts.colorFill));
    }
    DrawUiIcon(g, icon, bounds, ink);
}

