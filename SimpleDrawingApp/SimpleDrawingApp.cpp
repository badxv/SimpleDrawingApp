#include <windows.h>
#include <windowsx.h>
#include "framework.h"
#include "SimpleDrawingApp.h"
#include "FileManager.h"
#include "ColorPicker.h"
#include "CanvasHistory.h"
#include "DrawingTools.h"
#include "Resource.h"

#include <commctrl.h>
#include <objidl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <cstdio>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Dwmapi.lib")

using namespace Gdiplus;

namespace {
const char CLASS_NAME[] = "SimpleDrawingAppWindowClass";
const char VIEWPORT_CLASS_NAME[] = "SimpleDrawingAppViewport";
constexpr int TOOLBAR_HEIGHT = 92;
constexpr int STATUS_HEIGHT = 24;
constexpr int DEFAULT_DOC_WIDTH = 1280;
constexpr int DEFAULT_DOC_HEIGHT = 720;
constexpr int MIN_DOC_SIZE = 1;
constexpr int MAX_DOC_SIZE = 10000;
constexpr COLORREF WORKSPACE_COLOR = RGB(180, 180, 180);

const COLORREF kSwatches[8] = {
    RGB(0, 0, 0),
    RGB(255, 255, 255),
    RGB(232, 17, 35),
    RGB(247, 99, 12),
    RGB(255, 185, 0),
    RGB(16, 124, 16),
    RGB(0, 120, 212),
    RGB(136, 23, 152)
};

struct CanvasPreset {
    const char* label;
    int width;
    int height;
};

const CanvasPreset kPresets[] = {
    { "800 x 600", 800, 600 },
    { "1024 x 768", 1024, 768 },
    { "1280 x 720", 1280, 720 },
    { "1920 x 1080", 1920, 1080 },
    { "Custom", 0, 0 }
};

AppTheme gTheme;
CanvasHistory gHistory;
HFONT gUiFont = nullptr;
HBRUSH gChromeBrush = nullptr;
HACCEL gAccel = nullptr;

HWND hwndViewport = nullptr;
HWND hwndSlider = nullptr;
HWND hwndPenWidthBox = nullptr;
HWND hwndOpacitySlider = nullptr;
HWND hwndOpacityBox = nullptr;
HWND hwndStatus = nullptr;
HWND hwndToolButtons[3] = {};
HWND hwndSwatches[8] = {};
HWND hwndActionButtons[7] = {};

POINT lastPoint = {};
bool isDrawing = false;
bool suppressEditNotify = false;

int docWidth = DEFAULT_DOC_WIDTH;
int docHeight = DEFAULT_DOC_HEIGHT;
int scrollX = 0;
int scrollY = 0;

Bitmap* canvasBitmap = nullptr;
Graphics* canvasGraphics = nullptr;
Bitmap* strokeLayer = nullptr;
Graphics* strokeGraphics = nullptr;
WNDPROC gOldTrackbarProc = nullptr;
ULONG_PTR gdiplusToken = 0;
}

COLORREF penColor = RGB(0, 0, 0);
int penWidth = 5;
int penOpacity = 100; // percent 1-100
DrawTool currentTool = DrawTool::Pen;
bool documentDirty = false;

static BYTE OpacityToAlpha() {
    int pct = penOpacity;
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    return static_cast<BYTE>((pct * 255 + 50) / 100);
}

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

    char part0[64];
    char part1[64];
    char part2[64];
    sprintf_s(part0, "Tool: %s", toolName);
    sprintf_s(part1, "Size: %d x %d", docWidth, docHeight);
    sprintf_s(part2, "W:%d  Op:%d%%  %s", penWidth, penOpacity, documentDirty ? "Modified" : "Saved");

    SendMessageA(hwndStatus, SB_SETTEXTA, 0, (LPARAM)part0);
    SendMessageA(hwndStatus, SB_SETTEXTA, 1, (LPARAM)part1);
    SendMessageA(hwndStatus, SB_SETTEXTA, 2, (LPARAM)part2);
    (void)hwnd;
}

static void MarkDirty(HWND hwnd) {
    documentDirty = true;
    UpdateWindowTitle(hwnd);
    UpdateStatusBar(hwnd);
}

static void MarkClean(HWND hwnd) {
    documentDirty = false;
    UpdateWindowTitle(hwnd);
    UpdateStatusBar(hwnd);
}

static void ApplyUiFont(HWND control) {
    if (gUiFont && control) {
        SendMessageA(control, WM_SETFONT, (WPARAM)gUiFont, TRUE);
    }
}

static void SetActiveTool(DrawTool tool) {
    currentTool = tool;
    const int ids[3] = { IDC_TOOL_PEN, IDC_TOOL_ERASER, IDC_TOOL_FILL };
    for (int i = 0; i < 3; ++i) {
        const bool selected = (static_cast<int>(tool) == i);
        SendMessageA(hwndToolButtons[i], BM_SETCHECK, selected ? BST_CHECKED : BST_UNCHECKED, 0);
        (void)ids;
    }
}

static void GetChromeMetrics(HWND hwnd, int& toolbarH, int& statusH) {
    toolbarH = TOOLBAR_HEIGHT;
    statusH = STATUS_HEIGHT;
    if (hwndStatus) {
        RECT sb = {};
        GetWindowRect(hwndStatus, &sb);
        statusH = sb.bottom - sb.top;
        if (statusH < 1) statusH = STATUS_HEIGHT;
    }
    (void)hwnd;
}

static void ConfigureCanvasGraphics(Graphics* g) {
    if (!g) return;
    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetCompositingMode(CompositingModeSourceOver);
}

static void DestroyStrokeLayer();
static void CommitStrokeLayer();
static void BeginStrokeLayer();
static void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1);
static void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY);
static void UpdateScrollBars();
static void SyncDocSizeFromBitmap();
static void InvalidateCanvas();
static void LayoutViewport(HWND hwnd);
static bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
static INT_PTR CALLBACK CanvasSizeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static int gCanvasDlgWidth = 0;
static int gCanvasDlgHeight = 0;

static void SyncDocSizeFromBitmap() {
    if (!canvasBitmap) return;
    docWidth = static_cast<int>(canvasBitmap->GetWidth());
    docHeight = static_cast<int>(canvasBitmap->GetHeight());
    if (docWidth < 1) docWidth = 1;
    if (docHeight < 1) docHeight = 1;
}

static void EnsureCanvas(HWND hwnd) {
    if (canvasBitmap) return;

    canvasBitmap = new Bitmap(docWidth, docHeight, PixelFormat32bppARGB);
    canvasGraphics = Graphics::FromImage(canvasBitmap);
    canvasGraphics->Clear(GdiplusFromColor(gTheme.canvasBg));
    ConfigureCanvasGraphics(canvasGraphics);
    (void)hwnd;
}

static void UpdateScrollBars() {
    if (!hwndViewport) return;

    RECT rc = {};
    GetClientRect(hwndViewport, &rc);
    const int viewW = rc.right - rc.left;
    const int viewH = rc.bottom - rc.top;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;

    si.nMin = 0;
    si.nMax = (docWidth > 1) ? (docWidth - 1) : 0;
    si.nPage = (viewW > 0) ? static_cast<UINT>(viewW) : 1;
    if (scrollX > docWidth - viewW) scrollX = (docWidth > viewW) ? (docWidth - viewW) : 0;
    if (scrollX < 0) scrollX = 0;
    si.nPos = scrollX;
    SetScrollInfo(hwndViewport, SB_HORZ, &si, TRUE);
    scrollX = GetScrollPos(hwndViewport, SB_HORZ);

    si.nMin = 0;
    si.nMax = (docHeight > 1) ? (docHeight - 1) : 0;
    si.nPage = (viewH > 0) ? static_cast<UINT>(viewH) : 1;
    if (scrollY > docHeight - viewH) scrollY = (docHeight > viewH) ? (docHeight - viewH) : 0;
    if (scrollY < 0) scrollY = 0;
    si.nPos = scrollY;
    SetScrollInfo(hwndViewport, SB_VERT, &si, TRUE);
    scrollY = GetScrollPos(hwndViewport, SB_VERT);
}

static void LayoutViewport(HWND hwnd) {
    if (!hwndViewport) return;

    int toolbarH = 0, statusH = 0;
    GetChromeMetrics(hwnd, toolbarH, statusH);

    RECT client = {};
    GetClientRect(hwnd, &client);
    const int x = 0;
    const int y = toolbarH;
    const int w = client.right - client.left;
    int h = client.bottom - client.top - toolbarH - statusH;
    if (h < 1) h = 1;

    MoveWindow(hwndViewport, x, y, w, h, TRUE);
    UpdateScrollBars();
}

static bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink) {
    if (newWidth < MIN_DOC_SIZE) newWidth = MIN_DOC_SIZE;
    if (newHeight < MIN_DOC_SIZE) newHeight = MIN_DOC_SIZE;
    if (newWidth > MAX_DOC_SIZE) newWidth = MAX_DOC_SIZE;
    if (newHeight > MAX_DOC_SIZE) newHeight = MAX_DOC_SIZE;

    EnsureCanvas(hwnd);

    if (newWidth == docWidth && newHeight == docHeight) {
        return true;
    }

    if (warnOnShrink && (newWidth < docWidth || newHeight < docHeight)) {
        const int result = MessageBoxA(
            hwnd,
            "Shrinking the canvas will crop content outside the new size. Continue?",
            "Canvas Size",
            MB_OKCANCEL | MB_ICONWARNING);
        if (result != IDOK) {
            return false;
        }
    }

    if (isDrawing) {
        isDrawing = false;
        CommitStrokeLayer();
    }
    DestroyStrokeLayer();

    if (pushHistory) {
        gHistory.Push(canvasBitmap);
    }

    Bitmap* newBitmap = new Bitmap(newWidth, newHeight, PixelFormat32bppARGB);
    Graphics* newGraphics = Graphics::FromImage(newBitmap);
    newGraphics->Clear(GdiplusFromColor(gTheme.canvasBg));
    ConfigureCanvasGraphics(newGraphics);

    if (canvasBitmap) {
        // Grow pads white from top-left; shrink crops from top-left.
        newGraphics->DrawImage(canvasBitmap, 0, 0);
    }

    delete canvasGraphics;
    delete canvasBitmap;
    canvasBitmap = newBitmap;
    canvasGraphics = newGraphics;

    docWidth = newWidth;
    docHeight = newHeight;
    scrollX = 0;
    scrollY = 0;
    UpdateScrollBars();
    MarkDirty(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    return true;
}

static void ClearCanvas(HWND hwnd, bool pushHistory) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    if (pushHistory) {
        gHistory.Push(canvasBitmap);
    }
    canvasGraphics->Clear(GdiplusFromColor(gTheme.canvasBg));
    MarkDirty(hwnd);
    InvalidateCanvas();
}

static bool ViewportToDocument(int localX, int localY, int& docX, int& docY) {
    docX = localX + scrollX;
    docY = localY + scrollY;
    if (docX < 0 || docY < 0 || docX >= docWidth || docY >= docHeight) {
        return false;
    }
    return true;
}

static void InvalidateCanvas() {
    if (hwndViewport) {
        InvalidateRect(hwndViewport, NULL, FALSE);
    }
}

static void EnableDarkTitleBar(HWND hwnd) {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
}

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void SyncPresetSelection(HWND hDlg) {
    HWND list = GetDlgItem(hDlg, IDC_CANVAS_PRESET);
    if (!list) return;

    char widthBuf[32] = {};
    char heightBuf[32] = {};
    GetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, widthBuf, sizeof(widthBuf));
    GetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, heightBuf, sizeof(heightBuf));
    const int w = atoi(widthBuf);
    const int h = atoi(heightBuf);

    int select = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])) - 1; // Custom
    for (int i = 0; i < select; ++i) {
        if (kPresets[i].width == w && kPresets[i].height == h) {
            select = i;
            break;
        }
    }
    SendMessageA(list, LB_SETCURSEL, select, 0);
}

static INT_PTR CALLBACK CanvasSizeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        char buf[32];
        sprintf_s(buf, "%d", docWidth);
        SetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, buf);
        sprintf_s(buf, "%d", docHeight);
        SetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, buf);

        HWND list = GetDlgItem(hDlg, IDC_CANVAS_PRESET);
        for (const CanvasPreset& preset : kPresets) {
            SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)preset.label);
        }
        SyncPresetSelection(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_CANVAS_PRESET && code == LBN_SELCHANGE) {
            HWND list = GetDlgItem(hDlg, IDC_CANVAS_PRESET);
            const int sel = static_cast<int>(SendMessageA(list, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])) &&
                kPresets[sel].width > 0) {
                char buf[32];
                sprintf_s(buf, "%d", kPresets[sel].width);
                SetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, buf);
                sprintf_s(buf, "%d", kPresets[sel].height);
                SetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, buf);
            }
            return TRUE;
        }

        if ((id == IDC_CANVAS_WIDTH || id == IDC_CANVAS_HEIGHT) && code == EN_CHANGE) {
            SyncPresetSelection(hDlg);
            return TRUE;
        }

        if (id == IDOK) {
            char widthBuf[32] = {};
            char heightBuf[32] = {};
            GetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, widthBuf, sizeof(widthBuf));
            GetDlgItemTextA(hDlg, IDC_CANVAS_HEIGHT, heightBuf, sizeof(heightBuf));
            int w = atoi(widthBuf);
            int h = atoi(heightBuf);
            if (w < MIN_DOC_SIZE || h < MIN_DOC_SIZE || w > MAX_DOC_SIZE || h > MAX_DOC_SIZE) {
                MessageBoxA(
                    hDlg,
                    "Enter width and height between 1 and 10000.",
                    "Canvas Size",
                    MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            gCanvasDlgWidth = w;
            gCanvasDlgHeight = h;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static void DoCanvasSize(HWND hwnd) {
    gCanvasDlgWidth = docWidth;
    gCanvasDlgHeight = docHeight;
    if (DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_CANVAS_SIZE), hwnd, CanvasSizeDlgProc) == IDOK) {
        ResizeDocument(hwnd, gCanvasDlgWidth, gCanvasDlgHeight, true, true);
    }
}

static bool PromptSaveIfDirty(HWND hwnd) {
    if (!documentDirty) return true;
    const int result = MessageBoxA(
        hwnd,
        "You have unsaved changes. Save before continuing?",
        "Simple Drawing App",
        MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) return false;
    if (result == IDNO) return true;

    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SAVE, 0), 0);
    return !documentDirty;
}

static void DoSave(HWND hwnd) {
    EnsureCanvas(hwnd);
    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "png";

    if (GetSaveFileNameA(&ofn)) {
        if (SaveCanvasToFile(canvasBitmap, filePath)) {
            MarkClean(hwnd);
        }
        else {
            MessageBoxA(hwnd, "Failed to save image.", "Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void DoOpen(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;

    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        DestroyStrokeLayer();
        isDrawing = false;
        if (LoadImageFromFile(filePath, canvasBitmap, canvasGraphics)) {
            ConfigureCanvasGraphics(canvasGraphics);
            SyncDocSizeFromBitmap();
            scrollX = 0;
            scrollY = 0;
            UpdateScrollBars();
            gHistory.Clear();
            MarkClean(hwnd);
            InvalidateCanvas();
            UpdateStatusBar(hwnd);
        }
        else {
            MessageBoxA(hwnd, "Failed to load image.", "Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void DoNew(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    // Keep current document size; clear content only (Paint-like New).
    canvasGraphics->Clear(GdiplusFromColor(gTheme.canvasBg));
    gHistory.Clear();
    MarkClean(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}

static void DoUndo(HWND hwnd) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    if (gHistory.Undo(canvasBitmap, canvasGraphics)) {
        ConfigureCanvasGraphics(canvasGraphics);
        SyncDocSizeFromBitmap();
        UpdateScrollBars();
        MarkDirty(hwnd);
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
    }
}

static void DoRedo(HWND hwnd) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    if (gHistory.Redo(canvasBitmap, canvasGraphics)) {
        ConfigureCanvasGraphics(canvasGraphics);
        SyncDocSizeFromBitmap();
        UpdateScrollBars();
        MarkDirty(hwnd);
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
    }
}

static void DestroyStrokeLayer() {
    delete strokeGraphics;
    delete strokeLayer;
    strokeGraphics = nullptr;
    strokeLayer = nullptr;
}

static void BeginStrokeLayer() {
    DestroyStrokeLayer();
    if (!canvasBitmap) return;

    const int width = static_cast<int>(canvasBitmap->GetWidth());
    const int height = static_cast<int>(canvasBitmap->GetHeight());
    strokeLayer = new Bitmap(width, height, PixelFormat32bppARGB);
    strokeGraphics = Graphics::FromImage(strokeLayer);
    strokeGraphics->Clear(Color(0, 0, 0, 0));
    strokeGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
    strokeGraphics->SetCompositingMode(CompositingModeSourceOver);
}

static void DrawStrokeOnto(Graphics* target, int x0, int y0, int x1, int y1) {
    if (!target) return;

    // Draw fully opaque ink onto the stroke layer; opacity is applied once when compositing.
    COLORREF strokeColor = (currentTool == DrawTool::Eraser) ? gTheme.canvasBg : penColor;
    Pen pen(GdiplusFromColor(strokeColor, 255), static_cast<REAL>(penWidth));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    target->DrawLine(&pen, x0, y0, x1, y1);
}

static void DrawStrokeLayerWithOpacity(Graphics* dest, int destX, int destY) {
    if (!strokeLayer || !dest) return;

    const REAL alpha = static_cast<REAL>(OpacityToAlpha()) / 255.0f;
    ColorMatrix matrix = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alpha, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };

    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    const int width = static_cast<int>(strokeLayer->GetWidth());
    const int height = static_cast<int>(strokeLayer->GetHeight());
    dest->DrawImage(
        strokeLayer,
        Rect(destX, destY, width, height),
        0, 0, width, height,
        UnitPixel,
        &attrs);
}

static void CommitStrokeLayer() {
    if (!strokeLayer || !canvasGraphics) {
        DestroyStrokeLayer();
        return;
    }
    DrawStrokeLayerWithOpacity(canvasGraphics, 0, 0);
    DestroyStrokeLayer();
}

static int ScrollByMessage(HWND hwnd, int bar, WPARAM wParam, int current, int maxScroll) {
    int pos = current;
    // SB_LINELEFT/SB_LINEUP (and similar H/V pairs) share the same numeric values.
    switch (LOWORD(wParam)) {
    case SB_LINELEFT: // also SB_LINEUP
        pos -= 16;
        break;
    case SB_LINERIGHT: // also SB_LINEDOWN
        pos += 16;
        break;
    case SB_PAGELEFT: { // also SB_PAGEUP
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_PAGE;
        GetScrollInfo(hwnd, bar, &si);
        pos -= static_cast<int>(si.nPage);
        break;
    }
    case SB_PAGERIGHT: { // also SB_PAGEDOWN
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_PAGE;
        GetScrollInfo(hwnd, bar, &si);
        pos += static_cast<int>(si.nPage);
        break;
    }
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(hwnd, bar, &si);
        pos = si.nTrackPos;
        break;
    }
    case SB_TOP: // also SB_LEFT
        pos = 0;
        break;
    case SB_BOTTOM: // also SB_RIGHT
        pos = maxScroll;
        break;
    default:
        break;
    }
    if (pos < 0) pos = 0;
    if (pos > maxScroll) pos = maxScroll;
    return pos;
}

static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        UpdateScrollBars();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_HSCROLL: {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int maxScroll = (docWidth > rc.right) ? (docWidth - rc.right) : 0;
        const int newPos = ScrollByMessage(hwnd, SB_HORZ, wParam, scrollX, maxScroll);
        if (newPos != scrollX) {
            scrollX = newPos;
            SetScrollPos(hwnd, SB_HORZ, scrollX, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_VSCROLL: {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int maxScroll = (docHeight > rc.bottom) ? (docHeight - rc.bottom) : 0;
        const int newPos = ScrollByMessage(hwnd, SB_VERT, wParam, scrollY, maxScroll);
        if (newPos != scrollY) {
            scrollY = newPos;
            SetScrollPos(hwnd, SB_VERT, scrollY, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        HWND parent = GetParent(hwnd);
        if (parent) {
            return SendMessageA(parent, WM_MOUSEWHEEL, wParam, lParam);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        HWND parent = GetParent(hwnd);
        if (!parent) break;
        EnsureCanvas(parent);

        int docX = 0, docY = 0;
        if (!ViewportToDocument(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), docX, docY)) {
            break;
        }

        if (currentTool == DrawTool::Fill) {
            gHistory.Push(canvasBitmap);
            if (FloodFillCanvas(canvasBitmap, docX, docY, penColor, OpacityToAlpha())) {
                MarkDirty(parent);
                InvalidateCanvas();
            }
            break;
        }

        gHistory.Push(canvasBitmap);
        BeginStrokeLayer();
        isDrawing = true;
        lastPoint.x = docX;
        lastPoint.y = docY;
        DrawStrokeOnto(strokeGraphics, docX, docY, docX, docY);
        SetCapture(hwnd);
        MarkDirty(parent);
        InvalidateCanvas();
        break;
    }
    case WM_MOUSEMOVE: {
        if (isDrawing && strokeGraphics &&
            (currentTool == DrawTool::Pen || currentTool == DrawTool::Eraser)) {
            int docX = 0, docY = 0;
            if (ViewportToDocument(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), docX, docY)) {
                DrawStrokeOnto(strokeGraphics, lastPoint.x, lastPoint.y, docX, docY);
                lastPoint.x = docX;
                lastPoint.y = docY;
                InvalidateCanvas();
            }
        }
        break;
    }
    case WM_LBUTTONUP:
        if (isDrawing) {
            isDrawing = false;
            CommitStrokeLayer();
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            InvalidateCanvas();
            if (HWND parent = GetParent(hwnd)) {
                UpdateStatusBar(parent);
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (isDrawing) {
            isDrawing = false;
            CommitStrokeLayer();
            InvalidateCanvas();
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client = {};
        GetClientRect(hwnd, &client);
        const int viewW = client.right - client.left;
        const int viewH = client.bottom - client.top;

        HWND parent = GetParent(hwnd);
        if (parent) {
            EnsureCanvas(parent);
        }

        if (viewW > 0 && viewH > 0) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, viewW, viewH);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            {
                Graphics g(memDC);
                g.Clear(Color(255, GetRValue(WORKSPACE_COLOR), GetGValue(WORKSPACE_COLOR), GetBValue(WORKSPACE_COLOR)));
                g.SetCompositingMode(CompositingModeSourceOver);
                if (canvasBitmap) {
                    g.DrawImage(canvasBitmap, -scrollX, -scrollY);
                }
                if (strokeLayer) {
                    DrawStrokeLayerWithOpacity(&g, -scrollX, -scrollY);
                }
            }

            BitBlt(hdc, 0, 0, viewW, viewH, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK TrackbarWheelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOUSEWHEEL) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            return SendMessageA(parent, WM_MOUSEWHEEL, wParam, lParam);
        }
    }
    return CallWindowProcA(gOldTrackbarProc, hwnd, msg, wParam, lParam);
}

static void SubclassTrackbarWheel(HWND trackbar) {
    if (!trackbar) return;
    WNDPROC prev = (WNDPROC)SetWindowLongPtrA(trackbar, GWLP_WNDPROC, (LONG_PTR)TrackbarWheelProc);
    if (!gOldTrackbarProc) {
        gOldTrackbarProc = prev;
    }
}

static void LayoutStatusParts(HWND hwnd) {
    if (!hwndStatus) return;
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    int parts[4] = { width / 3, (width * 2) / 3, width - 1, -1 };
    SendMessageA(hwndStatus, SB_SETPARTS, 3, (LPARAM)parts);
}

static HWND CreateToolbarButton(HWND parent, int id, const char* label, int x, int y, int w, int h, bool pushLike) {
    const DWORD style = WS_TABSTOP | WS_VISIBLE | WS_CHILD |
        (pushLike ? (BS_PUSHLIKE | BS_AUTOCHECKBOX) : BS_PUSHBUTTON);
    HWND btn = CreateWindowA("BUTTON", label, style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    ApplyUiFont(btn);
    return btn;
}

static void CreateToolbar(HWND hwnd) {
    const int h = 28;
    const int toolW = 64;
    const int y1 = 8;
    const int y2 = 50;

    int x = 12;
    hwndToolButtons[0] = CreateToolbarButton(hwnd, IDC_TOOL_PEN, "Pen", x, y1, toolW, h, true); x += toolW + 6;
    hwndToolButtons[1] = CreateToolbarButton(hwnd, IDC_TOOL_ERASER, "Eraser", x, y1, toolW + 8, h, true); x += toolW + 14;
    hwndToolButtons[2] = CreateToolbarButton(hwnd, IDC_TOOL_FILL, "Fill", x, y1, toolW, h, true); x += toolW + 16;

    for (int i = 0; i < 8; ++i) {
        hwndSwatches[i] = CreateWindowA("BUTTON", "",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            x, y1 + 3, 22, 22,
            hwnd, (HMENU)(INT_PTR)(IDC_SWATCH0 + i), GetModuleHandle(NULL), NULL);
        x += 26;
    }
    x += 8;

    hwndActionButtons[0] = CreateToolbarButton(hwnd, IDC_COLOR_BUTTON, "Color...", x, y1, 72, h, false);
    x += 84;
    hwndActionButtons[1] = CreateToolbarButton(hwnd, IDC_NEW_BUTTON, "New", x, y1, 52, h, false); x += 58;
    hwndActionButtons[2] = CreateToolbarButton(hwnd, IDC_UNDO_BUTTON, "Undo", x, y1, 52, h, false); x += 58;
    hwndActionButtons[3] = CreateToolbarButton(hwnd, IDC_REDO_BUTTON, "Redo", x, y1, 52, h, false); x += 58;
    hwndActionButtons[4] = CreateToolbarButton(hwnd, IDC_CLEAR_BUTTON, "Clear", x, y1, 52, h, false); x += 58;
    hwndActionButtons[5] = CreateToolbarButton(hwnd, IDC_SAVE_BUTTON, "Save", x, y1, 52, h, false); x += 58;
    hwndActionButtons[6] = CreateToolbarButton(hwnd, IDC_LOAD_BUTTON, "Open", x, y1, 52, h, false);

    // Row 2: size + opacity
    x = 12;
    HWND sizeLabel = CreateWindowA("STATIC", "Size", WS_CHILD | WS_VISIBLE,
        x, y2 + 4, 34, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(sizeLabel);
    x += 38;

    hwndSlider = CreateWindowExA(0, TRACKBAR_CLASSA, NULL,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        x, y2 - 2, 150, 32,
        hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
    SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);
    SubclassTrackbarWheel(hwndSlider);
    x += 158;

    hwndPenWidthBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        x, y2 + 2, 40, 24,
        hwnd, (HMENU)(INT_PTR)IDC_WIDTH_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndPenWidthBox);
    x += 56;

    HWND opacityLabel = CreateWindowA("STATIC", "Opacity", WS_CHILD | WS_VISIBLE,
        x, y2 + 4, 54, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
    ApplyUiFont(opacityLabel);
    x += 58;

    hwndOpacitySlider = CreateWindowExA(0, TRACKBAR_CLASSA, NULL,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        x, y2 - 2, 150, 32,
        hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hwndOpacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hwndOpacitySlider, TBM_SETPOS, TRUE, penOpacity);
    SubclassTrackbarWheel(hwndOpacitySlider);
    x += 158;

    hwndOpacityBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        x, y2 + 2, 44, 24,
        hwnd, (HMENU)(INT_PTR)IDC_OPACITY_EDIT, GetModuleHandle(NULL), NULL);
    ApplyUiFont(hwndOpacityBox);

    SetActiveTool(DrawTool::Pen);
    UpdatePenWidthDisplay();
    UpdateOpacityDisplay();
}

static void DrawToolbarBackground(HDC hdc, const RECT& client) {
    RECT toolbar = client;
    toolbar.bottom = TOOLBAR_HEIGHT;
    FillRect(hdc, &toolbar, gChromeBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, gTheme.chromeLine);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, 0, TOOLBAR_HEIGHT - 1, NULL);
    LineTo(hdc, client.right, TOOLBAR_HEIGHT - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icex);

        gUiFont = CreateFontA(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        gChromeBrush = CreateSolidBrush(gTheme.chromeBg);

        CreateToolbar(hwnd);

        hwndStatus = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "", hwnd, 1);
        ApplyUiFont(hwndStatus);
        LayoutStatusParts(hwnd);

        hwndViewport = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            VIEWPORT_CLASS_NAME,
            "",
            WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPCHILDREN,
            0, TOOLBAR_HEIGHT, 100, 100,
            hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL);

        EnsureCanvas(hwnd);
        LayoutViewport(hwnd);
        UpdateStatusBar(hwnd);
        UpdateWindowTitle(hwnd);
        EnableDarkTitleBar(hwnd);
        break;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, gTheme.chromeBg);
        SetTextColor(hdc, gTheme.text);
        return (LRESULT)gChromeBrush;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID >= IDC_SWATCH0 && dis->CtlID <= IDC_SWATCH7) {
            const int index = dis->CtlID - IDC_SWATCH0;
            HBRUSH fill = CreateSolidBrush(kSwatches[index]);
            FillRect(dis->hDC, &dis->rcItem, fill);
            DeleteObject(fill);

            const bool selected = (penColor == kSwatches[index]);
            HPEN border = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? gTheme.accent : RGB(90, 90, 90));
            HGDIOBJ oldPen = SelectObject(dis->hDC, border);
            HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldBrush);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(border);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT client = {};
        GetClientRect(hwnd, &client);
        DrawToolbarBackground(hdc, client);
        return 1;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            SendMessageA(hwndStatus, WM_SIZE, 0, 0);
            LayoutStatusParts(hwnd);
            LayoutViewport(hwnd);
            UpdateStatusBar(hwnd);
        }
        break;
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

        if (cmdId >= IDC_SWATCH0 && cmdId <= IDC_SWATCH7) {
            penColor = kSwatches[cmdId - IDC_SWATCH0];
            for (HWND sw : hwndSwatches) {
                InvalidateRect(sw, NULL, TRUE);
            }
            break;
        }

        switch (cmdId) {
        case IDC_TOOL_PEN:
            SetActiveTool(DrawTool::Pen);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_ERASER:
            SetActiveTool(DrawTool::Eraser);
            UpdateStatusBar(hwnd);
            break;
        case IDC_TOOL_FILL:
            SetActiveTool(DrawTool::Fill);
            UpdateStatusBar(hwnd);
            break;
        case IDC_COLOR_BUTTON: {
            COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
            penColor = newColor;
            for (HWND sw : hwndSwatches) {
                InvalidateRect(sw, NULL, TRUE);
            }
            break;
        }
        case IDC_NEW_BUTTON:
        case IDM_NEW:
            DoNew(hwnd);
            break;
        case IDC_CLEAR_BUTTON:
        case IDM_CLEAR:
            ClearCanvas(hwnd, true);
            UpdateStatusBar(hwnd);
            break;
        case IDC_UNDO_BUTTON:
        case IDM_UNDO:
            DoUndo(hwnd);
            break;
        case IDC_REDO_BUTTON:
        case IDM_REDO:
            DoRedo(hwnd);
            break;
        case IDC_SAVE_BUTTON:
        case IDM_SAVE:
            DoSave(hwnd);
            break;
        case IDC_LOAD_BUTTON:
        case IDM_OPEN:
            DoOpen(hwnd);
            break;
        case IDM_CANVAS_SIZE:
            DoCanvasSize(hwnd);
            break;
        case IDM_ABOUT:
            DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABOUTBOX), hwnd, AboutDlgProc);
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
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0) {
                AdjustOpacity(hwnd, steps * 5);
            }
            else {
                AdjustPenWidth(hwnd, steps);
            }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_OEM_4) { // [
            AdjustPenWidth(hwnd, -1);
        }
        else if (wParam == VK_OEM_6) { // ]
            AdjustPenWidth(hwnd, 1);
        }
        break;
    }
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
        DrawToolbarBackground(hdc, client);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        if (GetCapture() == hwnd || (hwndViewport && GetCapture() == hwndViewport)) {
            ReleaseCapture();
        }
        DestroyStrokeLayer();
        gHistory.Clear();
        delete canvasGraphics;
        delete canvasBitmap;
        canvasGraphics = nullptr;
        canvasBitmap = nullptr;
        hwndViewport = nullptr;
        if (gUiFont) {
            DeleteObject(gUiFont);
            gUiFont = nullptr;
        }
        if (gChromeBrush) {
            DeleteObject(gChromeBrush);
            gChromeBrush = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static bool RegisterViewportClass(HINSTANCE hInstance) {
    WNDCLASSA vc = {};
    vc.lpfnWndProc = ViewportProc;
    vc.hInstance = hInstance;
    vc.lpszClassName = VIEWPORT_CLASS_NAME;
    vc.hCursor = LoadCursor(NULL, IDC_ARROW);
    vc.hbrBackground = NULL;
    vc.style = CS_DBLCLKS;
    return RegisterClassA(&vc) != 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    if (!RegisterViewportClass(hInstance)) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // painted manually
    wc.lpszMenuName = MAKEINTRESOURCEA(IDC_SIMPLEDRAWINGAPP);
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_SIMPLEDRAWINGAPP));
    wc.style = CS_DBLCLKS;

    if (!RegisterClassA(&wc)) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Simple Drawing App",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    gAccel = LoadAcceleratorsA(hInstance, MAKEINTRESOURCEA(IDC_SIMPLEDRAWINGAPP));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAcceleratorA(hwnd, gAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
