#pragma once

#include <windows.h>
#include "AppMetrics.h"

void DestroyChromeCache();
void EnsureChromeCache(int width, int height, const ChromeLayout& chrome);
void DrawToolbarBackground(HDC hdc, HWND hwnd, const RECT& client);
bool RegisterBrandClass(HINSTANCE hInstance);
void InvalidateBrandMark();
void EnsureBrandStrip(int topH);
