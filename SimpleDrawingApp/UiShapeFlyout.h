#pragma once

#include <windows.h>

bool RegisterShapeFlyoutClass(HINSTANCE hInstance);
void CloseShapeFlyout();
void OpenShapeFlyout(HWND parent);
void SyncShapeFlyoutChecks();
