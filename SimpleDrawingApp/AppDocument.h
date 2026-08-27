#pragma once

#include <windows.h>

bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
void ClearCanvas(HWND hwnd, bool pushHistory);
void ResizeCanvas(HWND hwnd);
bool PromptSaveIfDirty(HWND hwnd);
void SaveDocument(HWND hwnd);
void SaveDocumentAs(HWND hwnd);
void OpenDocument(HWND hwnd);
void OpenLastDocument(HWND hwnd);
void OpenRecentDocument(HWND hwnd, int index);
bool LastDocumentAvailable();
void ClearRecentDocuments();
void SyncRecentFileMenu(HMENU fileMenu);
void LoadSessionState();
void NewDocument(HWND hwnd);
void UndoDocument(HWND hwnd);
void RedoDocument(HWND hwnd);
