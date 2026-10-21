#include "AppDocument.h"
#include "AppState.h"
#include "AppSelection.h"
#include "AppFeatureFlags.h"
#include "SimpleDrawingApp.h"
#include "FileManager.h"
#include "AtelierRaii.h"
#include "Resource.h"
#include "AppPaths.h"
#include "AppSession.h"
#include <commdlg.h>
#include <cstdio>
#include <cstring>

using namespace Gdiplus;

namespace {
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

bool WriteDocumentToPath(HWND hwnd, const wchar_t* path) {
    ClearSelection(true);
    Bitmap* flat = GetCompositeBitmap();
    if (flat && SaveCanvasToFile(flat, path)) {
        SetDocumentPath(path);
        MarkClean(hwnd);
        ClearAutosaveRecovery();
        return true;
    }
    MessageBoxW(hwnd, L"Failed to save image.", L"Error", MB_OK | MB_ICONERROR);
    return false;
}

bool OpenDocumentFromPath(HWND hwnd, const wchar_t* filePath) {
    if (!filePath || !filePath[0]) return false;

    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);

    Atelier::GdiplusBitmapPtr loaded = LoadImageFromFile(filePath);
    if (!loaded) {
        return false;
    }
    if (!gLayers.ReplaceWithImage(loaded.get())) {
        return false;
    }
    SyncDocSizeFromBitmap();
    scrollX = 0;
    scrollY = 0;
    InvalidateComposite();
    UpdateScrollBars();
    gHistory.Clear();
    SetDocumentPath(filePath);
    MarkClean(hwnd);
    RefreshLayerList();
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    return true;
}
}

bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink) {
    if (newWidth < MIN_DOC_SIZE) newWidth = MIN_DOC_SIZE;
    if (newHeight < MIN_DOC_SIZE) newHeight = MIN_DOC_SIZE;
    if (newWidth > MAX_DOC_SIZE) newWidth = MAX_DOC_SIZE;
    if (newHeight > MAX_DOC_SIZE) newHeight = MAX_DOC_SIZE;

    EnsureCanvas(hwnd);

    if (newWidth == DocWidth() && newHeight == DocHeight()) {
        return true;
    }

    if (warnOnShrink && IsFeatureEnabled(AppFeature::WarnCanvasShrink)
        && (newWidth < DocWidth() || newHeight < DocHeight())) {
        const int result = MessageBoxW(hwnd, L"Shrinking the canvas will crop content outside the new size. Continue?", L"Canvas Size", MB_OKCANCEL | MB_ICONWARNING);
        if (result != IDOK) {
            return false;
        }
    }

    if (isDrawing) {
        isDrawing = false;
        CommitStrokeLayer();
    }
    DestroyStrokeLayer();
    ClearSelection(false);

    std::unique_ptr<LayerStack> before;
    if (pushHistory) {
        before = LayerHistory::Capture(gLayers);
    }

    if (!gLayers.Resize(newWidth, newHeight, gTheme.canvasBg)) {
        MessageBoxW(hwnd, L"Could not resize the canvas (out of memory).", L"Canvas Size", MB_OK | MB_ICONERROR);
        return false;
    }
    if (before) {
        gHistory.Commit(std::move(before));
    }
    docWidth = newWidth;
    docHeight = newHeight;
    scrollX = 0;
    scrollY = 0;
    InvalidateComposite();
    UpdateScrollBars();
    MarkDirty(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
    RefreshLayerList();
    return true;
}

void ClearCanvas(HWND hwnd, bool pushHistory) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    if (pushHistory) {
        gHistory.Push(gLayers);
    }
    gLayers.ClearAllContent(gTheme.canvasBg);
    InvalidateComposite();
    MarkDirty(hwnd);
    InvalidateCanvas();
}

void FlattenLayers(HWND hwnd) {
    EnsureCanvas(hwnd);
    if (gLayers.Count() < 2) return;

    if (isDrawing) {
        isDrawing = false;
        CommitStrokeLayer();
    }
    DestroyStrokeLayer();
    ClearSelection(false);

    auto before = LayerHistory::Capture(gLayers);
    if (!gLayers.FlattenVisible()) {
        return;
    }
    gHistory.Commit(std::move(before));

    InvalidateComposite();
    MarkDirty(hwnd);
    RefreshLayerList();
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}

namespace {

void BeginImageGeometryEdit(HWND hwnd) {
    EnsureCanvas(hwnd);
    if (isDrawing) {
        isDrawing = false;
        CommitStrokeLayer();
    }
    DestroyStrokeLayer();
    ClearSelection(false);
}

void EndImageGeometryEdit(HWND hwnd, bool sizeChanged) {
    if (sizeChanged) {
        docWidth = gLayers.Width();
        docHeight = gLayers.Height();
        scrollX = 0;
        scrollY = 0;
        UpdateScrollBars();
    }
    InvalidateComposite();
    MarkDirty(hwnd);
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}

bool CommitGeometryIf(HWND hwnd, bool ok, std::unique_ptr<LayerStack> before, bool sizeChanged) {
    if (!ok) return false;
    gHistory.Commit(std::move(before));
    EndImageGeometryEdit(hwnd, sizeChanged);
    return true;
}

} // namespace

void FlipDocumentHorizontal(HWND hwnd) {
    BeginImageGeometryEdit(hwnd);
    auto before = LayerHistory::Capture(gLayers);
    CommitGeometryIf(hwnd, gLayers.FlipHorizontal(), std::move(before), false);
}

void FlipDocumentVertical(HWND hwnd) {
    BeginImageGeometryEdit(hwnd);
    auto before = LayerHistory::Capture(gLayers);
    CommitGeometryIf(hwnd, gLayers.FlipVertical(), std::move(before), false);
}

void RotateDocument90Cw(HWND hwnd) {
    BeginImageGeometryEdit(hwnd);
    auto before = LayerHistory::Capture(gLayers);
    CommitGeometryIf(hwnd, gLayers.Rotate90Clockwise(), std::move(before), true);
}

void RotateDocument90Ccw(HWND hwnd) {
    BeginImageGeometryEdit(hwnd);
    auto before = LayerHistory::Capture(gLayers);
    CommitGeometryIf(hwnd, gLayers.Rotate90CounterClockwise(), std::move(before), true);
}

void SyncPresetSelection(HWND hDlg) {
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

INT_PTR CALLBACK CanvasSizeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        char buf[32];
        sprintf_s(buf, "%d", DocWidth());
        SetDlgItemTextA(hDlg, IDC_CANVAS_WIDTH, buf);
        sprintf_s(buf, "%d", DocHeight());
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
                MessageBoxW(hDlg, L"Enter width and height between 1 and 10000.", L"Canvas Size", MB_OK | MB_ICONWARNING);
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

void ResizeCanvas(HWND hwnd) {
    gCanvasDlgWidth = DocWidth();
    gCanvasDlgHeight = DocHeight();
    if (DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_CANVAS_SIZE), hwnd, CanvasSizeDlgProc) == IDOK) {
        ResizeDocument(hwnd, gCanvasDlgWidth, gCanvasDlgHeight, true, true);
    }
}

bool PromptSaveIfDirty(HWND hwnd) {
    if (!documentDirty) return true;
    const int result = MessageBoxW(hwnd, L"You have unsaved changes. Save before continuing?", L"Simple Drawing App", MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) return false;
    if (result == IDNO) return true;

    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SAVE, 0), 0);
    return !documentDirty;
}

void SaveDocument(HWND hwnd) {
    EnsureCanvas(hwnd);

    if (gDocumentPath[0]) {
        WriteDocumentToPath(hwnd, gDocumentPath);
        return;
    }

    SaveDocumentAs(hwnd);
}

void SaveDocumentAs(HWND hwnd) {
    EnsureCanvas(hwnd);

    wchar_t filePath[MAX_PATH] = L"";
    if (gDocumentPath[0]) {
        wcsncpy_s(filePath, gDocumentPath, _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";
    if (gLastBrowseDir[0]) {
        ofn.lpstrInitialDir = gLastBrowseDir;
    }

    if (GetSaveFileNameW(&ofn)) {
        WriteDocumentToPath(hwnd, filePath);
    }
}

void ExportDocument(HWND hwnd) {
    EnsureCanvas(hwnd);

    wchar_t filePath[MAX_PATH] = L"";
    if (gDocumentPath[0]) {
        // Suggest "name-export.png" beside the current document.
        wcsncpy_s(filePath, gDocumentPath, _TRUNCATE);
        wchar_t* dot = wcsrchr(filePath, L'.');
        wchar_t* slash = wcsrchr(filePath, L'\\');
        if (!slash) slash = wcsrchr(filePath, L'/');
        if (dot && (!slash || dot > slash)) {
            *dot = L'\0';
            wchar_t base[MAX_PATH];
            wcsncpy_s(base, filePath, _TRUNCATE);
            swprintf_s(filePath, L"%s-export.png", base);
        } else {
            wcscat_s(filePath, L"-export.png");
        }
    } else {
        wcsncpy_s(filePath, L"export.png", _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";
    ofn.lpstrTitle = L"Export As";
    if (gLastBrowseDir[0]) {
        ofn.lpstrInitialDir = gLastBrowseDir;
    }

    if (!GetSaveFileNameW(&ofn)) return;

    // Flatten layers only — do not change the active document path or dirty flag.
    Bitmap* flat = GetCompositeBitmap();
    if (flat && SaveCanvasToFile(flat, filePath)) {
        RememberBrowseDirFromPath(filePath);
        return;
    }
    MessageBoxW(hwnd, L"Failed to export image.", L"Error", MB_OK | MB_ICONERROR);
}

void PrintDocument(HWND hwnd) {
    EnsureCanvas(hwnd);

    Bitmap* flat = GetCompositeBitmap();
    if (!flat) {
        MessageBoxW(hwnd, L"Nothing to print.", L"Print", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PRINTDLGA pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hwnd;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS;
    if (!PrintDlgA(&pd)) {
        return; // cancelled or no printers — no allocations to free
    }

    // PrintDlg may allocate these even when hDC is null; always free on exit.
    HDC hdc = pd.hDC;
    bool ok = false;
    if (!hdc) {
        MessageBoxW(hwnd, L"Failed to open printer.", L"Print", MB_OK | MB_ICONERROR);
    } else {
        DOCINFOA di = {};
        di.cbSize = sizeof(di);
        di.lpszDocName = "Simple Drawing App";

        if (StartDocA(hdc, &di) > 0) {
            if (StartPage(hdc) > 0) {
                const int pageW = GetDeviceCaps(hdc, HORZRES);
                const int pageH = GetDeviceCaps(hdc, VERTRES);
                const int imgW = flat->GetWidth();
                const int imgH = flat->GetHeight();

                // Fit image on page, preserve aspect ratio, center.
                float scale = 1.0f;
                if (imgW > 0 && imgH > 0 && pageW > 0 && pageH > 0) {
                    const float sx = static_cast<float>(pageW) / static_cast<float>(imgW);
                    const float sy = static_cast<float>(pageH) / static_cast<float>(imgH);
                    scale = (sx < sy) ? sx : sy;
                }
                const int destW = static_cast<int>(imgW * scale);
                const int destH = static_cast<int>(imgH * scale);
                const int destX = (pageW - destW) / 2;
                const int destY = (pageH - destH) / 2;

                Graphics printer(hdc);
                printer.SetPageUnit(UnitPixel);
                printer.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                printer.DrawImage(flat, destX, destY, destW, destH);
                EndPage(hdc);
                ok = true;
            }
            EndDoc(hdc);
        }
        DeleteDC(hdc);
    }

    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);

    if (!ok && hdc) {
        MessageBoxW(hwnd, L"Print job failed to start.", L"Print", MB_OK | MB_ICONERROR);
    }
}

void AutosaveIfNeeded(HWND hwnd) {
    if (!IsFeatureEnabled(AppFeature::AutosaveRecovery)) return;
    if (!documentDirty) return;
    EnsureCanvas(hwnd);

    wchar_t path[MAX_PATH] = {};
    GetAutosavePath(path, MAX_PATH);
    if (!path[0]) return;

    Bitmap* flat = GetCompositeBitmap();
    if (!flat) return;
    SaveCanvasToFile(flat, path);
}

bool OfferAutosaveRecovery(HWND hwnd) {
    if (!IsFeatureEnabled(AppFeature::AutosaveRecovery)) return false;

    wchar_t path[MAX_PATH] = {};
    GetAutosavePath(path, MAX_PATH);
    if (!path[0] || !PathIsExistingFile(path)) return false;

    const int choice = MessageBoxW(hwnd, L"A recovered drawing was found from the last session.\n\nRestore it?", L"Autosave Recovery", MB_YESNO | MB_ICONQUESTION);
    if (choice != IDYES) {
        ClearAutosaveRecovery();
        return false;
    }

    if (!OpenDocumentFromPath(hwnd, path)) {
        MessageBoxW(hwnd, L"Failed to restore the autosave.", L"Error", MB_OK | MB_ICONERROR);
        ClearAutosaveRecovery();
        return false;
    }

    // Treat as untitled dirty work — do not keep autosave.png as the document path.
    RemoveRecentPath(path);
    SaveSessionState();
    SetDocumentPath(nullptr);
    MarkDirty(hwnd);
    return true;
}

void OpenDocument(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;

    wchar_t filePath[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (gLastBrowseDir[0]) {
        ofn.lpstrInitialDir = gLastBrowseDir;
    }

    if (GetOpenFileNameW(&ofn)) {
        if (!OpenDocumentFromPath(hwnd, filePath)) {
            MessageBoxW(hwnd, L"Failed to load image.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void OpenLastDocument(HWND hwnd) {
    PruneMissingRecentPaths();
    if (!LastDocumentAvailable()) {
        SaveSessionState();
        return;
    }
    if (!PromptSaveIfDirty(hwnd)) return;
    wchar_t path[MAX_PATH];
    wcsncpy_s(path, gLastDocumentPath, _TRUNCATE);
    if (!OpenDocumentFromPath(hwnd, path)) {
        MessageBoxW(hwnd, L"Failed to open the last document.", L"Error", MB_OK | MB_ICONERROR);
        RemoveRecentPath(path);
        SaveSessionState();
    }
}

void OpenRecentDocument(HWND hwnd, int index) {
    PruneMissingRecentPaths();
    if (index < 0 || index >= gRecentDocumentCount) return;
    wchar_t path[MAX_PATH];
    wcsncpy_s(path, gRecentDocuments[index], _TRUNCATE);
    if (!PathIsExistingFile(path)) {
        RemoveRecentPath(path);
        SaveSessionState();
        return;
    }
    if (!PromptSaveIfDirty(hwnd)) return;
    if (!OpenDocumentFromPath(hwnd, path)) {
        MessageBoxW(hwnd, L"Failed to open the document.", L"Error", MB_OK | MB_ICONERROR);
        RemoveRecentPath(path);
        SaveSessionState();
    }
}

void NewDocument(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    // Keep current document size; reset to Background + Layer 1 (active).
    gLayers.Reset(DocWidth(), DocHeight(), gTheme.canvasBg);
    gHistory.Clear();
    SetDocumentPath(nullptr);
    InvalidateComposite();
    MarkClean(hwnd);
    ClearAutosaveRecovery();
    RefreshLayerList();
    InvalidateCanvas();
    UpdateStatusBar(hwnd);
}

void UndoDocument(HWND hwnd) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    if (gHistory.Undo(gLayers)) {
        SyncDocSizeFromBitmap();
        InvalidateComposite();
        UpdateScrollBars();
        MarkDirty(hwnd);
        RefreshLayerList();
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
    }
}

void RedoDocument(HWND hwnd) {
    EnsureCanvas(hwnd);
    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);
    if (gHistory.Redo(gLayers)) {
        SyncDocSizeFromBitmap();
        InvalidateComposite();
        UpdateScrollBars();
        MarkDirty(hwnd);
        RefreshLayerList();
        InvalidateCanvas();
        UpdateStatusBar(hwnd);
    }
}
