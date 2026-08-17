#pragma once

#include <windows.h>

bool ResizeDocument(HWND hwnd, int newWidth, int newHeight, bool pushHistory, bool warnOnShrink);
void ClearCanvas(HWND hwnd, bool pushHistory);
void DoCanvasSize(HWND hwnd);
bool PromptSaveIfDirty(HWND hwnd);
void DoSave(HWND hwnd);
void DoOpen(HWND hwnd);
void DoNew(HWND hwnd);
void DoUndo(HWND hwnd);
void DoRedo(HWND hwnd);
