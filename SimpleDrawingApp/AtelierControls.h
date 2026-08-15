#pragma once

#include <windows.h>
#include "DrawingTools.h"

// Custom atelier-styled instrument slider + canvas scrollbars.
// Sliders accept the same TBM_SETRANGE / TBM_SETPOS / TBM_GETPOS messages as
// TRACKBAR and notify the parent with WM_HSCROLL (lParam = HWND).

constexpr int ATL_SCROLL_THICK = 13;

bool AtelierControls_Register();
void AtelierControls_SetTheme(const AppTheme* theme);

HWND AtelierSlider_Create(HWND parent, int x, int y, int w, int h, HMENU idOrNull);

HWND AtelierScroll_Create(HWND parent, bool vertical, int x, int y, int w, int h);
void AtelierScroll_SetInfo(HWND hwnd, int minVal, int maxVal, int page, int pos, bool redraw);
int AtelierScroll_GetPos(HWND hwnd);
int AtelierScroll_GetPage(HWND hwnd);
bool AtelierScroll_IsNeeded(HWND hwnd);
