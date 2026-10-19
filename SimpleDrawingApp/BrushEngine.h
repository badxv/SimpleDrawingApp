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
void InitBrushStrokeSmoothing(int x, int y);
// Pull freehand sample toward (rawX,rawY); returns false if no movement after smooth.
bool SmoothBrushStrokePoint(int rawX, int rawY, int& outX, int& outY);
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
int GetBrushTipAngle();
int GetBrushSizeJitter();
int GetBrushOpacityJitter();
int GetBrushSmoothing();
int GetBrushStampCount();
int GetBrushRoundness();
int GetBrushMinDiameter();
int GetBrushWetEdges();
int GetBrushRoundnessJitter();
int GetBrushFlipX();
int GetBrushFlipY();
int GetBrushFlowJitter();
int GetBrushColorJitter();
int GetBrushHueJitter();
int GetBrushSatJitter();
int GetBrushBrightJitter();
bool IsBrushExperimental();
int GetBrushEchoDelay();
int GetBrushEchoFeedback();
int GetBrushPing();
int GetBrushBiolum();
int GetBrushHeat();
int GetBrushClockPhase();
int GetBrushCompassLock();
int GetBrushAfterimage();
int GetBrushAging();
int GetBrushWind();
void SetBrushFlow(int flow);
void SetBrushHardness(int hardness);
void SetBrushScatter(int scatter); // 0..100
void SetBrushAngleJitter(int jitter); // 0..100
void SetBrushTipAngle(int degrees); // 0..180
void SetBrushSizeJitter(int jitter); // 0..100
void SetBrushOpacityJitter(int jitter); // 0..100
void SetBrushSmoothing(int amount); // 0..100
void SetBrushStampCount(int count); // 1..8
void SetBrushRoundness(int percent); // 1..100
void SetBrushMinDiameter(int percent); // 0..100
void SetBrushWetEdges(int percent); // 0..100
void SetBrushRoundnessJitter(int jitter); // 0..100
void SetBrushFlipX(int percent); // 0..100 chance
void SetBrushFlipY(int percent); // 0..100 chance
void SetBrushFlowJitter(int jitter); // 0..100
void SetBrushColorJitter(int jitter); // 0..100 — blend toward backColor
void SetBrushHueJitter(int jitter); // 0..100
void SetBrushSatJitter(int jitter); // 0..100
void SetBrushBrightJitter(int jitter); // 0..100
void SetBrushExperimental(bool enabled); // opt-in experimental atelier
void SetBrushEchoDelay(int delay); // 0..100
void SetBrushEchoFeedback(int feedback); // 0..100
void SetBrushPing(int strength); // 0..100
void SetBrushBiolum(int amount); // 0..100
void SetBrushHeat(int amount); // 0..100
void SetBrushClockPhase(int amount); // 0..100
void SetBrushCompassLock(int amount); // 0..100
void SetBrushAfterimage(int amount); // 0..100 — eraser afterimage ghosts
void SetBrushAging(int amount); // 0..100 — wet→dry over stroke age
void SetBrushWind(int amount); // 0..100 — edge-biased scatter

// Deferred stamp queue (echo / radar ping). Returns true if anything was drawn.
bool BrushTemporalPending();
void ClearBrushTemporalQueue();
bool ProcessBrushTemporalQueue();

// Expanding sonar ring from (cx,cy); no-op unless Experimental + Ping > 0.
void FireBrushPing(int cx, int cy, COLORREF color, int tipSize, bool eraseTransparent);

// Heat ledger field map (visit count). Overlay bitmap may be null when inactive.
void ClearBrushHeatMap();
void EnsureBrushHeatMap(int width, int height);
Gdiplus::Bitmap* GetBrushHeatViz(); // do not free; null if unused

// Tip used for painting/preview (applies wet-edges remap when enabled). Do not free.
Gdiplus::Bitmap* BrushTipForDraw(Gdiplus::Bitmap* tip);

// Stamp spacing as percent of tip size (5..100 → 0.05..1.0).
int GetBrushSpacingPercent();
void SetBrushSpacingPercent(int percent);

// Updates the active custom brush's defaultSize and persists to disk.
void PersistActiveBrushDefaultSize(int size);

void LoadBrushSettings();
void SaveBrushSettings();
void SyncBrushMenuItems();
