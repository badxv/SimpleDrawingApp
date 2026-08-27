#include "UiToolbar.h"
#include "UiControls.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "Resource.h"
#include "SimpleDrawingApp.h"
#include "AtelierControls.h"
#include "AtelierPalette.h"
#include <commctrl.h>

void CreateAppToolbar(HWND hwnd) {
    hwndTooltip = CreateWindowExA(0, TOOLTIPS_CLASSA, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
    if (hwndTooltip) {
        SendMessageA(hwndTooltip, TTM_SETMAXTIPWIDTH, 0, 240);
    }

    hwndToolButtons[0] = CreateIconButton(hwnd, IDC_TOOL_PEN, "Pen\r\nShortcut: B", true);
    hwndToolButtons[1] = CreateIconButton(hwnd, IDC_TOOL_ERASER, "Eraser\r\nShortcut: E", true);
    hwndToolButtons[2] = CreateIconButton(hwnd, IDC_TOOL_FILL, "Fill\r\nShortcut: G", true);
    hwndToolButtons[3] = CreateIconButton(hwnd, IDC_TOOL_SELECT, "Select\r\nShortcut: M", true);
    hwndToolButtons[4] = CreateIconButton(hwnd, IDC_TOOL_LINE, "Line\r\nShortcut: L", true);
    hwndToolButtons[5] = CreateIconButton(hwnd, IDC_TOOL_SHAPES, "Shapes…\r\nShortcut: U", true);

    hwndActionButtons[0] = CreateIconButton(hwnd, IDC_COLOR_BUTTON, "Foreground color\r\nClick to pick", false);
    hwndBgButton = CreateIconButton(hwnd, IDC_BG_BUTTON, "Background color (shape fill)\r\nClick to pick", false);
    hwndSwapColors = CreateIconButton(hwnd, IDC_SWAP_COLORS, "Swap FG/BG\r\nShortcut: X", false);

    AtelierPalette_SetTheme(&gTheme);
    hwndPalette = AtelierPalette_Create(hwnd, 0, 0, TOOL_RAIL_WIDTH - 12, 160);
    if (hwndPalette) {
        AtelierPalette_SetColors(hwndPalette, penColor, backColor);
    }

    hwndActionButtons[1] = CreateIconButton(hwnd, IDC_NEW_BUTTON, "New (Ctrl+N)", false);
    hwndActionButtons[2] = CreateIconButton(hwnd, IDC_UNDO_BUTTON, "Undo (Ctrl+Z)", false);
    hwndActionButtons[3] = CreateIconButton(hwnd, IDC_REDO_BUTTON, "Redo (Ctrl+Y)", false);
    hwndActionButtons[4] = CreateIconButton(hwnd, IDC_CLEAR_BUTTON, "Clear canvas", false);
    hwndActionButtons[5] = CreateIconButton(hwnd, IDC_SAVE_BUTTON, "Save (Ctrl+S)", false);
    hwndActionButtons[6] = CreateIconButton(hwnd, IDC_LOAD_BUTTON, "Open (Ctrl+O)", false);

    hwndToggleRail = CreateIconButton(hwnd, IDC_TOGGLE_RAIL, "Hide tools\r\nShortcut: Tab", false);
    hwndToggleLayers = CreateIconButton(hwnd, IDC_TOGGLE_LAYERS, "Hide layers\r\nShortcut: F9", false);

    static const char* kMenuLabels[6] = { "File", "Edit", "Image", "View", "Tools", "Help" };
    static const int kMenuIds[6] = {
        IDC_MENU_FILE, IDC_MENU_EDIT, IDC_MENU_IMAGE,
        IDC_MENU_VIEW, IDC_MENU_TOOLS, IDC_MENU_HELP
    };
    for (int i = 0; i < 6; ++i) {
        hwndMenuButtons[i] = CreateWindowA("BUTTON", kMenuLabels[i],
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            0, 0, MENU_BTN_W, 22,
            hwnd, (HMENU)(INT_PTR)kMenuIds[i], GetModuleHandle(NULL), NULL);
        ApplyUiFont(hwndMenuButtons[i]);
    }

    hwndSizeLabel = CreateWindowA("STATIC", "Size", WS_CHILD | WS_VISIBLE,
        0, 0, 34, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndSizeLabel);

    hwndSlider = AtelierSlider_Create(hwnd, 0, 0, 150, 28, NULL);
    SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
    SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);

    hwndPenWidthBox = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
        0, 0, 40, 22, hwnd, (HMENU)(INT_PTR)IDC_WIDTH_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndPenWidthBox);

    hwndOpacityLabel = CreateWindowA("STATIC", "Opacity", WS_CHILD | WS_VISIBLE,
        0, 0, 54, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndOpacityLabel);

    hwndOpacitySlider = AtelierSlider_Create(hwnd, 0, 0, 150, 28, NULL);
    SendMessage(hwndOpacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, penOpacity);

    hwndOpacityBox = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
        0, 0, 40, 22, hwnd, (HMENU)(INT_PTR)IDC_OPACITY_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndOpacityBox);

    SetActiveTool(DrawTool::Pen);
    UpdatePenWidthDisplay();
    UpdateOpacityDisplay();
}

void CreateLayerPanel(HWND hwnd) {
    hwndLayerAdd = CreateIconButton(hwnd, IDC_LAYER_ADD, "Add layer", false);
    hwndLayerDel = CreateIconButton(hwnd, IDC_LAYER_DEL, "Delete layer", false);
    hwndLayerUp = CreateIconButton(hwnd, IDC_LAYER_UP, "Move layer up", false);
    hwndLayerDown = CreateIconButton(hwnd, IDC_LAYER_DOWN, "Move layer down", false);
    hwndLayerDup = CreateIconButton(hwnd, IDC_LAYER_DUP, "Duplicate layer", false);

    hwndLayerVisible = CreateWindowA("BUTTON", "Visible",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        0, 0, 100, 22, hwnd, (HMENU)(INT_PTR)IDC_LAYER_VISIBLE, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndLayerVisible);

    hwndLayerList = CreateWindowExA(0, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)IDC_LAYER_LIST, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndLayerList);

    hwndLayerOpacity = AtelierSlider_Create(hwnd, 0, 0, 100, 28, (HMENU)(INT_PTR)IDC_LAYER_OPACITY);
    SendMessage(hwndLayerOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndLayerOpacity, TBM_SETPOS, TRUE, 100);

    RefreshLayerList();
}
