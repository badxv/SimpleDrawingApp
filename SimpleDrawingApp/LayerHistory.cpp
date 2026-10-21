#include "LayerHistory.h"

void LayerHistory::Clear() {
    FreeStack(undoStack_);
    FreeStack(redoStack_);
    maxDepth_ = kMaxDepth;
}

void LayerHistory::FreeStack(std::deque<std::unique_ptr<LayerStack>>& stack) {
    stack.clear();
}

void LayerHistory::TrimFront(std::deque<std::unique_ptr<LayerStack>>& stack, size_t maxDepth) {
    while (stack.size() > maxDepth) {
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

std::unique_ptr<LayerStack> LayerHistory::Capture(const LayerStack& stack) {
    return std::unique_ptr<LayerStack>(stack.Clone());
}

void LayerHistory::Commit(std::unique_ptr<LayerStack> snapshot) {
    if (!snapshot) return;
    RememberDepthFor(*snapshot);
    undoStack_.push_back(std::move(snapshot));
    TrimFront(undoStack_, maxDepth_);
    FreeStack(redoStack_);
}

void LayerHistory::Push(const LayerStack& stack) {
    Commit(Capture(stack));
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

    // Must be able to park the current stack on redo before mutating undo.
    std::unique_ptr<LayerStack> current(stack.Clone());
    if (!current) return false;

    std::unique_ptr<LayerStack> previous = std::move(undoStack_.back());
    undoStack_.pop_back();

    stack.TakeFrom(previous.get());

    redoStack_.push_back(std::move(current));
    TrimFront(redoStack_, maxDepth_);
    return true;
}

bool LayerHistory::Redo(LayerStack& stack) {
    if (redoStack_.empty()) return false;

    RememberDepthFor(stack);

    std::unique_ptr<LayerStack> current(stack.Clone());
    if (!current) return false;

    std::unique_ptr<LayerStack> next = std::move(redoStack_.back());
    redoStack_.pop_back();

    stack.TakeFrom(next.get());

    undoStack_.push_back(std::move(current));
    TrimFront(undoStack_, maxDepth_);
    return true;
}
