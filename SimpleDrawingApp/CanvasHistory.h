#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>

// Snapshot-based undo/redo for the raster canvas.
class CanvasHistory {
public:
    static constexpr size_t kMaxDepth = 40;

    CanvasHistory() = default;
    ~CanvasHistory() { Clear(); }

    CanvasHistory(const CanvasHistory&) = delete;
    CanvasHistory& operator=(const CanvasHistory&) = delete;

    void Clear();
    void Push(Gdiplus::Bitmap* canvas);
    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo(Gdiplus::Bitmap*& canvas, Gdiplus::Graphics*& graphics);
    bool Redo(Gdiplus::Bitmap*& canvas, Gdiplus::Graphics*& graphics);

private:
    std::vector<Gdiplus::Bitmap*> undoStack_;
    std::vector<Gdiplus::Bitmap*> redoStack_;

    static Gdiplus::Bitmap* CloneBitmap(Gdiplus::Bitmap* source);
    static void FreeStack(std::vector<Gdiplus::Bitmap*>& stack);
    static bool Restore(Gdiplus::Bitmap* snapshot, Gdiplus::Bitmap*& canvas, Gdiplus::Graphics*& graphics);
};
