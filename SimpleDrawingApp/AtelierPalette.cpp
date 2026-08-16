#include "AtelierPalette.h"

#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

namespace {

const char kClassName[] = "AtelierColorPalette";
constexpr int kRecentMax = 8;
constexpr int kFavMax = 8;

const AppTheme* gTheme = nullptr;

const AppTheme& Theme() {
    static AppTheme fallback{};
    return gTheme ? *gTheme : fallback;
}

COLORREF HsvToRgb(float h, float s, float v) {
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    const float c = v * s;
    const float hp = std::fmod(h, 360.0f) / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (0.0f <= hp && hp < 1.0f) { r = c; g = x; }
    else if (1.0f <= hp && hp < 2.0f) { r = x; g = c; }
    else if (2.0f <= hp && hp < 3.0f) { g = c; b = x; }
    else if (3.0f <= hp && hp < 4.0f) { g = x; b = c; }
    else if (4.0f <= hp && hp < 5.0f) { r = x; b = c; }
    else { r = c; b = x; }
    const float m = v - c;
    return RGB(
        static_cast<int>((r + m) * 255.0f + 0.5f),
        static_cast<int>((g + m) * 255.0f + 0.5f),
        static_cast<int>((b + m) * 255.0f + 0.5f));
}

void RgbToHsv(COLORREF c, float& h, float& s, float& v) {
    const float r = GetRValue(c) / 255.0f;
    const float g = GetGValue(c) / 255.0f;
    const float b = GetBValue(c) / 255.0f;
    const float maxc = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    const float minc = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    const float d = maxc - minc;
    v = maxc;
    s = (maxc <= 0.0001f) ? 0.0f : (d / maxc);
    if (d <= 0.0001f) {
        h = 0.0f;
        return;
    }
    if (maxc == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
    else if (maxc == g) h = 60.0f * ((b - r) / d + 2.0f);
    else h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;
}

bool SameColor(COLORREF a, COLORREF b) {
    return GetRValue(a) == GetRValue(b)
        && GetGValue(a) == GetGValue(b)
        && GetBValue(a) == GetBValue(b);
}

std::wstring ModuleDirW() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return full.substr(0, slash);
}

std::wstring ColorsPathW() {
    return ModuleDirW() + L"\\atelier-colors.txt";
}

struct PaletteState {
    COLORREF fg = RGB(0, 0, 0);
    COLORREF bg = RGB(255, 255, 255);
    float value = 1.0f; // wheel brightness
    std::vector<COLORREF> recent;
    std::vector<COLORREF> favorites;
    HBITMAP wheelBmp = nullptr;
    int wheelSize = 0;
    bool dragging = false;
    bool dragBg = false;
};

PaletteState* StateOf(HWND hwnd) {
    return reinterpret_cast<PaletteState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
}

void NotifyParent(HWND hwnd, int code) {
    HWND parent = GetParent(hwnd);
    if (!parent) return;
    SendMessageA(parent, WM_COMMAND, MAKEWPARAM(ID_PALETTE, code), (LPARAM)hwnd);
}

void DestroyWheel(PaletteState* st) {
    if (st && st->wheelBmp) {
        DeleteObject(st->wheelBmp);
        st->wheelBmp = nullptr;
        st->wheelSize = 0;
    }
}

void RebuildWheel(PaletteState* st, int size) {
    if (!st || size < 8) return;
    if (st->wheelBmp && st->wheelSize == size) return;
    DestroyWheel(st);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    st->wheelBmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!st->wheelBmp || !bits) return;
    st->wheelSize = size;

    auto* px = static_cast<DWORD*>(bits);
    const float cx = (size - 1) * 0.5f;
    const float cy = (size - 1) * 0.5f;
    const float radius = cx - 1.0f;
    const float inner = radius * 0.28f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = x - cx;
            const float dy = y - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            DWORD out = 0;
            if (dist <= radius + 0.5f) {
                if (dist <= inner) {
                    // Neutral center — shows current value grey ring later in paint.
                    const int g = static_cast<int>(st->value * 230.0f);
                    out = RGB(g, g, g) | 0xFF000000;
                } else {
                    float ang = std::atan2(dy, dx) * 180.0f / 3.14159265f;
                    if (ang < 0.0f) ang += 360.0f;
                    const float sat = (dist - inner) / (radius - inner);
                    COLORREF c = HsvToRgb(ang, sat, st->value);
                    // Soft edge AA
                    float a = 1.0f;
                    if (dist > radius - 1.0f) a = radius + 0.5f - dist;
                    if (a < 0.0f) a = 0.0f;
                    if (a > 1.0f) a = 1.0f;
                    const int aa = static_cast<int>(a * 255.0f);
                    out = (aa << 24)
                        | (GetRValue(c))
                        | (GetGValue(c) << 8)
                        | (GetBValue(c) << 16);
                }
            }
            px[y * size + x] = out;
        }
    }
}

void PushUniqueFront(std::vector<COLORREF>& list, COLORREF c, int maxCount) {
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (SameColor(*it, c)) {
            list.erase(it);
            break;
        }
    }
    list.insert(list.begin(), c);
    if (static_cast<int>(list.size()) > maxCount) {
        list.resize(maxCount);
    }
}

bool RemoveColor(std::vector<COLORREF>& list, COLORREF c) {
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (SameColor(*it, c)) {
            list.erase(it);
            return true;
        }
    }
    return false;
}

bool ContainsColor(const std::vector<COLORREF>& list, COLORREF c) {
    for (COLORREF x : list) {
        if (SameColor(x, c)) return true;
    }
    return false;
}

RECT WheelRect(const RECT& client) {
    const int side = (client.right - client.left) - 8;
    const int size = side > 24 ? side : 24;
    RECT rc = {
        client.left + (client.right - client.left - size) / 2,
        client.top + 2,
        0, 0
    };
    rc.right = rc.left + size;
    rc.bottom = rc.top + size;
    return rc;
}

RECT ChipAt(int index, int rowY, int clientW, int chip) {
    // 2×4 grid so 8 recent/favorites fit a narrow tool rail.
    constexpr int kCols = 4;
    constexpr int kGap = 3;
    const int col = index % kCols;
    const int row = index / kCols;
    const int blockW = kCols * chip + (kCols - 1) * kGap;
    int x0 = (clientW - blockW) / 2;
    if (x0 < 2) x0 = 2;
    RECT rc = {
        x0 + col * (chip + kGap),
        rowY + row * (chip + kGap),
        0, 0
    };
    rc.right = rc.left + chip;
    rc.bottom = rc.top + chip;
    return rc;
}

int ChipRowsHeight(int chip) {
    return 2 * chip + 3; // two rows + gap
}

void FillCircle(HDC hdc, int cx, int cy, int r, COLORREF fill, COLORREF rim) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, rim);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
    DeleteObject(br);
}

void PaintPalette(HWND hwnd, HDC hdc) {
    PaletteState* st = StateOf(hwnd);
    if (!st) return;
    const AppTheme& th = Theme();

    RECT client = {};
    GetClientRect(hwnd, &client);
    HBRUSH bg = CreateSolidBrush(th.chromeDeep);
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    RECT wheel = WheelRect(client);
    const int size = wheel.right - wheel.left;
    RebuildWheel(st, size);

    if (st->wheelBmp) {
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, st->wheelBmp);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, wheel.left, wheel.top, size, size, mem, 0, 0, size, size, bf);
        SelectObject(mem, old);
        DeleteDC(mem);
    }

    // FG marker on wheel rim (tiny gilt ring).
    float h = 0, s = 0, v = 0;
    RgbToHsv(st->fg, h, s, v);
    {
        const float cx = (wheel.left + wheel.right) * 0.5f;
        const float cy = (wheel.top + wheel.bottom) * 0.5f;
        const float radius = size * 0.5f - 1.0f;
        const float inner = radius * 0.28f;
        const float dist = inner + s * (radius - inner);
        const float rad = h * 3.14159265f / 180.0f;
        const int mx = static_cast<int>(cx + std::cos(rad) * dist + 0.5f);
        const int my = static_cast<int>(cy + std::sin(rad) * dist + 0.5f);
        FillCircle(hdc, mx, my, 4, st->fg, th.accentDeep);
    }

    // Value strip under wheel.
    const int stripY = wheel.bottom + 6;
    const int stripH = 8;
    const int stripX0 = client.left + 6;
    const int stripX1 = client.right - 6;
    for (int x = stripX0; x < stripX1; ++x) {
        const float t = (stripX1 > stripX0)
            ? static_cast<float>(x - stripX0) / static_cast<float>(stripX1 - stripX0)
            : 1.0f;
        COLORREF c = HsvToRgb(h, (s < 0.05f ? 0.0f : s), t);
        HPEN pen = CreatePen(PS_SOLID, 1, c);
        HGDIOBJ old = SelectObject(hdc, pen);
        MoveToEx(hdc, x, stripY, nullptr);
        LineTo(hdc, x, stripY + stripH);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }
    {
        const int kx = stripX0 + static_cast<int>((stripX1 - stripX0) * st->value + 0.5f);
        FillCircle(hdc, kx, stripY + stripH / 2, 4, RGB(250, 244, 230), th.accentDeep);
    }

    // Recent chips (2×4).
    int y = stripY + stripH + 10;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, th.accentDeep);
    TextOutA(hdc, 6, y - 12, "Recent", 6);
    const int chip = 12;
    const int clientW = client.right - client.left;
    const int nRecent = static_cast<int>(st->recent.size());
    for (int i = 0; i < nRecent; ++i) {
        RECT rc = ChipAt(i, y, clientW, chip);
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        FillCircle(hdc, cx, cy, chip / 2, st->recent[i], th.chromeLine);
    }

    // Favorites chips (2×4).
    y += ChipRowsHeight(chip) + 14;
    TextOutA(hdc, 6, y - 12, "Favorites", 9);
    const int nFav = static_cast<int>(st->favorites.size());
    for (int i = 0; i < nFav; ++i) {
        RECT rc = ChipAt(i, y, clientW, chip);
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        FillCircle(hdc, cx, cy, chip / 2, st->favorites[i], th.accentDeep);
        // Tiny pin mark (gilt dot).
        FillCircle(hdc, cx + chip / 2 - 1, cy - chip / 2 + 1, 2, th.accent, th.accentDeep);
    }

    // Pin current FG affordance (+ button beside Favorites label).
    {
        RECT pin = { client.right - 16, y - 13, client.right - 4, y - 1 };
        HPEN pen = CreatePen(PS_SOLID, 1, th.accent);
        HGDIOBJ old = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        Rectangle(hdc, pin.left, pin.top, pin.right, pin.bottom);
        // + mark
        MoveToEx(hdc, pin.left + 6, pin.top + 2, nullptr);
        LineTo(hdc, pin.left + 6, pin.bottom - 2);
        MoveToEx(hdc, pin.left + 2, pin.top + 6, nullptr);
        LineTo(hdc, pin.right - 2, pin.top + 6);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }
}

bool HitWheel(const RECT& wheel, int x, int y, float* outH, float* outS) {
    const float cx = (wheel.left + wheel.right) * 0.5f;
    const float cy = (wheel.top + wheel.bottom) * 0.5f;
    const float dx = x - cx;
    const float dy = y - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float radius = (wheel.right - wheel.left) * 0.5f - 1.0f;
    const float inner = radius * 0.28f;
    if (dist > radius) return false;
    if (dist <= inner) return false;
    float ang = std::atan2(dy, dx) * 180.0f / 3.14159265f;
    if (ang < 0.0f) ang += 360.0f;
    *outH = ang;
    *outS = (dist - inner) / (radius - inner);
    if (*outS < 0.0f) *outS = 0.0f;
    if (*outS > 1.0f) *outS = 1.0f;
    return true;
}

int HitChipRow(const std::vector<COLORREF>& list, int rowY, int clientW, int x, int y, int chip) {
    const int n = static_cast<int>(list.size());
    for (int i = 0; i < n; ++i) {
        RECT rc = ChipAt(i, rowY, clientW, chip);
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) return i;
    }
    return -1;
}

void Persist(PaletteState* st) {
    if (!st) return;
    FILE* f = nullptr;
#ifdef _MSC_VER
    _wfopen_s(&f, ColorsPathW().c_str(), L"wb");
#else
    f = _wfopen(ColorsPathW().c_str(), L"wb");
#endif
    if (!f) return;
    fprintf(f, "recent=");
    for (size_t i = 0; i < st->recent.size(); ++i) {
        if (i) fputc(',', f);
        fprintf(f, "%02X%02X%02X", GetRValue(st->recent[i]), GetGValue(st->recent[i]), GetBValue(st->recent[i]));
    }
    fprintf(f, "\nfavorite=");
    for (size_t i = 0; i < st->favorites.size(); ++i) {
        if (i) fputc(',', f);
        fprintf(f, "%02X%02X%02X", GetRValue(st->favorites[i]), GetGValue(st->favorites[i]), GetBValue(st->favorites[i]));
    }
    fprintf(f, "\n");
    fclose(f);
}

COLORREF ParseHex6(const char* s) {
    unsigned r = 0, g = 0, b = 0;
    if (std::sscanf(s, "%02X%02X%02X", &r, &g, &b) == 3) {
        return RGB(r, g, b);
    }
    return RGB(0, 0, 0);
}

void LoadList(std::vector<COLORREF>& out, const char* line, int maxCount) {
    out.clear();
    const char* p = line;
    while (*p && out.size() < static_cast<size_t>(maxCount)) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        char buf[8] = {};
        int n = 0;
        while (*p && *p != ',' && n < 6) buf[n++] = *p++;
        buf[n] = 0;
        if (n == 6) out.push_back(ParseHex6(buf));
        if (*p == ',') ++p;
    }
}

void LoadPersist(PaletteState* st) {
    if (!st) return;
    FILE* f = nullptr;
#ifdef _MSC_VER
    _wfopen_s(&f, ColorsPathW().c_str(), L"rb");
#else
    f = _wfopen(ColorsPathW().c_str(), L"rb");
#endif
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "recent=", 7) == 0) LoadList(st->recent, line + 7, kRecentMax);
        else if (std::strncmp(line, "favorite=", 9) == 0) LoadList(st->favorites, line + 9, kFavMax);
    }
    fclose(f);
}

LRESULT CALLBACK PaletteProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCREATE: {
        auto* st = new PaletteState();
        // Seed a few defaults into recent if empty after load.
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        return TRUE;
    }
    case WM_CREATE:
        if (PaletteState* st = StateOf(hwnd)) {
            LoadPersist(st);
            if (st->recent.empty()) {
                st->recent = {
                    RGB(0, 0, 0), RGB(255, 255, 255), RGB(232, 17, 35), RGB(247, 99, 12),
                    RGB(255, 185, 0), RGB(16, 124, 16), RGB(0, 120, 212), RGB(136, 23, 152)
                };
            }
        }
        return 0;
    case WM_NCDESTROY: {
        PaletteState* st = StateOf(hwnd);
        if (st) {
            Persist(st);
            DestroyWheel(st);
            delete st;
        }
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
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintPalette(hwnd, mem);
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        PaletteState* st = StateOf(hwnd);
        if (!st) return 0;
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        RECT client = {};
        GetClientRect(hwnd, &client);
        RECT wheel = WheelRect(client);
        const int size = wheel.right - wheel.left;
        const int stripY = wheel.bottom + 6;
        const int stripH = 8;
        const int chip = 12;
        const int recentY = stripY + stripH + 10;
        const int favY = recentY + ChipRowsHeight(chip) + 14;

        // Pin button
        RECT pin = { client.right - 16, favY - 13, client.right - 4, favY - 1 };
        POINT pt = { x, y };
        if (msg == WM_LBUTTONDOWN && PtInRect(&pin, pt)) {
            PushUniqueFront(st->favorites, st->fg, kFavMax);
            Persist(st);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        float hh = 0, ss = 0;
        if (HitWheel(wheel, x, y, &hh, &ss)) {
            COLORREF c = HsvToRgb(hh, ss, st->value);
            SetCapture(hwnd);
            st->dragging = true;
            st->dragBg = (msg == WM_RBUTTONDOWN) || ((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (st->dragBg) {
                st->bg = c;
                NotifyParent(hwnd, 2);
            } else {
                st->fg = c;
                PushUniqueFront(st->recent, c, kRecentMax);
                NotifyParent(hwnd, 1);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Value strip
        if (msg == WM_LBUTTONDOWN && y >= stripY && y < stripY + stripH
            && x >= client.left + 6 && x <= client.right - 6) {
            const float t = static_cast<float>(x - (client.left + 6))
                / static_cast<float>((client.right - 6) - (client.left + 6));
            st->value = t < 0 ? 0 : (t > 1 ? 1 : t);
            DestroyWheel(st);
            float fh, fs, fv;
            RgbToHsv(st->fg, fh, fs, fv);
            st->fg = HsvToRgb(fh, fs, st->value);
            PushUniqueFront(st->recent, st->fg, kRecentMax);
            NotifyParent(hwnd, 1);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Center of wheel → open picker
        {
            const float cx = (wheel.left + wheel.right) * 0.5f;
            const float cy = (wheel.top + wheel.bottom) * 0.5f;
            const float radius = size * 0.5f - 1.0f;
            const float inner = radius * 0.28f;
            const float dx = x - cx;
            const float dy = y - cy;
            if (std::sqrt(dx * dx + dy * dy) <= inner) {
                NotifyParent(hwnd, (msg == WM_RBUTTONDOWN) ? 4 : 3);
                return 0;
            }
        }

        int ri = HitChipRow(st->recent, recentY, client.right - client.left, x, y, chip);
        if (ri >= 0) {
            COLORREF c = st->recent[ri];
            if (msg == WM_RBUTTONDOWN) {
                PushUniqueFront(st->favorites, c, kFavMax);
                Persist(st);
            } else {
                st->fg = c;
                PushUniqueFront(st->recent, c, kRecentMax);
                NotifyParent(hwnd, 1);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        int fi = HitChipRow(st->favorites, favY, client.right - client.left, x, y, chip);
        if (fi >= 0) {
            COLORREF c = st->favorites[fi];
            if (msg == WM_RBUTTONDOWN) {
                RemoveColor(st->favorites, c);
                Persist(st);
            } else {
                st->fg = c;
                PushUniqueFront(st->recent, c, kRecentMax);
                NotifyParent(hwnd, 1);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        PaletteState* st = StateOf(hwnd);
        if (!st || !st->dragging) return 0;
        RECT client = {};
        GetClientRect(hwnd, &client);
        RECT wheel = WheelRect(client);
        float hh = 0, ss = 0;
        if (HitWheel(wheel, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &hh, &ss)) {
            COLORREF c = HsvToRgb(hh, ss, st->value);
            if (st->dragBg) {
                if (!SameColor(st->bg, c)) {
                    st->bg = c;
                    NotifyParent(hwnd, 2);
                }
            } else {
                if (!SameColor(st->fg, c)) {
                    st->fg = c;
                    NotifyParent(hwnd, 1);
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        PaletteState* st = StateOf(hwnd);
        if (st && st->dragging) {
            st->dragging = false;
            ReleaseCapture();
            if (!st->dragBg) {
                PushUniqueFront(st->recent, st->fg, kRecentMax);
                Persist(st);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: {
        PaletteState* st = StateOf(hwnd);
        if (st) st->dragging = false;
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

bool AtelierPalette_Register() {
    static bool done = false;
    if (done) return true;
    WNDCLASSA wc = {};
    wc.lpfnWndProc = PaletteProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    done = true;
    return true;
}

void AtelierPalette_SetTheme(const AppTheme* theme) {
    gTheme = theme;
}

HWND AtelierPalette_Create(HWND parent, int x, int y, int w, int h) {
    return CreateWindowExA(
        0, kClassName, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, w, h,
        parent, (HMENU)(INT_PTR)ID_PALETTE, GetModuleHandle(NULL), NULL);
}

int AtelierPalette_IdealHeight(int width) {
    const int wheel = (width > 16) ? (width - 8) : 24;
    const int chip = 12;
    // wheel + value strip + recent block + favorites block + padding
    return 2 + wheel + 6 + 8 + 10 + ChipRowsHeight(chip) + 14 + ChipRowsHeight(chip) + 8;
}

void AtelierPalette_SetColors(HWND hwnd, COLORREF fg, COLORREF bg) {
    PaletteState* st = StateOf(hwnd);
    if (!st) return;
    float h, s, v;
    RgbToHsv(fg, h, s, v);
    st->fg = fg;
    st->bg = bg;
    if (v > 0.02f) st->value = v;
    DestroyWheel(st);
    InvalidateRect(hwnd, NULL, FALSE);
}

void AtelierPalette_GetColors(HWND hwnd, COLORREF* fg, COLORREF* bg) {
    PaletteState* st = StateOf(hwnd);
    if (!st) return;
    if (fg) *fg = st->fg;
    if (bg) *bg = st->bg;
}

void AtelierPalette_NoteColor(HWND hwnd, COLORREF color) {
    PaletteState* st = StateOf(hwnd);
    if (!st) return;
    PushUniqueFront(st->recent, color, kRecentMax);
    Persist(st);
    InvalidateRect(hwnd, NULL, FALSE);
}

void AtelierPalette_Load(HWND hwnd) {
    PaletteState* st = StateOf(hwnd);
    if (!st) return;
    LoadPersist(st);
    InvalidateRect(hwnd, NULL, FALSE);
}

void AtelierPalette_Save(HWND hwnd) {
    PaletteState* st = StateOf(hwnd);
    if (!st) return;
    Persist(st);
}
