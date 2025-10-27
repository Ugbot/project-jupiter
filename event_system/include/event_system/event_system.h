#pragma once

#ifdef _WIN32
    #ifdef EVENT_SYSTEM_EXPORTS
        #define EVENT_SYSTEM_API __declspec(dllexport)
    #elif defined(EVENT_SYSTEM_IMPORTS)
        #define EVENT_SYSTEM_API __declspec(dllimport)
    #else
        #define EVENT_SYSTEM_API
    #endif
#else
    #define EVENT_SYSTEM_API
#endif

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>

namespace jupiter {
namespace event_system {

/**
 * @brief Base event class
 */
class EVENT_SYSTEM_API Event {
public:
    virtual ~Event() = default;

    /**
     * @brief Get event type name
     * @return String identifier for the event type
     */
    virtual const char* getTypeName() const = 0;

    /**
     * @brief Get event category
     * @return Event category string
     */
    virtual const char* getCategory() const { return "default"; }
};

/**
 * @brief Event listener interface
 */
class EVENT_SYSTEM_API IEventListener {
public:
    virtual ~IEventListener() = default;

    /**
     * @brief Called when an event is fired
     * @param event The event that was fired
     */
    virtual void onEvent(const Event& event) = 0;
};

/**
 * @brief Event dispatcher for managing event routing
 */
class EVENT_SYSTEM_API EventDispatcher {
public:
    EventDispatcher();
    ~EventDispatcher();

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    /**
     * @brief Register an event listener
     * @param listener The listener to register
     * @param eventTypeName The event type to listen for (empty for all events)
     * @param category The event category to listen for (empty for all categories)
     */
    void addListener(IEventListener* listener,
                     const std::string& eventTypeName = "",
                     const std::string& category = "");

    /**
     * @brief Unregister an event listener
     * @param listener The listener to unregister
     * @param eventTypeName The event type to stop listening for
     * @param category The event category to stop listening for
     */
    void removeListener(IEventListener* listener,
                        const std::string& eventTypeName = "",
                        const std::string& category = "");

    /**
     * @brief Fire an event immediately (synchronous)
     * @param event The event to fire
     */
    void fireEvent(const Event& event);

    /**
     * @brief Queue an event for later processing (asynchronous)
     * @param event The event to queue (takes ownership)
     */
    void queueEvent(std::unique_ptr<Event> event);

    /**
     * @brief Process all queued events
     */
    void processQueuedEvents();

    /**
     * @brief Clear all queued events without processing them
     */
    void clearQueuedEvents();

private:
    struct ListenerInfo {
        IEventListener* listener;
        std::string eventTypeName;
        std::string category;
    };

    std::vector<ListenerInfo> m_listeners;
    std::queue<std::unique_ptr<Event>> m_eventQueue;
};

/**
 * @brief Event manager singleton for global event handling
 */
class EVENT_SYSTEM_API EventManager {
public:
    static EventManager& getInstance();

    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    /**
     * @brief Get the global event dispatcher
     * @return Reference to the global dispatcher
     */
    EventDispatcher& getDispatcher();

    /**
     * @brief Initialize the event manager
     * @return true if initialization was successful
     */
    static bool initialize();

    /**
     * @brief Shutdown the event manager
     */
    static void shutdown();

private:
    EventManager();
    ~EventManager();

    std::unique_ptr<EventDispatcher> m_dispatcher;
};

// Convenience macros for event handling
#define EVENT_TYPE_DECL(type) \
    static const char* getStaticTypeName() { return #type; } \
    virtual const char* getTypeName() const override { return #type; }

#define EVENT_TYPE(type) \
    EVENT_TYPE_DECL(type)

// Common event types
class EVENT_SYSTEM_API WindowResizeEvent : public Event {
public:
    WindowResizeEvent(int width, int height) : m_width(width), m_height(height) {}

    EVENT_TYPE(WindowResizeEvent)

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    int m_width;
    int m_height;
};

class EVENT_SYSTEM_API KeyEvent : public Event {
public:
    enum KeyAction { PRESSED, RELEASED, REPEAT };

    KeyEvent(int keyCode, KeyAction action) : m_keyCode(keyCode), m_action(action) {}

    EVENT_TYPE(KeyEvent)

    int getKeyCode() const { return m_keyCode; }
    KeyAction getAction() const { return m_action; }

private:
    int m_keyCode;
    KeyAction m_action;
};

class EVENT_SYSTEM_API MouseEvent : public Event {
public:
    enum MouseAction { PRESSED, RELEASED, MOVED };

    MouseEvent(int button, MouseAction action, float x, float y)
        : m_button(button), m_action(action), m_x(x), m_y(y) {}

    EVENT_TYPE(MouseEvent)

    int getButton() const { return m_button; }
    MouseAction getAction() const { return m_action; }
    float getX() const { return m_x; }
    float getY() const { return m_y; }

private:
    int m_button;
    MouseAction m_action;
    float m_x;
    float m_y;
};

class EVENT_SYSTEM_API ApplicationEvent : public Event {
public:
    enum AppEventType { START, UPDATE, RENDER, SHUTDOWN };

    ApplicationEvent(AppEventType type, float deltaTime = 0.0f)
        : m_type(type), m_deltaTime(deltaTime) {}

    EVENT_TYPE(ApplicationEvent)

    AppEventType getEventType() const { return m_type; }
    float getDeltaTime() const { return m_deltaTime; }

private:
    AppEventType m_type;
    float m_deltaTime;
};

/**
 * @brief Initialize the event system
 * @return true if initialization was successful, false otherwise
 */
EVENT_SYSTEM_API bool initialize();

/**
 * @brief Shutdown the event system
 */
EVENT_SYSTEM_API void shutdown();

} // namespace event_system
} // namespace jupiter
