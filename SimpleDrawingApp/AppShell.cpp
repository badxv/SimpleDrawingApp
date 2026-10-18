#include "AppShell.h"
#include "AppState.h"
#include "AppMetrics.h"
#include "AppFeatureFlags.h"
#include "BrushEngine.h"
#include "EventBus.h"
#include "AtelierEvents.h"
#include "UiShapeFlyout.h"
#include "UiBrushFlyout.h"
#include "LayerStack.h"
#include <commctrl.h>
#include <cstdio>
#include <cstring>

namespace {

const char* StatusToolName() {
    if (currentTool == DrawTool::Eraser) return "Eraser";
    if (currentTool == DrawTool::Fill) return "Fill";
    if (currentTool == DrawTool::Line) return "Line";
    if (currentTool == DrawTool::Select) return "Select";
    if (currentTool == DrawTool::Shape) {
        switch (currentShape) {
        case ShapeKind::Rectangle: return "Rectangle";
        case ShapeKind::Ellipse: return "Ellipse";
        case ShapeKind::Triangle: return "Triangle";
        case ShapeKind::Star: return "Star";
        case ShapeKind::Diamond: return "Diamond";
        case ShapeKind::RoundRect: return "Round rect";
        }
    }
    return "Pen";
}

void AppendExtras(char* dest, size_t destChars, const char* piece) {
    if (!dest || !piece || !piece[0] || destChars == 0) return;
    strcat_s(dest, destChars, piece);
}

void AppendPct(char* dest, size_t destChars, const char* key, int value, int hideIf) {
    if (value == hideIf) return;
    char buf[32];
    sprintf_s(buf, " %s:%d%%", key, value);
    AppendExtras(dest, destChars, buf);
}

void AppendIntKey(char* dest, size_t destChars, const char* key, int value, int hideIf) {
    if (value == hideIf) return;
    char buf[32];
    sprintf_s(buf, " %s:%d", key, value);
    AppendExtras(dest, destChars, buf);
}

void AppendDeg(char* dest, size_t destChars, const char* key, int value, int hideIf) {
    if (value == hideIf) return;
    char buf[32];
    sprintf_s(buf, " %s:%ddeg", key, value);
    AppendExtras(dest, destChars, buf);
}

void BuildStatusTip(char* out, size_t outChars) {
    if (!out || outChars == 0) return;
    out[0] = '\0';

    const char* tip = "B  [ / ] size";
    if (currentTool == DrawTool::Eraser) tip = "E  erase on active layer";
    else if (currentTool == DrawTool::Fill) tip = "G  click to flood fill";
    else if (currentTool == DrawTool::Line) tip = "L  drag  Shift: H/V/45";
    else if (currentTool == DrawTool::Select) tip = "M  drag  Ctrl+C/X/V  Del";
    else if (currentTool == DrawTool::Shape) tip = "U  Alt=fill  Ctrl=both  Shift=constrain";
    else if (currentTool == DrawTool::Pen) tip = "B  [ / ] size  draw freely";

    // Only emit non-default dynamics so the tip pane stays readable (avoids overflow/clip).
    char extras[256] = "";
    if (currentTool == DrawTool::Pen) {
        AppendExtras(extras, sizeof(extras), "  ");
        AppendExtras(extras, sizeof(extras), GetActiveBrushName());
        AppendPct(extras, sizeof(extras), "Fx", brushFlipX, 0);
        AppendPct(extras, sizeof(extras), "Fy", brushFlipY, 0);
        AppendPct(extras, sizeof(extras), "Fj", brushFlowJitter, 0);
        AppendPct(extras, sizeof(extras), "Cj", brushColorJitter, 0);
        AppendPct(extras, sizeof(extras), "Hj", brushHueJitter, 0);
        AppendPct(extras, sizeof(extras), "Sj", brushSatJitter, 0);
        AppendPct(extras, sizeof(extras), "Bj", brushBrightJitter, 0);
        AppendPct(extras, sizeof(extras), "Z", brushSizeJitter, 0);
        AppendPct(extras, sizeof(extras), "Md", brushMinDiameter, 0);
        AppendPct(extras, sizeof(extras), "We", brushWetEdges, 0);
        AppendPct(extras, sizeof(extras), "Rj", brushRoundnessJitter, 0);
        AppendPct(extras, sizeof(extras), "S", brushScatter, 0);
        AppendPct(extras, sizeof(extras), "A", brushAngleJitter, 0);
        AppendDeg(extras, sizeof(extras), "T", brushTipAngle, 0);
        AppendPct(extras, sizeof(extras), "O", brushOpacityJitter, 0);
        AppendIntKey(extras, sizeof(extras), "C", brushStampCount, 1);
        AppendPct(extras, sizeof(extras), "Rd", brushRoundness, 100);
        AppendPct(extras, sizeof(extras), "F", brushFlow, 100);
        AppendPct(extras, sizeof(extras), "H", brushHardness, 100);
        AppendPct(extras, sizeof(extras), "M", brushSmoothing, 0);
        if (penPressureEnabled) {
            char buf[24];
            sprintf_s(buf, "  P:%d%%", static_cast<int>(lastPenPressure * 100.0f + 0.5f));
            AppendExtras(extras, sizeof(extras), buf);
        }
    }
    if (IsFeatureEnabled(AppFeature::SnapToGrid)) {
        AppendExtras(extras, sizeof(extras), "  Snap");
    }
    if (IsFeatureEnabled(AppFeature::CanvasGrid)) {
        AppendExtras(extras, sizeof(extras), "  Grid");
    }

    sprintf_s(out, outChars, "%s - %s%s", StatusToolName(), tip, extras);
}

}  // namespace

void UpdatePenWidthDisplay() {
    if (!hwndPenWidthBox) return;
    char buf[16];
    sprintf_s(buf, "%d", penWidth);
    suppressEditNotify = true;
    SetWindowTextA(hwndPenWidthBox, buf);
    suppressEditNotify = false;
}

void ApplyPenWidth(HWND hwnd, int width) {
    if (width < kPenWidthMin) width = kPenWidthMin;
    if (width > kPenWidthMax) width = kPenWidthMax;
    if (width == penWidth) {
        SyncFeatureFlagMenuItems();
        return;
    }
    penWidth = width;
    if (hwndSlider) {
        SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);
    }
    UpdatePenWidthDisplay();
    UpdateStatusBar(hwnd);
    SaveFeatureFlags();
    SyncFeatureFlagMenuItems();
    if (IsActiveBrushCustom()) {
        PersistActiveBrushDefaultSize(penWidth);
    }
    SyncBrushFlyoutChecks();
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
    wchar_t nameBuf[MAX_PATH] = L"Untitled";
    const wchar_t* name = nameBuf;
    if (gDocumentPath[0]) {
        wchar_t pathW[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, gDocumentPath, -1, pathW, MAX_PATH);
        const wchar_t* slash = wcsrchr(pathW, L'\\');
        if (!slash) slash = wcsrchr(pathW, L'/');
        const wchar_t* base = slash ? (slash + 1) : pathW;
        wcsncpy_s(nameBuf, base, _TRUNCATE);
    }

    wchar_t title[MAX_PATH + 64];
    // Em dash (U+2014) via UTF-16 — SetWindowTextA would mojibake UTF-8.
    swprintf_s(title, L"%s%s \x2014 Simple Drawing App", name, documentDirty ? L" *" : L"");
    SetWindowTextW(hwnd, title);
}

void UpdateStatusBar(HWND hwnd) {
    if (!hwndStatus) return;

    const int zoomPct = static_cast<int>(zoomFactor * 100.0f + 0.5f);

    char part0[256];
    char part1[96];
    char part2[80];
    BuildStatusTip(part0, sizeof(part0));

    if (const Layer* layer = gLayers.ActiveLayer()) {
        if (gStatusHoverDocX >= 0 && gStatusHoverDocY >= 0) {
            sprintf_s(part1, "%s | %dx%d %d%% | %d, %d",
                layer->name.c_str(), docWidth, docHeight, zoomPct,
                gStatusHoverDocX, gStatusHoverDocY);
        } else {
            sprintf_s(part1, "%s | %dx%d %d%%",
                layer->name.c_str(), docWidth, docHeight, zoomPct);
        }
    } else if (gStatusHoverDocX >= 0 && gStatusHoverDocY >= 0) {
        sprintf_s(part1, "%dx%d %d%% | %d, %d",
            docWidth, docHeight, zoomPct, gStatusHoverDocX, gStatusHoverDocY);
    } else {
        sprintf_s(part1, "%dx%d %d%%", docWidth, docHeight, zoomPct);
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
        if (tool != DrawTool::Pen) {
            CloseBrushFlyout();
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
    SyncBrushFlyoutChecks();
    if (changed) {
        EventPayload payload{};
        payload.type = AtelierEvent::ActiveToolChanged;
        payload.tool = tool;
        AppEventBus().Publish(payload);
        HWND mainWnd = hwndToolButtons[0] ? GetAncestor(hwndToolButtons[0], GA_ROOT) : nullptr;
        if (mainWnd) UpdateStatusBar(mainWnd);
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
    // Wider tip pane (left) so brush dynamics (Z/Md/…) are not clipped.
    int parts[4] = {
        (width * 62) / 100,
        (width * 82) / 100,
        width - 1,
        -1
    };
    SendMessageA(hwndStatus, SB_SETPARTS, 3, (LPARAM)parts);
    (void)hwnd;
}
