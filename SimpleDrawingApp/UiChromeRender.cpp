#include "UiChromeRender.h"
#include "UiChromeLayout.h"
#include "AppState.h"
#include "AtelierRaii.h"
#include "AtelierFonts.h"
#include "AtelierArtwork.h"
#include "UiChrome.h"
#include <gdiplus.h>

using namespace Gdiplus;
using Atelier::MakeBitmap;

namespace {

void DestroyBrandStrip() {
    gBrandStrip.reset();
    gBrandStripHbmp.reset();
    gBrandStripH = 0;
}

void RebuildBrandStrip(int topH) {
    if (topH < 1) return;
    if (!gBrandStrip || gBrandStripH != topH) {
        DestroyBrandStrip();
        gBrandStrip = MakeBitmap(BRAND_STRIP_W, topH, PixelFormat32bppPARGB);
        if (!gBrandStrip) {
            gBrandStripH = 0;
            return;
        }
        gBrandStripH = topH;
    }
    if (!gBrandStrip) return;

    Graphics g(gBrandStrip.get());
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);

    if (gChromeCache && gChromeCacheW >= BRAND_STRIP_W && gChromeCacheH >= topH) {
        g.DrawImage(
            gChromeCache.get(),
            Rect(0, 0, BRAND_STRIP_W, topH),
            0, 0, BRAND_STRIP_W, topH,
            UnitPixel);
    } else {
        SolidBrush fill(Color(255,
            GetRValue(gTheme.chromeBg), GetGValue(gTheme.chromeBg), GetBValue(gTheme.chromeBg)));
        g.FillRectangle(&fill, 0, 0, BRAND_STRIP_W, topH);
        HDC hdc = g.GetHDC();
        if (hdc) {
            HFONT brand = gBrandFont ? gBrandFont : gUiFont;
            HGDIOBJ oldFont = brand ? SelectObject(hdc, brand) : nullptr;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, gTheme.ink);
            TextOutA(hdc, 48, (topH - 20) / 2, "ATELIER", 7);
            if (oldFont) SelectObject(hdc, oldFont);
            g.ReleaseHDC(hdc);
        }
    }

    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const Color gold(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    DrawBrandCompass(g, 25.0f, static_cast<REAL>(topH) * 0.5f, 11.0f, gold, gUiCompassAngle);

    HBITMAP hb = nullptr;
    if (gBrandStrip->GetHBITMAP(
            Color(255, GetRValue(gTheme.chromeBg), GetGValue(gTheme.chromeBg), GetBValue(gTheme.chromeBg)),
            &hb) == Ok && hb) {
        gBrandStripHbmp.reset(hb);
    } else {
        gBrandStripHbmp.reset();
    }
}

void PaintBrandChild(HDC hdc, int width, int height) {
    if (width < 1 || height < 1) return;
    RebuildBrandStrip(height);
    if (gBrandStripHbmp) {
        HDC mem = CreateCompatibleDC(hdc);
        if (mem) {
            HGDIOBJ old = SelectObject(mem, gBrandStripHbmp.get());
            BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
            return;
        }
    }
    if (gBrandStrip) {
        Graphics g(hdc);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(gBrandStrip.get(), 0, 0, width, height);
    }
}

LRESULT CALLBACK BrandProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        PaintBrandChild(hdc, rc.right - rc.left, rc.bottom - rc.top);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void PaintChromeInto(Graphics& g, int width, int height, const ChromeLayout& chrome) {
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetCompositingMode(CompositingModeSourceCopy);

    const Color stoneA(255, GetRValue(gTheme.chromeBg), GetGValue(gTheme.chromeBg), GetBValue(gTheme.chromeBg));
    const Color stoneB(255, 226, 216, 200);
    const Color deepA(255, GetRValue(gTheme.chromeDeep), GetGValue(gTheme.chromeDeep), GetBValue(gTheme.chromeDeep));
    const Color deepB(255, 208, 196, 178);
    const Color grain(26, 120, 92, 58);

    SolidBrush clear(stoneA);
    g.FillRectangle(&clear, 0, 0, width, height);
    g.SetCompositingMode(CompositingModeSourceOver);

    RectF topR(0.0f, 0.0f, static_cast<REAL>(width), static_cast<REAL>(chrome.topH));
    DrawFrescoPanel(g, topR, stoneA, stoneB, true);
    DrawFrescoGrain(g, topR, grain);

    RectF railR(0.0f, static_cast<REAL>(chrome.topH), static_cast<REAL>(chrome.railW),
        static_cast<REAL>((height - chrome.statusH) - chrome.topH));
    DrawFrescoPanel(g, railR, deepA, deepB, false);
    DrawFrescoGrain(g, railR, grain);
    DrawFrescoArtwork(g, railR, true);

    RectF bottomR(static_cast<REAL>(chrome.railW),
        static_cast<REAL>(height - chrome.statusH - chrome.bottomH),
        static_cast<REAL>((width - chrome.layerW) - chrome.railW),
        static_cast<REAL>(chrome.bottomH));
    DrawFrescoPanel(g, bottomR, stoneB, stoneA, true);
    DrawFrescoGrain(g, bottomR, grain);

    RectF panelR(static_cast<REAL>(width - chrome.layerW), static_cast<REAL>(chrome.topH),
        static_cast<REAL>(chrome.layerW),
        static_cast<REAL>((height - chrome.statusH) - chrome.topH));
    DrawFrescoPanel(g, panelR, stoneA, Color(255, 230, 220, 204), true);
    DrawFrescoGrain(g, panelR, grain);
    DrawFrescoArtwork(g, panelR, false);

    {
        LinearGradientBrush wash(
            PointF(0.0f, 0.0f), PointF(160.0f, 0.0f),
            Color(40, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)),
            Color(0, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent)));
        g.FillRectangle(&wash, RectF(0.0f, 0.0f, 160.0f, static_cast<REAL>(chrome.topH)));
    }

    Pen rule(Color(255, GetRValue(gTheme.chromeLine), GetGValue(gTheme.chromeLine), GetBValue(gTheme.chromeLine)), 1.15f);
    g.DrawLine(&rule, 0.0f, static_cast<REAL>(chrome.topH) - 0.5f, static_cast<REAL>(width), static_cast<REAL>(chrome.topH) - 0.5f);
    g.DrawLine(&rule, static_cast<REAL>(chrome.railW) - 0.5f, static_cast<REAL>(chrome.topH),
        static_cast<REAL>(chrome.railW) - 0.5f, static_cast<REAL>(height - chrome.statusH));
    g.DrawLine(&rule, panelR.X, static_cast<REAL>(chrome.topH), panelR.X, static_cast<REAL>(height - chrome.statusH));
    g.DrawLine(&rule, static_cast<REAL>(chrome.railW), bottomR.Y, panelR.X, bottomR.Y);

    const Color bronze(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));
    const Color rim(255, GetRValue(gTheme.wellRim), GetGValue(gTheme.wellRim), GetBValue(gTheme.wellRim));
    const Color gilt(255, GetRValue(gTheme.accent), GetGValue(gTheme.accent), GetBValue(gTheme.accent));

    RectF well(
        static_cast<REAL>(chrome.railW),
        static_cast<REAL>(chrome.topH),
        static_cast<REAL>(width - chrome.railW - chrome.layerW),
        static_cast<REAL>(height - chrome.topH - chrome.bottomH - chrome.statusH));
    if (well.Width > 8 && well.Height > 8) {
        DrawCanvasWell(g, well, rim, gilt);
    }

    DrawHudCornerTicks(g, topR, bronze, 10.0f);
    DrawHudCornerTicks(g, railR, bronze, 8.0f);
    DrawHudCornerTicks(g, panelR, bronze, 8.0f);
    DrawHudCornerTicks(g, bottomR, bronze, 8.0f);

    HDC hdcCaps = g.GetHDC();
    if (hdcCaps) {
        HFONT caption = gUiFont;
        HGDIOBJ old = caption ? SelectObject(hdcCaps, caption) : nullptr;
        SetBkMode(hdcCaps, TRANSPARENT);
        SetTextColor(hdcCaps, gTheme.accentDeep);
        if (chrome.layerW > 64) {
            TextOutA(hdcCaps, width - chrome.layerW + 10, chrome.topH + 4, "LAYERS", 6);
        }
        if (chrome.railW > 40) {
            TextOutA(hdcCaps, chrome.railW + 14, height - chrome.statusH - chrome.bottomH + 6, "INSTRUMENT", 10);
        }
        if (old) SelectObject(hdcCaps, old);
        g.ReleaseHDC(hdcCaps);
    }
}

void DrawToolbarBackgroundCheap(HDC hdc, const RECT& client, const ChromeLayout& chrome) {
    RECT top = client; top.bottom = chrome.topH;
    FillRect(hdc, &top, gChromeBrush);
    RECT rail = client;
    rail.top = chrome.topH;
    rail.right = chrome.railW;
    rail.bottom = client.bottom - chrome.statusH;
    FillRect(hdc, &rail, gChromeDeepBrush ? gChromeDeepBrush : gChromeBrush);
    RECT bottom = client;
    bottom.top = client.bottom - chrome.statusH - chrome.bottomH;
    bottom.bottom = client.bottom - chrome.statusH;
    bottom.left = chrome.railW;
    bottom.right = client.right - chrome.layerW;
    FillRect(hdc, &bottom, gChromeBrush);
    RECT panel = client;
    panel.left = client.right - chrome.layerW;
    panel.top = chrome.topH;
    panel.bottom = client.bottom - chrome.statusH;
    FillRect(hdc, &panel, gChromeBrush);
}

}  // namespace

void DestroyChromeCache() {
    gChromeCache.reset();
    gChromeCacheW = 0;
    gChromeCacheH = 0;
    gChromeCacheStatusH = -1;
    gChromeCacheRailW = -1;
    gChromeCacheLayerW = -1;
    DestroyBrandStrip();
}

void EnsureChromeCache(int width, int height, const ChromeLayout& chrome) {
    if (width < 1 || height < 1) return;
    if (gChromeCache
        && gChromeCacheW == width
        && gChromeCacheH == height
        && gChromeCacheStatusH == chrome.statusH
        && gChromeCacheRailW == chrome.railW
        && gChromeCacheLayerW == chrome.layerW) {
        return;
    }

    DestroyChromeCache();
    gChromeCache = MakeBitmap(width, height, PixelFormat32bppPARGB);
    if (!gChromeCache) {
        return;
    }
    gChromeCacheW = width;
    gChromeCacheH = height;
    gChromeCacheStatusH = chrome.statusH;
    gChromeCacheRailW = chrome.railW;
    gChromeCacheLayerW = chrome.layerW;

    Graphics g(gChromeCache.get());
    PaintChromeInto(g, width, height, chrome);

    HDC hdc = g.GetHDC();
    if (hdc) {
        HFONT brand = gBrandFont ? gBrandFont : gUiFont;
        HGDIOBJ oldFont = brand ? SelectObject(hdc, brand) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, gTheme.ink);
        TextOutA(hdc, 48, (chrome.topH - 20) / 2, "ATELIER", 7);
        if (oldFont) SelectObject(hdc, oldFont);
        g.ReleaseHDC(hdc);
    }
}

void DrawToolbarBackground(HDC hdc, HWND hwnd, const RECT& client) {
    const ChromeLayout chrome = GetChromeLayout(hwnd);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width < 1 || height < 1) return;

    const bool cacheReady = gChromeCache
        && gChromeCacheW == width
        && gChromeCacheH == height
        && gChromeCacheStatusH == chrome.statusH
        && gChromeCacheRailW == chrome.railW
        && gChromeCacheLayerW == chrome.layerW;
    if (!cacheReady) {
        DrawToolbarBackgroundCheap(hdc, client, chrome);
    } else {
        Graphics g(hdc);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.DrawImage(gChromeCache.get(), 0, 0, width, height);
    }
}

bool RegisterBrandClass(HINSTANCE hInstance) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = BrandProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SimpleDrawingAppBrand";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = 0;
    return RegisterClassA(&wc) != 0;
}

void InvalidateBrandMark() {
    if (hwndBrand) {
        InvalidateRect(hwndBrand, NULL, FALSE);
    }
}

void EnsureBrandStrip(int topH) {
    RebuildBrandStrip(topH);
}
