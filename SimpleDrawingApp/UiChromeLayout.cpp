#include "UiChromeLayout.h"
#include "UiChromeRender.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "EventBus.h"
#include "CaptionBar.h"
#include "AtelierPalette.h"
#include "SimpleDrawingApp.h"
#include <commctrl.h>

namespace {

void UpdateButtonTooltip(HWND parent, HWND btn, const char* text) {
    if (!hwndTooltip || !btn || !text) return;
    TOOLINFOA ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = (UINT_PTR)btn;
    ti.lpszText = const_cast<char*>(text);
    SendMessageA(hwndTooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
}

void LayoutLayerPanel(HWND hwnd) {
    if (!hwndLayerList && !hwndToggleLayers) return;

    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int panelX = client.right - chrome.layerW;
    const int panelY = chrome.topH;
    const int panelH = MaxInt(1, client.bottom - client.top - chrome.topH - chrome.bottomH - chrome.statusH);

    if (hwndToggleLayers) {
        const int tb = 22;
        if (gLayersOpen) {
            MoveWindow(hwndToggleLayers, panelX + chrome.layerW - tb - 6, panelY + 4, tb, tb, TRUE);
        } else {
            MoveWindow(hwndToggleLayers, panelX + (chrome.layerW - tb) / 2, panelY + 8, tb, tb, TRUE);
        }
    }

    if (!gLayersOpen) return;

    const int btn = 28;
    const int pad = 10;
    int y = panelY + 22;

    if (hwndLayerAdd) MoveWindow(hwndLayerAdd, panelX + pad, y, btn, btn, TRUE);
    if (hwndLayerDel) MoveWindow(hwndLayerDel, panelX + pad + btn + 4, y, btn, btn, TRUE);
    if (hwndLayerUp) MoveWindow(hwndLayerUp, panelX + pad + (btn + 4) * 2, y, btn, btn, TRUE);
    if (hwndLayerDown) MoveWindow(hwndLayerDown, panelX + pad + (btn + 4) * 3, y, btn, btn, TRUE);
    y += btn + 8;

    if (hwndLayerVisible) {
        MoveWindow(hwndLayerVisible, panelX + pad, y, chrome.layerW - pad * 2, 22, TRUE);
    }
    y += 28;

    const int opacityH = 28;
    const int motifBand = 68;
    const int listH = MaxInt(60, panelH - (y - panelY) - opacityH - motifBand - 14);
    if (hwndLayerList) {
        MoveWindow(hwndLayerList, panelX + pad, y, chrome.layerW - pad * 2, listH, TRUE);
    }

    if (hwndLayerOpacity) {
        const int opacityY = panelY + panelH - opacityH - 8;
        MoveWindow(hwndLayerOpacity, panelX + pad, opacityY, chrome.layerW - pad * 2, opacityH, TRUE);
    }
}

void LayoutChromeControls(HWND hwnd) {
    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int right = client.right - client.left;

    const int topY = (chrome.topH - ICON_BTN) / 2;
    const int menuY = (chrome.topH - 22) / 2;
    int x = BRAND_STRIP_W + 4;
    for (int i = 0; i < 6; ++i) {
        if (hwndMenuButtons[i]) {
            MoveWindow(hwndMenuButtons[i], x, menuY, MENU_BTN_W, 22, TRUE);
            x += MENU_BTN_W + 2;
        }
    }
    x += 10;
    if (hwndActionButtons[2]) MoveWindow(hwndActionButtons[2], x, topY, ICON_BTN, ICON_BTN, TRUE);
    x += ICON_BTN + 4;
    if (hwndActionButtons[3]) MoveWindow(hwndActionButtons[3], x, topY, ICON_BTN, ICON_BTN, TRUE);
    x += ICON_BTN + 8;
    if (hwndActionButtons[4]) MoveWindow(hwndActionButtons[4], x, topY, ICON_BTN, ICON_BTN, TRUE);

    x = right - (IsRunningUnderWine() ? 0 : CAPTION_BTN_W * 3) - (ICON_BTN + 4) * 3 - 12;
    if (hwndActionButtons[1]) MoveWindow(hwndActionButtons[1], x, topY, ICON_BTN, ICON_BTN, TRUE);
    x += ICON_BTN + 4;
    if (hwndActionButtons[6]) MoveWindow(hwndActionButtons[6], x, topY, ICON_BTN, ICON_BTN, TRUE);
    x += ICON_BTN + 4;
    if (hwndActionButtons[5]) MoveWindow(hwndActionButtons[5], x, topY, ICON_BTN, ICON_BTN, TRUE);

    LayoutCaptionButtons(hwnd);

    if (hwndBrand) {
        MoveWindow(hwndBrand, 0, 0, BRAND_STRIP_W, chrome.topH, TRUE);
    }

    const int tb = 20;
    if (hwndToggleRail) {
        if (gRailOpen) {
            MoveWindow(hwndToggleRail, chrome.railW - tb - 4, chrome.topH + 4, tb, tb, TRUE);
        } else {
            MoveWindow(hwndToggleRail, (chrome.railW - tb) / 2, chrome.topH + 6, tb, tb, TRUE);
        }
    }

    if (gBottomOpen) {
        const int bottomY = client.bottom - chrome.statusH - chrome.bottomH;
        const int bottomX = chrome.railW + 10;
        if (hwndSizeLabel) MoveWindow(hwndSizeLabel, bottomX, bottomY + 8, 34, 18, TRUE);
        if (hwndSlider) MoveWindow(hwndSlider, bottomX + 36, bottomY + 2, 140, 28, TRUE);
        if (hwndPenWidthBox) MoveWindow(hwndPenWidthBox, bottomX + 184, bottomY + 6, 40, 22, TRUE);
        if (hwndOpacityLabel) MoveWindow(hwndOpacityLabel, bottomX + 236, bottomY + 8, 54, 18, TRUE);
        if (hwndOpacitySlider) MoveWindow(hwndOpacitySlider, bottomX + 290, bottomY + 2, 140, 28, TRUE);
        if (hwndOpacityBox) MoveWindow(hwndOpacityBox, bottomX + 438, bottomY + 6, 40, 22, TRUE);
    }

    if (!gRailOpen) {
        return;
    }

    const int railX = 8;
    int y = chrome.topH + 26;
    const int order[kToolButtonCount] = { 0, 1, 2, 3, 4, 5 };
    for (int i = 0; i < kToolButtonCount; ++i) {
        const int idx = order[i];
        if (hwndToolButtons[idx]) {
            MoveWindow(hwndToolButtons[idx], railX, y, ICON_BTN, ICON_BTN, TRUE);
        }
        y += ICON_BTN + 5;
        if (i == 2 || i == 3) y += 4;
    }

    if (!gPaletteFloating) {
        y += 6;
        if (hwndBgButton) MoveWindow(hwndBgButton, railX + 14, y + 10, 20, 20, TRUE);
        if (hwndActionButtons[0]) MoveWindow(hwndActionButtons[0], railX, y, 22, 22, TRUE);
        if (hwndSwapColors) MoveWindow(hwndSwapColors, railX + 28, y - 2, 18, 18, TRUE);
        y += 36;

        if (hwndPalette) {
            const int palW = chrome.railW - 10;
            const int palH = AtelierPalette_IdealHeight(palW);
            const int maxH = (client.bottom - chrome.statusH - chrome.bottomH) - y - 6;
            const int h = (palH < maxH) ? palH : (maxH > 72 ? maxH : 72);
            MoveWindow(hwndPalette, 5, y, palW, h, TRUE);
        }
    }
}

}  // namespace

ChromeLayout GetChromeLayout(HWND hwnd) {
    ChromeLayout layout;
    layout.railW = gRailOpen ? TOOL_RAIL_WIDTH : PANEL_EDGE_WIDTH;
    layout.layerW = gLayersOpen ? LAYER_PANEL_WIDTH : PANEL_EDGE_WIDTH;
    layout.bottomH = gBottomOpen ? BOTTOMBAR_HEIGHT : 0;
    if (hwndStatus) {
        RECT sb = {};
        GetWindowRect(hwndStatus, &sb);
        layout.statusH = sb.bottom - sb.top;
        if (layout.statusH < 1) layout.statusH = STATUS_HEIGHT;
    }
    (void)hwnd;
    return layout;
}

void ApplyPanelVisibility() {
    const int railShow = gRailOpen ? SW_SHOW : SW_HIDE;
    for (HWND btn : hwndToolButtons) {
        if (btn) ShowWindow(btn, railShow);
    }

    if (!gPaletteFloating) {
        if (hwndActionButtons[0]) ShowWindow(hwndActionButtons[0], railShow);
        if (hwndBgButton) ShowWindow(hwndBgButton, railShow);
        if (hwndSwapColors) ShowWindow(hwndSwapColors, railShow);
        if (hwndPalette) ShowWindow(hwndPalette, railShow);
    }

    const int layerShow = gLayersOpen ? SW_SHOW : SW_HIDE;
    if (hwndLayerList) ShowWindow(hwndLayerList, layerShow);
    if (hwndLayerAdd) ShowWindow(hwndLayerAdd, layerShow);
    if (hwndLayerDel) ShowWindow(hwndLayerDel, layerShow);
    if (hwndLayerUp) ShowWindow(hwndLayerUp, layerShow);
    if (hwndLayerDown) ShowWindow(hwndLayerDown, layerShow);
    if (hwndLayerVisible) ShowWindow(hwndLayerVisible, layerShow);
    if (hwndLayerOpacity) ShowWindow(hwndLayerOpacity, layerShow);

    const int bottomShow = gBottomOpen ? SW_SHOW : SW_HIDE;
    if (hwndSizeLabel) ShowWindow(hwndSizeLabel, bottomShow);
    if (hwndSlider) ShowWindow(hwndSlider, bottomShow);
    if (hwndPenWidthBox) ShowWindow(hwndPenWidthBox, bottomShow);
    if (hwndOpacityLabel) ShowWindow(hwndOpacityLabel, bottomShow);
    if (hwndOpacitySlider) ShowWindow(hwndOpacitySlider, bottomShow);
    if (hwndOpacityBox) ShowWindow(hwndOpacityBox, bottomShow);

    if (hwndToggleRail) ShowWindow(hwndToggleRail, SW_SHOW);
    if (hwndToggleLayers) ShowWindow(hwndToggleLayers, SW_SHOW);
}

void LayoutViewport(HWND hwnd) {
    if (!hwndViewport) return;

    const ChromeLayout chrome = GetChromeLayout(hwnd);
    RECT client = {};
    GetClientRect(hwnd, &client);

    const int x = chrome.railW + WELL_FRAME;
    const int y = chrome.topH + WELL_FRAME;
    const int w = MaxInt(1, client.right - client.left - chrome.railW - chrome.layerW - WELL_FRAME * 2);
    int h = client.bottom - client.top - chrome.topH - chrome.bottomH - chrome.statusH - WELL_FRAME * 2;
    if (h < 1) h = 1;

    MoveWindow(hwndViewport, x, y, w, h, TRUE);
    LayoutLayerPanel(hwnd);
    LayoutChromeControls(hwnd);
    UpdateScrollBars();
}

void SetRailOpen(HWND hwnd, bool open) {
    if (gRailOpen == open) return;
    gRailOpen = open;
    if (!gRailOpen && hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
        CloseShapeFlyout();
    }
    if (gRailOpen) {
        DockPaletteInstruments(hwnd);
        if (hwndActionButtons[0]) ShowWindow(hwndActionButtons[0], SW_SHOW);
        if (hwndBgButton) ShowWindow(hwndBgButton, SW_SHOW);
        if (hwndSwapColors) ShowWindow(hwndSwapColors, SW_SHOW);
        if (hwndPalette) ShowWindow(hwndPalette, SW_SHOW);
    } else {
        for (HWND btn : hwndToolButtons) {
            if (btn) ShowWindow(btn, SW_HIDE);
        }
        UndockPaletteInstruments(hwnd);
    }
    ApplyPanelVisibility();
    UpdateButtonTooltip(hwnd, hwndToggleRail, gRailOpen ? "Hide tools (Tab)" : "Show tools (Tab)");
    DestroyChromeCache();
    LayoutViewport(hwnd);
    if (hwndToggleRail) InvalidateRect(hwndToggleRail, NULL, FALSE);
    InvalidateRect(hwnd, NULL, FALSE);
    RequestChromeRebuild(hwnd, 1);
}

void SetLayersOpen(HWND hwnd, bool open) {
    if (gLayersOpen == open) return;
    gLayersOpen = open;
    ApplyPanelVisibility();
    UpdateButtonTooltip(hwnd, hwndToggleLayers, gLayersOpen ? "Hide layers (F9)" : "Show layers (F9)");
    DestroyChromeCache();
    LayoutViewport(hwnd);
    if (hwndToggleLayers) InvalidateRect(hwndToggleLayers, NULL, FALSE);
    InvalidateRect(hwnd, NULL, FALSE);
    RequestChromeRebuild(hwnd, 1);
}

void SetBottomOpen(HWND hwnd, bool open) {
    if (gBottomOpen == open) return;
    gBottomOpen = open;
    ApplyPanelVisibility();
    DestroyChromeCache();
    LayoutViewport(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    RequestChromeRebuild(hwnd, 1);
}
