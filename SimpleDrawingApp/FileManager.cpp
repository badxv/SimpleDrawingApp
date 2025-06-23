#include "FileManager.h"
#include <gdiplus.h>
#include <string>
#include <cstring>

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

// Helper: convert const char* to wstring
static std::wstring StringToWString(const char* s) {
    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &ws[0], len);
    ws.resize(len - 1); // Remove null terminator
    return ws;
}

static std::string GetFileExtension(const char* filename) {
    std::string file(filename);
    size_t pos = file.find_last_of('.');
    if (pos == std::string::npos) return "";
    std::string ext = file.substr(pos + 1);
    for (auto& c : ext) c = tolower(c);
    return ext;
}

bool SaveBitmapToFile(HWND hwnd, const char* filename) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC hdcWindow = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcWindow, width, height);
    HGDIOBJ oldBitmap = SelectObject(hdcMem, hbmMem);

    BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

    Bitmap bitmap(hbmMem, nullptr);

    CLSID clsid;
    std::string ext = GetFileExtension(filename);
    if (ext == "bmp") {
        CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid); // BMP
    }
    else if (ext == "jpg" || ext == "jpeg") {
        CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", &clsid); // JPEG
    }
    else if (ext == "png") {
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &clsid); // PNG
    }
    else {
        CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid); // default BMP
    }

    bool result = (bitmap.Save(StringToWString(filename).c_str(), &clsid, NULL) == Ok);

    SelectObject(hdcMem, oldBitmap);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);

    return result;
}

bool SaveCanvasToFile(Bitmap* bitmap, const char* filename) {
    if (!bitmap || !filename) return false;

    std::string ext = GetFileExtension(filename);
    CLSID clsid;

    if (ext == "bmp")
        CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid);
    else if (ext == "jpg" || ext == "jpeg")
        CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", &clsid);
    else if (ext == "png")
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &clsid);
    else
        CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid); // default BMP

    return (bitmap->Save(StringToWString(filename).c_str(), &clsid, NULL) == Ok);
}

bool LoadImageFromFile(const char* filename, Bitmap*& bitmap, Graphics*& graphics) {
    if (!filename) return false;

    delete graphics;
    delete bitmap;

    std::wstring wfile = StringToWString(filename);
    bitmap = Gdiplus::Bitmap::FromFile(wfile.c_str(), FALSE);

    if (!bitmap || bitmap->GetLastStatus() != Ok) {
        bitmap = nullptr;
        graphics = nullptr;
        return false;
    }

    graphics = Graphics::FromImage(bitmap);
    return true;
}
