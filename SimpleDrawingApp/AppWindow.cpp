#include "AppWindow.h"
#include "AppShell.h"
#include "AppDialogs.h"
#include "AppFeatureFlags.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppCanvas.h"
#include "AppStroke.h"
#include "AppViewport.h"
#include "AppSelection.h"
#include "AppDocument.h"
#include "AppApi.h"
#include "UiChromeLayout.h"
#include "UiChromeRender.h"
#include "UiControls.h"
#include "UiPaletteFloat.h"
#include "UiShapeFlyout.h"
#include "UiToolbar.h"
#include "EventBus.h"
#include "AtelierEvents.h"
#include "AtelierRaii.h"
#include "FileManager.h"
#include "ColorPicker.h"
#include "LayerHistory.h"
#include "LayerStack.h"
#include "DrawingTools.h"
#include "UiChrome.h"
#include "AtelierFonts.h"
#include "AtelierControls.h"
#include "AtelierArtwork.h"
#include "AtelierPalette.h"
#include "CaptionBar.h"
#include "Resource.h"

#include <commctrl.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <cstdio>
#include <cmath>

using namespace Gdiplus;

namespace {

static void AdjustPenWidth(HWND hwnd, int delta) {
    int next = penWidth + delta;
    if (next < 1) next = 1;
    if (next > 50) next = 50;
    if (next == penWidth) return;

    penWidth = next;
    if (hwndSlider) {
        SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);
    }
    UpdatePenWidthDisplay();
    UpdateStatusBar(hwnd);
}

static void AdjustOpacity(HWND hwnd, int delta) {
    int next = penOpacity + delta;
    if (next < 1) next = 1;
    if (next > 100) next = 100;
    if (next == penOpacity) return;

    penOpacity = next;
    if (hwndOpacitySlider) {
        SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, penOpacity);
    }
    UpdateOpacityDisplay();
    UpdateStatusBar(hwnd);
}
static void InvalidateActiveToolButton() {
    const int idx = static_cast<int>(currentTool);
    if (idx >= 0 && idx < kToolButtonCount && hwndToolButtons[idx]) {
        InvalidateRect(hwndToolButtons[idx], NULL, FALSE);
    }
}

static void SyncPaletteFromApp() {
    if (hwndPalette) {
        AtelierPalette_SetColors(hwndPalette, penColor, backColor);
    }
}

static void InvalidateColorChips() {
    if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
    if (hwndBgButton) InvalidateRect(hwndBgButton, NULL, FALSE);
    SyncPaletteFromApp();
}

static bool ShouldRunIdleMotion(HWND hwnd) {
    if (!hwnd || gUiSizing) return false;
    if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (isDrawing) return false;
    if (gSel.creating || gSel.moving) return false;
    const HWND cap = GetCapture();
    if (cap == hwnd || (hwndViewport && cap == hwndViewport)) return false;
    return true;
}

static void GetChromeMetrics(HWND hwnd, int& toolbarH, int& statusH) {
    const ChromeLayout layout = GetChromeLayout(hwnd);
    toolbarH = layout.topH;
    statusH = layout.statusH + layout.bottomH;
}
static void PopupAppMenu(HWND hwnd, int menuIndex, HWND anchorBtn) {
    if (!gAppMenu || menuIndex < 0) return;
    HMENU sub = GetSubMenu(gAppMenu, menuIndex);
    if (!sub) return;
    SyncFeatureFlagMenuItems();
    RECT br = {};
    if (anchorBtn) GetWindowRect(anchorBtn, &br);
    else GetWindowRect(hwnd, &br);
    TrackPopupMenu(sub, TPM_LEFTALIGN | TPM_TOPALIGN, br.left, br.bottom, 0, hwnd, NULL);
}

}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icex);

        gUiFont = AtelierFonts_Ui(13, false);
        gBrandFont = AtelierFonts_Display(18, false);
        if (!gUiFont) {
            gUiFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        }
        if (!gBrandFont) {
            gBrandFont = CreateFontA(-18, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Georgia");
        }
        gChromeBrush = CreateSolidBrush(gTheme.chromeBg);
        gChromeDeepBrush = CreateSolidBrush(gTheme.chromeDeep);
        gChromeElevatedBrush = CreateSolidBrush(gTheme.chromeElevated);
        AtelierControls_SetTheme(&gTheme);
        AtelierPalette_SetTheme(&gTheme);

        hwndBrand = CreateWindowExA(
            0,
            "SimpleDrawingAppBrand",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, BRAND_STRIP_W, TOPBAR_HEIGHT,
            hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL);

        CreateAppToolbar(hwnd);
        CreateLayerPanel(hwnd);

        hwndStatus = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "", hwnd, 1);
        ApplyUiFont(hwndStatus);
        LayoutStatusParts(hwnd);

        hwndViewport = CreateWindowExA(
            0, // no CLIENTEDGE — custom bronze well frame instead
            kAppViewportClassName,
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            TOOL_RAIL_WIDTH + WELL_FRAME, TOPBAR_HEIGHT + WELL_FRAME, 100, 100,
            hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL);
        hwndScrollH = AtelierScroll_Create(hwndViewport, false, 0, 0, 40, ATL_SCROLL_THICK);
        hwndScrollV = AtelierScroll_Create(hwndViewport, true, 0, 0, ATL_SCROLL_THICK, 40);
        hwndScrollCorner = AtelierScroll_Create(hwndViewport, false, 0, 0, ATL_SCROLL_THICK, ATL_SCROLL_THICK);
        ShowWindow(hwndScrollH, SW_HIDE);
        ShowWindow(hwndScrollV, SW_HIDE);
        ShowWindow(hwndScrollCorner, SW_HIDE);

        EnsureCanvas(hwnd);
        RefreshLayerList();
        LayoutViewport(hwnd);
        UpdateStatusBar(hwnd);
        UpdateWindowTitle(hwnd);
        EnableCustomTitleBar(hwnd);
        CreateCaptionButtons(hwnd);
        // Fresco cache once; low-rate idle motion (pauses while drawing/resizing).
        RequestChromeRebuild(hwnd, 1);
        SetTimer(hwnd, IDT_UI_IDLE, 100, NULL); // ~10fps overlay only
        break;
    }
    case WM_TIMER:
        if (wParam == IDT_UI_IDLE) {
            if (!ShouldRunIdleMotion(hwnd)) break;

            gIdlePhase += 0.14f;
            if (gIdlePhase > 6.2831853f) gIdlePhase -= 6.2831853f;
            const float breath = 0.5f + 0.5f * sinf(gIdlePhase);
            gUiCompassAngle += 2.2f; // ~22°/s at 10fps
            if (gUiCompassAngle >= 360.0f) gUiCompassAngle -= 360.0f;

            if (gToolFlash > 0.0f) {
                gToolFlash -= 0.12f;
                if (gToolFlash < 0.0f) gToolFlash = 0.0f;
            }
            gUiPulse = breath * 0.65f + gToolFlash * 0.35f;

            // Professional pattern: compose offscreen, invalidate only the brand child.
            EnsureBrandStrip(TOPBAR_HEIGHT);
            InvalidateBrandMark();
            InvalidateActiveToolButton();

            // Photoshop-style marching ants while a selection sits idle.
            if (gSel.hasMarquee && !gSel.creating && !gSel.moving) {
                gSelAntOffset += 1.25f;
                if (gSelAntOffset >= 9.0f) gSelAntOffset -= 9.0f;
                InvalidateCanvas();
            }
        }
        else if (wParam == IDT_CHROME_REBUILD) {
            KillTimer(hwnd, IDT_CHROME_REBUILD);
            RECT client = {};
            GetClientRect(hwnd, &client);
            const ChromeLayout chrome = GetChromeLayout(hwnd);
            EnsureChromeCache(client.right - client.left, client.bottom - client.top, chrome);
            EnsureBrandStrip(chrome.topH);
            InvalidateRect(hwnd, NULL, FALSE);
            InvalidateBrandMark();
        }
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, gTheme.chromeBg);
        SetTextColor(hdc, gTheme.ink);
        return (LRESULT)gChromeBrush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, gTheme.chromeElevated);
        SetTextColor(hdc, gTheme.ink);
        return (LRESULT)(gChromeElevatedBrush ? gChromeElevatedBrush : gChromeBrush);
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) break;
        if (IsIconControlId(static_cast<int>(dis->CtlID))) {
            const int id = static_cast<int>(dis->CtlID);
            IconPaintOpts opts;
            opts.chromeBg = IsToolRailControlId(id) ? gTheme.chromeDeep : gTheme.chromeBg;
            opts.accent = gTheme.accent;
            opts.accentDeep = gTheme.accentDeep;
            opts.text = gTheme.ink;
            opts.selectedBg = gTheme.toolSelectedBg;
            opts.elevated = gTheme.chromeElevated;
            opts.pulse = 0.0f;
            opts.pressScale = 1.0f;
            int activeToolId = IDC_TOOL_PEN;
            switch (currentTool) {
            case DrawTool::Pen: activeToolId = IDC_TOOL_PEN; break;
            case DrawTool::Eraser: activeToolId = IDC_TOOL_ERASER; break;
            case DrawTool::Fill: activeToolId = IDC_TOOL_FILL; break;
            case DrawTool::Select: activeToolId = IDC_TOOL_SELECT; break;
            case DrawTool::Line: activeToolId = IDC_TOOL_LINE; break;
            case DrawTool::Shape: activeToolId = IDC_TOOL_SHAPES; break;
            }
            if (IsToolRailControlId(id) && id != IDC_COLOR_BUTTON && id != IDC_BG_BUTTON && id != IDC_SWAP_COLORS) {
                opts.useAppSelected = true;
                opts.appSelected = (id == activeToolId);
            }
            // Shape flyout selection / paint mode.
            if (id >= IDC_SHAPE_RECT && id <= IDC_SHAPE_ROUNDRECT) {
                opts.useAppSelected = true;
                opts.appSelected = (currentTool == DrawTool::Shape
                    && static_cast<int>(currentShape) == (id - IDC_SHAPE_RECT));
                opts.chromeBg = gTheme.chromeElevated;
            }
            if (id >= IDC_SHAPE_MODE_STROKE && id <= IDC_SHAPE_MODE_BOTH) {
                opts.useAppSelected = true;
                opts.appSelected = (static_cast<int>(shapePaintMode) == (id - IDC_SHAPE_MODE_STROKE));
                opts.chromeBg = gTheme.chromeElevated;
            }
            if (id == activeToolId) {
                opts.pressScale = 1.0f + gToolFlash * 0.10f;
                opts.pulse = gUiPulse;
            }
            if (id == IDC_COLOR_BUTTON) {
                opts.useColorFill = true;
                opts.colorFill = penColor;
            }
            if (id == IDC_BG_BUTTON) {
                opts.useColorFill = true;
                opts.colorFill = backColor;
            }
            if (id == IDC_TOGGLE_RAIL) {
                opts.useAppSelected = true;
                opts.appSelected = gRailOpen;
                opts.chromeBg = gTheme.chromeDeep;
            }
            if (id == IDC_TOGGLE_LAYERS) {
                opts.useAppSelected = true;
                opts.appSelected = gLayersOpen;
            }
            if (gChromeCache && dis->hwndItem) {
                POINT pt = { dis->rcItem.left, dis->rcItem.top };
                MapWindowPoints(dis->hwndItem, hwnd, &pt, 1);
                opts.frescoCache = gChromeCache.get();
                opts.frescoX = pt.x;
                opts.frescoY = pt.y;
            }
            PaintIconButton(dis, opts);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND:
        // Avoid double-paint flicker: chrome is drawn once in WM_PAINT from cache.
        return 1;

    case WM_NCCALCSIZE: {
        // Replace the OS caption with our in-client top bar; keep resize borders.
        // Skip on Wine — its window manager still draws a caption and our expanded
        // client would sit underneath it, clipping brand/menus.
        if (wParam == TRUE && !IsRunningUnderWine()) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const int frameX = FrameBorderX();
            const int frameY = FrameBorderY();
            params->rgrc[0].left += frameX;
            params->rgrc[0].right -= frameX;
            params->rgrc[0].bottom -= frameY;
            if (IsZoomed(hwnd)) {
                params->rgrc[0].top += frameY;
            }
            return 0;
        }
        break;
    }

    case WM_NCHITTEST: {
        if (IsRunningUnderWine()) break;

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT wr = {};
        GetWindowRect(hwnd, &wr);
        const int frameX = FrameBorderX();
        const int frameY = FrameBorderY();

        if (pt.y >= wr.top && pt.y < wr.top + frameY) {
            if (pt.x < wr.left + frameX) return HTTOPLEFT;
            if (pt.x >= wr.right - frameX) return HTTOPRIGHT;
            return HTTOP;
        }
        if (pt.y >= wr.bottom - frameY && pt.y < wr.bottom) {
            if (pt.x < wr.left + frameX) return HTBOTTOMLEFT;
            if (pt.x >= wr.right - frameX) return HTBOTTOMRIGHT;
            return HTBOTTOM;
        }
        if (pt.x >= wr.left && pt.x < wr.left + frameX) return HTLEFT;
        if (pt.x >= wr.right - frameX && pt.x < wr.right) return HTRIGHT;

        POINT clientPt = pt;
        ScreenToClient(hwnd, &clientPt);
        RECT client = {};
        GetClientRect(hwnd, &client);

        if (clientPt.y >= 0 && clientPt.y < TOPBAR_HEIGHT) {
            if (PointOverTopBarControl(hwnd, clientPt)) return HTCLIENT;
            return HTCAPTION;
        }

        if (PtInRect(&client, clientPt)) return HTCLIENT;
        return HTNOWHERE;
    }

    case WM_NCACTIVATE:
        if (IsRunningUnderWine()) break;
        // Keep custom chrome painted; skip default caption redraw flash.
        InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;

    case WM_ENTERSIZEMOVE:
        gUiSizing = true;
        break;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            SendMessageA(hwndStatus, WM_SIZE, 0, 0);
            LayoutStatusParts(hwnd);
            LayoutViewport(hwnd);
            UpdateStatusBar(hwnd);
            // Drop stale fresco bitmap during live resize (avoids leak-like growth + hitch).
            DestroyChromeCache();
            RequestChromeRebuild(hwnd, 60);
            InvalidateRect(hwnd, NULL, FALSE);
            if (hwndCaptionMax) InvalidateRect(hwndCaptionMax, nullptr, FALSE);
        }
        break;
    case WM_EXITSIZEMOVE: {
        gUiSizing = false;
        KillTimer(hwnd, IDT_CHROME_REBUILD);
        RECT client = {};
        GetClientRect(hwnd, &client);
        EnsureChromeCache(client.right - client.left, client.bottom - client.top, GetChromeLayout(hwnd));
        EnsureBrandStrip(TOPBAR_HEIGHT);
        InvalidateRect(hwnd, NULL, FALSE);
        InvalidateBrandMark();
        break;
    }
    case WM_HSCROLL: {
        if ((HWND)lParam == hwndSlider) {
            penWidth = static_cast<int>(SendMessage(hwndSlider, TBM_GETPOS, 0, 0));
            UpdatePenWidthDisplay();
            UpdateStatusBar(hwnd);
        }
        else if ((HWND)lParam == hwndOpacitySlider) {
            penOpacity = static_cast<int>(SendMessage(hwndOpacitySlider, TBM_GETPOS, 0, 0));
            UpdateOpacityDisplay();
            UpdateStatusBar(hwnd);
        }
        else if ((HWND)lParam == hwndLayerOpacity) {
            if (suppressLayerNotify) break;
            const int opacity = static_cast<int>(SendMessage(hwndLayerOpacity, TBM_GETPOS, 0, 0));
            gLayers.SetActiveOpacity(opacity);
            InvalidateComposite();
            RefreshLayerList();
            MarkDirty(hwnd);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
        }
        break;
    }
    case WM_COMMAND: {
        const int cmdId = LOWORD(wParam);
        const int notifyCode = HIWORD(wParam);

        if (cmdId == IDC_WIDTH_EDIT && notifyCode == EN_CHANGE) {
            if (suppressEditNotify) break;
            char buf[16];
            GetWindowTextA(hwndPenWidthBox, buf, sizeof(buf));
            int val = atoi(buf);
            if (val >= 1 && val <= 50) {
                penWidth = val;
                SendMessage(hwndSlider, TBM_SETPOS, TRUE, val);
                UpdateStatusBar(hwnd);
            }
            break;
        }

        if (cmdId == IDC_OPACITY_EDIT && notifyCode == EN_CHANGE) {
            if (suppressEditNotify) break;
            char buf[16];
            GetWindowTextA(hwndOpacityBox, buf, sizeof(buf));
            int val = atoi(buf);
            if (val >= 1 && val <= 100) {
                penOpacity = val;
                SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, val);
                UpdateStatusBar(hwnd);
            }
            break;
        }

        if (cmdId == ID_PALETTE) {
            COLORREF fg = penColor;
            COLORREF bg = backColor;
            AtelierPalette_GetColors(hwndPalette, &fg, &bg);
            if (notifyCode == 1) {
                penColor = fg;
                if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
            } else if (notifyCode == 2) {
                backColor = bg;
                if (hwndBgButton) InvalidateRect(hwndBgButton, NULL, FALSE);
            } else             if (notifyCode == 3) {
                COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
                penColor = newColor;
                InvalidateColorChips();
            } else if (notifyCode == 4) {
                COLORREF newColor = ColorPicker::PickColor(hwnd, backColor);
                backColor = newColor;
                InvalidateColorChips();
            }
            UpdateStatusBar(hwnd);
            break;
        }

        if (cmdId >= IDC_SHAPE_RECT && cmdId <= IDC_SHAPE_ROUNDRECT) {
            currentShape = static_cast<ShapeKind>(cmdId - IDC_SHAPE_RECT);
            SetActiveTool(DrawTool::Shape);
            CloseShapeFlyout();
            UpdateStatusBar(hwnd);
            break;
        }
        if (cmdId >= IDC_SHAPE_MODE_STROKE && cmdId <= IDC_SHAPE_MODE_BOTH) {
            shapePaintMode = static_cast<ShapePaintMode>(cmdId - IDC_SHAPE_MODE_STROKE);
            SyncShapeFlyoutChecks();
            if (currentTool != DrawTool::Shape) {
                SetActiveTool(DrawTool::Shape);
            }
            UpdateStatusBar(hwnd);
            break;
        }

        switch (cmdId) {
        case IDC_TOOL_PEN:
        case IDM_TOOL_PEN:
            ClearSelection(true);
            SetActiveTool(DrawTool::Pen);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_ERASER:
        case IDM_TOOL_ERASER:
            ClearSelection(true);
            SetActiveTool(DrawTool::Eraser);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_FILL:
        case IDM_TOOL_FILL:
            ClearSelection(true);
            SetActiveTool(DrawTool::Fill);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_LINE:
        case IDM_TOOL_LINE:
            ClearSelection(true);
            SetActiveTool(DrawTool::Line);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_SHAPES:
        case IDM_TOOL_SHAPES:
            ClearSelection(true);
            if (cmdId == IDC_TOOL_SHAPES
                && currentTool == DrawTool::Shape && hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
                CloseShapeFlyout();
            } else {
                SetActiveTool(DrawTool::Shape);
                OpenShapeFlyout(hwnd);
            }
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_SELECT:
        case IDM_TOOL_SELECT:
            SetActiveTool(DrawTool::Select);
            UpdateStatusBar(hwnd);
            break;
        case IDC_SWAP_COLORS:
        case IDM_SWAP_COLORS: {
            const COLORREF tmp = penColor;
            penColor = backColor;
            backColor = tmp;
            InvalidateColorChips();
            break;
        }
        case IDC_TOGGLE_RAIL:
            SetRailOpen(hwnd, !gRailOpen);
            break;
        case IDC_TOGGLE_LAYERS:
            SetLayersOpen(hwnd, !gLayersOpen);
            break;
        case IDC_TOGGLE_BOTTOM:
            SetBottomOpen(hwnd, !gBottomOpen);
            break;
        case IDC_MENU_FILE:
            PopupAppMenu(hwnd, 0, hwndMenuButtons[0]);
            break;
        case IDC_MENU_EDIT:
            PopupAppMenu(hwnd, 1, hwndMenuButtons[1]);
            break;
        case IDC_MENU_IMAGE:
            PopupAppMenu(hwnd, 2, hwndMenuButtons[2]);
            break;
        case IDC_MENU_VIEW:
            PopupAppMenu(hwnd, 3, hwndMenuButtons[3]);
            break;
        case IDC_MENU_TOOLS:
            PopupAppMenu(hwnd, 4, hwndMenuButtons[4]);
            break;
        case IDC_MENU_HELP:
            PopupAppMenu(hwnd, 5, hwndMenuButtons[5]);
            break;
        case IDC_BG_BUTTON: {
            COLORREF newColor = ColorPicker::PickColor(hwnd, backColor);
            backColor = newColor;
            InvalidateColorChips();
            break;
        }
        case IDC_LAYER_ADD:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.AddLayer()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
                UpdateStatusBar(hwnd);
            }
            break;
        case IDC_LAYER_DEL:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.DeleteActiveLayer()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
                UpdateStatusBar(hwnd);
            }
            break;
        case IDC_LAYER_UP:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.MoveActiveUp()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
            }
            break;
        case IDC_LAYER_DOWN:
            ClearSelection(true);
            gHistory.Push(gLayers);
            if (gLayers.MoveActiveDown()) {
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
            }
            break;
        case IDC_LAYER_VISIBLE:
            if (suppressLayerNotify) break;
            {
                const bool checked = SendMessageA(hwndLayerVisible, BM_GETCHECK, 0, 0) == BST_CHECKED;
                gHistory.Push(gLayers);
                gLayers.SetActiveVisible(checked);
                InvalidateComposite();
                RefreshLayerList();
                MarkDirty(hwnd);
                InvalidateCanvas();
            }
            break;
        case IDC_LAYER_LIST:
            if (suppressLayerNotify) break;
            if (notifyCode == LBN_SELCHANGE) {
                const int row = static_cast<int>(SendMessageA(hwndLayerList, LB_GETCURSEL, 0, 0));
                if (row >= 0) {
                    const int layerIndex = static_cast<int>(SendMessageA(hwndLayerList, LB_GETITEMDATA, row, 0));
                    ClearSelection(true);
                    gLayers.SetActiveIndex(layerIndex);
                    RefreshLayerList();
                    UpdateStatusBar(hwnd);
                }
            }
            break;
        case IDM_CUT:
            CutSelection(hwnd);
            break;
        case IDM_COPY:
            CopySelection(hwnd);
            break;
        case IDM_PASTE:
            PasteSelection(hwnd);
            break;
        case IDM_DELETE_SEL:
            DeleteSelection(hwnd);
            break;
        case IDM_SELECT_ALL:
            SelectAll(hwnd);
            break;
        case IDM_ZOOM_IN:
            ZoomByFactor(hwnd, ZOOM_STEP);
            break;
        case IDM_ZOOM_OUT:
            ZoomByFactor(hwnd, 1.0f / ZOOM_STEP);
            break;
        case IDM_ZOOM_100:
            ZoomToActual(hwnd);
            break;
        case IDM_ZOOM_FIT:
            ZoomToFit(hwnd);
            break;
        case IDM_FEAT_PASTE_VIEW:
            ToggleFeatureFlag(AppFeature::PasteAtViewOrigin);
            break;
        case IDM_FEAT_SEL_VEIL:
            ToggleFeatureFlag(AppFeature::SelectionExteriorVeil);
            InvalidateCanvas();
            break;
        case IDM_FEAT_WARN_SHRINK:
            ToggleFeatureFlag(AppFeature::WarnCanvasShrink);
            break;
        case IDM_FEAT_EVENT_DIRTY:
            ToggleFeatureFlag(AppFeature::EventBusDirtyUi);
            break;
        case IDC_COLOR_BUTTON: {
            COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
            penColor = newColor;
            InvalidateColorChips();
            break;
        }
        case IDC_NEW_BUTTON:
        case IDM_NEW:
            NewDocument(hwnd);
            break;
        case IDC_CAPTION_MIN:
            SendMessageA(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case IDC_CAPTION_MAX:
            SendMessageA(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
            break;
        case IDC_CAPTION_CLOSE:
            SendMessageA(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
        case IDC_CLEAR_BUTTON:
        case IDM_CLEAR:
            ClearCanvas(hwnd, true);
            UpdateStatusBar(hwnd);
            break;
        case IDC_UNDO_BUTTON:
        case IDM_UNDO:
            UndoDocument(hwnd);
            break;
        case IDC_REDO_BUTTON:
        case IDM_REDO:
            RedoDocument(hwnd);
            break;
        case IDC_SAVE_BUTTON:
        case IDM_SAVE:
            SaveDocument(hwnd);
            break;
        case IDC_LOAD_BUTTON:
        case IDM_OPEN:
            OpenDocument(hwnd);
            break;
        case IDM_CANVAS_SIZE:
            ResizeCanvas(hwnd);
            break;
        case IDM_ABOUT:
            ShowAboutDialog(hwnd);
            break;
        case IDM_SHORTCUTS:
            ShowShortcutsDialog(hwnd);
            break;
        case IDM_EXIT:
            SendMessageA(hwnd, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int steps = wheelDelta / WHEEL_DELTA;
        if (steps != 0) {
            const bool ctrlDown = ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0)
                || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0)
                || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrlDown) {
                // Zoom toward cursor when possible.
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (hwndViewport) {
                    ScreenToClient(hwndViewport, &pt);
                    float factor = 1.0f;
                    for (int i = 0; i < (steps > 0 ? steps : -steps); ++i) {
                        factor *= (steps > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP);
                    }
                    SetZoomAtViewportPoint(hwnd, zoomFactor * factor, pt.x, pt.y);
                }
                else {
                    ZoomByFactor(hwnd, (steps > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP));
                }
            }
            else if (shiftDown) {
                AdjustOpacity(hwnd, steps * 5);
            }
            else {
                AdjustPenWidth(hwnd, steps);
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (wParam == VK_ESCAPE) {
            if (hwndShapeFlyout && IsWindowVisible(hwndShapeFlyout)) {
                CloseShapeFlyout();
                break;
            }
            ClearSelection(true);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
            break;
        }

        // Live shape fill/stroke modifiers while dragging.
        if (wParam == VK_MENU || wParam == VK_CONTROL || wParam == VK_SHIFT) {
            RefreshShapePreviewIfDrawing();
        }

        if (!IsTypingInEdit()
            && (GetKeyState(VK_CONTROL) & 0x8000) == 0
            && (GetKeyState(VK_MENU) & 0x8000) == 0) {
            const WPARAM key = (wParam >= 'a' && wParam <= 'z') ? (wParam - 'a' + 'A') : wParam;
            switch (key) {
            case 'B': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_PEN, 0), 0); break;
            case 'E': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_ERASER, 0), 0); break;
            case 'G': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_FILL, 0), 0); break;
            case 'M': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_SELECT, 0), 0); break;
            case 'L': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_LINE, 0), 0); break;
            case 'U': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_TOOL_SHAPES, 0), 0); break;
            case 'X': SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SWAP_COLORS, 0), 0); break;
            case VK_TAB:
                SetRailOpen(hwnd, !gRailOpen);
                break;
            case VK_F9:
                SetLayersOpen(hwnd, !gLayersOpen);
                break;
            case VK_F8:
                SetBottomOpen(hwnd, !gBottomOpen);
                break;
            default: break;
            }
        }

        if (wParam == VK_OEM_4) { // [
            AdjustPenWidth(hwnd, -1);
        }
        else if (wParam == VK_OEM_6) { // ]
            AdjustPenWidth(hwnd, 1);
        }
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam == VK_MENU || wParam == VK_CONTROL || wParam == VK_SHIFT) {
            RefreshShapePreviewIfDrawing();
        }
        break;
    case WM_CLOSE:
        if (!PromptSaveIfDirty(hwnd)) {
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client = {};
        GetClientRect(hwnd, &client);
        DrawToolbarBackground(hdc, hwnd, client);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd, IDT_UI_ANIM);
        KillTimer(hwnd, IDT_UI_IDLE);
        KillTimer(hwnd, IDT_CHROME_REBUILD);
        if (GetCapture() == hwnd || (hwndViewport && GetCapture() == hwndViewport)) {
            ReleaseCapture();
        }
        DestroyStrokeLayer();
        Selection_Shutdown();
        ShutdownAppEventHandlers();
        gHistory.Clear();
        DestroyCompositeCache();
        DestroyChromeCache();
        gLayers.Destroy();
        if (hwndShapeFlyout && IsWindow(hwndShapeFlyout)) {
            DestroyWindow(hwndShapeFlyout);
        }
        hwndShapeFlyout = nullptr;
        if (hwndPalette && IsWindow(hwndPalette)) {
            AtelierPalette_Save(hwndPalette);
        }
        hwndPalette = nullptr;
        if (hwndPaletteFloat && IsWindow(hwndPaletteFloat)) {
            DestroyWindow(hwndPaletteFloat);
        }
        hwndPaletteFloat = nullptr;
        gAppMenu = nullptr;
        hwndBrand = nullptr;
        hwndCaptionMin = nullptr;
        hwndCaptionMax = nullptr;
        hwndCaptionClose = nullptr;
        gCaptionHot = nullptr;
        hwndViewport = nullptr;
        if (gUiFont) {
            DeleteObject(gUiFont);
            gUiFont = nullptr;
        }
        if (gBrandFont) {
            DeleteObject(gBrandFont);
            gBrandFont = nullptr;
        }
        if (gChromeBrush) {
            DeleteObject(gChromeBrush);
            gChromeBrush = nullptr;
        }
        if (gChromeDeepBrush) {
            DeleteObject(gChromeDeepBrush);
            gChromeDeepBrush = nullptr;
        }
        if (gChromeElevatedBrush) {
            DeleteObject(gChromeElevatedBrush);
            gChromeElevatedBrush = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
