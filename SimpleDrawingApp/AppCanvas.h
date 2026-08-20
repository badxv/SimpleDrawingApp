#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

void SyncDocSizeFromBitmap();
void EnsureCanvas(HWND hwnd);
void DestroyCompositeCache();
void InvalidateComposite();
Gdiplus::Bitmap* GetCompositeBitmap();
void UpdateScrollBars();
void InvalidateCanvas();
int ScaledContentWidth();
int ScaledContentHeight();
void ZoomByFactor(HWND hwnd, float factor);
void ZoomToActual(HWND hwnd);
void ZoomToFit(HWND hwnd);
void SetZoomAtViewportPoint(HWND hwnd, float newZoom, int localX, int localY);
