#include "AtelierControls.h"

#include <commctrl.h>
#include <windowsx.h>

namespace {

const char kSliderClass[] = "AtelierSlider";
const char kScrollClass[] = "AtelierScroll";

const AppTheme* gTheme = nullptr;

const AppTheme& Theme() {
    static AppTheme fallback{};
    return gTheme ? *gTheme : fallback;
}

COLORREF MixRgb(COLORREF a, COLORREF b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
    const int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
    return RGB(
        static_cast<int>(ar + (br - ar) * t + 0.5f),
        static_cast<int>(ag + (bg - ag) * t + 0.5f),
        static_cast<int>(ab + (bb - ab) * t + 0.5f));
}

int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

struct SliderState {
    int minVal = 1;
    int maxVal = 100;
    int pos = 1;
    bool dragging = false;
};

struct ScrollState {
    bool vertical = false;
    int minVal = 0;
    int maxVal = 0;
    int page = 1;
    int pos = 0;
    bool dragging = false;
    int dragGrab = 0;
};

SliderState* SliderOf(HWND hwnd) {
    return reinterpret_cast<SliderState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
}

ScrollState* ScrollOf(HWND hwnd) {
    return reinterpret_cast<ScrollState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
}

int ScrollTravel(const ScrollState& s) {
    const int span = (s.maxVal - s.minVal + 1) - s.page;
    return (span > 0) ? span : 0;
}

bool ScrollIsNeeded(const ScrollState& s) {
    return ScrollTravel(s) > 0;
}

void FillRectColor(HDC hdc, const RECT& rc, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

void FrameRectColor(HDC hdc, const RECT& rc, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FrameRect(hdc, &rc, br);
    DeleteObject(br);
}

RECT SliderTrackRect(const RECT& client) {
    const int cy = (client.top + client.bottom) / 2;
    RECT rc = { client.left + 6, cy - 3, client.right - 6, cy + 3 };
    return rc;
}

int SliderThumbX(const SliderState& st, const RECT& track) {
    const int range = st.maxVal - st.minVal;
    if (range <= 0) return track.left;
    const int travel = track.right - track.left;
    const int t = st.pos - st.minVal;
    return track.left + (travel * t) / range;
}

RECT SliderThumbRect(const SliderState& st, const RECT& client) {
    const RECT track = SliderTrackRect(client);
    const int cx = SliderThumbX(st, track);
    const int cy = (client.top + client.bottom) / 2;
    RECT rc = { cx - 7, cy - 9, cx + 7, cy + 9 };
    return rc;
}

int SliderPosFromX(const SliderState& st, const RECT& track, int x) {
    const int range = st.maxVal - st.minVal;
    if (range <= 0) return st.minVal;
    const int travel = track.right - track.left;
    if (travel <= 0) return st.minVal;
    int t = x - track.left;
    if (t < 0) t = 0;
    if (t > travel) t = travel;
    return st.minVal + (t * range + travel / 2) / travel;
}

void NotifySlider(HWND hwnd, WORD code) {
    const SliderState* st = SliderOf(hwnd);
    if (!st) return;
    HWND parent = GetParent(hwnd);
    if (!parent) return;
    SendMessageA(parent, WM_HSCROLL, MAKEWPARAM(code, st->pos), (LPARAM)hwnd);
}

void PaintSlider(HWND hwnd, HDC hdc) {
    const SliderState* st = SliderOf(hwnd);
    if (!st) return;
    const AppTheme& th = Theme();

    RECT client = {};
    GetClientRect(hwnd, &client);
    FillRectColor(hdc, client, th.chromeBg);

    RECT track = SliderTrackRect(client);
    FillRectColor(hdc, track, MixRgb(th.chromeDeep, th.wellRim, 0.35f));
    FrameRectColor(hdc, track, MixRgb(th.chromeLine, th.accentDeep, 0.4f));

    RECT fill = track;
    fill.right = SliderThumbX(*st, track);
    if (fill.right > fill.left) {
        FillRectColor(hdc, fill, MixRgb(th.accent, th.chromeElevated, 0.55f));
    }

    RECT thumb = SliderThumbRect(*st, client);
    FillRectColor(hdc, thumb, MixRgb(th.chromeElevated, th.toolSelectedBg, 0.35f));
    FrameRectColor(hdc, thumb, th.accent);
    RECT inner = { thumb.left + 2, thumb.top + 2, thumb.right - 2, thumb.bottom - 2 };
    FrameRectColor(hdc, inner, MixRgb(th.accent, th.chromeElevated, 0.5f));
}

LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCREATE: {
        auto* st = new SliderState();
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        return TRUE;
    }
    case WM_NCDESTROY: {
        delete SliderOf(hwnd);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right > 0 ? rc.right : 1;
        const int h = rc.bottom > 0 ? rc.bottom : 1;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = mem ? CreateCompatibleBitmap(hdc, w, h) : nullptr;
        if (!mem || !bmp) {
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
            PaintSlider(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintSlider(hwnd, mem);
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case TBM_SETRANGE: {
        SliderState* st = SliderOf(hwnd);
        if (!st) return 0;
        st->minVal = (int)LOWORD(lParam);
        st->maxVal = (int)HIWORD(lParam);
        if (st->minVal > st->maxVal) {
            const int t = st->minVal;
            st->minVal = st->maxVal;
            st->maxVal = t;
        }
        st->pos = ClampInt(st->pos, st->minVal, st->maxVal);
        if (wParam) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case TBM_SETPOS: {
        SliderState* st = SliderOf(hwnd);
        if (!st) return 0;
        st->pos = ClampInt((int)lParam, st->minVal, st->maxVal);
        if (wParam) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case TBM_GETPOS: {
        const SliderState* st = SliderOf(hwnd);
        return st ? st->pos : 0;
    }
    case WM_LBUTTONDOWN: {
        SliderState* st = SliderOf(hwnd);
        if (!st) return 0;
        SetCapture(hwnd);
        st->dragging = true;
        RECT client = {};
        GetClientRect(hwnd, &client);
        st->pos = SliderPosFromX(*st, SliderTrackRect(client), GET_X_LPARAM(lParam));
        InvalidateRect(hwnd, NULL, FALSE);
        NotifySlider(hwnd, SB_THUMBTRACK);
        return 0;
    }
    case WM_MOUSEMOVE: {
        SliderState* st = SliderOf(hwnd);
        if (!st || !st->dragging) return 0;
        RECT client = {};
        GetClientRect(hwnd, &client);
        const int next = SliderPosFromX(*st, SliderTrackRect(client), GET_X_LPARAM(lParam));
        if (next != st->pos) {
            st->pos = next;
            InvalidateRect(hwnd, NULL, FALSE);
            NotifySlider(hwnd, SB_THUMBTRACK);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        SliderState* st = SliderOf(hwnd);
        if (!st) return 0;
        if (st->dragging) {
            st->dragging = false;
            ReleaseCapture();
            NotifySlider(hwnd, SB_THUMBPOSITION);
            NotifySlider(hwnd, SB_ENDSCROLL);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: {
        SliderState* st = SliderOf(hwnd);
        if (st) st->dragging = false;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        HWND parent = GetParent(hwnd);
        if (parent) return SendMessageA(parent, WM_MOUSEWHEEL, wParam, lParam);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void ThumbMetrics(const ScrollState& s, const RECT& client, int* outThumbLen, int* outThumbPos) {
    const int thick = s.vertical ? (client.right - client.left) : (client.bottom - client.top);
    const int trackLen = s.vertical ? (client.bottom - client.top) : (client.right - client.left);
    const int travel = ScrollTravel(s);
    int thumbLen = trackLen;
    if (travel > 0 && s.page > 0) {
        const int content = s.maxVal - s.minVal + 1;
        if (content > 0) {
            thumbLen = (trackLen * s.page) / content;
        }
        const int minThumb = thick + 4;
        if (thumbLen < minThumb) thumbLen = minThumb;
        if (thumbLen > trackLen) thumbLen = trackLen;
    }
    int thumbPos = 0;
    if (travel > 0) {
        const int free = trackLen - thumbLen;
        if (free > 0) {
            thumbPos = (free * (s.pos - s.minVal)) / travel;
        }
    }
    *outThumbLen = thumbLen;
    *outThumbPos = thumbPos;
}

RECT ScrollThumbRect(const ScrollState& s, const RECT& client) {
    int thumbLen = 0, thumbPos = 0;
    ThumbMetrics(s, client, &thumbLen, &thumbPos);
    RECT rc = client;
    if (s.vertical) {
        rc.top = client.top + thumbPos;
        rc.bottom = rc.top + thumbLen;
        rc.left = client.left + 1;
        rc.right = client.right - 1;
    } else {
        rc.left = client.left + thumbPos;
        rc.right = rc.left + thumbLen;
        rc.top = client.top + 1;
        rc.bottom = client.bottom - 1;
    }
    return rc;
}

int ScrollPosFromPointer(const ScrollState& s, const RECT& client, int pointer, int grab) {
    int thumbLen = 0, unused = 0;
    ThumbMetrics(s, client, &thumbLen, &unused);
    const int trackLen = s.vertical ? (client.bottom - client.top) : (client.right - client.left);
    const int travel = ScrollTravel(s);
    if (travel <= 0) return s.minVal;
    const int free = trackLen - thumbLen;
    if (free <= 0) return s.minVal;
    int nextThumb = pointer - grab - (s.vertical ? client.top : client.left);
    nextThumb = ClampInt(nextThumb, 0, free);
    return s.minVal + (nextThumb * travel + free / 2) / free;
}

void NotifyScroll(HWND hwnd, WORD code) {
    const ScrollState* st = ScrollOf(hwnd);
    if (!st) return;
    HWND parent = GetParent(hwnd);
    if (!parent) return;
    const UINT msg = st->vertical ? WM_VSCROLL : WM_HSCROLL;
    SendMessageA(parent, msg, MAKEWPARAM(code, st->pos), (LPARAM)hwnd);
}

void PaintScroll(HWND hwnd, HDC hdc) {
    const ScrollState* st = ScrollOf(hwnd);
    if (!st) return;
    const AppTheme& th = Theme();

    RECT client = {};
    GetClientRect(hwnd, &client);

    FillRectColor(hdc, client, MixRgb(th.chromeDeep, th.wellRim, 0.25f));
    FrameRectColor(hdc, client, MixRgb(th.chromeLine, th.accentDeep, 0.35f));

    if (!ScrollIsNeeded(*st)) return;

    RECT thumb = ScrollThumbRect(*st, client);
    FillRectColor(hdc, thumb, MixRgb(th.chromeElevated, th.toolSelectedBg, 0.4f));
    FrameRectColor(hdc, thumb, th.accent);
    RECT inner = { thumb.left + 1, thumb.top + 1, thumb.right - 1, thumb.bottom - 1 };
    if (inner.right > inner.left && inner.bottom > inner.top) {
        FrameRectColor(hdc, inner, MixRgb(th.accent, th.chromeElevated, 0.55f));
    }
}

LRESULT CALLBACK ScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* st = new ScrollState();
        st->vertical = cs->lpCreateParams && (reinterpret_cast<INT_PTR>(cs->lpCreateParams) != 0);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        return TRUE;
    }
    case WM_NCDESTROY: {
        delete ScrollOf(hwnd);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right > 0 ? rc.right : 1;
        const int h = rc.bottom > 0 ? rc.bottom : 1;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = mem ? CreateCompatibleBitmap(hdc, w, h) : nullptr;
        if (!mem || !bmp) {
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
            PaintScroll(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintScroll(hwnd, mem);
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        ScrollState* st = ScrollOf(hwnd);
        if (!st || !ScrollIsNeeded(*st)) return 0;
        RECT client = {};
        GetClientRect(hwnd, &client);
        const int px = GET_X_LPARAM(lParam);
        const int py = GET_Y_LPARAM(lParam);
        const RECT thumb = ScrollThumbRect(*st, client);
        const int pointer = st->vertical ? py : px;
        POINT pt = { px, py };
        if (PtInRect(&thumb, pt)) {
            SetCapture(hwnd);
            st->dragging = true;
            st->dragGrab = pointer - (st->vertical ? thumb.top : thumb.left);
            return 0;
        }
        // Page jump toward click.
        const int page = st->page > 0 ? st->page : 1;
        const int travel = ScrollTravel(*st);
        int delta = 0;
        if (st->vertical) {
            delta = (py < thumb.top) ? -page : page;
        } else {
            delta = (px < thumb.left) ? -page : page;
        }
        st->pos = ClampInt(st->pos + delta, st->minVal, st->minVal + travel);
        InvalidateRect(hwnd, NULL, FALSE);
        NotifyScroll(hwnd, SB_THUMBTRACK);
        NotifyScroll(hwnd, SB_ENDSCROLL);
        return 0;
    }
    case WM_MOUSEMOVE: {
        ScrollState* st = ScrollOf(hwnd);
        if (!st || !st->dragging) return 0;
        RECT client = {};
        GetClientRect(hwnd, &client);
        const int pointer = st->vertical ? GET_Y_LPARAM(lParam) : GET_X_LPARAM(lParam);
        const int next = ScrollPosFromPointer(*st, client, pointer, st->dragGrab);
        if (next != st->pos) {
            st->pos = next;
            InvalidateRect(hwnd, NULL, FALSE);
            NotifyScroll(hwnd, SB_THUMBTRACK);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ScrollState* st = ScrollOf(hwnd);
        if (!st) return 0;
        if (st->dragging) {
            st->dragging = false;
            ReleaseCapture();
            NotifyScroll(hwnd, SB_THUMBPOSITION);
            NotifyScroll(hwnd, SB_ENDSCROLL);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: {
        ScrollState* st = ScrollOf(hwnd);
        if (st) st->dragging = false;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        HWND parent = GetParent(hwnd);
        if (parent) return SendMessageA(parent, WM_MOUSEWHEEL, wParam, lParam);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

bool AtelierControls_Register() {
    static bool done = false;
    if (done) return true;

    WNDCLASSA sc = {};
    sc.lpfnWndProc = SliderProc;
    sc.hInstance = GetModuleHandle(NULL);
    sc.lpszClassName = kSliderClass;
    sc.hCursor = LoadCursor(NULL, IDC_HAND);
    sc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassA(&sc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSA bc = {};
    bc.lpfnWndProc = ScrollProc;
    bc.hInstance = GetModuleHandle(NULL);
    bc.lpszClassName = kScrollClass;
    bc.hCursor = LoadCursor(NULL, IDC_ARROW);
    bc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassA(&bc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    done = true;
    return true;
}

void AtelierControls_SetTheme(const AppTheme* theme) {
    gTheme = theme;
}

HWND AtelierSlider_Create(HWND parent, int x, int y, int w, int h, HMENU idOrNull) {
    return CreateWindowExA(
        0, kSliderClass, "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h,
        parent, idOrNull, GetModuleHandle(NULL), NULL);
}

HWND AtelierScroll_Create(HWND parent, bool vertical, int x, int y, int w, int h) {
    return CreateWindowExA(
        0, kScrollClass, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, w, h,
        parent, NULL, GetModuleHandle(NULL),
        reinterpret_cast<LPVOID>(static_cast<INT_PTR>(vertical ? 1 : 0)));
}

void AtelierScroll_SetInfo(HWND hwnd, int minVal, int maxVal, int page, int pos, bool redraw) {
    ScrollState* st = ScrollOf(hwnd);
    if (!st) return;
    st->minVal = minVal;
    st->maxVal = maxVal;
    st->page = page > 0 ? page : 1;
    const int travel = ScrollTravel(*st);
    st->pos = ClampInt(pos, st->minVal, st->minVal + travel);
    if (redraw) InvalidateRect(hwnd, NULL, FALSE);
}

int AtelierScroll_GetPos(HWND hwnd) {
    const ScrollState* st = ScrollOf(hwnd);
    return st ? st->pos : 0;
}

int AtelierScroll_GetPage(HWND hwnd) {
    const ScrollState* st = ScrollOf(hwnd);
    return st ? st->page : 1;
}

bool AtelierScroll_IsNeeded(HWND hwnd) {
    const ScrollState* st = ScrollOf(hwnd);
    return st && ScrollIsNeeded(*st);
}
