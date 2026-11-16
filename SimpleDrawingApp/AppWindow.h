#pragma once

#include <windows.h>

inline constexpr const wchar_t kAppMainWindowClassName[] = L"SimpleDrawingAppWindowClass";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
