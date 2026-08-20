#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

void DestroySelFloat();
void ClearSelection(bool stampFloating);
void NormalizeSelRect(int x0, int y0, int x1, int y1, int& x, int& y, int& w, int& h);
bool SelectionHitTest(int docX, int docY);
void LiftSelection();
void StampFloatingSelection();
void DrawSelectionOverlay(Gdiplus::Graphics* g);
void CopySelection(HWND hwnd);
void CutSelection(HWND hwnd);
void PasteSelection(HWND hwnd);
void DeleteSelection(HWND hwnd);
void SelectAll(HWND hwnd);
void Selection_Shutdown();

inline void DoCopy(HWND hwnd) { CopySelection(hwnd); }
inline void DoCut(HWND hwnd) { CutSelection(hwnd); }
inline void DoPaste(HWND hwnd) { PasteSelection(hwnd); }
inline void DoDeleteSelection(HWND hwnd) { DeleteSelection(hwnd); }
inline void DoSelectAll(HWND hwnd) { SelectAll(hwnd); }
