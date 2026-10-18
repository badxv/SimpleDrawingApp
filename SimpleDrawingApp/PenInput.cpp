#include "PenInput.h"

void InitPenInput() {
    // Keep classic mouse messages for real mice; pen/tablet also sends WM_POINTER*.
    EnableMouseInPointer(TRUE);
}

bool IsSyntheticPenMouseMessage() {
    return (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700;
}

bool TryReadPenPointer(HWND hwnd, UINT32 pointerId, PenPointerSample& out) {
    out = {};
    POINTER_PEN_INFO penInfo = {};
    if (!GetPointerPenInfo(pointerId, &penInfo)) {
        return false;
    }
    if (penInfo.pointerInfo.pointerType != PT_PEN) {
        return false;
    }
    POINT pt = penInfo.pointerInfo.ptPixelLocation;
    if (hwnd) {
        ScreenToClient(hwnd, &pt);
    }
    out.clientX = pt.x;
    out.clientY = pt.y;
    const UINT32 rawPressure = penInfo.pressure;
    if (rawPressure == 0) {
        out.pressure = 1.0f;
    } else {
        out.pressure = static_cast<float>(rawPressure) / 1024.0f;
        if (out.pressure > 1.0f) out.pressure = 1.0f;
    }
    return true;
}
