#pragma once

#include <windows.h>
#include "AppMetrics.h"

ChromeLayout GetChromeLayout(HWND hwnd);
void ApplyPanelVisibility();
void LayoutViewport(HWND hwnd);
void SetRailOpen(HWND hwnd, bool open);
void SetLayersOpen(HWND hwnd, bool open);
void SetBottomOpen(HWND hwnd, bool open);
