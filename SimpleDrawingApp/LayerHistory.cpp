#include "LayerHistory.h"

void LayerHistory::Clear() {
    FreeStack(undoStack_);
    FreeStack(redoStack_);
    maxDepth_ = kMaxDepth;
}

void LayerHistory::FreeStack(std::deque<LayerStack*>& stack) {
    for (LayerStack* snap : stack) {
        delete snap;
    }
    stack.clear();
}

void LayerHistory::TrimFront(std::deque<LayerStack*>& stack, size_t maxDepth) {
    while (stack.size() > maxDepth) {
        delete stack.front();
        stack.pop_front();
    }
}

size_t LayerHistory::EffectiveMaxDepth(int docWidth, int docHeight) {
    if (docWidth < 1 || docHeight < 1) return kMaxDepth;
    const long long pixels = static_cast<long long>(docWidth) * static_cast<long long>(docHeight);
    // Full layer-stack clones are large; shrink history for bigger canvases.
    if (pixels >= 8LL * 1024 * 1024) return kMinDepth;          // >= 8 MP
    if (pixels >= 2LL * 1024 * 1024) return (kMaxDepth * 2) / 3; // >= 2 MP → 20
    return kMaxDepth;
}

void LayerHistory::RememberDepthFor(const LayerStack& stack) {
    maxDepth_ = EffectiveMaxDepth(stack.Width(), stack.Height());
    if (maxDepth_ < kMinDepth) maxDepth_ = kMinDepth;
    if (maxDepth_ > kMaxDepth) maxDepth_ = kMaxDepth;
}

void LayerHistory::Push(const LayerStack& stack) {
    RememberDepthFor(stack);

    LayerStack* snapshot = stack.Clone();
    if (!snapshot) return;

    undoStack_.push_back(snapshot);
    TrimFront(undoStack_, maxDepth_);
    FreeStack(redoStack_);
}

bool LayerHistory::CanUndo() const {
    return !undoStack_.empty();
}

bool LayerHistory::CanRedo() const {
    return !redoStack_.empty();
}

bool LayerHistory::Undo(LayerStack& stack) {
    if (undoStack_.empty()) return false;

    RememberDepthFor(stack);

    LayerStack* current = stack.Clone();
    LayerStack* previous = undoStack_.back();
    undoStack_.pop_back();

    stack.TakeFrom(previous);
    delete previous;

    if (current) {
        redoStack_.push_back(current);
        TrimFront(redoStack_, maxDepth_);
    }
    return true;
}

bool LayerHistory::Redo(LayerStack& stack) {
    if (redoStack_.empty()) return false;

    RememberDepthFor(stack);

    LayerStack* current = stack.Clone();
    LayerStack* next = redoStack_.back();
    redoStack_.pop_back();

    stack.TakeFrom(next);
    delete next;

    if (current) {
        undoStack_.push_back(current);
        TrimFront(undoStack_, maxDepth_);
    }
    return true;
}
