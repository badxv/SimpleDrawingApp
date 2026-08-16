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
    case IDC_TOOL_SHAPES: return UiIcon::Shapes;
    case IDC_SHAPE_RECT: return UiIcon::Rect;
    case IDC_SHAPE_ELLIPSE: return UiIcon::Ellipse;
    case IDC_SHAPE_TRIANGLE: return UiIcon::Triangle;
    case IDC_SHAPE_STAR: return UiIcon::Star;
    case IDC_SHAPE_DIAMOND: return UiIcon::Diamond;
    case IDC_SHAPE_ROUNDRECT: return UiIcon::RoundRect;
    case IDC_SHAPE_MODE_STROKE: return UiIcon::ShapeStroke;
    case IDC_SHAPE_MODE_FILL: return UiIcon::ShapeFill;
    case IDC_SHAPE_MODE_BOTH: return UiIcon::ShapeBoth;
    case IDC_SWAP_COLORS: return UiIcon::SwapColors;
    case IDC_NEW_BUTTON: return UiIcon::NewDoc;
    case IDC_LOAD_BUTTON: return UiIcon::Open;
    case IDC_SAVE_BUTTON: return UiIcon::Save;
    case IDC_UNDO_BUTTON: return UiIcon::Undo;
    case IDC_REDO_BUTTON: return UiIcon::Redo;
    case IDC_CLEAR_BUTTON: return UiIcon::Clear;
    case IDC_COLOR_BUTTON: return UiIcon::Color;
    case IDC_BG_BUTTON: return UiIcon::Color;
    case IDC_LAYER_ADD: return UiIcon::LayerAdd;
    case IDC_LAYER_DEL: return UiIcon::LayerDel;
    case IDC_LAYER_UP: return UiIcon::LayerUp;
    case IDC_LAYER_DOWN: return UiIcon::LayerDown;
    case IDC_TOGGLE_RAIL:
        // Direction filled by paint opts via selected state — default left.
        return UiIcon::ChevronLeft;
    case IDC_TOGGLE_LAYERS:
        return UiIcon::ChevronRight;
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
    case IDC_TOOL_SHAPES:
    case IDC_SHAPE_RECT:
    case IDC_SHAPE_ELLIPSE:
    case IDC_SHAPE_TRIANGLE:
    case IDC_SHAPE_STAR:
    case IDC_SHAPE_DIAMOND:
    case IDC_SHAPE_ROUNDRECT:
    case IDC_SHAPE_MODE_STROKE:
    case IDC_SHAPE_MODE_FILL:
    case IDC_SHAPE_MODE_BOTH:
    case IDC_SWAP_COLORS:
    case IDC_NEW_BUTTON:
    case IDC_LOAD_BUTTON:
    case IDC_SAVE_BUTTON:
    case IDC_UNDO_BUTTON:
    case IDC_REDO_BUTTON:
    case IDC_CLEAR_BUTTON:
    case IDC_COLOR_BUTTON:
    case IDC_BG_BUTTON:
    case IDC_LAYER_ADD:
    case IDC_LAYER_DEL:
    case IDC_LAYER_UP:
    case IDC_LAYER_DOWN:
    case IDC_TOGGLE_RAIL:
    case IDC_TOGGLE_LAYERS:
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
    case IDC_TOOL_SHAPES:
    case IDC_COLOR_BUTTON:
    case IDC_BG_BUTTON:
    case IDC_SWAP_COLORS:
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

namespace {

void MotifStroke(Pen& pen) {
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
}

// One wallpaper cell: manuscript volute × thin orbital HUD (very low contrast).
void DrawMotifCell(Graphics& g, float cx, float cy, float scale, Color ink, int variant) {
    const BYTE a = ink.GetA() > 0 ? ink.GetA() : static_cast<BYTE>(22);
    const BYTE aSoft = static_cast<BYTE>((a * 2) / 3);
    const BYTE aFaint = static_cast<BYTE>((a * 2) / 5);

    Pen pen(Color(a, ink.GetR(), ink.GetG(), ink.GetB()), 1.0f);
    MotifStroke(pen);
    Pen soft(Color(aSoft, ink.GetR(), ink.GetG(), ink.GetB()), 0.9f);
    MotifStroke(soft);
    Pen faint(Color(aFaint, ink.GetR(), ink.GetG(), ink.GetB()), 0.8f);
    MotifStroke(faint);

    const float r = scale;
    // Futurism: nested construction rings + dashed outer orbit.
    g.DrawEllipse(&faint, cx - r * 1.15f, cy - r * 1.15f, r * 2.3f, r * 2.3f);
    g.DrawEllipse(&soft, cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f);

    REAL dashVals[2] = { 2.5f, 3.5f };
    faint.SetDashStyle(DashStyleCustom);
    faint.SetDashPattern(dashVals, 2);
    g.DrawEllipse(&faint, cx - r * 1.45f, cy - r * 1.45f, r * 2.9f, r * 2.9f);
    faint.SetDashStyle(DashStyleSolid);

    // Crosshair ticks (HUD).
    const float tick = r * 0.22f;
    g.DrawLine(&soft, cx - r * 1.45f, cy, cx - r * 1.45f + tick, cy);
    g.DrawLine(&soft, cx + r * 1.45f - tick, cy, cx + r * 1.45f, cy);
    g.DrawLine(&soft, cx, cy - r * 1.45f, cx, cy - r * 1.45f + tick);
    g.DrawLine(&soft, cx, cy + r * 1.45f - tick, cx, cy + r * 1.45f);

    // Renaissance: paired volutes (mirror) — variant flips leaf bias.
    const float sx = (variant & 1) ? -1.0f : 1.0f;
    GraphicsPath volL;
    volL.AddBezier(
        PointF(cx - r * 0.15f, cy + r * 0.1f),
        PointF(cx - r * 0.85f, cy + r * 0.55f),
        PointF(cx - r * 1.05f, cy - r * 0.15f),
        PointF(cx - r * 0.45f, cy - r * 0.35f));
    GraphicsPath volR;
    volR.AddBezier(
        PointF(cx + r * 0.15f, cy + r * 0.1f),
        PointF(cx + r * 0.85f, cy + r * 0.55f),
        PointF(cx + r * 1.05f, cy - r * 0.15f),
        PointF(cx + r * 0.45f, cy - r * 0.35f));
    g.DrawPath(&pen, &volL);
    g.DrawPath(&pen, &volR);

    // Tiny fleuron / lozenge hub.
    const float d = r * 0.18f;
    PointF dia[4] = {
        PointF(cx, cy - d),
        PointF(cx + d, cy),
        PointF(cx, cy + d),
        PointF(cx - d, cy)
    };
    g.DrawPolygon(&soft, dia, 4);

    // Leaf tip accents.
    GraphicsPath leaf;
    leaf.AddBezier(
        PointF(cx + sx * r * 0.55f, cy - r * 0.55f),
        PointF(cx + sx * r * 0.95f, cy - r * 0.95f),
        PointF(cx + sx * r * 1.15f, cy - r * 0.35f),
        PointF(cx + sx * r * 0.7f, cy - r * 0.25f));
    g.DrawPath(&faint, &leaf);

    // Astrolabe chord (futurist).
    if ((variant % 3) != 0) {
        const float ang = (variant & 2) ? 0.55f : -0.4f;
        const float c = cosf(ang);
        const float s = sinf(ang);
        g.DrawLine(&faint,
            cx + c * r * 0.2f, cy + s * r * 0.2f,
            cx + c * r * 1.2f, cy + s * r * 1.2f);
    }
}

void DrawMotifBandRule(Graphics& g, float x0, float x1, float y, Color ink) {
    const BYTE a = ink.GetA() > 0 ? ink.GetA() : static_cast<BYTE>(18);
    Pen pen(Color(a, ink.GetR(), ink.GetG(), ink.GetB()), 0.85f);
    MotifStroke(pen);
    const float mid = (x0 + x1) * 0.5f;
    g.DrawLine(&pen, x0 + 6.0f, y, mid - 8.0f, y);
    g.DrawLine(&pen, mid + 8.0f, y, x1 - 6.0f, y);
    // Ornamental break: tiny diamond
    const float d = 2.4f;
    PointF dia[4] = {
        PointF(mid, y - d),
        PointF(mid + d, y),
        PointF(mid, y + d),
        PointF(mid - d, y)
    };
    g.DrawPolygon(&pen, dia, 4);
}

} // namespace

void DrawFrescoMotifs(Graphics& g, const RectF& bounds, Color ink, bool verticalPanel) {
    if (bounds.Width < 20.0f || bounds.Height < 24.0f) return;

    const SmoothingMode prevSmooth = g.GetSmoothingMode();

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetClip(bounds, CombineModeIntersect);

    if (verticalPanel) {
        const float cx = bounds.X + bounds.Width * 0.5f;
        const bool narrow = bounds.Width < 70.0f;
        const float scale = narrow ? 10.0f : 16.0f;
        const float step = scale * (narrow ? 4.4f : 4.0f);
        // Start below typical caption / first control row.
        float y = bounds.Y + (narrow ? 40.0f : 48.0f);
        int variant = 0;
        for (; y < bounds.Y + bounds.Height - 24.0f; y += step, ++variant) {
            DrawMotifCell(g, cx, y, scale, ink, variant);
            // Soft linking vine between cells (manuscript column).
            if (y + step < bounds.Y + bounds.Height - 24.0f) {
                const BYTE a = ink.GetA() > 0 ? ink.GetA() : static_cast<BYTE>(30);
                Pen vine(Color(static_cast<BYTE>((a * 3) / 5), ink.GetR(), ink.GetG(), ink.GetB()), 0.9f);
                MotifStroke(vine);
                GraphicsPath link;
                link.AddBezier(
                    PointF(cx, y + scale * 1.55f),
                    PointF(cx + scale * 0.6f, y + step * 0.35f),
                    PointF(cx - scale * 0.6f, y + step * 0.65f),
                    PointF(cx, y + step - scale * 1.55f));
                g.DrawPath(&vine, &link);
            }
        }
        // Wide panels: offset secondary column of fainter cells (wallpaper density).
        if (!narrow && bounds.Width > 120.0f) {
            Color faintInk(
                static_cast<BYTE>(ink.GetA() > 8 ? ink.GetA() - 8 : ink.GetA()),
                ink.GetR(), ink.GetG(), ink.GetB());
            const float cx2 = bounds.X + bounds.Width * 0.72f;
            const float scale2 = scale * 0.55f;
            int v2 = 1;
            for (float y2 = bounds.Y + 70.0f; y2 < bounds.Y + bounds.Height - 30.0f; y2 += step * 1.15f, ++v2) {
                DrawMotifCell(g, cx2, y2, scale2, faintInk, v2 + 3);
            }
        }
        // Side hairline pilasters (engraved plate edge).
        {
            const BYTE a = ink.GetA() > 0 ? ink.GetA() : static_cast<BYTE>(26);
            Pen edge(Color(static_cast<BYTE>((a * 3) / 5), ink.GetR(), ink.GetG(), ink.GetB()), 0.85f);
            MotifStroke(edge);
            g.DrawLine(&edge, bounds.X + 3.5f, bounds.Y + 10.0f,
                bounds.X + 3.5f, bounds.Y + bounds.Height - 10.0f);
            g.DrawLine(&edge, bounds.X + bounds.Width - 3.5f, bounds.Y + 10.0f,
                bounds.X + bounds.Width - 3.5f, bounds.Y + bounds.Height - 10.0f);
        }
    } else {
        // Horizontal chrome strip: sparse cells + ornamental rules.
        const float cy = bounds.Y + bounds.Height * 0.55f;
        const float scale = 8.0f;
        const float step = 96.0f;
        int variant = 0;
        for (float x = bounds.X + 48.0f; x < bounds.X + bounds.Width - 40.0f; x += step, ++variant) {
            if ((variant % 2) == 0) {
                DrawMotifCell(g, x, cy, scale, ink, variant);
            }
        }
        DrawMotifBandRule(g, bounds.X, bounds.X + bounds.Width,
            bounds.Y + bounds.Height - 3.0f, ink);
    }

    g.ResetClip();
    g.SetSmoothingMode(prevSmooth);
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
    case UiIcon::Triangle: {
        PointF pts[3] = {
            { cx, r.Y + 2 },
            { r.X + 2, bottom - 2 },
            { right - 2, bottom - 2 }
        };
        g.DrawPolygon(&pen, pts, 3);
        break;
    }
    case UiIcon::Star: {
        PointF pts[10];
        for (int i = 0; i < 10; ++i) {
            const float ang = -1.5707963f + i * 3.14159265f / 5.0f;
            const float rad = (i % 2 == 0) ? 0.48f : 0.20f;
            pts[i] = PointF(cx + cosf(ang) * r.Width * rad, cy + sinf(ang) * r.Height * rad);
        }
        g.DrawPolygon(&pen, pts, 10);
        break;
    }
    case UiIcon::Diamond: {
        PointF pts[4] = {
            { cx, r.Y + 2 },
            { right - 2, cy },
            { cx, bottom - 2 },
            { r.X + 2, cy }
        };
        g.DrawPolygon(&pen, pts, 4);
        break;
    }
    case UiIcon::RoundRect: {
        const REAL rr = 4.0f;
        GraphicsPath path;
        RectF box(r.X + 2, r.Y + 3, r.Width - 4, r.Height - 6);
        path.AddArc(box.X, box.Y, rr * 2, rr * 2, 180, 90);
        path.AddArc(box.X + box.Width - rr * 2, box.Y, rr * 2, rr * 2, 270, 90);
        path.AddArc(box.X + box.Width - rr * 2, box.Y + box.Height - rr * 2, rr * 2, rr * 2, 0, 90);
        path.AddArc(box.X, box.Y + box.Height - rr * 2, rr * 2, rr * 2, 90, 90);
        path.CloseFigure();
        g.DrawPath(&pen, &path);
        break;
    }
    case UiIcon::Shapes: {
        g.DrawRectangle(&pen, RectF(r.X + 3, r.Y + 8, r.Width * 0.42f, r.Height * 0.42f));
        g.DrawEllipse(&pen, RectF(cx - 1, r.Y + 3, r.Width * 0.42f, r.Height * 0.42f));
        break;
    }
    case UiIcon::ShapeStroke:
        g.DrawRectangle(&pen, RectF(r.X + 4, r.Y + 5, r.Width - 8, r.Height - 10));
        break;
    case UiIcon::ShapeFill: {
        SolidBrush fill(color);
        g.FillRectangle(&fill, RectF(r.X + 4, r.Y + 5, r.Width - 8, r.Height - 10));
        break;
    }
    case UiIcon::ShapeBoth: {
        SolidBrush fill(Color(160, color.GetR(), color.GetG(), color.GetB()));
        RectF box(r.X + 4, r.Y + 5, r.Width - 8, r.Height - 10);
        g.FillRectangle(&fill, box);
        g.DrawRectangle(&pen, box);
        break;
    }
    case UiIcon::SwapColors: {
        g.DrawLine(&pen, r.X + 4, cy - 4, right - 4, cy - 4);
        g.DrawLine(&pen, right - 8, cy - 8, right - 4, cy - 4);
        g.DrawLine(&pen, right - 8, cy, right - 4, cy - 4);
        g.DrawLine(&pen, right - 4, cy + 4, r.X + 4, cy + 4);
        g.DrawLine(&pen, r.X + 4, cy + 4, r.X + 8, cy);
        g.DrawLine(&pen, r.X + 4, cy + 4, r.X + 8, cy + 8);
        break;
    }
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
    case UiIcon::ChevronLeft: {
        PointF pts[3] = {
            { r.X + 5, cy },
            { right - 5, r.Y + 4 },
            { right - 5, bottom - 4 }
        };
        g.DrawPolygon(&pen, pts, 3);
        break;
    }
    case UiIcon::ChevronRight: {
        PointF pts[3] = {
            { right - 5, cy },
            { r.X + 5, r.Y + 4 },
            { r.X + 5, bottom - 4 }
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

// Corner cartouche: L-hairline + double volute + leaf, sized for the mount band.
void DrawCornerVolute(Graphics& g, float cx, float cy, float sx, float sy, Color gilt) {
    Pen arm(Color(230, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.15f);
    StrokeRound(arm);
    const float reach = 16.0f;
    g.DrawLine(&arm, cx + sx * 0.5f, cy, cx + sx * reach, cy);
    g.DrawLine(&arm, cx, cy + sy * 0.5f, cx, cy + sy * reach);

    // Outer acanthus scroll
    Pen scroll(Color(210, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.25f);
    StrokeRound(scroll);
    GraphicsPath outer;
    const float x0 = cx + sx * 2.0f;
    const float y0 = cy + sy * 2.0f;
    outer.AddBezier(
        PointF(cx + sx * 1.0f, cy + sy * 10.0f),
        PointF(x0 + sx * 0.5f, y0 + sy * 8.5f),
        PointF(x0 + sx * 8.5f, y0 + sy * 8.0f),
        PointF(x0 + sx * 9.0f, y0 + sy * 2.5f));
    g.DrawPath(&scroll, &outer);

    // Inner counter-scroll
    Pen inner(Color(190, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.05f);
    StrokeRound(inner);
    GraphicsPath coil;
    coil.AddBezier(
        PointF(x0 + sx * 3.0f, y0 + sy * 3.0f),
        PointF(x0 + sx * 3.2f, y0 + sy * 6.8f),
        PointF(x0 + sx * 7.2f, y0 + sy * 6.5f),
        PointF(x0 + sx * 7.0f, y0 + sy * 3.5f));
    coil.AddBezier(
        PointF(x0 + sx * 7.0f, y0 + sy * 3.5f),
        PointF(x0 + sx * 6.8f, y0 + sy * 1.6f),
        PointF(x0 + sx * 4.4f, y0 + sy * 1.8f),
        PointF(x0 + sx * 4.6f, y0 + sy * 3.8f));
    g.DrawPath(&inner, &coil);

    SolidBrush eye(Color(200, gilt.GetR(), gilt.GetG(), gilt.GetB()));
    g.FillEllipse(&eye, RectF(x0 + sx * 5.0f - 1.5f, y0 + sy * 3.6f - 1.5f, 3.0f, 3.0f));

    // Trifoliate leaf tip along the top/side arm
    GraphicsPath leaf;
    leaf.AddBezier(
        PointF(cx + sx * 10.0f, cy + sy * 0.6f),
        PointF(cx + sx * 13.5f, cy - sy * 1.2f),
        PointF(cx + sx * 15.0f, cy + sy * 3.5f),
        PointF(cx + sx * 11.5f, cy + sy * 3.8f));
    leaf.AddBezier(
        PointF(cx + sx * 11.5f, cy + sy * 3.8f),
        PointF(cx + sx * 13.0f, cy + sy * 5.5f),
        PointF(cx + sx * 10.5f, cy + sy * 5.0f),
        PointF(cx + sx * 9.5f, cy + sy * 2.5f));
    Pen leafPen(Color(175, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.0f);
    StrokeRound(leafPen);
    g.DrawPath(&leafPen, &leaf);
}

// Mid-edge fleuron: lozenge rosette with side curls.
void DrawEdgeFleuron(Graphics& g, float cx, float cy, bool horizontal, Color gilt) {
    Pen pen(Color(205, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.1f);
    StrokeRound(pen);
    const float a = 3.6f;
    PointF diamond[4] = {
        PointF(cx, cy - a),
        PointF(cx + a, cy),
        PointF(cx, cy + a),
        PointF(cx - a, cy)
    };
    g.DrawPolygon(&pen, diamond, 4);
    // Tiny inner diamond
    const float b = 1.6f;
    PointF inner[4] = {
        PointF(cx, cy - b),
        PointF(cx + b, cy),
        PointF(cx, cy + b),
        PointF(cx - b, cy)
    };
    Pen fine(Color(160, gilt.GetR(), gilt.GetG(), gilt.GetB()), 0.85f);
    g.DrawPolygon(&fine, inner, 4);

    Pen curl(Color(170, gilt.GetR(), gilt.GetG(), gilt.GetB()), 0.95f);
    StrokeRound(curl);
    if (horizontal) {
        GraphicsPath left;
        left.AddBezier(
            PointF(cx - a - 1.0f, cy),
            PointF(cx - a - 4.0f, cy - 3.5f),
            PointF(cx - a - 7.0f, cy + 2.5f),
            PointF(cx - a - 3.5f, cy + 1.0f));
        GraphicsPath right;
        right.AddBezier(
            PointF(cx + a + 1.0f, cy),
            PointF(cx + a + 4.0f, cy + 3.5f),
            PointF(cx + a + 7.0f, cy - 2.5f),
            PointF(cx + a + 3.5f, cy - 1.0f));
        g.DrawPath(&curl, &left);
        g.DrawPath(&curl, &right);
    } else {
        GraphicsPath up;
        up.AddBezier(
            PointF(cx, cy - a - 1.0f),
            PointF(cx - 3.5f, cy - a - 4.0f),
            PointF(cx + 2.5f, cy - a - 7.0f),
            PointF(cx + 1.0f, cy - a - 3.5f));
        GraphicsPath down;
        down.AddBezier(
            PointF(cx, cy + a + 1.0f),
            PointF(cx + 3.5f, cy + a + 4.0f),
            PointF(cx - 2.5f, cy + a + 7.0f),
            PointF(cx - 1.0f, cy + a + 3.5f));
        g.DrawPath(&curl, &up);
        g.DrawPath(&curl, &down);
    }
}

} // namespace

void DrawCanvasWell(Graphics& g, const RectF& bounds, Color rim, Color gilt) {
    if (bounds.Width < 24.0f || bounds.Height < 24.0f) return;

    const SmoothingMode prevSmooth = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Quiet mount — no heavy shadow slab.
    SolidBrush mount(Color(22, rim.GetR(), rim.GetG(), rim.GetB()));
    g.FillRectangle(&mount, bounds);

    // Single outer hairline.
    Pen outer(Color(215, gilt.GetR(), gilt.GetG(), gilt.GetB()), 1.0f);
    StrokeRound(outer);
    g.DrawRectangle(&outer, RectF(bounds.X + 0.5f, bounds.Y + 0.5f,
        bounds.Width - 1.0f, bounds.Height - 1.0f));

    // Whisper inner rule near the viewport edge of the mount.
    const float inset = 9.0f;
    if (bounds.Width > inset * 2 + 8.0f && bounds.Height > inset * 2 + 8.0f) {
        Pen inner(Color(100, rim.GetR(), rim.GetG(), rim.GetB()), 0.8f);
        StrokeRound(inner);
        g.DrawRectangle(&inner, RectF(
            bounds.X + inset, bounds.Y + inset,
            bounds.Width - inset * 2.0f, bounds.Height - inset * 2.0f));
    }

    // Corners sit on the mount; slight outward bias so scrolls read against fresco.
    const float x0 = bounds.X + 0.5f;
    const float y0 = bounds.Y + 0.5f;
    const float x1 = bounds.X + bounds.Width - 0.5f;
    const float y1 = bounds.Y + bounds.Height - 0.5f;

    DrawCornerVolute(g, x0, y0, 1.0f, 1.0f, gilt);
    DrawCornerVolute(g, x1, y0, -1.0f, 1.0f, gilt);
    DrawCornerVolute(g, x0, y1, 1.0f, -1.0f, gilt);
    DrawCornerVolute(g, x1, y1, -1.0f, -1.0f, gilt);

    // Fleurons centered in the mount band (away from scrollbar thumbs when possible).
    const float band = 5.5f;
    const float mx = bounds.X + bounds.Width * 0.5f;
    const float my = bounds.Y + bounds.Height * 0.5f;
    DrawEdgeFleuron(g, mx, y0 + band, true, gilt);
    DrawEdgeFleuron(g, mx, y1 - band, true, gilt);
    DrawEdgeFleuron(g, x0 + band, my, false, gilt);
    DrawEdgeFleuron(g, x1 - band, my, false, gilt);

    g.SetSmoothingMode(prevSmooth);
}

void PaintIconButton(const DRAWITEMSTRUCT* dis, const IconPaintOpts& opts) {
    if (!dis) return;

    bool checked = false;
    if (opts.useAppSelected) {
        checked = opts.appSelected;
    } else {
        checked = (dis->itemState & ODS_CHECKED) != 0;
        if (dis->hwndItem && (SendMessageA(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED)) {
            checked = true;
        }
    }
    // Momentary mouse-down only — do not treat as sticky selection.
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    // Sticky selected plate = checked/appSelected. Pressed is transient overlay.
    const bool selected = checked;
    const bool showPlate = selected || pressed || hot;

    RectF bounds(
        static_cast<REAL>(dis->rcItem.left),
        static_cast<REAL>(dis->rcItem.top),
        static_cast<REAL>(dis->rcItem.right - dis->rcItem.left),
        static_cast<REAL>(dis->rcItem.bottom - dis->rcItem.top));

    Graphics g(dis->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetCompositingMode(CompositingModeSourceCopy);

    const int bw = dis->rcItem.right - dis->rcItem.left;
    const int bh = dis->rcItem.bottom - dis->rcItem.top;

    bool sampledFresco = false;
    if (!showPlate && opts.frescoCache && bw > 0 && bh > 0) {
        const int cw = opts.frescoCache->GetWidth();
        const int ch = opts.frescoCache->GetHeight();
        if (opts.frescoX >= 0 && opts.frescoY >= 0
            && opts.frescoX + bw <= cw && opts.frescoY + bh <= ch) {
            g.DrawImage(
                opts.frescoCache,
                Rect(0, 0, bw, bh),
                opts.frescoX, opts.frescoY, bw, bh,
                UnitPixel);
            sampledFresco = true;
        }
    }
    if (!sampledFresco) {
        SolidBrush base(Color(255, GetRValue(opts.chromeBg), GetGValue(opts.chromeBg), GetBValue(opts.chromeBg)));
        g.FillRectangle(&base, bounds);
    }
    g.SetCompositingMode(CompositingModeSourceOver);

    if (showPlate) {
        const bool sticky = selected;
        const Color top = sticky
            ? Color(255, GetRValue(opts.selectedBg), GetGValue(opts.selectedBg), GetBValue(opts.selectedBg))
            : Color(255, GetRValue(opts.elevated), GetGValue(opts.elevated), GetBValue(opts.elevated));
        const Color bot = sticky
            ? Color(255,
                (GetRValue(opts.selectedBg) * 3 + GetRValue(opts.accent)) / 4,
                (GetGValue(opts.selectedBg) * 3 + GetGValue(opts.accent)) / 4,
                (GetBValue(opts.selectedBg) * 3 + GetBValue(opts.accent)) / 4)
            : Color(255, GetRValue(opts.chromeBg), GetGValue(opts.chromeBg), GetBValue(opts.chromeBg));
        LinearGradientBrush wash(bounds, top, bot, LinearGradientModeVertical);
        g.FillRectangle(&wash, bounds);

        const Color bronze(255, GetRValue(opts.accent), GetGValue(opts.accent), GetBValue(opts.accent));
        const Color deep(255, GetRValue(opts.accentDeep), GetGValue(opts.accentDeep), GetBValue(opts.accentDeep));
        Pen rim(sticky ? deep : bronze, sticky ? 1.5f : 1.1f);
        g.DrawRectangle(&rim, RectF(bounds.X + 0.5f, bounds.Y + 0.5f, bounds.Width - 1.0f, bounds.Height - 1.0f));
        if (sticky) {
            Pen inner(Color(static_cast<BYTE>(100 + static_cast<int>(opts.pulse * 80)), bronze.GetR(), bronze.GetG(), bronze.GetB()), 1.0f);
            g.DrawRectangle(&inner, RectF(bounds.X + 2.0f, bounds.Y + 2.0f, bounds.Width - 4.0f, bounds.Height - 4.0f));
        }
    }

    // Transient press: slight icon scale (does not stick after mouse-up).
    float scale = opts.pressScale;
    if (pressed && !selected) {
        scale = 0.92f;
    }
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

    const UiIcon iconBase = UiIconFromControlId(static_cast<int>(dis->CtlID));
    UiIcon icon = iconBase;
    if (dis->CtlID == IDC_TOGGLE_RAIL) {
        icon = opts.appSelected ? UiIcon::ChevronLeft : UiIcon::ChevronRight;
    } else if (dis->CtlID == IDC_TOGGLE_LAYERS) {
        icon = opts.appSelected ? UiIcon::ChevronRight : UiIcon::ChevronLeft;
    }
    Color ink = disabled
        ? Color(255, 150, 140, 125)
        : Color(255, GetRValue(opts.text), GetGValue(opts.text), GetBValue(opts.text));
    if (selected) {
        ink = Color(255, GetRValue(opts.accentDeep), GetGValue(opts.accentDeep), GetBValue(opts.accentDeep));
    }
    if (icon == UiIcon::Color && opts.useColorFill) {
        SolidBrush fill(Color(255, GetRValue(opts.colorFill), GetGValue(opts.colorFill), GetBValue(opts.colorFill)));
        const RectF chip(bounds.X + 3.0f, bounds.Y + 3.0f, bounds.Width - 6.0f, bounds.Height - 6.0f);
        g.FillRectangle(&fill, chip);
        Pen rim(Color(230, GetRValue(opts.accentDeep), GetGValue(opts.accentDeep), GetBValue(opts.accentDeep)), 1.2f);
        g.DrawRectangle(&rim, chip);
        // Checker hint for near-white colors.
        if (GetRValue(opts.colorFill) > 245 && GetGValue(opts.colorFill) > 245 && GetBValue(opts.colorFill) > 245) {
            Pen edge(Color(180, 160, 150, 140), 1.0f);
            g.DrawRectangle(&edge, chip);
        }
        return;
    }
    DrawUiIcon(g, icon, bounds, ink);
}

