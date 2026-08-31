#pragma once

// Loads custom brush tips from brushes/ next to the executable.
void LoadCustomBrushesFromDisk();

// Persists non-built-in presets to brushes/brushes.ini + PNG tips.
void SaveCustomBrushesToDisk();
