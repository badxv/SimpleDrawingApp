#pragma once

#include <windows.h>

// User-selectable present path for the viewport.
enum class GraphicsBackend {
    Auto = 0,      // Prefer Direct2D when the OS provides it; else GDI+
    Software = 1,  // GDI+ only
    Hardware = 2,  // Direct2D present when available; falls back to GDI+
};

GraphicsBackend GetGraphicsBackend();
void SetGraphicsBackend(GraphicsBackend backend);
bool IsDirect2DAvailable();
GraphicsBackend EffectiveGraphicsBackend(); // resolves Auto

// Present a memory DC (viewW x viewH) to the viewport paint HDC.
// Returns true if Hardware/Auto path drew; false means caller should BitBlt.
bool PresentViewportAccelerated(HWND hwnd, HDC destDc, HDC srcMemDc, int viewW, int viewH);

void SyncGraphicsBackendMenuItems();
void LoadGraphicsBackend();
void SaveGraphicsBackend();
