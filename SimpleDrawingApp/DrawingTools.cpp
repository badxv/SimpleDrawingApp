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

static BYTE BlendChannel(BYTE dst, BYTE src, BYTE alpha) {
    return static_cast<BYTE>((src * alpha + dst * (255 - alpha)) / 255);
}

static int RgbDist2(int r1, int g1, int b1, int r2, int g2, int b2) {
    const int dr = r1 - r2;
    const int dg = g1 - g2;
    const int db = b1 - b2;
    return dr * dr + dg * dg + db * db;
}

// Seed-aware match: transparent empties must not equal opaque black (both RGB 0,0,0).
// Small RGB/alpha tolerance eats antialiased fringe without jumping solid strokes.
static bool MatchesFillTarget(
    BYTE tr, BYTE tg, BYTE tb, BYTE ta,
    BYTE pr, BYTE pg, BYTE pb, BYTE pa)
{
    constexpr int kEmptyAlpha = 20;       // treat as empty / absorb soft fringe
    constexpr int kRgbTol2 = 28 * 28 * 3; // ~28 per channel

    const bool seedEmpty = (ta <= kEmptyAlpha);
    const bool pixelEmpty = (pa <= kEmptyAlpha);

    if (seedEmpty) {
        // Flood through empty + soft AA fringe only; stop at solid ink (e.g. black brush).
        return pixelEmpty || pa < 48;
    }

    // Opaque / colored seed: do not leak into empty holes.
    if (pixelEmpty) return false;

    if (RgbDist2(tr, tg, tb, pr, pg, pb) > kRgbTol2) return false;

    // Alpha should be in a similar ballpark (keeps soft edges coherent).
    const int da = static_cast<int>(ta) - static_cast<int>(pa);
    return (da < 40 && da > -40);
}

bool FloodFillCanvas(Bitmap* bitmap, int x, int y, COLORREF fillColor, BYTE alpha) {
    if (!bitmap) return false;
    if (alpha == 0) return false;

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

    BYTE* seedPx = pixels + y * stride + x * 4;
    const BYTE tb = seedPx[0];
    const BYTE tg = seedPx[1];
    const BYTE tr = seedPx[2];
    const BYTE ta = seedPx[3];

    // Already filled with the same opaque color.
    if (alpha >= 255 && ta >= 250
        && tr == GetRValue(fillColor)
        && tg == GetGValue(fillColor)
        && tb == GetBValue(fillColor)) {
        bitmap->UnlockBits(&data);
        return false;
    }

    auto setColor = [&](int px, int py) {
        BYTE* p = pixels + py * stride + px * 4;
        if (alpha >= 255) {
            p[0] = GetBValue(fillColor);
            p[1] = GetGValue(fillColor);
            p[2] = GetRValue(fillColor);
            p[3] = 255;
        }
        else {
            // Filling empty pixels: write fill with requested alpha.
            if (p[3] <= 20) {
                p[0] = GetBValue(fillColor);
                p[1] = GetGValue(fillColor);
                p[2] = GetRValue(fillColor);
                p[3] = alpha;
            }
            else {
                p[0] = BlendChannel(p[0], GetBValue(fillColor), alpha);
                p[1] = BlendChannel(p[1], GetGValue(fillColor), alpha);
                p[2] = BlendChannel(p[2], GetRValue(fillColor), alpha);
                const int outA = p[3] + ((255 - p[3]) * alpha) / 255;
                p[3] = static_cast<BYTE>(outA > 255 ? 255 : outA);
            }
        }
    };

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

            BYTE* n = pixels + ny * stride + nx * 4;
            if (!MatchesFillTarget(tr, tg, tb, ta, n[2], n[1], n[0], n[3])) continue;

            visited[idx] = 1;
            q.push(POINT{ nx, ny });
        }
    }

    bitmap->UnlockBits(&data);
    return true;
}
