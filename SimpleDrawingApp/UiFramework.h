#pragma once

#include "DrawingTools.h"
#include "UiChromeLayout.h"

struct UiContext {
    const AppTheme& theme;
    ChromeLayout (*getLayout)(HWND hwnd) = nullptr;
};

UiContext AppUiContext();
ChromeLayout UiGetLayout(const UiContext& ctx, HWND hwnd);
