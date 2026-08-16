#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

// Quiet parchment artworks for chrome side panels (Renaissance × futurism sketches).

bool AtelierArtwork_Init();
void AtelierArtwork_Shutdown();

// Draw loaded sketch into a chrome panel band with low opacity (watermark).
void DrawFrescoArtwork(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds, bool leftRail);
