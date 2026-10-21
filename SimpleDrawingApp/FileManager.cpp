#include "FileManager.h"
#include <gdiplus.h>
#include <string>
#include <cstring>
#include <cwctype>

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

namespace {

std::wstring GetFileExtensionW(const wchar_t* filename) {
    if (!filename) return L"";
    const wchar_t* dot = wcsrchr(filename, L'.');
    if (!dot || !dot[1]) return L"";
    std::wstring ext(dot + 1);
    for (wchar_t& c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return ext;
}

bool GetEncoderClsidW(const wchar_t* filename, CLSID* clsid) {
    const std::wstring ext = GetFileExtensionW(filename);
    if (ext == L"jpg" || ext == L"jpeg") {
        return CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", clsid) == S_OK;
    }
    if (ext == L"png") {
        return CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", clsid) == S_OK;
    }
    return CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", clsid) == S_OK;
}

} // namespace

bool SaveBitmapToFile(HWND hwnd, const wchar_t* filename) {
    if (!hwnd || !filename) return false;

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width < 1 || height < 1) return false;

    HDC hdcWindow = GetDC(hwnd);
    if (!hdcWindow) return false;
    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    if (!hdcMem) {
        ReleaseDC(hwnd, hdcWindow);
        return false;
    }
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcWindow, width, height);
    if (!hbmMem) {
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(hdcMem, hbmMem);
    BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

    bool result = false;
    {
        // Bitmap(HBITMAP) does not take ownership — HBITMAP must outlive Bitmap.
        Bitmap bitmap(hbmMem, nullptr);
        CLSID clsid = {};
        if (GetEncoderClsidW(filename, &clsid)) {
            result = (bitmap.Save(filename, &clsid, nullptr) == Ok);
        }
    }

    SelectObject(hdcMem, oldBitmap);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);
    return result;
}

bool SaveCanvasToFile(Bitmap* bitmap, const wchar_t* filename) {
    if (!bitmap || !filename) return false;

    CLSID clsid;
    if (!GetEncoderClsidW(filename, &clsid)) return false;

    const std::wstring ext = GetFileExtensionW(filename);
    if (ext == L"jpg" || ext == L"jpeg") {
        EncoderParameters params = {};
        params.Count = 1;
        params.Parameter[0].Guid = EncoderQuality;
        params.Parameter[0].Type = EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        ULONG quality = 92;
        params.Parameter[0].Value = &quality;
        return (bitmap->Save(filename, &clsid, &params) == Ok);
    }

    return (bitmap->Save(filename, &clsid, nullptr) == Ok);
}

Atelier::GdiplusBitmapPtr LoadImageFromFile(const wchar_t* filename) {
    if (!filename || !filename[0]) return Atelier::GdiplusBitmapPtr();

    Atelier::GdiplusBitmapPtr loaded(Gdiplus::Bitmap::FromFile(filename, FALSE));
    if (!loaded || loaded->GetLastStatus() != Ok) {
        return Atelier::GdiplusBitmapPtr();
    }
    return loaded;
}
