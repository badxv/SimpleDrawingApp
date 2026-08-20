#pragma once

#include <windows.h>

inline constexpr const char kAppViewportClassName[] = "SimpleDrawingAppViewport";

bool RegisterViewportClass(HINSTANCE hInstance);
