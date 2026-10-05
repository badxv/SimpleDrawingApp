#pragma once

#include <windows.h>

struct BrushPreset;

// Draws a brush tip thumbnail (atelier parchment bg when onDark is false).
void DrawBrushTipPreview(HDC hdc, const RECT& rc, const BrushPreset* preset, bool onDark = false);

// Opens brush gallery (list + preview). Returns true if the active brush changed.
bool ShowBrushGalleryDialog(HWND owner);
