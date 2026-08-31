#pragma once

#include <windows.h>

bool RegisterBrushFlyoutClass(HINSTANCE hInstance);
void CloseBrushFlyout();
void OpenBrushFlyout(HWND parent);
void SyncBrushFlyoutChecks();
int BrushFlyoutPresetIndexFromCmd(int cmdId);
void RebuildBrushFlyoutPresets();
