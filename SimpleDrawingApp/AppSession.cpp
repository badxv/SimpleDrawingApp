#include "AppSession.h"
#include "AppDocState.h"
#include "AppPaths.h"
#include "Resource.h"

#include <cstdio>
#include <cstring>

namespace {

void GetSessionIniPath(wchar_t* path, size_t pathChars) {
    GetModuleSiblingPathW(path, pathChars, L"session.ini");
}

void SyncLastDocumentFromRecent() {
    if (gRecentDocumentCount > 0 && gRecentDocuments[0][0]) {
        wcsncpy_s(gLastDocumentPath, gRecentDocuments[0], _TRUNCATE);
    } else {
        gLastDocumentPath[0] = L'\0';
    }
}

void FormatRecentMenuLabel(int index, const wchar_t* path, wchar_t* out, size_t outChars) {
    if (!out || outChars < 8 || !path) return;
    const wchar_t* name = path;
    const wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    if (slash && slash[1]) name = slash + 1;

    wchar_t truncated[MAX_PATH];
    wcsncpy_s(truncated, name, _TRUNCATE);
    const size_t maxName = 42;
    if (wcslen(truncated) > maxName) {
        truncated[maxName - 3] = L'\0';
        wcscat_s(truncated, L"...");
    }
    swprintf_s(out, outChars, L"&%d %s", index + 1, truncated);
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

} // namespace

void RememberBrowseDirFromPath(const wchar_t* path) {
    if (!path || !path[0]) return;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, path, _TRUNCATE);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) slash = wcsrchr(dir, L'/');
    if (!slash) return;
    *slash = L'\0';
    wcsncpy_s(gLastBrowseDir, dir, _TRUNCATE);
}

void GetAutosavePath(wchar_t* path, size_t pathChars) {
    GetModuleSiblingPathW(path, pathChars, L"autosave.png");
}

void ClearAutosaveRecovery() {
    wchar_t path[MAX_PATH] = {};
    GetAutosavePath(path, MAX_PATH);
    if (path[0]) DeleteFileW(path);
}

bool PathIsExistingFile(const wchar_t* path) {
    if (!path || !path[0]) return false;
    const DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void RememberRecentPath(const wchar_t* path) {
    if (!path || !path[0]) return;

    wchar_t normalized[MAX_PATH];
    wcsncpy_s(normalized, path, _TRUNCATE);

    int existing = -1;
    for (int i = 0; i < gRecentDocumentCount; ++i) {
        if (_wcsicmp(gRecentDocuments[i], normalized) == 0) {
            existing = i;
            break;
        }
    }

    if (existing == 0) {
        SyncLastDocumentFromRecent();
        return;
    }

    if (existing > 0) {
        wchar_t moved[MAX_PATH];
        wcsncpy_s(moved, gRecentDocuments[existing], _TRUNCATE);
        for (int i = existing; i > 0; --i) {
            wcsncpy_s(gRecentDocuments[i], gRecentDocuments[i - 1], _TRUNCATE);
        }
        wcsncpy_s(gRecentDocuments[0], moved, _TRUNCATE);
    } else {
        const int count = (gRecentDocumentCount < kMaxRecentDocuments)
            ? gRecentDocumentCount + 1
            : kMaxRecentDocuments;
        for (int i = count - 1; i > 0; --i) {
            wcsncpy_s(gRecentDocuments[i], gRecentDocuments[i - 1], _TRUNCATE);
        }
        wcsncpy_s(gRecentDocuments[0], normalized, _TRUNCATE);
        gRecentDocumentCount = count;
    }
    SyncLastDocumentFromRecent();
}

void RemoveRecentPath(const wchar_t* path) {
    if (!path || !path[0] || gRecentDocumentCount <= 0) return;
    int found = -1;
    for (int i = 0; i < gRecentDocumentCount; ++i) {
        if (_wcsicmp(gRecentDocuments[i], path) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) return;
    for (int i = found; i < gRecentDocumentCount - 1; ++i) {
        wcsncpy_s(gRecentDocuments[i], gRecentDocuments[i + 1], _TRUNCATE);
    }
    --gRecentDocumentCount;
    gRecentDocuments[gRecentDocumentCount][0] = L'\0';
    SyncLastDocumentFromRecent();
}

void PruneMissingRecentPaths() {
    int write = 0;
    for (int read = 0; read < gRecentDocumentCount; ++read) {
        if (!PathIsExistingFile(gRecentDocuments[read])) continue;
        if (write != read) {
            wcsncpy_s(gRecentDocuments[write], gRecentDocuments[read], _TRUNCATE);
        }
        ++write;
    }
    for (int i = write; i < gRecentDocumentCount; ++i) {
        gRecentDocuments[i][0] = L'\0';
    }
    gRecentDocumentCount = write;
    SyncLastDocumentFromRecent();
}

void SaveSessionState() {
    wchar_t iniPath[MAX_PATH] = {};
    GetSessionIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    WritePrivateProfileStringW(L"Session", L"LastDocument",
        gLastDocumentPath[0] ? gLastDocumentPath : L"", iniPath);
    for (int i = 0; i < kMaxRecentDocuments; ++i) {
        wchar_t key[32];
        swprintf_s(key, L"Recent%d", i);
        WritePrivateProfileStringW(L"Session", key,
            (i < gRecentDocumentCount && gRecentDocuments[i][0]) ? gRecentDocuments[i] : L"",
            iniPath);
    }
}

void SetDocumentPath(const wchar_t* path) {
    if (path && path[0]) {
        wcsncpy_s(gDocumentPath, path, _TRUNCATE);
        RememberBrowseDirFromPath(path);
        RememberRecentPath(path);
        SaveSessionState();
    } else {
        gDocumentPath[0] = L'\0';
    }
}

bool LastDocumentAvailable() {
    return PathIsExistingFile(gLastDocumentPath);
}

void LoadSessionState() {
    wchar_t iniPath[MAX_PATH] = {};
    GetSessionIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;

    gRecentDocumentCount = 0;
    for (int i = 0; i < kMaxRecentDocuments; ++i) {
        gRecentDocuments[i][0] = L'\0';
        wchar_t key[32];
        swprintf_s(key, L"Recent%d", i);
        wchar_t path[MAX_PATH] = {};
        GetPrivateProfileStringW(L"Session", key, L"", path, MAX_PATH, iniPath);
        if (!path[0]) continue;
        bool dup = false;
        for (int j = 0; j < gRecentDocumentCount; ++j) {
            if (_wcsicmp(gRecentDocuments[j], path) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        wcsncpy_s(gRecentDocuments[gRecentDocumentCount], path, _TRUNCATE);
        ++gRecentDocumentCount;
    }

    wchar_t legacy[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Session", L"LastDocument", L"", legacy, MAX_PATH, iniPath);
    if (legacy[0]) {
        bool already = false;
        for (int i = 0; i < gRecentDocumentCount; ++i) {
            if (_wcsicmp(gRecentDocuments[i], legacy) == 0) {
                already = true;
                break;
            }
        }
        if (!already) {
            RememberRecentPath(legacy);
        } else if (gRecentDocumentCount > 0
            && _wcsicmp(gRecentDocuments[0], legacy) != 0) {
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
        AppendMenuW(recent, MF_STRING | MF_GRAYED, IDM_RECENT_NONE, L"(None)");
        return;
    }

    for (int i = 0; i < gRecentDocumentCount; ++i) {
        wchar_t label[MAX_PATH + 8];
        FormatRecentMenuLabel(i, gRecentDocuments[i], label, MAX_PATH + 8);
        AppendMenuW(recent, MF_STRING, IDM_RECENT_0 + i, label);
    }
    AppendMenuW(recent, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(recent, MF_STRING, IDM_CLEAR_RECENT, L"&Clear Recent List");
}

void ClearRecentDocuments() {
    for (int i = 0; i < kMaxRecentDocuments; ++i) {
        gRecentDocuments[i][0] = L'\0';
    }
    gRecentDocumentCount = 0;
    gLastDocumentPath[0] = L'\0';
    SaveSessionState();
}
