#include "UiFramework.h"
#include "AppState.h"

UiContext AppUiContext() {
    return UiContext{ gTheme, GetChromeLayout };
}

ChromeLayout UiGetLayout(const UiContext& ctx, HWND hwnd) {
    if (ctx.getLayout) return ctx.getLayout(hwnd);
    return ChromeLayout{};
}
