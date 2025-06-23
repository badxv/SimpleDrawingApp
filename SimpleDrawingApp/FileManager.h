#pragma once
#include <windows.h>
#include <gdiplus.h>

bool SaveBitmapToFile(HWND hwnd, const char* filename);
bool SaveCanvasToFile(Gdiplus::Bitmap* bitmap, const char* filename);
bool LoadImageFromFile(const char* filename, Gdiplus::Bitmap*& bitmap, Gdiplus::Graphics*& graphics);
