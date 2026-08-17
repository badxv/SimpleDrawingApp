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
void DoCopy(HWND hwnd);
void DoCut(HWND hwnd);
void DoPaste(HWND hwnd);
void DoDeleteSelection(HWND hwnd);
void DoSelectAll(HWND hwnd);
void Selection_Shutdown();
