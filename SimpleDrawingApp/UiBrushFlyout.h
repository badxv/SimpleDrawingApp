#pragma once

#include <windows.h>

bool RegisterBrushFlyoutClass(HINSTANCE hInstance);
void CloseBrushFlyout();
/// Hide only the dynamics sub-panel. Returns true if it was visible.
bool CloseBrushSubFlyout();
void OpenBrushFlyout(HWND parent);
void SyncBrushFlyoutChecks();
