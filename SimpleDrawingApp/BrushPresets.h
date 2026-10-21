#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "AtelierRaii.h"

// Internal preset list helpers shared by BrushEngine and BrushImport.
bool BrushPresetsCanAddCustom();
bool BrushPresetsAddCustom(const char* name, Atelier::GdiplusBitmapPtr tip, float spacing, int defaultSize);
void BrushPresetsSetActive(int index);
const char* BrushPresetsActiveName();
void BrushPresetsSaveSettings();
void BrushPresetsSyncMenu();
