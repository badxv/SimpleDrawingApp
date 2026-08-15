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
}

void DrawFrescoGrain(Graphics& g, const RectF& bounds, Color grain) {
    if (bounds.Width < 8.0f || bounds.Height < 8.0f) return;
    // Very sparse hatch — baked into a chrome cache, not redrawn every frame.
    Pen pen(grain, 1.0f);
    const REAL step = 36.0f;
    const REAL x0 = bounds.X;
    const REAL y0 = bounds.Y;
    const REAL y1 = bounds.Y + bounds.Height;
    for (REAL t = -bounds.Height; t < bounds.Width + bounds.Height; t += step) {
        g.DrawLine(&pen, x0 + t, y0, x0 + t + bounds.Height, y1);
    }
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

    COLORREF bg = opts.chromeBg;
    if (selected) {
        const int pulse = static_cast<int>(opts.pulse * 18.0f);
        bg = RGB(
            (GetRValue(opts.selectedBg) + pulse > 255) ? 255 : GetRValue(opts.selectedBg) + pulse,
            (GetGValue(opts.selectedBg) + pulse / 2 > 255) ? 255 : GetGValue(opts.selectedBg) + pulse / 2,
            GetBValue(opts.selectedBg));
    }
    else if (hot) {
        bg = RGB(
            (GetRValue(opts.chromeBg) * 3 + GetRValue(opts.selectedBg)) / 4,
            (GetGValue(opts.chromeBg) * 3 + GetGValue(opts.selectedBg)) / 4,
            (GetBValue(opts.chromeBg) * 3 + GetBValue(opts.selectedBg)) / 4);
    }

    HBRUSH fill = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, fill);
    DeleteObject(fill);

    Graphics g(dis->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    if (selected) {
        const BYTE alpha = static_cast<BYTE>(130 + static_cast<int>(opts.pulse * 110.0f));
        Pen border(Color(alpha, GetRValue(opts.accent), GetGValue(opts.accent), GetBValue(opts.accent)), 1.6f);
        const REAL inset = 0.5f;
        g.DrawRectangle(&border, RectF(
            static_cast<REAL>(dis->rcItem.left) + inset,
            static_cast<REAL>(dis->rcItem.top) + inset,
            static_cast<REAL>(dis->rcItem.right - dis->rcItem.left) - inset * 2.0f,
            static_cast<REAL>(dis->rcItem.bottom - dis->rcItem.top) - inset * 2.0f));
    }

    RectF bounds(
        static_cast<REAL>(dis->rcItem.left),
        static_cast<REAL>(dis->rcItem.top),
        static_cast<REAL>(dis->rcItem.right - dis->rcItem.left),
        static_cast<REAL>(dis->rcItem.bottom - dis->rcItem.top));

    float scale = opts.pressScale;
    if (scale < 0.85f) scale = 0.85f;
    if (scale > 1.12f) scale = 1.12f;
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
    if (icon == UiIcon::Color && opts.useColorFill) {
        ink = Color(255, GetRValue(opts.colorFill), GetGValue(opts.colorFill), GetBValue(opts.colorFill));
    }
    DrawUiIcon(g, icon, bounds, ink);
}
