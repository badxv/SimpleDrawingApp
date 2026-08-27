#include "AppDocument.h"
#include "AppState.h"
#include "AppSelection.h"
#include "AppFeatureFlags.h"
#include "SimpleDrawingApp.h"
#include "FileManager.h"
#include "Resource.h"
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

void RememberBrowseDirFromPath(const char* path) {
    if (!path || !path[0]) return;
    char dir[MAX_PATH];
    sprintf_s(dir, "%s", path);
    char* slash = strrchr(dir, '\\');
    if (!slash) slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';
    sprintf_s(gLastBrowseDir, "%s", dir);
}

void GetSessionIniPath(char* path, size_t pathChars) {
    if (!path || pathChars == 0) return;
    path[0] = '\0';
    DWORD len = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(pathChars));
    if (len == 0 || len >= pathChars) {
        path[0] = '\0';
        return;
    }
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') {
            path[i + 1] = '\0';
            break;
        }
    }
    const size_t used = strlen(path);
    const char suffix[] = "session.ini";
    if (used + sizeof(suffix) > pathChars) {
        path[0] = '\0';
        return;
    }
    strcat_s(path, pathChars, suffix);
}

void SaveSessionState() {
    char iniPath[MAX_PATH] = {};
    GetSessionIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    WritePrivateProfileStringA("Session", "LastDocument",
        gLastDocumentPath[0] ? gLastDocumentPath : "", iniPath);
}

void SetDocumentPath(const char* path) {
    if (path && path[0]) {
        sprintf_s(gDocumentPath, "%s", path);
        sprintf_s(gLastDocumentPath, "%s", path);
        RememberBrowseDirFromPath(path);
        SaveSessionState();
    } else {
        gDocumentPath[0] = '\0';
    }
}

bool WriteDocumentToPath(HWND hwnd, const char* path) {
    ClearSelection(true);
    Bitmap* flat = GetCompositeBitmap();
    if (flat && SaveCanvasToFile(flat, path)) {
        SetDocumentPath(path);
        MarkClean(hwnd);
        return true;
    }
    MessageBoxA(hwnd, "Failed to save image.", "Error", MB_OK | MB_ICONERROR);
    return false;
}

bool OpenDocumentFromPath(HWND hwnd, const char* filePath) {
    if (!filePath || !filePath[0]) return false;

    DestroyStrokeLayer();
    isDrawing = false;
    ClearSelection(false);

    Bitmap* loaded = nullptr;
    Graphics* loadedG = nullptr;
    if (!LoadImageFromFile(filePath, loaded, loadedG)) {
        return false;
    }
    delete loadedG;
    if (!gLayers.ReplaceWithImage(loaded)) {
        delete loaded;
        return false;
    }
    delete loaded;
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

    if (newWidth == docWidth && newHeight == docHeight) {
        return true;
    }

    if (warnOnShrink && IsFeatureEnabled(AppFeature::WarnCanvasShrink)
        && (newWidth < docWidth || newHeight < docHeight)) {
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
    ClearSelection(false);

    if (pushHistory) {
        gHistory.Push(gLayers);
    }

    if (!gLayers.Resize(newWidth, newHeight, gTheme.canvasBg)) {
        MessageBoxA(hwnd, "Could not resize the canvas (out of memory).", "Canvas Size", MB_OK | MB_ICONERROR);
        return false;
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

void ResizeCanvas(HWND hwnd) {
    gCanvasDlgWidth = docWidth;
    gCanvasDlgHeight = docHeight;
    if (DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_CANVAS_SIZE), hwnd, CanvasSizeDlgProc) == IDOK) {
        ResizeDocument(hwnd, gCanvasDlgWidth, gCanvasDlgHeight, true, true);
    }
}

bool PromptSaveIfDirty(HWND hwnd) {
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

    char filePath[MAX_PATH] = "";
    if (gDocumentPath[0]) {
        sprintf_s(filePath, "%s", gDocumentPath);
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "png";
    if (gLastBrowseDir[0]) {
        ofn.lpstrInitialDir = gLastBrowseDir;
    }

    if (GetSaveFileNameA(&ofn)) {
        WriteDocumentToPath(hwnd, filePath);
    }
}

void OpenDocument(HWND hwnd) {
    if (!PromptSaveIfDirty(hwnd)) return;

    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (gLastBrowseDir[0]) {
        ofn.lpstrInitialDir = gLastBrowseDir;
    }

    if (GetOpenFileNameA(&ofn)) {
        if (!OpenDocumentFromPath(hwnd, filePath)) {
            MessageBoxA(hwnd, "Failed to load image.", "Error", MB_OK | MB_ICONERROR);
        }
    }
}

bool LastDocumentAvailable() {
    if (!gLastDocumentPath[0]) return false;
    const DWORD attrs = GetFileAttributesA(gLastDocumentPath);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void LoadSessionState() {
    char iniPath[MAX_PATH] = {};
    GetSessionIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    GetPrivateProfileStringA("Session", "LastDocument", "",
        gLastDocumentPath, MAX_PATH, iniPath);
    if (gLastDocumentPath[0]) {
        RememberBrowseDirFromPath(gLastDocumentPath);
    }
}

void OpenLastDocument(HWND hwnd) {
    if (!LastDocumentAvailable()) {
        if (gLastDocumentPath[0]) {
            gLastDocumentPath[0] = '\0';
            SaveSessionState();
        }
        return;
    }
    if (!PromptSaveIfDirty(hwnd)) return;
    if (!OpenDocumentFromPath(hwnd, gLastDocumentPath)) {
        MessageBoxA(hwnd, "Failed to open the last document.", "Error", MB_OK | MB_ICONERROR);
        gLastDocumentPath[0] = '\0';
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
    gLayers.Reset(docWidth, docHeight, gTheme.canvasBg);
    gHistory.Clear();
    SetDocumentPath(nullptr);
    InvalidateComposite();
    MarkClean(hwnd);
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
