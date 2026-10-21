#pragma once

#include "LayerStack.h"
#include <cstddef>
#include <deque>
#include <memory>

// Snapshot-based undo/redo for the full layer stack.
class LayerHistory {
public:
    // Soft cap on retained undo (and redo) snapshots. Large canvases use a
    // smaller effective depth so memory stays bounded (see EffectiveMaxDepth).
    static constexpr size_t kMaxDepth = 30;
    static constexpr size_t kMinDepth = 8;

    LayerHistory() = default;
    ~LayerHistory() { Clear(); }

    LayerHistory(const LayerHistory&) = delete;
    LayerHistory& operator=(const LayerHistory&) = delete;

    void Clear();
    // Snapshot `stack` now (pre-mutation). Null on OOM.
    static std::unique_ptr<LayerStack> Capture(const LayerStack& stack);
    // Takes ownership; no-op if snapshot is null. Clears the redo stack.
    void Commit(std::unique_ptr<LayerStack> snapshot);
    // Convenience: Capture + Commit in one step (use only when the mutation
    // cannot fail after this call, e.g. stroke begin / clear canvas).
    void Push(const LayerStack& stack);

    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo(LayerStack& stack);
    bool Redo(LayerStack& stack);

    size_t UndoCount() const { return undoStack_.size(); }
    size_t RedoCount() const { return redoStack_.size(); }
    static size_t EffectiveMaxDepth(int docWidth, int docHeight);

private:
    std::deque<std::unique_ptr<LayerStack>> undoStack_;
    std::deque<std::unique_ptr<LayerStack>> redoStack_;

    size_t maxDepth_ = kMaxDepth;

    static void FreeStack(std::deque<std::unique_ptr<LayerStack>>& stack);
    static void TrimFront(std::deque<std::unique_ptr<LayerStack>>& stack, size_t maxDepth);
    void RememberDepthFor(const LayerStack& stack);
};
