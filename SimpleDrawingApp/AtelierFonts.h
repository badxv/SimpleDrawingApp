#pragma once

#include <windows.h>

// Private OFL font bundle (Cinzel display + DM Sans UI).
// Call AtelierFonts_Init once after WinMain starts; Shutdown on exit.

bool AtelierFonts_Init();
void AtelierFonts_Shutdown();

// GDI fonts (may fall back to Georgia / Segoe UI if bundle missing).
HFONT AtelierFonts_Display(int heightPx, bool italic = false); // Cinzel
HFONT AtelierFonts_Ui(int heightPx, bool bold = false);        // DM Sans

// Family names after AddFontResourceEx (for GDI+ FontFamily).
const wchar_t* AtelierFonts_DisplayFamilyW();
const wchar_t* AtelierFonts_UiFamilyW();
