#pragma once
#include <windows.h>

class ColorPicker {
public:
    static COLORREF PickColor(HWND owner, COLORREF initialColor = RGB(0, 0, 0));
};
