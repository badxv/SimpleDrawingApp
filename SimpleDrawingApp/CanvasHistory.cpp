#include "CanvasHistory.h"

using namespace Gdiplus;

void CanvasHistory::Clear() {
    FreeStack(undoStack_);
    FreeStack(redoStack_);
}

Gdiplus::Bitmap* CanvasHistory::CloneBitmap(Bitmap* source) {
    if (!source) return nullptr;
    Bitmap* copy = source->Clone(0, 0, source->GetWidth(), source->GetHeight(), source->GetPixelFormat());
    if (!copy || copy->GetLastStatus() != Ok) {
        delete copy;
        return nullptr;
    }
    return copy;
}

void CanvasHistory::FreeStack(std::vector<Bitmap*>& stack) {
    for (Bitmap* bmp : stack) {
        delete bmp;
    }
    stack.clear();
}

void CanvasHistory::Push(Bitmap* canvas) {
    Bitmap* snapshot = CloneBitmap(canvas);
    if (!snapshot) return;

    undoStack_.push_back(snapshot);
    if (undoStack_.size() > kMaxDepth) {
        delete undoStack_.front();
        undoStack_.erase(undoStack_.begin());
    }

    FreeStack(redoStack_);
}

bool CanvasHistory::CanUndo() const {
    return !undoStack_.empty();
}

bool CanvasHistory::CanRedo() const {
    return !redoStack_.empty();
}

bool CanvasHistory::Restore(Bitmap* snapshot, Bitmap*& canvas, Graphics*& graphics) {
    if (!snapshot) return false;

    Bitmap* restored = CloneBitmap(snapshot);
    if (!restored) return false;

    Graphics* g = Graphics::FromImage(restored);
    if (!g || g->GetLastStatus() != Ok) {
        delete g;
        delete restored;
        return false;
    }
    g->SetSmoothingMode(SmoothingModeAntiAlias);

    delete graphics;
    delete canvas;
    canvas = restored;
    graphics = g;
    return true;
}

bool CanvasHistory::Undo(Bitmap*& canvas, Graphics*& graphics) {
    if (undoStack_.empty()) return false;

    Bitmap* current = CloneBitmap(canvas);
    Bitmap* previous = undoStack_.back();
    undoStack_.pop_back();

    if (!Restore(previous, canvas, graphics)) {
        undoStack_.push_back(previous);
        delete current;
        return false;
    }

    delete previous;
    if (current) {
        redoStack_.push_back(current);
    }
    return true;
}

bool CanvasHistory::Redo(Bitmap*& canvas, Graphics*& graphics) {
    if (redoStack_.empty()) return false;

    Bitmap* current = CloneBitmap(canvas);
    Bitmap* next = redoStack_.back();
    redoStack_.pop_back();

    if (!Restore(next, canvas, graphics)) {
        redoStack_.push_back(next);
        delete current;
        return false;
    }

    delete next;
    if (current) {
        undoStack_.push_back(current);
    }
    return true;
}
