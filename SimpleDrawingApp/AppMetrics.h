#pragma once

#include <windows.h>

#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92
#endif
#ifndef SM_CYPADDEDBORDER
#define SM_CYPADDEDBORDER 92
#endif

constexpr int TOPBAR_HEIGHT = 38;       // doubles as custom titlebar height
constexpr int TOOL_RAIL_WIDTH = 100;
constexpr int PANEL_EDGE_WIDTH = 22;    // collapsed rail / layers strip
constexpr int BOTTOMBAR_HEIGHT = 34;
constexpr int STATUS_HEIGHT = 22;
constexpr int LAYER_PANEL_WIDTH = 168;
constexpr int ICON_BTN = 30;
constexpr int WELL_FRAME = 6;
constexpr int BRAND_STRIP_W = 118;
constexpr int MENU_BTN_W = 44;
constexpr int FLOAT_DRAG_H = 22;
constexpr int FLOAT_CHIP_H = 36;
constexpr int CAPTION_BTN_W = 46;
constexpr int CAPTION_BTN_H = 30;

constexpr UINT_PTR IDT_UI_ANIM = 42;
constexpr UINT_PTR IDT_CHROME_REBUILD = 43;
constexpr UINT_PTR IDT_UI_IDLE = 44;
constexpr UINT_PTR IDT_AUTOSAVE = 45;
constexpr UINT kAutosaveIntervalMs = 30000;

constexpr int DEFAULT_DOC_WIDTH = 1280;
constexpr int DEFAULT_DOC_HEIGHT = 720;
constexpr int MIN_DOC_SIZE = 1;
constexpr int MAX_DOC_SIZE = 10000;
constexpr COLORREF WORKSPACE_COLOR = RGB(168, 158, 146);
constexpr float ZOOM_MIN = 0.25f;
constexpr float ZOOM_MAX = 8.0f;
constexpr float ZOOM_STEP = 1.25f;

struct ChromeLayout {
    int topH = TOPBAR_HEIGHT;
    int railW = TOOL_RAIL_WIDTH;
    int bottomH = BOTTOMBAR_HEIGHT;
    int statusH = STATUS_HEIGHT;
    int layerW = LAYER_PANEL_WIDTH;
};

inline int MaxInt(int a, int b) { return (a > b) ? a : b; }
inline float MaxFloat(float a, float b) { return (a > b) ? a : b; }

inline bool IsRunningUnderWine() {
    static int cached = -1;
    if (cached < 0) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        cached = (ntdll && GetProcAddress(ntdll, "wine_get_version")) ? 1 : 0;
    }
    return cached == 1;
}
