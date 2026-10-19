#pragma once

#include <windows.h>

bool RegisterBrushFlyoutClass(HINSTANCE hInstance);
void CloseBrushFlyout();
void OpenBrushFlyout(HWND parent);
bool IsBrushFlyoutVisible(); // main, sub, or Experimental Lab flyout showing
void SyncBrushFlyoutChecks();
int BrushFlyoutPresetIndexFromCmd(int cmdId);
void RebuildBrushFlyoutPresets();
int BrushFlyoutContextPresetIndex(); // last right-clicked preset, or -1
void OpenBrushLabSubPanel(int brushRowCmdId, int screenAnchorY); // Lab → shared slider
