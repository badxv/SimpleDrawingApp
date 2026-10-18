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
bool IsActiveBrushCustom();
bool DeleteBrushPreset(int index); // built-ins cannot be deleted
bool RenameBrushPreset(int index, const char* name); // built-ins cannot be renamed

void ResetBrushStrokeState();
void DrawBrushStrokeSegment(Gdiplus::Graphics* target, int x0, int y0, int x1, int y1,
    COLORREF color, int width, bool eraseTransparent, bool eraseOpaque,
    float pressure0 = 1.0f, float pressure1 = 1.0f);

bool IsPenPressureEnabled();
void SetPenPressureEnabled(bool enabled);
float PenPressureFactor(float pressure);
int PenPressureWidth(int baseWidth, float pressure);

bool ImportBrushTipFromFile(HWND owner, const char* path);
bool PromptImportBrushTip(HWND owner);
bool ImportAbrBrushesFromFile(HWND owner, const char* path);
bool PromptImportAbrBrushes(HWND owner);

int GetBrushFlow();
int GetBrushHardness();
int GetBrushScatter();
int GetBrushAngleJitter();
int GetBrushSizeJitter();
int GetBrushOpacityJitter();
void SetBrushFlow(int flow);
void SetBrushHardness(int hardness);
void SetBrushScatter(int scatter); // 0..100
void SetBrushAngleJitter(int jitter); // 0..100
void SetBrushSizeJitter(int jitter); // 0..100
void SetBrushOpacityJitter(int jitter); // 0..100

// Stamp spacing as percent of tip size (5..100 → 0.05..1.0).
int GetBrushSpacingPercent();
void SetBrushSpacingPercent(int percent);

// Updates the active custom brush's defaultSize and persists to disk.
void PersistActiveBrushDefaultSize(int size);

void LoadBrushSettings();
void SaveBrushSettings();
void SyncBrushMenuItems();
