#include "EventBus.h"
#include "AppState.h"
#include "AppMetrics.h"
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

void RequestChromeRebuild(HWND hwnd, unsigned int delayMs) {
    if (hwnd) {
        SetTimer(hwnd, IDT_CHROME_REBUILD, delayMs, NULL);
    }
    EventPayload payload{};
    payload.type = AtelierEvent::ChromeRebuildRequested;
    payload.hwnd = hwnd;
    AppEventBus().Publish(payload);
}

void PublishLayerListChanged(HWND hwnd) {
    EventPayload payload{};
    payload.type = AtelierEvent::LayerListChanged;
    payload.hwnd = hwnd;
    payload.activeLayerIndex = gLayers.ActiveIndex();
    payload.layerCount = gLayers.Count();
    AppEventBus().Publish(payload);
}

namespace {

HWND ResolveMainHwnd(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) return hwnd;
    if (hwndViewport && IsWindow(hwndViewport)) {
        return GetAncestor(hwndViewport, GA_ROOT);
    }
    if (hwndToolButtons[0] && IsWindow(hwndToolButtons[0])) {
        return GetAncestor(hwndToolButtons[0], GA_ROOT);
    }
    if (hwndLayerList && IsWindow(hwndLayerList)) {
        return GetAncestor(hwndLayerList, GA_ROOT);
    }
    return nullptr;
}

} // namespace

void InitAppEventHandlers() {
    if (gHandlersInstalled) return;
    gHandlersInstalled = true;

    AppEventBus().Subscribe(AtelierEvent::DocumentDirtyChanged, [](const EventPayload& e) {
        HWND hwnd = ResolveMainHwnd(e.hwnd);
        if (!hwnd) return;
        UpdateWindowTitle(hwnd);
        UpdateStatusBar(hwnd);
    });

    AppEventBus().Subscribe(AtelierEvent::ChromeRebuildRequested, [](const EventPayload& e) {
        if (hwndBrand) {
            InvalidateRect(hwndBrand, NULL, FALSE);
        }
        if (HWND hwnd = ResolveMainHwnd(e.hwnd)) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
    });

    AppEventBus().Subscribe(AtelierEvent::ActiveToolChanged, [](const EventPayload& e) {
        if (HWND hwnd = ResolveMainHwnd(e.hwnd)) {
            UpdateStatusBar(hwnd);
        }
    });

    AppEventBus().Subscribe(AtelierEvent::LayerListChanged, [](const EventPayload& e) {
        if (HWND hwnd = ResolveMainHwnd(e.hwnd)) {
            UpdateStatusBar(hwnd);
        }
    });

    AppEventBus().Subscribe(AtelierEvent::SelectionChanged, [](const EventPayload& e) {
        if (HWND hwnd = ResolveMainHwnd(e.hwnd)) {
            UpdateStatusBar(hwnd);
        }
    });
}

void ShutdownAppEventHandlers() {
    AppEventBus().Clear();
    gHandlersInstalled = false;
}
