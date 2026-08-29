#include "BrushGallery.h"
#include "BrushEngine.h"
#include "Resource.h"

#include <gdiplus.h>
#include <cstdio>
#include <cstring>

using namespace Gdiplus;

namespace {

constexpr int kListRowHeight = 52;
constexpr int kThumbSize = 40;

struct BrushGalleryState {
    int selected = 0;
    HWND previewHwnd = nullptr;
    WNDPROC previewOrigProc = nullptr;
};

void DrawTipPreview(HDC hdc, const RECT& rc, const BrushPreset* preset) {
    if (!hdc) return;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    SolidBrush bg(Color(255, 36, 40, 48));
    g.FillRectangle(&bg, rc.left, rc.top, w, h);

    if (!preset || !preset->tip) return;

    const int pad = 4;
    const int fit = (w < h ? w : h) - pad * 2;
    if (fit < 8) return;

    const REAL alphaScale = 0.95f;
    ColorMatrix matrix = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alphaScale, 0.0f,
        0.85f, 0.85f, 0.85f, 0.0f, 1.0f
    };
    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    const REAL x = static_cast<REAL>(rc.left + (w - fit) / 2);
    const REAL y = static_cast<REAL>(rc.top + (h - fit) / 2);
    const RectF dest(x, y, static_cast<REAL>(fit), static_cast<REAL>(fit));
    const int tw = static_cast<int>(preset->tip->GetWidth());
    const int th = static_cast<int>(preset->tip->GetHeight());
    g.DrawImage(preset->tip.get(), dest, 0.0f, 0.0f, static_cast<REAL>(tw), static_cast<REAL>(th),
        UnitPixel, &attrs);
}

void UpdatePreviewPane(HWND hDlg, BrushGalleryState* state) {
    if (!state || !state->previewHwnd) return;
    InvalidateRect(state->previewHwnd, nullptr, FALSE);

    char details[128] = "";
    if (const BrushPreset* preset = GetBrushPreset(state->selected)) {
        snprintf(details, sizeof(details), "%s  |  %s  |  spacing %.0f%%  |  size %d px",
            preset->name.c_str(),
            preset->builtin ? "Built-in" : "Custom",
            preset->spacing * 100.0f,
            preset->defaultSize);
    }
    SetDlgItemTextA(hDlg, IDC_BRUSH_GALLERY_INFO, details);
}

static LRESULT CALLBACK PreviewPaneProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        const BrushGalleryState* state = reinterpret_cast<BrushGalleryState*>(
            GetWindowLongPtrA(GetParent(hwnd), GWLP_USERDATA));
        if (state) {
            RECT rc = ps.rcPaint;
            if (const BrushPreset* preset = GetBrushPreset(state->selected)) {
                DrawTipPreview(hdc, rc, preset);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static INT_PTR CALLBACK BrushGalleryDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    BrushGalleryState* state = reinterpret_cast<BrushGalleryState*>(GetWindowLongPtrA(hDlg, GWLP_USERDATA));

    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<BrushGalleryState*>(lParam);
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return TRUE;

        state->selected = GetActiveBrushIndex();
        if (state->selected < 0) state->selected = 0;

        HWND list = GetDlgItem(hDlg, IDC_BRUSH_GALLERY_LIST);
        if (list) {
            const int count = BrushPresetCount();
            for (int i = 0; i < count; ++i) {
                const BrushPreset* preset = GetBrushPreset(i);
                if (!preset) continue;
                const int idx = static_cast<int>(SendMessageA(list, LB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(preset->name.c_str())));
                if (idx >= 0) {
                    SendMessageA(list, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
                }
            }
            SendMessageA(list, LB_SETCURSEL, state->selected, 0);
        }

        state->previewHwnd = GetDlgItem(hDlg, IDC_BRUSH_PREVIEW);
        if (state->previewHwnd) {
            state->previewOrigProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
                state->previewHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PreviewPaneProc)));
        }
        UpdatePreviewPane(hDlg, state);
        return TRUE;
    }
    case WM_DESTROY:
        if (state && state->previewHwnd && state->previewOrigProc) {
            SetWindowLongPtrA(state->previewHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state->previewOrigProc));
            state->previewOrigProc = nullptr;
        }
        return FALSE;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis && mis->CtlID == IDC_BRUSH_GALLERY_LIST) {
            mis->itemHeight = kListRowHeight;
        }
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis || dis->CtlID != IDC_BRUSH_GALLERY_LIST) break;

        const int brushIndex = static_cast<int>(SendMessageA(dis->hwndItem, LB_GETITEMDATA, dis->itemID, 0));
        const BrushPreset* preset = GetBrushPreset(brushIndex);

        HBRUSH bgBrush = nullptr;
        if (dis->itemState & ODS_SELECTED) {
            bgBrush = CreateSolidBrush(RGB(62, 98, 130));
        } else {
            bgBrush = CreateSolidBrush(RGB(48, 52, 60));
        }
        FillRect(dis->hDC, &dis->rcItem, bgBrush);
        DeleteObject(bgBrush);

        RECT thumbRc = dis->rcItem;
        thumbRc.left += 6;
        thumbRc.top += (dis->rcItem.bottom - dis->rcItem.top - kThumbSize) / 2;
        thumbRc.right = thumbRc.left + kThumbSize;
        thumbRc.bottom = thumbRc.top + kThumbSize;
        DrawTipPreview(dis->hDC, thumbRc, preset);

        char label[96] = "";
        if (preset) {
            snprintf(label, sizeof(label), "%s%s", preset->name.c_str(),
                preset->builtin ? "" : "  (custom)");
        }
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, RGB(230, 232, 238));
        RECT textRc = dis->rcItem;
        textRc.left = thumbRc.right + 10;
        DrawTextA(dis->hDC, label, -1, &textRc, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (dis->itemState & ODS_FOCUS) {
            DrawFocusRect(dis->hDC, &dis->rcItem);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BRUSH_GALLERY_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE && state) {
                HWND list = GetDlgItem(hDlg, IDC_BRUSH_GALLERY_LIST);
                const int sel = static_cast<int>(SendMessageA(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    state->selected = static_cast<int>(SendMessageA(list, LB_GETITEMDATA, sel, 0));
                    UpdatePreviewPane(hDlg, state);
                }
            }
            return TRUE;
        case IDOK:
            if (state) {
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

bool ShowBrushGalleryDialog(HWND owner) {
    const int previous = GetActiveBrushIndex();
    BrushGalleryState state = {};
    state.selected = previous;

    if (DialogBoxParamA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_BRUSH_GALLERY), owner,
            BrushGalleryDlgProc, reinterpret_cast<LPARAM>(&state)) != IDOK) {
        return false;
    }

    if (state.selected < 0 || state.selected >= BrushPresetCount()) {
        return false;
    }
    if (state.selected == previous) {
        return false;
    }

    SetActiveBrushIndex(state.selected);
    return true;
}
