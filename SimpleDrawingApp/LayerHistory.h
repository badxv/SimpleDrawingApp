#pragma once

#include "LayerStack.h"
#include <vector>

// Snapshot-based undo/redo for the full layer stack.
class LayerHistory {
public:
    static constexpr size_t kMaxDepth = 30;

    LayerHistory() = default;
    ~LayerHistory() { Clear(); }

    LayerHistory(const LayerHistory&) = delete;
    LayerHistory& operator=(const LayerHistory&) = delete;

    void Clear();
    void Push(const LayerStack& stack);
    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo(LayerStack& stack);
    bool Redo(LayerStack& stack);

private:
    std::vector<LayerStack*> undoStack_;
    std::vector<LayerStack*> redoStack_;

    static void FreeStack(std::vector<LayerStack*>& stack);
};
