#pragma once

#include <windows.h>
#include "DrawingTools.h"

void UpdatePenWidthDisplay();
void UpdateOpacityDisplay();
void ApplyPenWidth(HWND hwnd, int width);
void UpdateWindowTitle(HWND hwnd);
void UpdateStatusBar(HWND hwnd);
void LayoutStatusParts(HWND hwnd);
void MarkDirty(HWND hwnd);
void MarkClean(HWND hwnd);
void SetActiveTool(DrawTool tool);
void RefreshLayerList();
bool IsTypingInEdit();
