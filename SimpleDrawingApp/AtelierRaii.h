#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>

namespace Atelier {

struct GdiplusObjectDeleter {
    void operator()(Gdiplus::Bitmap* bmp) const noexcept { delete bmp; }
    void operator()(Gdiplus::Graphics* gfx) const noexcept { delete gfx; }
};

using GdiplusBitmapPtr = std::unique_ptr<Gdiplus::Bitmap, GdiplusObjectDeleter>;
using GdiplusGraphicsPtr = std::unique_ptr<Gdiplus::Graphics, GdiplusObjectDeleter>;

struct WinGdiObjectDeleter {
    void operator()(HFONT font) const noexcept {
        if (font) DeleteObject(font);
    }
    void operator()(HBRUSH brush) const noexcept {
        if (brush) DeleteObject(brush);
    }
    void operator()(HBITMAP bmp) const noexcept {
        if (bmp) DeleteObject(bmp);
    }
};

using WinFontHandle = std::unique_ptr<std::remove_pointer_t<HFONT>, WinGdiObjectDeleter>;
using WinBrushHandle = std::unique_ptr<std::remove_pointer_t<HBRUSH>, WinGdiObjectDeleter>;
using WinBitmapHandle = std::unique_ptr<std::remove_pointer_t<HBITMAP>, WinGdiObjectDeleter>;

inline GdiplusBitmapPtr MakeBitmap(int width, int height, Gdiplus::PixelFormat format) {
    auto bmp = GdiplusBitmapPtr(new Gdiplus::Bitmap(width, height, format));
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        return GdiplusBitmapPtr();
    }
    return bmp;
}

inline GdiplusGraphicsPtr MakeGraphics(Gdiplus::Bitmap* bmp) {
    if (!bmp) return GdiplusGraphicsPtr();
    auto gfx = GdiplusGraphicsPtr(Gdiplus::Graphics::FromImage(bmp));
    if (!gfx || gfx->GetLastStatus() != Gdiplus::Ok) {
        return GdiplusGraphicsPtr();
    }
    return gfx;
}

}  // namespace Atelier
