#pragma once

#ifdef _WIN32
    #ifdef UI_EXPORTS
        #define UI_API __declspec(dllexport)
    #elif defined(UI_IMPORTS)
        #define UI_API __declspec(dllimport)
    #else
        #define UI_API
    #endif
#else
    #define UI_API
#endif

#include <string>
#include <memory>
#include <vector>
#include <functional>

// Include math types for UI
#include "../../math/include/math/math.h"

namespace jupiter {

namespace ui {

/**
 * @brief UI element base class
 */
class UI_API UIElement {
public:
    virtual ~UIElement() = default;

    /**
     * @brief Update the element
     * @param deltaTime Time since last update
     */
    virtual void update(float deltaTime) {}

    /**
     * @brief Render the element
     */
    virtual void render() {}

    /**
     * @brief Handle input event
     * @param event Input event data
     * @return true if event was handled
     */
    virtual bool handleInput(const void* event) { (void)event; return false; }

    /**
     * @brief Set element position
     * @param position New position
     */
    virtual void setPosition(const math::Vector2& position);

    /**
     * @brief Get element position
     * @return Current position
     */
    virtual math::Vector2 getPosition() const;

    /**
     * @brief Set element size
     * @param size New size
     */
    virtual void setSize(const math::Vector2& size);

    /**
     * @brief Get element size
     * @return Current size
     */
    virtual math::Vector2 getSize() const;

    /**
     * @brief Check if point is inside element bounds
     * @param point Point to check
     * @return true if point is inside
     */
    virtual bool containsPoint(const math::Vector2& point) const;

    /**
     * @brief Set element visibility
     * @param visible true to show, false to hide
     */
    virtual void setVisible(bool visible);

    /**
     * @brief Check if element is visible
     * @return true if visible
     */
    virtual bool isVisible() const;

protected:
    math::Vector2 m_position;
    math::Vector2 m_size;
    bool m_visible;
};

/**
 * @brief Button UI element
 */
class UI_API Button : public UIElement {
public:
    Button();
    ~Button() override = default;

    /**
     * @brief Set button text
     * @param text Button text
     */
    void setText(const std::string& text);

    /**
     * @brief Get button text
     * @return Button text
     */
    const std::string& getText() const;

    /**
     * @brief Set click callback
     * @param callback Function to call when button is clicked
     */
    void setOnClickCallback(std::function<void()> callback);

    void update(float deltaTime) override;
    void render() override;
    bool handleInput(const void* event) override;

private:
    std::string m_text;
    std::function<void()> m_onClickCallback;
    bool m_pressed;
    bool m_hovered;
};

/**
 * @brief Label UI element
 */
class UI_API Label : public UIElement {
public:
    Label();
    ~Label() override = default;

    /**
     * @brief Set label text
     * @param text Label text
     */
    void setText(const std::string& text);

    /**
     * @brief Get label text
     * @return Label text
     */
    const std::string& getText() const;

    /**
     * @brief Set text color
     * @param color Text color (RGBA)
     */
    void setTextColor(const math::Vector4& color);

    /**
     * @brief Get text color
     * @return Text color
     */
    math::Vector4 getTextColor() const;

    void update(float deltaTime) override;
    void render() override;

private:
    std::string m_text;
    math::Vector4 m_textColor;
};

/**
 * @brief Text input field UI element
 */
class UI_API TextField : public UIElement {
public:
    TextField();
    ~TextField() override = default;

    /**
     * @brief Set field text
     * @param text Field text
     */
    void setText(const std::string& text);

    /**
     * @brief Get field text
     * @return Field text
     */
    const std::string& getText() const;

    /**
     * @brief Set placeholder text
     * @param placeholder Placeholder text
     */
    void setPlaceholder(const std::string& placeholder);

    /**
     * @brief Get placeholder text
     * @return Placeholder text
     */
    const std::string& getPlaceholder() const;

    /**
     * @brief Set text change callback
     * @param callback Function to call when text changes
     */
    void setOnTextChangedCallback(std::function<void(const std::string&)> callback);

    void update(float deltaTime) override;
    void render() override;
    bool handleInput(const void* event) override;

private:
    std::string m_text;
    std::string m_placeholder;
    std::function<void(const std::string&)> m_onTextChangedCallback;
    bool m_focused;
    size_t m_cursorPosition;
};

/**
 * @brief Panel container UI element
 */
class UI_API Panel : public UIElement {
public:
    Panel();
    ~Panel() override = default;

    /**
     * @brief Add child element
     * @param element Element to add
     */
    void addChild(std::unique_ptr<UIElement> element);

    /**
     * @brief Remove child element
     * @param element Element to remove
     */
    void removeChild(UIElement* element);

    /**
     * @brief Remove all children
     */
    void clearChildren();

    /**
     * @brief Get child elements
     * @return Vector of child elements
     */
    const std::vector<std::unique_ptr<UIElement>>& getChildren() const;

    /**
     * @brief Set background color
     * @param color Background color (RGBA)
     */
    void setBackgroundColor(const math::Vector4& color);

    /**
     * @brief Get background color
     * @return Background color
     */
    math::Vector4 getBackgroundColor() const;

    void update(float deltaTime) override;
    void render() override;
    bool handleInput(const void* event) override;

private:
    std::vector<std::unique_ptr<UIElement>> m_children;
    math::Vector4 m_backgroundColor;
};

/**
 * @brief UI manager for handling UI elements and input
 */
class UI_API UIManager {
public:
    UIManager();
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    /**
     * @brief Add root UI element
     * @param element Element to add
     */
    void addElement(std::unique_ptr<UIElement> element);

    /**
     * @brief Remove root UI element
     * @param element Element to remove
     */
    void removeElement(UIElement* element);

    /**
     * @brief Clear all root elements
     */
    void clearElements();

    /**
     * @brief Update all UI elements
     * @param deltaTime Time since last update
     */
    void update(float deltaTime);

    /**
     * @brief Render all UI elements
     */
    void render();

    /**
     * @brief Handle input event
     * @param event Input event data
     * @return true if event was handled by UI
     */
    bool handleInput(const void* event);

    /**
     * @brief Set UI viewport size
     * @param size Viewport size
     */
    void setViewportSize(const math::Vector2& size);

    /**
     * @brief Get UI viewport size
     * @return Viewport size
     */
    math::Vector2 getViewportSize() const;

private:
    std::vector<std::unique_ptr<UIElement>> m_rootElements;
    math::Vector2 m_viewportSize;
};

/**
 * @brief Initialize the UI subsystem
 * @return true if initialization was successful, false otherwise
 */
UI_API bool initialize();

/**
 * @brief Shutdown the UI subsystem
 */
UI_API void shutdown();

/**
 * @brief Get the global UI manager
 * @return Reference to the global UI manager
 */
UI_API UIManager& getUIManager();

} // namespace ui
} // namespace jupiter
