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

void GetAutosavePath(char* path, size_t pathChars) {
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
    const char suffix[] = "autosave.png";
    if (used + sizeof(suffix) > pathChars) {
        path[0] = '\0';
        return;
    }
    strcat_s(path, pathChars, suffix);
}

void ClearAutosaveRecovery() {
    char path[MAX_PATH] = {};
    GetAutosavePath(path, MAX_PATH);
    if (path[0]) DeleteFileA(path);
}

bool PathIsExistingFile(const char* path) {
    if (!path || !path[0]) return false;
    const DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void SyncLastDocumentFromRecent() {
    if (gRecentDocumentCount > 0 && gRecentDocuments[0][0]) {
        sprintf_s(gLastDocumentPath, "%s", gRecentDocuments[0]);
    } else {
        gLastDocumentPath[0] = '\0';
    }
}

void RememberRecentPath(const char* path) {
    if (!path || !path[0]) return;

    char normalized[MAX_PATH];
    sprintf_s(normalized, "%s", path);

    int existing = -1;
    for (int i = 0; i < gRecentDocumentCount; ++i) {
        if (_stricmp(gRecentDocuments[i], normalized) == 0) {
            existing = i;
            break;
        }
    }

    if (existing == 0) {
        SyncLastDocumentFromRecent();
        return;
    }

    if (existing > 0) {
        char moved[MAX_PATH];
        sprintf_s(moved, "%s", gRecentDocuments[existing]);
        for (int i = existing; i > 0; --i) {
            sprintf_s(gRecentDocuments[i], "%s", gRecentDocuments[i - 1]);
        }
        sprintf_s(gRecentDocuments[0], "%s", moved);
    } else {
        const int count = (gRecentDocumentCount < kMaxRecentDocuments)
            ? gRecentDocumentCount + 1
            : kMaxRecentDocuments;
        for (int i = count - 1; i > 0; --i) {
            sprintf_s(gRecentDocuments[i], "%s", gRecentDocuments[i - 1]);
        }
        sprintf_s(gRecentDocuments[0], "%s", normalized);
        gRecentDocumentCount = count;
    }
    SyncLastDocumentFromRecent();
}

void RemoveRecentPath(const char* path) {
    if (!path || !path[0] || gRecentDocumentCount <= 0) return;
    int found = -1;
    for (int i = 0; i < gRecentDocumentCount; ++i) {
        if (_stricmp(gRecentDocuments[i], path) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) return;
    for (int i = found; i < gRecentDocumentCount - 1; ++i) {
        sprintf_s(gRecentDocuments[i], "%s", gRecentDocuments[i + 1]);
    }
    --gRecentDocumentCount;
    gRecentDocuments[gRecentDocumentCount][0] = '\0';
    SyncLastDocumentFromRecent();
}

void PruneMissingRecentPaths() {
    int write = 0;
    for (int read = 0; read < gRecentDocumentCount; ++read) {
        if (!PathIsExistingFile(gRecentDocuments[read])) continue;
        if (write != read) {
            sprintf_s(gRecentDocuments[write], "%s", gRecentDocuments[read]);
        }
        ++write;
    }
    for (int i = write; i < gRecentDocumentCount; ++i) {
        gRecentDocuments[i][0] = '\0';
    }
    gRecentDocumentCount = write;
    SyncLastDocumentFromRecent();
}

void FormatRecentMenuLabel(int index, const char* path, char* out, size_t outChars) {
    if (!out || outChars < 8 || !path) return;
    const char* name = path;
    const char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (slash && slash[1]) name = slash + 1;

    // "&1 filename.png" — keep labels short for the menu.
    char truncated[MAX_PATH];
    sprintf_s(truncated, "%s", name);
    const size_t maxName = 42;
    if (strlen(truncated) > maxName) {
        truncated[maxName - 3] = '\0';
        strcat_s(truncated, "...");
    }
    sprintf_s(out, outChars, "&%d %s", index + 1, truncated);
}

HMENU FindRecentFilesSubMenu(HMENU fileMenu) {
    if (!fileMenu) return nullptr;
    const int count = GetMenuItemCount(fileMenu);
    for (int i = 0; i < count; ++i) {
        HMENU sub = GetSubMenu(fileMenu, i);
        if (!sub) continue;
        const UINT firstId = GetMenuItemID(sub, 0);
        if (firstId == IDM_RECENT_0 || firstId == IDM_RECENT_NONE
            || firstId == IDM_CLEAR_RECENT
            || (firstId >= IDM_RECENT_0 && firstId <= IDM_RECENT_4)) {
            return sub;
        }
    }
    return nullptr;
}

void SaveSessionState() {
    char iniPath[MAX_PATH] = {};
    GetSessionIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    WritePrivateProfileStringA("Session", "LastDocument",
        gLastDocumentPath[0] ? gLastDocumentPath : "", iniPath);
    for (int i = 0; i < kMaxRecentDocuments; ++i) {
        char key[32];
        sprintf_s(key, "Recent%d", i);
        WritePrivateProfileStringA("Session", key,
            (i < gRecentDocumentCount && gRecentDocuments[i][0]) ? gRecentDocuments[i] : "",
            iniPath);
    }
}

void SetDocumentPath(const char* path) {
    if (path && path[0]) {
        sprintf_s(gDocumentPath, "%s", path);
        RememberBrowseDirFromPath(path);
        RememberRecentPath(path);
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
        ClearAutosaveRecovery();
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

void ExportDocument(HWND hwnd) {
    EnsureCanvas(hwnd);

    char filePath[MAX_PATH] = "";
    if (gDocumentPath[0]) {
        // Suggest "name-export.png" beside the current document.
        sprintf_s(filePath, "%s", gDocumentPath);
        char* dot = strrchr(filePath, '.');
        char* slash = strrchr(filePath, '\\');
        if (!slash) slash = strrchr(filePath, '/');
        if (dot && (!slash || dot > slash)) {
            *dot = '\0';
            char base[MAX_PATH];
            sprintf_s(base, "%s", filePath);
            sprintf_s(filePath, "%s-export.png", base);
        } else {
            strcat_s(filePath, "-export.png");
        }
    } else {
        sprintf_s(filePath, "export.png");
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "png";
    ofn.lpstrTitle = "Export As";
    if (gLastBrowseDir[0]) {
        ofn.lpstrInitialDir = gLastBrowseDir;
    }

    if (!GetSaveFileNameA(&ofn)) return;

    // Flatten layers only — do not change the active document path or dirty flag.
    Bitmap* flat = GetCompositeBitmap();
    if (flat && SaveCanvasToFile(flat, filePath)) {
        RememberBrowseDirFromPath(filePath);
        return;
    }
    MessageBoxA(hwnd, "Failed to export image.", "Error", MB_OK | MB_ICONERROR);
}

void PrintDocument(HWND hwnd) {
    EnsureCanvas(hwnd);

    Bitmap* flat = GetCompositeBitmap();
    if (!flat) {
        MessageBoxA(hwnd, "Nothing to print.", "Print", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PRINTDLGA pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hwnd;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS;
    if (!PrintDlgA(&pd)) {
        return; // cancelled or no printers
    }

    HDC hdc = pd.hDC;
    if (!hdc) {
        MessageBoxA(hwnd, "Failed to open printer.", "Print", MB_OK | MB_ICONERROR);
        return;
    }

    DOCINFOA di = {};
    di.cbSize = sizeof(di);
    di.lpszDocName = "Simple Drawing App";

    bool ok = false;
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
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);

    if (!ok) {
        MessageBoxA(hwnd, "Print job failed to start.", "Print", MB_OK | MB_ICONERROR);
    }
}

void AutosaveIfNeeded(HWND hwnd) {
    if (!IsFeatureEnabled(AppFeature::AutosaveRecovery)) return;
    if (!documentDirty) return;
    EnsureCanvas(hwnd);

    char path[MAX_PATH] = {};
    GetAutosavePath(path, MAX_PATH);
    if (!path[0]) return;

    Bitmap* flat = GetCompositeBitmap();
    if (!flat) return;
    SaveCanvasToFile(flat, path);
}

bool OfferAutosaveRecovery(HWND hwnd) {
    if (!IsFeatureEnabled(AppFeature::AutosaveRecovery)) return false;

    char path[MAX_PATH] = {};
    GetAutosavePath(path, MAX_PATH);
    if (!path[0] || !PathIsExistingFile(path)) return false;

    const int choice = MessageBoxA(
        hwnd,
        "A recovered drawing was found from the last session.\n\nRestore it?",
        "Autosave Recovery",
        MB_YESNO | MB_ICONQUESTION);
    if (choice != IDYES) {
        ClearAutosaveRecovery();
        return false;
    }

    if (!OpenDocumentFromPath(hwnd, path)) {
        MessageBoxA(hwnd, "Failed to restore the autosave.", "Error", MB_OK | MB_ICONERROR);
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
    return PathIsExistingFile(gLastDocumentPath);
}

void LoadSessionState() {
    char iniPath[MAX_PATH] = {};
    GetSessionIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;

    gRecentDocumentCount = 0;
    for (int i = 0; i < kMaxRecentDocuments; ++i) {
        gRecentDocuments[i][0] = '\0';
        char key[32];
        sprintf_s(key, "Recent%d", i);
        char path[MAX_PATH] = {};
        GetPrivateProfileStringA("Session", key, "", path, MAX_PATH, iniPath);
        if (!path[0]) continue;
        // Dedupe while loading.
        bool dup = false;
        for (int j = 0; j < gRecentDocumentCount; ++j) {
            if (_stricmp(gRecentDocuments[j], path) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        sprintf_s(gRecentDocuments[gRecentDocumentCount], "%s", path);
        ++gRecentDocumentCount;
    }

    char legacy[MAX_PATH] = {};
    GetPrivateProfileStringA("Session", "LastDocument", "", legacy, MAX_PATH, iniPath);
    if (legacy[0]) {
        bool already = false;
        for (int i = 0; i < gRecentDocumentCount; ++i) {
            if (_stricmp(gRecentDocuments[i], legacy) == 0) {
                already = true;
                break;
            }
        }
        if (!already) {
            RememberRecentPath(legacy);
        } else if (gRecentDocumentCount > 0
            && _stricmp(gRecentDocuments[0], legacy) != 0) {
            RememberRecentPath(legacy);
        }
    }

    PruneMissingRecentPaths();
    SyncLastDocumentFromRecent();
    if (gLastDocumentPath[0]) {
        RememberBrowseDirFromPath(gLastDocumentPath);
    }
    SaveSessionState();
}

void SyncRecentFileMenu(HMENU fileMenu) {
    HMENU recent = FindRecentFilesSubMenu(fileMenu);
    if (!recent) return;

    PruneMissingRecentPaths();

    while (GetMenuItemCount(recent) > 0) {
        DeleteMenu(recent, 0, MF_BYPOSITION);
    }

    if (gRecentDocumentCount <= 0) {
        AppendMenuA(recent, MF_STRING | MF_GRAYED, IDM_RECENT_NONE, "(None)");
        return;
    }

    for (int i = 0; i < gRecentDocumentCount; ++i) {
        char label[MAX_PATH + 8];
        FormatRecentMenuLabel(i, gRecentDocuments[i], label, sizeof(label));
        AppendMenuA(recent, MF_STRING, IDM_RECENT_0 + i, label);
    }
    AppendMenuA(recent, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(recent, MF_STRING, IDM_CLEAR_RECENT, "&Clear Recent List");
}

void ClearRecentDocuments() {
    for (int i = 0; i < kMaxRecentDocuments; ++i) {
        gRecentDocuments[i][0] = '\0';
    }
    gRecentDocumentCount = 0;
    gLastDocumentPath[0] = '\0';
    SaveSessionState();
}

void OpenLastDocument(HWND hwnd) {
    PruneMissingRecentPaths();
    if (!LastDocumentAvailable()) {
        SaveSessionState();
        return;
    }
    if (!PromptSaveIfDirty(hwnd)) return;
    char path[MAX_PATH];
    sprintf_s(path, "%s", gLastDocumentPath);
    if (!OpenDocumentFromPath(hwnd, path)) {
        MessageBoxA(hwnd, "Failed to open the last document.", "Error", MB_OK | MB_ICONERROR);
        RemoveRecentPath(path);
        SaveSessionState();
    }
}

void OpenRecentDocument(HWND hwnd, int index) {
    PruneMissingRecentPaths();
    if (index < 0 || index >= gRecentDocumentCount) return;
    char path[MAX_PATH];
    sprintf_s(path, "%s", gRecentDocuments[index]);
    if (!PathIsExistingFile(path)) {
        RemoveRecentPath(path);
        SaveSessionState();
        return;
    }
    if (!PromptSaveIfDirty(hwnd)) return;
    if (!OpenDocumentFromPath(hwnd, path)) {
        MessageBoxA(hwnd, "Failed to open the document.", "Error", MB_OK | MB_ICONERROR);
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
    gLayers.Reset(docWidth, docHeight, gTheme.canvasBg);
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
