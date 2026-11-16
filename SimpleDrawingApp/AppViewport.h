#pragma once

#include <windows.h>

inline constexpr const wchar_t kAppViewportClassName[] = L"SimpleDrawingAppViewport";

bool RegisterViewportClass(HINSTANCE hInstance);
