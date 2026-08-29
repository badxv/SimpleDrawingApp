#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <vector>

struct BrushPreset {
    std::string name;
    std::unique_ptr<Gdiplus::Bitmap> tip;
    float spacing = 0.2f;      // fraction of pen width between stamps
    int defaultSize = 8;
    bool builtin = true;
};

void InitBrushEngine();
void ShutdownBrushEngine();

int BrushPresetCount();
const BrushPreset* GetBrushPreset(int index);
int GetActiveBrushIndex();
void SetActiveBrushIndex(int index);
const char* GetActiveBrushName();

void ResetBrushStrokeState();
void DrawBrushStrokeSegment(Gdiplus::Graphics* target, int x0, int y0, int x1, int y1,
    COLORREF color, int width, bool eraseTransparent, bool eraseOpaque);

bool ImportBrushTipFromFile(HWND owner, const char* path);
bool PromptImportBrushTip(HWND owner);
bool ImportAbrBrushesFromFile(HWND owner, const char* path);
bool PromptImportAbrBrushes(HWND owner);

int GetBrushFlow();
int GetBrushHardness();
void SetBrushFlow(int flow);
void SetBrushHardness(int hardness);

void LoadBrushSettings();
void SaveBrushSettings();
void SyncBrushMenuItems();
