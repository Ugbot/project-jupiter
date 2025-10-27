#include "event_system/event_system.h"
#include <algorithm>
#include <iostream>
#include <queue>

namespace jupiter {
namespace event_system {

// EventDispatcher implementation
EventDispatcher::EventDispatcher() = default;

EventDispatcher::~EventDispatcher() {
    clearQueuedEvents();
}

void EventDispatcher::addListener(IEventListener* listener,
                                  const std::string& eventTypeName,
                                  const std::string& category) {
    if (!listener) {
        return;
    }

    m_listeners.push_back({listener, eventTypeName, category});
}

void EventDispatcher::removeListener(IEventListener* listener,
                                     const std::string& eventTypeName,
                                     const std::string& category) {
    if (!listener) {
        return;
    }

    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [&](const ListenerInfo& info) {
                return info.listener == listener &&
                       (eventTypeName.empty() || info.eventTypeName == eventTypeName) &&
                       (category.empty() || info.category == category);
            }),
        m_listeners.end()
    );
}

void EventDispatcher::fireEvent(const Event& event) {
    for (const auto& listenerInfo : m_listeners) {
        // Check if this listener should receive this event
        if (!listenerInfo.eventTypeName.empty() &&
            listenerInfo.eventTypeName != event.getTypeName()) {
            continue;
        }

        if (!listenerInfo.category.empty() &&
            listenerInfo.category != event.getCategory()) {
            continue;
        }

        // Notify the listener
        listenerInfo.listener->onEvent(event);
    }
}

void EventDispatcher::queueEvent(std::unique_ptr<Event> event) {
    if (!event) {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void EventDispatcher::processQueuedEvents() {
    std::queue<std::unique_ptr<Event>> eventsToProcess;

    eventsToProcess.swap(m_eventQueue);

    while (!eventsToProcess.empty()) {
        fireEvent(*eventsToProcess.front());
        eventsToProcess.pop();
    }
}

void EventDispatcher::clearQueuedEvents() {
    while (!m_eventQueue.empty()) {
        m_eventQueue.pop();
    }
}

// EventManager implementation
EventManager::EventManager() : m_dispatcher(std::make_unique<EventDispatcher>()) {}

EventManager::~EventManager() = default;

EventManager& EventManager::getInstance() {
    static EventManager instance;
    return instance;
}

EventDispatcher& EventManager::getDispatcher() {
    return *m_dispatcher;
}

bool EventManager::initialize() {
    // Nothing to initialize for now
    return true;
}

void EventManager::shutdown() {
    if (auto& instance = getInstance(); instance.m_dispatcher) {
        instance.m_dispatcher->clearQueuedEvents();
    }
}

// Global functions
bool initialize() {
    return EventManager::initialize();
}

void shutdown() {
    EventManager::shutdown();
}

} // namespace event_system
} // namespace jupiter
