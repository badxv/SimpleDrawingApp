#include "AtelierArtwork.h"

#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

Bitmap* gRailArt = nullptr;
Bitmap* gLayersArt = nullptr;
bool gInited = false;

std::wstring ModuleDirW() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return full.substr(0, slash);
}

bool FileExistsW(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring ResolveArtwork(const wchar_t* file) {
    const std::wstring dir = ModuleDirW();
    const std::wstring name(file);
    const std::wstring candidates[] = {
        dir + L"\\artwork\\" + name,
        dir + L"\\..\\artwork\\" + name,
        L"artwork\\" + name,
        L"..\\artwork\\" + name,
    };
    for (const auto& c : candidates) {
        if (FileExistsW(c)) return c;
    }
    return {};
}

Bitmap* LoadPng(const wchar_t* file) {
    const std::wstring path = ResolveArtwork(file);
    if (path.empty()) return nullptr;
    Bitmap* bmp = Bitmap::FromFile(path.c_str(), FALSE);
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }
    return bmp;
}

void DrawWatermark(Graphics& g, Bitmap* art, const RectF& bounds, REAL opacity, bool cover) {
    if (!art || bounds.Width < 8.0f || bounds.Height < 8.0f) return;

    const UINT aw = art->GetWidth();
    const UINT ah = art->GetHeight();
    if (aw < 1 || ah < 1) return;

    ColorMatrix cm = {
        1, 0, 0, 0, 0,
        0, 1, 0, 0, 0,
        0, 0, 1, 0, 0,
        0, 0, 0, opacity, 0,
        0, 0, 0, 0, 1
    };
    ImageAttributes attr;
    attr.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    const SmoothingMode prevSmooth = g.GetSmoothingMode();
    const InterpolationMode prevInterp = g.GetInterpolationMode();
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetClip(bounds, CombineModeIntersect);

    if (cover) {
        // Scale to cover panel width, stack vertically to fill tall chrome.
        const REAL drawW = bounds.Width * 0.92f;
        const REAL drawH = drawW * (static_cast<REAL>(ah) / static_cast<REAL>(aw));
        REAL y = bounds.Y + 8.0f;
        int pass = 0;
        while (y < bounds.Y + bounds.Height - 8.0f && pass < 6) {
            const REAL x = bounds.X + (bounds.Width - drawW) * 0.5f;
            const REAL fade = (pass == 0) ? opacity : (opacity * 0.72f);
            cm.m[3][3] = fade;
            attr.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            RectF dest(x, y, drawW, drawH);
            g.DrawImage(art, dest, 0.0f, 0.0f,
                static_cast<REAL>(aw), static_cast<REAL>(ah),
                UnitPixel, &attr);
            y += drawH * 0.88f;
            ++pass;
        }
    } else {
        // Single centered plate for shorter bands.
        const REAL drawH = bounds.Height * 0.86f;
        const REAL drawW = drawH * (static_cast<REAL>(aw) / static_cast<REAL>(ah));
        const REAL x = bounds.X + (bounds.Width - drawW) * 0.5f;
        const REAL y = bounds.Y + (bounds.Height - drawH) * 0.5f;
        RectF dest(x, y, drawW, drawH);
        g.DrawImage(art, dest, 0.0f, 0.0f,
            static_cast<REAL>(aw), static_cast<REAL>(ah),
            UnitPixel, &attr);
    }

    g.ResetClip();
    g.SetInterpolationMode(prevInterp);
    g.SetSmoothingMode(prevSmooth);
}

} // namespace

bool AtelierArtwork_Init() {
    if (gInited) return gRailArt != nullptr || gLayersArt != nullptr;
    gInited = true;
    gRailArt = LoadPng(L"rail-ornithopter.png");
    gLayersArt = LoadPng(L"layers-armillary.png");
    return gRailArt != nullptr || gLayersArt != nullptr;
}

void AtelierArtwork_Shutdown() {
    delete gRailArt;
    delete gLayersArt;
    gRailArt = nullptr;
    gLayersArt = nullptr;
    gInited = false;
}

void DrawFrescoArtwork(Graphics& g, const RectF& bounds, bool leftRail) {
    if (!gInited) AtelierArtwork_Init();
    if (leftRail) {
        DrawWatermark(g, gRailArt, bounds, 0.20f, true);
    } else {
        DrawWatermark(g, gLayersArt, bounds, 0.24f, true);
    }
}
