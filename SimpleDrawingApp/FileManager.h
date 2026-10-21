#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

bool SaveBitmapToFile(HWND hwnd, const wchar_t* filename);
bool SaveCanvasToFile(Gdiplus::Bitmap* bitmap, const wchar_t* filename);
bool LoadImageFromFile(const wchar_t* filename, Gdiplus::Bitmap*& bitmap, Gdiplus::Graphics*& graphics);
