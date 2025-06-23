#pragma once
#include <windows.h>
#include <gdiplus.h>
#include "resource.h"
using namespace Gdiplus;

extern COLORREF penColor;
extern int penWidth;
extern POINT lastPoint;
extern bool isDrawing;
extern Bitmap* canvasBitmap;
extern Graphics* canvasGraphics;
extern ULONG_PTR gdiplusToken;

void UpdatePenWidthDisplay();
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
