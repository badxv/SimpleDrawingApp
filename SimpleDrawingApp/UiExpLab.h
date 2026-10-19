#pragma once

#include <windows.h>

bool RegisterExpLabClass(HINSTANCE hInstance);
void EnsureExpLabFlyout(HWND owner);
void OpenExpLabFlyout(HWND owner);
void CloseExpLabFlyout();
bool IsExpLabVisible();
void SyncExpLabLabels();
