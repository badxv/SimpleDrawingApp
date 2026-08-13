#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "Resource.h"
#include "DrawingTools.h"

using namespace Gdiplus;

extern COLORREF penColor;
extern int penWidth;
extern DrawTool currentTool;
extern bool documentDirty;

void UpdatePenWidthDisplay();
void UpdateStatusBar(HWND hwnd);
void UpdateWindowTitle(HWND hwnd);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
