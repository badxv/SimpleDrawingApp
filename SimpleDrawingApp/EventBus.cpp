#include "EventBus.h"
#include "SimpleDrawingApp.h"

namespace {
EventBus gBus;
bool gHandlersInstalled = false;
}

EventBus& AppEventBus() {
    return gBus;
}

void EventBus::Subscribe(AtelierEvent event, Handler handler) {
    const size_t idx = static_cast<size_t>(event);
    if (idx >= static_cast<size_t>(AtelierEvent::Count)) return;
    handlers_[idx].push_back(std::move(handler));
}

void EventBus::Publish(const EventPayload& payload) const {
    const size_t idx = static_cast<size_t>(payload.type);
    if (idx >= static_cast<size_t>(AtelierEvent::Count)) return;
    for (const Handler& handler : handlers_[idx]) {
        if (handler) handler(payload);
    }
}

void EventBus::Clear() {
    for (auto& list : handlers_) {
        list.clear();
    }
}

void InitAppEventHandlers() {
    if (gHandlersInstalled) return;
    gHandlersInstalled = true;

    AppEventBus().Subscribe(AtelierEvent::DocumentDirtyChanged, [](const EventPayload& e) {
        if (!e.hwnd) return;
        UpdateWindowTitle(e.hwnd);
        UpdateStatusBar(e.hwnd);
    });
}

void ShutdownAppEventHandlers() {
    AppEventBus().Clear();
    gHandlersInstalled = false;
}
