#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

// Internal preset list helpers shared by BrushEngine and BrushImport.
bool BrushPresetsCanAddCustom();
bool BrushPresetsAddCustom(const char* name, Gdiplus::Bitmap* tip, float spacing, int defaultSize);
void BrushPresetsSetActive(int index);
const char* BrushPresetsActiveName();
void BrushPresetsSaveSettings();
void BrushPresetsSyncMenu();
