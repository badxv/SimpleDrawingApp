#pragma once

#include <windows.h>

void InitPenInput();

// True when WM_LBUTTON* was synthesized from pen/tablet input (WM_POINTER handles those).
bool IsSyntheticPenMouseMessage();

struct PenPointerSample {
    int clientX = 0;
    int clientY = 0;
    float pressure = 1.0f;
};

// Reads pen location (client coords) and pressure for WM_POINTER* messages.
bool TryReadPenPointer(HWND hwnd, UINT32 pointerId, PenPointerSample& out);
