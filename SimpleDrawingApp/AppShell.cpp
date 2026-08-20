#include "AppShell.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "EventBus.h"
#include "AtelierEvents.h"
#include "UiShapeFlyout.h"
#include "LayerStack.h"
#include <commctrl.h>
#include <cstdio>

void UpdatePenWidthDisplay() {
    if (!hwndPenWidthBox) return;
    char buf[16];
    sprintf_s(buf, "%d", penWidth);
    suppressEditNotify = true;
    SetWindowTextA(hwndPenWidthBox, buf);
    suppressEditNotify = false;
}

void UpdateOpacityDisplay() {
    if (!hwndOpacityBox) return;
    char buf[16];
    sprintf_s(buf, "%d", penOpacity);
    suppressEditNotify = true;
    SetWindowTextA(hwndOpacityBox, buf);
    suppressEditNotify = false;
}
void UpdateWindowTitle(HWND hwnd) {
    char title[128];
    sprintf_s(title, "Simple Drawing App%s", documentDirty ? " *" : "");
    SetWindowTextA(hwnd, title);
}

void UpdateStatusBar(HWND hwnd) {
    if (!hwndStatus) return;

    const char* toolName = "Pen";
    if (currentTool == DrawTool::Eraser) toolName = "Eraser";
    else if (currentTool == DrawTool::Fill) toolName = "Fill";
    else if (currentTool == DrawTool::Line) toolName = "Line";
    else if (currentTool == DrawTool::Shape) {
        switch (currentShape) {
        case ShapeKind::Rectangle: toolName = "Rectangle"; break;
        case ShapeKind::Ellipse: toolName = "Ellipse"; break;
        case ShapeKind::Triangle: toolName = "Triangle"; break;
        case ShapeKind::Star: toolName = "Star"; break;
        case ShapeKind::Diamond: toolName = "Diamond"; break;
        case ShapeKind::RoundRect: toolName = "Round rect"; break;
        }
    }
    else if (currentTool == DrawTool::Select) toolName = "Select";

    const int zoomPct = static_cast<int>(zoomFactor * 100.0f + 0.5f);

    char part0[64];
    char part1[64];
    char part2[80];
    sprintf_s(part0, "Tool: %s", toolName);
    if (const Layer* layer = gLayers.ActiveLayer()) {
        sprintf_s(part1, "%s | %d x %d  %d%%", layer->name.c_str(), docWidth, docHeight, zoomPct);
    }
    else {
        sprintf_s(part1, "Size: %d x %d  %d%%", docWidth, docHeight, zoomPct);
    }
    sprintf_s(part2, "W:%d  Op:%d%%  %s", penWidth, penOpacity, documentDirty ? "Modified" : "Saved");

    SendMessageA(hwndStatus, SB_SETTEXTA, 0, (LPARAM)part0);
    SendMessageA(hwndStatus, SB_SETTEXTA, 1, (LPARAM)part1);
    SendMessageA(hwndStatus, SB_SETTEXTA, 2, (LPARAM)part2);
    (void)hwnd;
}

void MarkDirty(HWND hwnd) {
    documentDirty = true;
    EventPayload payload{};
    payload.type = AtelierEvent::DocumentDirtyChanged;
    payload.hwnd = hwnd;
    payload.documentDirty = true;
    AppEventBus().Publish(payload);
}

void MarkClean(HWND hwnd) {
    documentDirty = false;
    EventPayload payload{};
    payload.type = AtelierEvent::DocumentDirtyChanged;
    payload.hwnd = hwnd;
    payload.documentDirty = false;
    AppEventBus().Publish(payload);
}

void SetActiveTool(DrawTool tool) {
    const bool changed = (currentTool != tool);
    currentTool = tool;
    if (changed) {
        gToolFlash = 1.0f;
        if (tool != DrawTool::Shape) {
            CloseShapeFlyout();
        }
    }
    for (int i = 0; i < kToolButtonCount; ++i) {
        if (!hwndToolButtons[i]) continue;
        const bool selected = (static_cast<int>(tool) == i);
        SendMessageA(hwndToolButtons[i], BM_SETCHECK, selected ? BST_CHECKED : BST_UNCHECKED, 0);
        RedrawWindow(hwndToolButtons[i], NULL, NULL,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
    SyncShapeFlyoutChecks();
    if (changed) {
        EventPayload payload{};
        payload.type = AtelierEvent::ActiveToolChanged;
        payload.tool = tool;
        AppEventBus().Publish(payload);
    }
}
bool IsTypingInEdit() {
    HWND focus = GetFocus();
    if (!focus) return false;
    char cls[32] = {};
    GetClassNameA(focus, cls, static_cast<int>(sizeof(cls)));
    return _stricmp(cls, "Edit") == 0;
}
void RefreshLayerList() {
    if (!hwndLayerList) return;
    suppressLayerNotify = true;
    SendMessageA(hwndLayerList, LB_RESETCONTENT, 0, 0);

    // Show topmost layer first in the list.
    for (int i = gLayers.Count() - 1; i >= 0; --i) {
        const Layer* layer = gLayers.At(i);
        if (!layer) continue;
        char label[96];
        sprintf_s(label, "%s%s  %d%%",
            layer->visible ? "" : "[H] ",
            layer->name.c_str(),
            layer->opacity);
        const int idx = static_cast<int>(SendMessageA(hwndLayerList, LB_ADDSTRING, 0, (LPARAM)label));
        SendMessageA(hwndLayerList, LB_SETITEMDATA, idx, i);
    }

    // Select the row that matches active layer.
    const int count = static_cast<int>(SendMessageA(hwndLayerList, LB_GETCOUNT, 0, 0));
    for (int row = 0; row < count; ++row) {
        const int layerIndex = static_cast<int>(SendMessageA(hwndLayerList, LB_GETITEMDATA, row, 0));
        if (layerIndex == gLayers.ActiveIndex()) {
            SendMessageA(hwndLayerList, LB_SETCURSEL, row, 0);
            break;
        }
    }

    if (hwndLayerVisible) {
        const Layer* active = gLayers.ActiveLayer();
        const bool visible = active ? active->visible : true;
        SendMessageA(hwndLayerVisible, BM_SETCHECK, visible ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (hwndLayerOpacity) {
        const Layer* active = gLayers.ActiveLayer();
        const int opacity = active ? active->opacity : 100;
        SendMessage(hwndLayerOpacity, TBM_SETPOS, TRUE, opacity);
    }
    suppressLayerNotify = false;

    HWND mainWnd = hwndLayerList ? GetAncestor(hwndLayerList, GA_ROOT) : nullptr;
    PublishLayerListChanged(mainWnd);
}


void LayoutStatusParts(HWND hwnd) {
    if (!hwndStatus) return;
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    int parts[4] = { width / 3, (width * 2) / 3, width - 1, -1 };
    SendMessageA(hwndStatus, SB_SETPARTS, 3, (LPARAM)parts);
}
