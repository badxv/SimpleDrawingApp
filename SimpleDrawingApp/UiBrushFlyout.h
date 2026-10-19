#pragma once

#include <windows.h>

bool RegisterBrushFlyoutClass(HINSTANCE hInstance);
void CloseBrushFlyout();
void OpenBrushFlyout(HWND parent);
bool IsBrushFlyoutVisible(); // main or sub flyout showing
void SyncBrushFlyoutChecks();
int BrushFlyoutPresetIndexFromCmd(int cmdId);
void RebuildBrushFlyoutPresets();
int BrushFlyoutContextPresetIndex(); // last right-clicked preset, or -1
