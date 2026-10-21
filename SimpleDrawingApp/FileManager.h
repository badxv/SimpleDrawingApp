#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "AtelierRaii.h"

bool SaveBitmapToFile(HWND hwnd, const wchar_t* filename);
bool SaveCanvasToFile(Gdiplus::Bitmap* bitmap, const wchar_t* filename);
// Loads an image file; returns null on failure (no throwaway Graphics).
Atelier::GdiplusBitmapPtr LoadImageFromFile(const wchar_t* filename);
