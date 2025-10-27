#include "ui/ui.h"
#include <iostream>
#include <algorithm>

namespace jupiter {
namespace ui {

// Forward declare math types to avoid circular includes
using math::Vector2;
using math::Vector4;

// UIElement implementation
void UIElement::setPosition(const Vector2& position) {
    m_position = position;
}

Vector2 UIElement::getPosition() const {
    return m_position;
}

void UIElement::setSize(const Vector2& size) {
    m_size = size;
}

Vector2 UIElement::getSize() const {
    return m_size;
}

bool UIElement::containsPoint(const Vector2& point) const {
    return point.x >= m_position.x &&
           point.x <= m_position.x + m_size.x &&
           point.y >= m_position.y &&
           point.y <= m_position.y + m_size.y;
}

void UIElement::setVisible(bool visible) {
    m_visible = visible;
}

bool UIElement::isVisible() const {
    return m_visible;
}

// Button implementation
Button::Button() : m_pressed(false), m_hovered(false) {}

void Button::setText(const std::string& text) {
    m_text = text;
}

const std::string& Button::getText() const {
    return m_text;
}

void Button::setOnClickCallback(std::function<void()> callback) {
    m_onClickCallback = callback;
}

void Button::update(float deltaTime) {
    (void)deltaTime; // Placeholder implementation
}

void Button::render() {
    if (!m_visible) return;

    std::cout << "Rendering Button: '" << m_text << "' at ("
              << m_position.x << ", " << m_position.y << ") size ("
              << m_size.x << ", " << m_size.y << ")" << std::endl;
}

bool Button::handleInput(const void* event) {
    (void)event; // Placeholder implementation
    // Simulate click handling
    if (m_onClickCallback) {
        m_onClickCallback();
        return true;
    }
    return false;
}

// Label implementation
Label::Label() : m_textColor(1.0f, 1.0f, 1.0f, 1.0f) {}

void Label::setText(const std::string& text) {
    m_text = text;
}

const std::string& Label::getText() const {
    return m_text;
}

void Label::setTextColor(const Vector4& color) {
    m_textColor = color;
}

Vector4 Label::getTextColor() const {
    return m_textColor;
}

void Label::update(float deltaTime) {
    (void)deltaTime; // Placeholder implementation
}

void Label::render() {
    if (!m_visible) return;

    std::cout << "Rendering Label: '" << m_text << "' at ("
              << m_position.x << ", " << m_position.y << ")" << std::endl;
}

// TextField implementation
TextField::TextField() : m_focused(false), m_cursorPosition(0) {}

void TextField::setText(const std::string& text) {
    m_text = text;
    m_cursorPosition = std::min(m_cursorPosition, m_text.size());
}

const std::string& TextField::getText() const {
    return m_text;
}

void TextField::setPlaceholder(const std::string& placeholder) {
    m_placeholder = placeholder;
}

const std::string& TextField::getPlaceholder() const {
    return m_placeholder;
}

void TextField::setOnTextChangedCallback(std::function<void(const std::string&)> callback) {
    m_onTextChangedCallback = callback;
}

void TextField::update(float deltaTime) {
    (void)deltaTime; // Placeholder implementation
}

void TextField::render() {
    if (!m_visible) return;

    const std::string& displayText = m_text.empty() ? m_placeholder : m_text;
    std::cout << "Rendering TextField: '" << displayText << "' at ("
              << m_position.x << ", " << m_position.y << ") size ("
              << m_size.x << ", " << m_size.y << ")" << std::endl;
}

bool TextField::handleInput(const void* event) {
    (void)event; // Placeholder implementation
    // Simulate text input handling
    if (m_focused && m_onTextChangedCallback) {
        m_onTextChangedCallback(m_text);
        return true;
    }
    return false;
}

// Panel implementation
Panel::Panel() : m_backgroundColor(0.0f, 0.0f, 0.0f, 0.5f) {}

void Panel::addChild(std::unique_ptr<UIElement> element) {
    if (element) {
        m_children.push_back(std::move(element));
    }
}

void Panel::removeChild(UIElement* element) {
    if (!element) return;

    m_children.erase(
        std::remove_if(m_children.begin(), m_children.end(),
            [element](const std::unique_ptr<UIElement>& child) {
                return child.get() == element;
            }),
        m_children.end()
    );
}

void Panel::clearChildren() {
    m_children.clear();
}

const std::vector<std::unique_ptr<UIElement>>& Panel::getChildren() const {
    return m_children;
}

void Panel::setBackgroundColor(const Vector4& color) {
    m_backgroundColor = color;
}

Vector4 Panel::getBackgroundColor() const {
    return m_backgroundColor;
}

void Panel::update(float deltaTime) {
    if (!m_visible) return;

    for (auto& child : m_children) {
        child->update(deltaTime);
    }
}

void Panel::render() {
    if (!m_visible) return;

    std::cout << "Rendering Panel with " << m_children.size() << " children at ("
              << m_position.x << ", " << m_position.y << ") size ("
              << m_size.x << ", " << m_size.y << ")" << std::endl;

    for (auto& child : m_children) {
        child->render();
    }
}

bool Panel::handleInput(const void* event) {
    if (!m_visible) return false;

    // Handle children input (reverse order for proper layering)
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if ((*it)->handleInput(event)) {
            return true;
        }
    }

    return false;
}

// UIManager implementation
UIManager::UIManager() = default;

UIManager::~UIManager() = default;

void UIManager::addElement(std::unique_ptr<UIElement> element) {
    if (element) {
        m_rootElements.push_back(std::move(element));
    }
}

void UIManager::removeElement(UIElement* element) {
    if (!element) return;

    m_rootElements.erase(
        std::remove_if(m_rootElements.begin(), m_rootElements.end(),
            [element](const std::unique_ptr<UIElement>& elem) {
                return elem.get() == element;
            }),
        m_rootElements.end()
    );
}

void UIManager::clearElements() {
    m_rootElements.clear();
}

void UIManager::update(float deltaTime) {
    for (auto& element : m_rootElements) {
        element->update(deltaTime);
    }
}

void UIManager::render() {
    for (auto& element : m_rootElements) {
        element->render();
    }
}

bool UIManager::handleInput(const void* event) {
    // Handle root elements input (reverse order for proper layering)
    for (auto it = m_rootElements.rbegin(); it != m_rootElements.rend(); ++it) {
        if ((*it)->handleInput(event)) {
            return true;
        }
    }

    return false;
}

void UIManager::setViewportSize(const Vector2& size) {
    m_viewportSize = size;
}

Vector2 UIManager::getViewportSize() const {
    return m_viewportSize;
}

// Global functions
static UIManager* s_uiManager = nullptr;

bool initialize() {
    if (s_uiManager) {
        return true;
    }

    s_uiManager = new UIManager();
    std::cout << "UI subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    if (s_uiManager) {
        delete s_uiManager;
        s_uiManager = nullptr;
        std::cout << "UI subsystem shutdown" << std::endl;
    }
}

UIManager& getUIManager() {
    if (!s_uiManager) {
        initialize();
    }
    return *s_uiManager;
}

} // namespace ui
} // namespace jupiter
