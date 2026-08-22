#pragma once

#include <windows.h>

// Handles WM_COMMAND payloads for the main frame.
// Returns true if the command was consumed.
bool HandleAppCommand(HWND hwnd, int cmdId, int notifyCode);
