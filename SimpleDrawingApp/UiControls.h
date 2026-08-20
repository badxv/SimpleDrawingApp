#pragma once

#include <windows.h>

void ApplyUiFont(HWND control);
HWND CreateIconButton(HWND parent, int id, const char* tooltip, bool pushLike);
