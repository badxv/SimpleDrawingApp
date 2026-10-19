#include "GraphicsBackend.h"
#include "AppPaths.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "Resource.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <d2d1.h>
#ifdef _MSC_VER
#pragma comment(lib, "d2d1.lib")
#endif

namespace {

GraphicsBackend gBackend = GraphicsBackend::Auto;
bool gD2dProbed = false;
bool gD2dAvailable = false;

ID2D1Factory* gD2dFactory = nullptr;
ID2D1HwndRenderTarget* gD2dRt = nullptr;
HWND gD2dHwnd = nullptr;
int gD2dW = 0;
int gD2dH = 0;

void ReleaseD2dTarget() {
    if (gD2dRt) {
        gD2dRt->Release();
        gD2dRt = nullptr;
    }
    gD2dHwnd = nullptr;
    gD2dW = 0;
    gD2dH = 0;
}

bool ProbeD2d() {
    if (gD2dProbed) return gD2dAvailable;
    gD2dProbed = true;
    gD2dAvailable = false;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &gD2dFactory);
    if (FAILED(hr) || !gD2dFactory) {
        gD2dFactory = nullptr;
        return false;
    }
    gD2dAvailable = true;
    return true;
}

bool EnsureD2dTarget(HWND hwnd, int w, int h) {
    if (!ProbeD2d() || !gD2dFactory || !hwnd || w < 1 || h < 1) return false;
    if (gD2dRt && gD2dHwnd == hwnd && gD2dW == w && gD2dH == h) return true;
    ReleaseD2dTarget();
    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h));
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(hwnd, size);
    HRESULT hr = gD2dFactory->CreateHwndRenderTarget(rtp, hrtp, &gD2dRt);
    if (FAILED(hr) || !gD2dRt) {
        gD2dRt = nullptr;
        return false;
    }
    gD2dHwnd = hwnd;
    gD2dW = w;
    gD2dH = h;
    return true;
}

void SetMenuRadio(HMENU menu, UINT id, bool on) {
    if (!menu) return;
    CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

} // namespace

GraphicsBackend GetGraphicsBackend() {
    return gBackend;
}

void SetGraphicsBackend(GraphicsBackend backend) {
    gBackend = backend;
    if (backend == GraphicsBackend::Software) {
        ReleaseD2dTarget();
    }
    SaveGraphicsBackend();
    SyncGraphicsBackendMenuItems();
    if (hwndViewport) InvalidateRect(hwndViewport, NULL, FALSE);
}

bool IsDirect2DAvailable() {
    return ProbeD2d();
}

GraphicsBackend EffectiveGraphicsBackend() {
    if (gBackend == GraphicsBackend::Software) return GraphicsBackend::Software;
    if (gBackend == GraphicsBackend::Hardware) {
        return ProbeD2d() ? GraphicsBackend::Hardware : GraphicsBackend::Software;
    }
    // Auto: prefer Direct2D on native Windows; Wine's D2D path is usually slower.
    if (IsRunningUnderWine()) return GraphicsBackend::Software;
    return ProbeD2d() ? GraphicsBackend::Hardware : GraphicsBackend::Software;
}

bool PresentViewportAccelerated(HWND hwnd, HDC destDc, HDC srcMemDc, int viewW, int viewH) {
    (void)destDc;
    if (EffectiveGraphicsBackend() != GraphicsBackend::Hardware) return false;
    if (!EnsureD2dTarget(hwnd, viewW, viewH) || !gD2dRt || !srcMemDc) return false;

    HBITMAP hbmp = static_cast<HBITMAP>(GetCurrentObject(srcMemDc, OBJ_BITMAP));
    if (!hbmp) return false;

    BITMAP bm = {};
    if (!GetObject(hbmp, sizeof(bm), &bm) || bm.bmWidth < 1 || bm.bmHeight < 1) return false;

    const int w = viewW;
    const int h = viewH;
    const size_t stride = static_cast<size_t>(w) * 4u;
    std::vector<BYTE> pixels(stride * static_cast<size_t>(h));

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    if (GetDIBits(srcMemDc, hbmp, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS) == 0) {
        return false;
    }

    ID2D1Bitmap* bmp = nullptr;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    HRESULT hr = gD2dRt->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)),
        pixels.data(),
        static_cast<UINT32>(stride),
        props,
        &bmp);
    if (FAILED(hr) || !bmp) return false;

    gD2dRt->BeginDraw();
    gD2dRt->Clear(D2D1::ColorF(D2D1::ColorF::White));
    gD2dRt->DrawBitmap(bmp, D2D1::RectF(0, 0, static_cast<FLOAT>(w), static_cast<FLOAT>(h)));
    bmp->Release();
    hr = gD2dRt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseD2dTarget();
        return false;
    }
    return SUCCEEDED(hr);
}

void LoadGraphicsBackend() {
    gBackend = GraphicsBackend::Auto;
    char iniPath[MAX_PATH] = {};
    GetFeaturesIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    const int v = GetPrivateProfileIntA("Features", "GraphicsBackend", 0, iniPath);
    if (v == 1) gBackend = GraphicsBackend::Software;
    else if (v == 2) gBackend = GraphicsBackend::Hardware;
    else gBackend = GraphicsBackend::Auto;
}

void SaveGraphicsBackend() {
    char iniPath[MAX_PATH] = {};
    GetFeaturesIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    char buf[8];
    sprintf_s(buf, "%d", static_cast<int>(gBackend));
    WritePrivateProfileStringA("Features", "GraphicsBackend", buf, iniPath);
}

void SyncGraphicsBackendMenuItems() {
    if (!gAppMenu) return;
    HMENU viewMenu = GetSubMenu(gAppMenu, 3);
    if (!viewMenu) return;
    const GraphicsBackend b = GetGraphicsBackend();
    SetMenuRadio(viewMenu, IDM_GFX_AUTO, b == GraphicsBackend::Auto);
    SetMenuRadio(viewMenu, IDM_GFX_SOFTWARE, b == GraphicsBackend::Software);
    SetMenuRadio(viewMenu, IDM_GFX_HARDWARE, b == GraphicsBackend::Hardware);
    // Grey Hardware when D2D missing.
    EnableMenuItem(viewMenu, IDM_GFX_HARDWARE,
        MF_BYCOMMAND | (IsDirect2DAvailable() ? MF_ENABLED : MF_GRAYED));
}
