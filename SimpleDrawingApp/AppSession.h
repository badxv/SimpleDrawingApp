#pragma once

#include <windows.h>
#include <cstddef>

// Session.ini / recent files / autosave path helpers.

void RememberBrowseDirFromPath(const wchar_t* path);
void GetAutosavePath(wchar_t* path, size_t pathChars);
void ClearAutosaveRecovery();
bool PathIsExistingFile(const wchar_t* path);
void RememberRecentPath(const wchar_t* path);
void RemoveRecentPath(const wchar_t* path);
void PruneMissingRecentPaths();
void SaveSessionState();
void SetDocumentPath(const wchar_t* path);

void LoadSessionState();
void SyncRecentFileMenu(HMENU fileMenu);
void ClearRecentDocuments();
bool LastDocumentAvailable();
