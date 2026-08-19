#pragma once

#include "AtelierEvents.h"
#include <functional>
#include <vector>

class EventBus {
public:
    using Handler = std::function<void(const EventPayload&)>;

    void Subscribe(AtelierEvent event, Handler handler);
    void Publish(const EventPayload& payload) const;
    void Clear();

private:
    std::vector<Handler> handlers_[static_cast<size_t>(AtelierEvent::Count)];
};

EventBus& AppEventBus();
void InitAppEventHandlers();
void ShutdownAppEventHandlers();
