#pragma once

#include <windows.h>
#include "DrawingTools.h"
#include "AppMetrics.h"

// Phase-3 stub: shared read-only UI context for future component extraction.
// Chrome painting still lives in SimpleDrawingApp.cpp until panels migrate here.
struct UiContext {
    const AppTheme& theme;
    ChromeLayout (*getLayout)(HWND hwnd) = nullptr;
};

inline ChromeLayout UiGetLayout(const UiContext& ctx, HWND hwnd) {
    if (ctx.getLayout) return ctx.getLayout(hwnd);
    return ChromeLayout{};
}
