#pragma once

#include <windows.h>
#include "DrawingTools.h"

// Compact atelier color well: round HSV disc + recent + favorites.
// LMB on disc/recent/fav → FG; RMB on disc → BG; RMB on recent → pin favorite;
// RMB on favorite → unpin. Center click → parent should open picker.

#ifndef ID_PALETTE
#define ID_PALETTE 1062
#endif

bool AtelierPalette_Register();
void AtelierPalette_SetTheme(const AppTheme* theme);

HWND AtelierPalette_Create(HWND parent, int x, int y, int w, int h);
int AtelierPalette_IdealHeight(int width);

void AtelierPalette_SetColors(HWND hwnd, COLORREF fg, COLORREF bg);
void AtelierPalette_GetColors(HWND hwnd, COLORREF* fg, COLORREF* bg);
void AtelierPalette_NoteColor(HWND hwnd, COLORREF color); // push into recent
void AtelierPalette_Load(HWND hwnd);
void AtelierPalette_Save(HWND hwnd);

// Parent receives WM_COMMAND with LOWORD = ID_PALETTE when colors change.
// HIWORD: 1 = FG changed, 2 = BG changed, 3 = request FG picker, 4 = request BG picker.
