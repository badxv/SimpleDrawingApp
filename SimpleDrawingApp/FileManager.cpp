#include "FileManager.h"
#include <gdiplus.h>
#include <string>
#include <cstring>
#include <cctype>

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

// Helper: convert const char* to wstring
static std::wstring StringToWString(const char* s) {
    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring ws(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &ws[0], len);
    ws.resize(static_cast<size_t>(len - 1)); // Remove null terminator
    return ws;
}

static std::string GetFileExtension(const char* filename) {
    std::string file(filename);
    size_t pos = file.find_last_of('.');
    if (pos == std::string::npos) return "";
    std::string ext = file.substr(pos + 1);
    for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return ext;
}

static bool GetEncoderClsid(const char* filename, CLSID* clsid) {
    std::string ext = GetFileExtension(filename);
    if (ext == "jpg" || ext == "jpeg") {
        return CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", clsid) == S_OK;
    }
    if (ext == "png") {
        return CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", clsid) == S_OK;
    }
    // bmp or unknown -> BMP
    return CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", clsid) == S_OK;
}

bool SaveBitmapToFile(HWND hwnd, const char* filename) {
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
        if (GetEncoderClsid(filename, &clsid)) {
            result = (bitmap.Save(StringToWString(filename).c_str(), &clsid, nullptr) == Ok);
        }
    }

    SelectObject(hdcMem, oldBitmap);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);
    return result;
}

bool SaveCanvasToFile(Bitmap* bitmap, const char* filename) {
    if (!bitmap || !filename) return false;

    CLSID clsid;
    if (!GetEncoderClsid(filename, &clsid)) return false;

    return (bitmap->Save(StringToWString(filename).c_str(), &clsid, NULL) == Ok);
}

bool LoadImageFromFile(const char* filename, Bitmap*& bitmap, Graphics*& graphics) {
    if (!filename) return false;

    std::wstring wfile = StringToWString(filename);
    Bitmap* loaded = Gdiplus::Bitmap::FromFile(wfile.c_str(), FALSE);

    if (!loaded || loaded->GetLastStatus() != Ok) {
        delete loaded;
        return false;
    }

    Graphics* loadedGraphics = Graphics::FromImage(loaded);
    if (!loadedGraphics || loadedGraphics->GetLastStatus() != Ok) {
        delete loadedGraphics;
        delete loaded;
        return false;
    }

    delete graphics;
    delete bitmap;
    bitmap = loaded;
    graphics = loadedGraphics;
    return true;
}
