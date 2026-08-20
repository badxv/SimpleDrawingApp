#pragma once

#include <windows.h>

bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
void ClearCanvas(HWND hwnd, bool pushHistory);
void ResizeCanvas(HWND hwnd);
bool PromptSaveIfDirty(HWND hwnd);
void SaveDocument(HWND hwnd);
void OpenDocument(HWND hwnd);
void NewDocument(HWND hwnd);
void UndoDocument(HWND hwnd);
void RedoDocument(HWND hwnd);

// Legacy aliases (remove once all call sites migrate).
inline void DoCanvasSize(HWND hwnd) { ResizeCanvas(hwnd); }
inline void DoSave(HWND hwnd) { SaveDocument(hwnd); }
inline void DoOpen(HWND hwnd) { OpenDocument(hwnd); }
inline void DoNew(HWND hwnd) { NewDocument(hwnd); }
inline void DoUndo(HWND hwnd) { UndoDocument(hwnd); }
inline void DoRedo(HWND hwnd) { RedoDocument(hwnd); }
