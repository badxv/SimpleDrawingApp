#include "AppCommands.h"
#include "AppShell.h"
#include "AppDialogs.h"
#include "AppFeatureFlags.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppCanvas.h"
#include "AppSelection.h"
#include "AppDocument.h"
#include "UiChromeLayout.h"
#include "UiShapeFlyout.h"
#include "ColorPicker.h"
#include "LayerHistory.h"
#include "LayerStack.h"
#include "DrawingTools.h"
#include "AtelierPalette.h"
#include "Resource.h"

#include <commctrl.h>
#include <cstdlib>
#include <string>

namespace {

void SyncPaletteFromApp() {
    if (hwndPalette) {
        AtelierPalette_SetColors(hwndPalette, penColor, backColor);
    }
}

void InvalidateColorChips() {
    if (hwndActionButtons[0]) InvalidateRect(hwndActionButtons[0], NULL, FALSE);
    if (hwndBgButton) InvalidateRect(hwndBgButton, NULL, FALSE);
    SyncPaletteFromApp();
}

bool TryRenameActiveLayer(HWND hwnd) {
    char name[80] = {};
    if (!PromptLayerRename(hwnd, name, sizeof(name))) return false;
    std::string normalized;
    const Layer* cur = gLayers.ActiveLayer();
    if (!cur || !LayerStack::NormalizeLayerName(name, normalized) || normalized == cur->name) {
        return false;
    }
    gHistory.Push(gLayers);
    if (!gLayers.RenameActive(name)) return false;
    RefreshLayerList();
    MarkDirty(hwnd);
    UpdateStatusBar(hwnd);
    return true;
}

void PopupAppMenu(HWND hwnd, int menuIndex, HWND anchorBtn) {
    if (!gAppMenu || menuIndex < 0) return;
    HMENU sub = GetSubMenu(gAppMenu, menuIndex);
    if (!sub) return;
    SyncFeatureFlagMenuItems();
    if (menuIndex == 0) {
        EnableMenuItem(sub, IDM_OPEN_LAST,
            MF_BYCOMMAND | (LastDocumentAvailable() ? MF_ENABLED : MF_GRAYED));
        SyncRecentFileMenu(sub);
    }
    RECT br = {};
    if (anchorBtn) GetWindowRect(anchorBtn, &br);
    else GetWindowRect(hwnd, &br);
    TrackPopupMenu(sub, TPM_LEFTALIGN | TPM_TOPALIGN, br.left, br.bottom, 0, hwnd, NULL);
}

}  // namespace

bool HandleAppCommand(HWND hwnd, int cmdId, int notifyCode) {
    if (cmdId == IDC_WIDTH_EDIT && notifyCode == EN_CHANGE) {
        if (suppressEditNotify) return true;
        char buf[16];
        GetWindowTextA(hwndPenWidthBox, buf, sizeof(buf));
        int val = atoi(buf);
        if (val >= 1 && val <= 50) {
            penWidth = val;
            SendMessage(hwndSlider, TBM_SETPOS, TRUE, val);
            UpdateStatusBar(hwnd);
        }
        return true;
    }

    if (cmdId == IDC_OPACITY_EDIT && notifyCode == EN_CHANGE) {
        if (suppressEditNotify) return true;
        char buf[16];
        GetWindowTextA(hwndOpacityBox, buf, sizeof(buf));
        int val = atoi(buf);
        if (val >= 1 && val <= 100) {
            penOpacity = val;
            SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, val);
            UpdateStatusBar(hwnd);
        }
        return true;
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
        } else if (notifyCode == 3) {
            COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
            penColor = newColor;
            InvalidateColorChips();
        } else if (notifyCode == 4) {
            COLORREF newColor = ColorPicker::PickColor(hwnd, backColor);
            backColor = newColor;
            InvalidateColorChips();
        }
        UpdateStatusBar(hwnd);
        return true;
    }

    if (cmdId >= IDC_SHAPE_RECT && cmdId <= IDC_SHAPE_ROUNDRECT) {
        currentShape = static_cast<ShapeKind>(cmdId - IDC_SHAPE_RECT);
        SetActiveTool(DrawTool::Shape);
        CloseShapeFlyout();
        UpdateStatusBar(hwnd);
        return true;
    }
    if (cmdId >= IDC_SHAPE_MODE_STROKE && cmdId <= IDC_SHAPE_MODE_BOTH) {
        shapePaintMode = static_cast<ShapePaintMode>(cmdId - IDC_SHAPE_MODE_STROKE);
        SyncShapeFlyoutChecks();
        if (currentTool != DrawTool::Shape) {
            SetActiveTool(DrawTool::Shape);
        }
        UpdateStatusBar(hwnd);
        return true;
    }

    switch (cmdId) {
    case IDC_TOOL_PEN:
    case IDM_TOOL_PEN:
        ClearSelection(true);
        SetActiveTool(DrawTool::Pen);
        UpdateStatusBar(hwnd);
        return true;
    case IDC_TOOL_ERASER:
    case IDM_TOOL_ERASER:
        ClearSelection(true);
        SetActiveTool(DrawTool::Eraser);
        UpdateStatusBar(hwnd);
        return true;
    case IDC_TOOL_FILL:
    case IDM_TOOL_FILL:
        ClearSelection(true);
        SetActiveTool(DrawTool::Fill);
        UpdateStatusBar(hwnd);
        return true;
    case IDC_TOOL_LINE:
    case IDM_TOOL_LINE:
        ClearSelection(true);
        SetActiveTool(DrawTool::Line);
        UpdateStatusBar(hwnd);
        return true;
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
        return true;
    case IDC_TOOL_SELECT:
    case IDM_TOOL_SELECT:
        SetActiveTool(DrawTool::Select);
        UpdateStatusBar(hwnd);
        return true;
    case IDC_SWAP_COLORS:
    case IDM_SWAP_COLORS: {
        const COLORREF tmp = penColor;
        penColor = backColor;
        backColor = tmp;
        InvalidateColorChips();
        return true;
    }
    case IDC_TOGGLE_RAIL:
        SetRailOpen(hwnd, !gRailOpen);
        return true;
    case IDC_TOGGLE_LAYERS:
        SetLayersOpen(hwnd, !gLayersOpen);
        return true;
    case IDC_TOGGLE_BOTTOM:
        SetBottomOpen(hwnd, !gBottomOpen);
        return true;
    case IDC_MENU_FILE:
        PopupAppMenu(hwnd, 0, hwndMenuButtons[0]);
        return true;
    case IDC_MENU_EDIT:
        PopupAppMenu(hwnd, 1, hwndMenuButtons[1]);
        return true;
    case IDC_MENU_IMAGE:
        PopupAppMenu(hwnd, 2, hwndMenuButtons[2]);
        return true;
    case IDC_MENU_VIEW:
        PopupAppMenu(hwnd, 3, hwndMenuButtons[3]);
        return true;
    case IDC_MENU_TOOLS:
        PopupAppMenu(hwnd, 4, hwndMenuButtons[4]);
        return true;
    case IDC_MENU_HELP:
        PopupAppMenu(hwnd, 5, hwndMenuButtons[5]);
        return true;
    case IDC_BG_BUTTON: {
        COLORREF newColor = ColorPicker::PickColor(hwnd, backColor);
        backColor = newColor;
        InvalidateColorChips();
        return true;
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
        return true;
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
        return true;
    case IDC_LAYER_UP:
        ClearSelection(true);
        gHistory.Push(gLayers);
        if (gLayers.MoveActiveUp()) {
            InvalidateComposite();
            RefreshLayerList();
            MarkDirty(hwnd);
            InvalidateCanvas();
        }
        return true;
    case IDC_LAYER_DOWN:
        ClearSelection(true);
        gHistory.Push(gLayers);
        if (gLayers.MoveActiveDown()) {
            InvalidateComposite();
            RefreshLayerList();
            MarkDirty(hwnd);
            InvalidateCanvas();
        }
        return true;
    case IDC_LAYER_VISIBLE:
        if (suppressLayerNotify) return true;
        {
            const bool checked = SendMessageA(hwndLayerVisible, BM_GETCHECK, 0, 0) == BST_CHECKED;
            gHistory.Push(gLayers);
            gLayers.SetActiveVisible(checked);
            InvalidateComposite();
            RefreshLayerList();
            MarkDirty(hwnd);
            InvalidateCanvas();
        }
        return true;
    case IDC_LAYER_LIST:
        if (suppressLayerNotify) return true;
        if (notifyCode == LBN_SELCHANGE) {
            const int row = static_cast<int>(SendMessageA(hwndLayerList, LB_GETCURSEL, 0, 0));
            if (row >= 0) {
                const int layerIndex = static_cast<int>(SendMessageA(hwndLayerList, LB_GETITEMDATA, row, 0));
                ClearSelection(true);
                gLayers.SetActiveIndex(layerIndex);
                RefreshLayerList();
                UpdateStatusBar(hwnd);
            }
        } else if (notifyCode == LBN_DBLCLK) {
            TryRenameActiveLayer(hwnd);
        }
        return true;
    case IDC_LAYER_RENAME:
        TryRenameActiveLayer(hwnd);
        return true;
    case IDM_CUT:
        CutSelection(hwnd);
        return true;
    case IDM_COPY:
        CopySelection(hwnd);
        return true;
    case IDM_PASTE:
        PasteSelection(hwnd);
        return true;
    case IDM_DELETE_SEL:
        DeleteSelection(hwnd);
        return true;
    case IDM_SELECT_ALL:
        SelectAll(hwnd);
        return true;
    case IDM_ZOOM_IN:
        ZoomByFactor(hwnd, ZOOM_STEP);
        return true;
    case IDM_ZOOM_OUT:
        ZoomByFactor(hwnd, 1.0f / ZOOM_STEP);
        return true;
    case IDM_ZOOM_100:
        ZoomToActual(hwnd);
        return true;
    case IDM_ZOOM_FIT:
        ZoomToFit(hwnd);
        return true;
    case IDM_FEAT_PASTE_VIEW:
        ToggleFeatureFlag(AppFeature::PasteAtViewOrigin);
        return true;
    case IDM_FEAT_SEL_VEIL:
        ToggleFeatureFlag(AppFeature::SelectionExteriorVeil);
        InvalidateCanvas();
        return true;
        case IDM_FEAT_WARN_SHRINK:
            ToggleFeatureFlag(AppFeature::WarnCanvasShrink);
            return true;
        case IDM_FEAT_REOPEN_LAST:
            ToggleFeatureFlag(AppFeature::ReopenLastDocument);
            return true;
        case IDM_FEAT_AUTOSAVE:
            ToggleFeatureFlag(AppFeature::AutosaveRecovery);
            return true;
        case IDM_FEAT_GRID:
            ToggleFeatureFlag(AppFeature::CanvasGrid);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
            return true;
        case IDM_FEAT_SNAP_GRID:
            ToggleFeatureFlag(AppFeature::SnapToGrid);
            UpdateStatusBar(hwnd);
            return true;
        case IDM_GRID_8:
            SetGridSpacing(8);
            InvalidateCanvas();
            return true;
        case IDM_GRID_16:
            SetGridSpacing(16);
            InvalidateCanvas();
            return true;
        case IDM_GRID_32:
            SetGridSpacing(32);
            InvalidateCanvas();
            return true;
        case IDM_GRID_64:
            SetGridSpacing(64);
            InvalidateCanvas();
            return true;
        case IDM_BRUSH_FINE:
            ApplyPenWidth(hwnd, kBrushPresetFine);
            return true;
        case IDM_BRUSH_MEDIUM:
            ApplyPenWidth(hwnd, kBrushPresetMedium);
            return true;
        case IDM_BRUSH_BOLD:
            ApplyPenWidth(hwnd, kBrushPresetBold);
            return true;
        case IDC_COLOR_BUTTON: {
        COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
        penColor = newColor;
        InvalidateColorChips();
        return true;
    }
    case IDC_NEW_BUTTON:
    case IDM_NEW:
        NewDocument(hwnd);
        return true;
    case IDC_CAPTION_MIN:
        SendMessageA(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        return true;
    case IDC_CAPTION_MAX:
        SendMessageA(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
        return true;
    case IDC_CAPTION_CLOSE:
        SendMessageA(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        return true;
    case IDC_CLEAR_BUTTON:
    case IDM_CLEAR:
        ClearCanvas(hwnd, true);
        UpdateStatusBar(hwnd);
        return true;
    case IDC_UNDO_BUTTON:
    case IDM_UNDO:
        UndoDocument(hwnd);
        return true;
    case IDC_REDO_BUTTON:
    case IDM_REDO:
        RedoDocument(hwnd);
        return true;
        case IDC_SAVE_BUTTON:
        case IDM_SAVE:
            SaveDocument(hwnd);
            return true;
        case IDM_SAVE_AS:
            SaveDocumentAs(hwnd);
            return true;
        case IDM_EXPORT:
            ExportDocument(hwnd);
            return true;
        case IDM_PRINT:
            PrintDocument(hwnd);
            return true;
    case IDC_LOAD_BUTTON:
        case IDM_OPEN:
            OpenDocument(hwnd);
            return true;
        case IDM_OPEN_LAST:
            OpenLastDocument(hwnd);
            return true;
        case IDM_RECENT_0:
        case IDM_RECENT_1:
        case IDM_RECENT_2:
        case IDM_RECENT_3:
        case IDM_RECENT_4:
            OpenRecentDocument(hwnd, cmdId - IDM_RECENT_0);
            return true;
        case IDM_CLEAR_RECENT:
            ClearRecentDocuments();
            return true;
    case IDM_CANVAS_SIZE:
        ResizeCanvas(hwnd);
        return true;
    case IDM_FLATTEN_LAYERS:
        FlattenLayers(hwnd);
        return true;
    case IDM_FLIP_H:
        FlipDocumentHorizontal(hwnd);
        return true;
    case IDM_FLIP_V:
        FlipDocumentVertical(hwnd);
        return true;
    case IDM_ROTATE_90:
        RotateDocument90Cw(hwnd);
        return true;
    case IDM_ABOUT:
        ShowAboutDialog(hwnd);
        return true;
    case IDM_SHORTCUTS:
        ShowShortcutsDialog(hwnd);
        return true;
    case IDM_EXIT:
        SendMessageA(hwnd, WM_CLOSE, 0, 0);
        return true;
    default:
        return true;
    }
}
