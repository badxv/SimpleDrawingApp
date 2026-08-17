#pragma once

#include <windows.h>

void EnableDarkTitleBar(HWND hwnd);
int FrameBorderX();
int FrameBorderY();
void EnableCustomTitleBar(HWND hwnd);
void LayoutCaptionButtons(HWND hwnd);
bool RegisterCaptionBtnClass(HINSTANCE hInstance);
void CreateCaptionButtons(HWND hwnd);
bool PointOverTopBarControl(HWND hwnd, POINT ptClient);
