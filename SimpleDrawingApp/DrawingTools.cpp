#include "DrawingTools.h"
#include <queue>
#include <vector>

using namespace Gdiplus;

COLORREF ColorFromGdiplus(Color c) {
    return RGB(c.GetR(), c.GetG(), c.GetB());
}

Color GdiplusFromColor(COLORREF c, BYTE alpha) {
    return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static bool SameRgb(COLORREF a, COLORREF b) {
    return GetRValue(a) == GetRValue(b)
        && GetGValue(a) == GetGValue(b)
        && GetBValue(a) == GetBValue(b);
}

bool FloodFillCanvas(Bitmap* bitmap, int x, int y, COLORREF fillColor) {
    if (!bitmap) return false;

    const int width = static_cast<int>(bitmap->GetWidth());
    const int height = static_cast<int>(bitmap->GetHeight());
    if (x < 0 || y < 0 || x >= width || y >= height) return false;

    BitmapData data = {};
    Rect lockRect(0, 0, width, height);
    if (bitmap->LockBits(&lockRect, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) {
        return false;
    }

    auto* pixels = static_cast<BYTE*>(data.Scan0);
    const int stride = data.Stride;

    auto getColor = [&](int px, int py) -> COLORREF {
        BYTE* p = pixels + py * stride + px * 4;
        return RGB(p[2], p[1], p[0]); // BGRA
    };

    auto setColor = [&](int px, int py) {
        BYTE* p = pixels + py * stride + px * 4;
        p[0] = GetBValue(fillColor);
        p[1] = GetGValue(fillColor);
        p[2] = GetRValue(fillColor);
        p[3] = 255;
    };

    const COLORREF target = getColor(x, y);
    if (SameRgb(target, fillColor)) {
        bitmap->UnlockBits(&data);
        return false;
    }

    std::vector<char> visited(static_cast<size_t>(width * height), 0);
    std::queue<POINT> q;
    q.push(POINT{ x, y });
    visited[static_cast<size_t>(y * width + x)] = 1;

    const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    while (!q.empty()) {
        POINT p = q.front();
        q.pop();
        setColor(p.x, p.y);

        for (const auto& d : dirs) {
            const int nx = p.x + d[0];
            const int ny = p.y + d[1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            const size_t idx = static_cast<size_t>(ny * width + nx);
            if (visited[idx]) continue;
            if (!SameRgb(getColor(nx, ny), target)) continue;
            visited[idx] = 1;
            q.push(POINT{ nx, ny });
        }
    }

    bitmap->UnlockBits(&data);
    return true;
}
