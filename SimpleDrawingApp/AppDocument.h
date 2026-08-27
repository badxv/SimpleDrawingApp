#pragma once

#include <windows.h>

bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
void ClearCanvas(HWND hwnd, bool pushHistory);
void ResizeCanvas(HWND hwnd);
bool PromptSaveIfDirty(HWND hwnd);
void SaveDocument(HWND hwnd);
void SaveDocumentAs(HWND hwnd);
void OpenDocument(HWND hwnd);
void NewDocument(HWND hwnd);
void UndoDocument(HWND hwnd);
void RedoDocument(HWND hwnd);
