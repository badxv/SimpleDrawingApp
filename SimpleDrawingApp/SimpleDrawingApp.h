#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "Resource.h"
#include "DrawingTools.h"
#include "AppState.h"

void UpdatePenWidthDisplay();
void UpdateOpacityDisplay();
void UpdateStatusBar(HWND hwnd);
void UpdateWindowTitle(HWND hwnd);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
