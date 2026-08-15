#include "LayerHistory.h"

void LayerHistory::Clear() {
    FreeStack(undoStack_);
    FreeStack(redoStack_);
}

void LayerHistory::FreeStack(std::vector<LayerStack*>& stack) {
    for (LayerStack* snap : stack) {
        delete snap;
    }
    stack.clear();
}

void LayerHistory::Push(const LayerStack& stack) {
    LayerStack* snapshot = stack.Clone();
    if (!snapshot) return;

    undoStack_.push_back(snapshot);
    if (undoStack_.size() > kMaxDepth) {
        delete undoStack_.front();
        undoStack_.erase(undoStack_.begin());
    }
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

    LayerStack* current = stack.Clone();
    LayerStack* previous = undoStack_.back();
    undoStack_.pop_back();

    stack.TakeFrom(previous);
    delete previous;

    if (current) {
        redoStack_.push_back(current);
    }
    return true;
}

bool LayerHistory::Redo(LayerStack& stack) {
    if (redoStack_.empty()) return false;

    LayerStack* current = stack.Clone();
    LayerStack* next = redoStack_.back();
    redoStack_.pop_back();

    stack.TakeFrom(next);
    delete next;

    if (current) {
        undoStack_.push_back(current);
    }
    return true;
}
