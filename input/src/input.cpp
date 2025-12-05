/**
 * @file input.cpp
 * @brief Jupiter Engine Input System Implementation
 */

#include "input/input.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>

namespace jupiter {
namespace input {

// ============================================================================
// Input Code Names (for display and serialization)
// ============================================================================

static const char* INPUT_CODE_NAMES[] = {
    // These indices must match InputCode enum values
    // We'll use a function to look these up since enum values are sparse
};

const char* InputManager::getInputCodeName(InputCode code) {
    switch (code) {
        case InputCode::None: return "None";
        
        // Letters
        case InputCode::KeyA: return "A";
        case InputCode::KeyB: return "B";
        case InputCode::KeyC: return "C";
        case InputCode::KeyD: return "D";
        case InputCode::KeyE: return "E";
        case InputCode::KeyF: return "F";
        case InputCode::KeyG: return "G";
        case InputCode::KeyH: return "H";
        case InputCode::KeyI: return "I";
        case InputCode::KeyJ: return "J";
        case InputCode::KeyK: return "K";
        case InputCode::KeyL: return "L";
        case InputCode::KeyM: return "M";
        case InputCode::KeyN: return "N";
        case InputCode::KeyO: return "O";
        case InputCode::KeyP: return "P";
        case InputCode::KeyQ: return "Q";
        case InputCode::KeyR: return "R";
        case InputCode::KeyS: return "S";
        case InputCode::KeyT: return "T";
        case InputCode::KeyU: return "U";
        case InputCode::KeyV: return "V";
        case InputCode::KeyW: return "W";
        case InputCode::KeyX: return "X";
        case InputCode::KeyY: return "Y";
        case InputCode::KeyZ: return "Z";
        
        // Numbers
        case InputCode::Key1: return "1";
        case InputCode::Key2: return "2";
        case InputCode::Key3: return "3";
        case InputCode::Key4: return "4";
        case InputCode::Key5: return "5";
        case InputCode::Key6: return "6";
        case InputCode::Key7: return "7";
        case InputCode::Key8: return "8";
        case InputCode::Key9: return "9";
        case InputCode::Key0: return "0";
        
        // Function keys
        case InputCode::KeyF1: return "F1";
        case InputCode::KeyF2: return "F2";
        case InputCode::KeyF3: return "F3";
        case InputCode::KeyF4: return "F4";
        case InputCode::KeyF5: return "F5";
        case InputCode::KeyF6: return "F6";
        case InputCode::KeyF7: return "F7";
        case InputCode::KeyF8: return "F8";
        case InputCode::KeyF9: return "F9";
        case InputCode::KeyF10: return "F10";
        case InputCode::KeyF11: return "F11";
        case InputCode::KeyF12: return "F12";
        
        // Special keys
        case InputCode::KeyReturn: return "Enter";
        case InputCode::KeyEscape: return "Escape";
        case InputCode::KeyBackspace: return "Backspace";
        case InputCode::KeyTab: return "Tab";
        case InputCode::KeySpace: return "Space";
        case InputCode::KeyMinus: return "-";
        case InputCode::KeyEquals: return "=";
        case InputCode::KeyLeftBracket: return "[";
        case InputCode::KeyRightBracket: return "]";
        case InputCode::KeyBackslash: return "\\";
        case InputCode::KeySemicolon: return ";";
        case InputCode::KeyApostrophe: return "'";
        case InputCode::KeyGrave: return "`";
        case InputCode::KeyComma: return ",";
        case InputCode::KeyPeriod: return ".";
        case InputCode::KeySlash: return "/";
        case InputCode::KeyCapsLock: return "CapsLock";
        
        // Navigation
        case InputCode::KeyInsert: return "Insert";
        case InputCode::KeyHome: return "Home";
        case InputCode::KeyPageUp: return "PageUp";
        case InputCode::KeyDelete: return "Delete";
        case InputCode::KeyEnd: return "End";
        case InputCode::KeyPageDown: return "PageDown";
        case InputCode::KeyRight: return "Right";
        case InputCode::KeyLeft: return "Left";
        case InputCode::KeyDown: return "Down";
        case InputCode::KeyUp: return "Up";
        
        // Modifiers
        case InputCode::KeyLeftCtrl: return "LeftCtrl";
        case InputCode::KeyLeftShift: return "LeftShift";
        case InputCode::KeyLeftAlt: return "LeftAlt";
        case InputCode::KeyLeftGui: return "LeftSuper";
        case InputCode::KeyRightCtrl: return "RightCtrl";
        case InputCode::KeyRightShift: return "RightShift";
        case InputCode::KeyRightAlt: return "RightAlt";
        case InputCode::KeyRightGui: return "RightSuper";
        
        // Mouse
        case InputCode::MouseLeft: return "MouseLeft";
        case InputCode::MouseRight: return "MouseRight";
        case InputCode::MouseMiddle: return "MouseMiddle";
        case InputCode::MouseX1: return "MouseX1";
        case InputCode::MouseX2: return "MouseX2";
        case InputCode::MouseMoveX: return "MouseX";
        case InputCode::MouseMoveY: return "MouseY";
        case InputCode::MouseWheelX: return "WheelX";
        case InputCode::MouseWheelY: return "WheelY";
        
        // Gamepad
        case InputCode::GamepadA: return "GamepadA";
        case InputCode::GamepadB: return "GamepadB";
        case InputCode::GamepadX: return "GamepadX";
        case InputCode::GamepadY: return "GamepadY";
        case InputCode::GamepadLeftBumper: return "LB";
        case InputCode::GamepadRightBumper: return "RB";
        case InputCode::GamepadBack: return "Back";
        case InputCode::GamepadStart: return "Start";
        case InputCode::GamepadGuide: return "Guide";
        case InputCode::GamepadLeftStick: return "LS";
        case InputCode::GamepadRightStick: return "RS";
        case InputCode::GamepadDPadUp: return "DPadUp";
        case InputCode::GamepadDPadRight: return "DPadRight";
        case InputCode::GamepadDPadDown: return "DPadDown";
        case InputCode::GamepadDPadLeft: return "DPadLeft";
        case InputCode::GamepadLeftX: return "LeftStickX";
        case InputCode::GamepadLeftY: return "LeftStickY";
        case InputCode::GamepadRightX: return "RightStickX";
        case InputCode::GamepadRightY: return "RightStickY";
        case InputCode::GamepadLeftTrigger: return "LT";
        case InputCode::GamepadRightTrigger: return "RT";
        
        default: return "Unknown";
    }
}

const char* InputManager::getActionName(Action action) {
    switch (action) {
        case Action::None: return "None";
        case Action::MoveForward: return "MoveForward";
        case Action::MoveBackward: return "MoveBackward";
        case Action::MoveLeft: return "MoveLeft";
        case Action::MoveRight: return "MoveRight";
        case Action::MoveUp: return "MoveUp";
        case Action::MoveDown: return "MoveDown";
        case Action::Sprint: return "Sprint";
        case Action::Crouch: return "Crouch";
        case Action::Jump: return "Jump";
        case Action::LookUp: return "LookUp";
        case Action::LookDown: return "LookDown";
        case Action::LookLeft: return "LookLeft";
        case Action::LookRight: return "LookRight";
        case Action::ZoomIn: return "ZoomIn";
        case Action::ZoomOut: return "ZoomOut";
        case Action::CameraReset: return "CameraReset";
        case Action::PrimaryAction: return "PrimaryAction";
        case Action::SecondaryAction: return "SecondaryAction";
        case Action::Interact: return "Interact";
        case Action::Reload: return "Reload";
        case Action::NextWeapon: return "NextWeapon";
        case Action::PrevWeapon: return "PrevWeapon";
        case Action::MenuToggle: return "MenuToggle";
        case Action::MenuUp: return "MenuUp";
        case Action::MenuDown: return "MenuDown";
        case Action::MenuLeft: return "MenuLeft";
        case Action::MenuRight: return "MenuRight";
        case Action::MenuSelect: return "MenuSelect";
        case Action::MenuBack: return "MenuBack";
        case Action::Inventory: return "Inventory";
        case Action::Map: return "Map";
        case Action::Pause: return "Pause";
        case Action::DebugToggle: return "DebugToggle";
        case Action::DebugConsole: return "DebugConsole";
        case Action::Screenshot: return "Screenshot";
        default: {
            // Custom actions
            static char buf[32];
            std::snprintf(buf, sizeof(buf), "Custom%d", static_cast<int>(action));
            return buf;
        }
    }
}

InputCode InputManager::fromSDLScancode(int scancode) {
    // SDL scancodes match our InputCode for keyboard keys
    if (scancode >= 0 && scancode < 256) {
        return static_cast<InputCode>(scancode);
    }
    return InputCode::None;
}

InputCode InputManager::fromSDLMouseButton(int button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return InputCode::MouseLeft;
        case SDL_BUTTON_RIGHT: return InputCode::MouseRight;
        case SDL_BUTTON_MIDDLE: return InputCode::MouseMiddle;
        case SDL_BUTTON_X1: return InputCode::MouseX1;
        case SDL_BUTTON_X2: return InputCode::MouseX2;
        default: return InputCode::None;
    }
}

// ============================================================================
// InputManager Singleton
// ============================================================================

InputManager& InputManager::get() {
    static InputManager instance;
    return instance;
}

// ============================================================================
// Initialization
// ============================================================================

bool InputManager::initialize() {
    if (initialized_) {
        return true;
    }

    // Clear all state
    for (size_t i = 0; i < inputStates_.size(); ++i) {
        inputStates_[i].store(0, std::memory_order_relaxed);
        prevInputStates_[i].store(0, std::memory_order_relaxed);
        axisValues_[i].store(0, std::memory_order_relaxed);
    }

    for (size_t i = 0; i < actionStates_.size(); ++i) {
        actionStates_[i].store(0, std::memory_order_relaxed);
    }

    for (size_t i = 0; i < actionAxisValues_.size(); ++i) {
        actionAxisValues_[i].store(0, std::memory_order_relaxed);
    }

    // Clear bindings
    for (auto& binding : bindings_) {
        binding = InputBinding{};
    }
    for (auto& binding : axisBindings_) {
        binding = AxisBinding{};
    }

    initialized_ = true;
    return true;
}

void InputManager::shutdown() {
    if (!initialized_) {
        return;
    }

    // Release mouse if captured
    setMouseCaptured(false);

    initialized_ = false;
}

// ============================================================================
// Update
// ============================================================================

void InputManager::update() {
    if (!initialized_) {
        return;
    }

    // Save previous input states for edge detection
    for (size_t i = 0; i < inputStates_.size(); ++i) {
        prevInputStates_[i].store(inputStates_[i].load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
    }

    // Reset per-frame values
    mouseDeltaX_.store(0, std::memory_order_relaxed);
    mouseDeltaY_.store(0, std::memory_order_relaxed);
    mouseWheelX_.store(0, std::memory_order_relaxed);
    mouseWheelY_.store(0, std::memory_order_relaxed);

    // Process SDL events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN: {
                if (!event.key.repeat) {
                    InputCode code = fromSDLScancode(event.key.scancode);
                    if (code != InputCode::None) {
                        inputStates_[static_cast<size_t>(code)].store(1, std::memory_order_relaxed);
                        
                        // Handle rebinding
                        if (rebinding_.load(std::memory_order_acquire)) {
                            InputCode oldCode = bindings_[static_cast<size_t>(rebindAction_)].codes[rebindSlot_];
                            bindings_[static_cast<size_t>(rebindAction_)].codes[rebindSlot_] = code;
                            rebinding_.store(false, std::memory_order_release);
                            if (rebindCallback_) {
                                rebindCallback_(rebindAction_, oldCode, code);
                            }
                        }
                    }
                }
                break;
            }
            
            case SDL_EVENT_KEY_UP: {
                InputCode code = fromSDLScancode(event.key.scancode);
                if (code != InputCode::None) {
                    inputStates_[static_cast<size_t>(code)].store(0, std::memory_order_relaxed);
                }
                break;
            }
            
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                InputCode code = fromSDLMouseButton(event.button.button);
                if (code != InputCode::None) {
                    inputStates_[static_cast<size_t>(code)].store(1, std::memory_order_relaxed);
                    
                    // Handle rebinding
                    if (rebinding_.load(std::memory_order_acquire)) {
                        InputCode oldCode = bindings_[static_cast<size_t>(rebindAction_)].codes[rebindSlot_];
                        bindings_[static_cast<size_t>(rebindAction_)].codes[rebindSlot_] = code;
                        rebinding_.store(false, std::memory_order_release);
                        if (rebindCallback_) {
                            rebindCallback_(rebindAction_, oldCode, code);
                        }
                    }
                }
                break;
            }
            
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                InputCode code = fromSDLMouseButton(event.button.button);
                if (code != InputCode::None) {
                    inputStates_[static_cast<size_t>(code)].store(0, std::memory_order_relaxed);
                }
                break;
            }
            
            case SDL_EVENT_MOUSE_MOTION: {
                mouseX_.store(static_cast<int32_t>(event.motion.x * 1000), std::memory_order_relaxed);
                mouseY_.store(static_cast<int32_t>(event.motion.y * 1000), std::memory_order_relaxed);
                mouseDeltaX_.fetch_add(static_cast<int32_t>(event.motion.xrel * 1000), std::memory_order_relaxed);
                mouseDeltaY_.fetch_add(static_cast<int32_t>(event.motion.yrel * 1000), std::memory_order_relaxed);
                
                // Store axis values
                axisValues_[static_cast<size_t>(InputCode::MouseMoveX)].store(
                    static_cast<int32_t>(event.motion.xrel * 1000), std::memory_order_relaxed);
                axisValues_[static_cast<size_t>(InputCode::MouseMoveY)].store(
                    static_cast<int32_t>(event.motion.yrel * 1000), std::memory_order_relaxed);
                break;
            }
            
            case SDL_EVENT_MOUSE_WHEEL: {
                mouseWheelX_.fetch_add(static_cast<int32_t>(event.wheel.x * 1000), std::memory_order_relaxed);
                mouseWheelY_.fetch_add(static_cast<int32_t>(event.wheel.y * 1000), std::memory_order_relaxed);
                
                axisValues_[static_cast<size_t>(InputCode::MouseWheelX)].store(
                    static_cast<int32_t>(event.wheel.x * 1000), std::memory_order_relaxed);
                axisValues_[static_cast<size_t>(InputCode::MouseWheelY)].store(
                    static_cast<int32_t>(event.wheel.y * 1000), std::memory_order_relaxed);
                break;
            }
            
            // TODO: Gamepad support
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                break;
                
            default:
                break;
        }
    }

    // Update action states based on bindings
    updateActionStates();
}

void InputManager::updateActionStates() {
    // Update button actions
    for (size_t i = 0; i < MAX_ACTIONS; ++i) {
        const auto& binding = bindings_[i];
        if (binding.action == Action::None) continue;

        bool currentlyDown = false;
        
        // Check all bound inputs for this action
        for (const auto& code : binding.codes) {
            if (code != InputCode::None) {
                if (inputStates_[static_cast<size_t>(code)].load(std::memory_order_relaxed)) {
                    currentlyDown = true;
                    break;
                }
            }
        }

        // Determine state based on current and previous
        uint8_t prevState = actionStates_[i].load(std::memory_order_relaxed);
        bool wasDown = (prevState == static_cast<uint8_t>(InputState::Pressed) || 
                        prevState == static_cast<uint8_t>(InputState::Held));

        InputState newState;
        if (currentlyDown && !wasDown) {
            newState = InputState::Pressed;
        } else if (currentlyDown && wasDown) {
            newState = InputState::Held;
        } else if (!currentlyDown && wasDown) {
            newState = InputState::Released;
        } else {
            newState = InputState::Up;
        }

        actionStates_[i].store(static_cast<uint8_t>(newState), std::memory_order_relaxed);

        // Fire callbacks on state change
        if (newState != InputState::Up && newState != InputState::Held) {
            for (uint32_t j = 0; j < callbackCount_.load(std::memory_order_relaxed); ++j) {
                if (actionCallbacks_[j]) {
                    actionCallbacks_[j](binding.action, newState);
                }
            }
        }
    }

    // Update axis actions
    for (size_t i = 0; i < MAX_AXIS_ACTIONS; ++i) {
        const auto& axis = axisBindings_[i];
        if (axis.action == Action::None) continue;

        float value = 0.0f;

        // Check positive/negative keys
        if (axis.positiveInput != InputCode::None) {
            if (inputStates_[static_cast<size_t>(axis.positiveInput)].load(std::memory_order_relaxed)) {
                value += 1.0f;
            }
        }
        if (axis.negativeInput != InputCode::None) {
            if (inputStates_[static_cast<size_t>(axis.negativeInput)].load(std::memory_order_relaxed)) {
                value -= 1.0f;
            }
        }

        // Add analog input if present
        if (axis.axisInput != InputCode::None) {
            float analogValue = axisValues_[static_cast<size_t>(axis.axisInput)].load(std::memory_order_relaxed) / 1000.0f;
            if (std::abs(analogValue) > axis.deadzone) {
                value += analogValue;
            }
        }

        // Apply sensitivity and inversion
        value *= axis.sensitivity;
        if (axis.inverted) value = -value;

        // Clamp to -1..1
        value = std::clamp(value, -1.0f, 1.0f);

        actionAxisValues_[i].store(static_cast<int32_t>(value * 1000), std::memory_order_relaxed);
    }
}

// ============================================================================
// Action Registration
// ============================================================================

void InputManager::registerAction(Action action, InputCode primary,
                                   InputCode secondary, InputCode tertiary) {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS) return;

    bindings_[idx] = InputBinding(action, primary, secondary, tertiary);
    defaultBindings_[idx] = bindings_[idx];
}

void InputManager::registerAxis(Action action, InputCode positive, InputCode negative,
                                 InputCode axisInput) {
    // Find free slot
    for (size_t i = 0; i < MAX_AXIS_ACTIONS; ++i) {
        if (axisBindings_[i].action == Action::None || axisBindings_[i].action == action) {
            axisBindings_[i].action = action;
            axisBindings_[i].positiveInput = positive;
            axisBindings_[i].negativeInput = negative;
            axisBindings_[i].axisInput = axisInput;
            return;
        }
    }
}

// ============================================================================
// Action State Queries
// ============================================================================

bool InputManager::isActionPressed(Action action) const {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS) return false;
    return actionStates_[idx].load(std::memory_order_relaxed) == static_cast<uint8_t>(InputState::Pressed);
}

bool InputManager::isActionHeld(Action action) const {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS) return false;
    uint8_t state = actionStates_[idx].load(std::memory_order_relaxed);
    return state == static_cast<uint8_t>(InputState::Pressed) || 
           state == static_cast<uint8_t>(InputState::Held);
}

bool InputManager::isActionReleased(Action action) const {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS) return false;
    return actionStates_[idx].load(std::memory_order_relaxed) == static_cast<uint8_t>(InputState::Released);
}

InputState InputManager::getActionState(Action action) const {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS) return InputState::Up;
    return static_cast<InputState>(actionStates_[idx].load(std::memory_order_relaxed));
}

float InputManager::getAxisValue(Action action) const {
    // Search axis bindings
    for (size_t i = 0; i < MAX_AXIS_ACTIONS; ++i) {
        if (axisBindings_[i].action == action) {
            return actionAxisValues_[i].load(std::memory_order_relaxed) / 1000.0f;
        }
    }
    return 0.0f;
}

// ============================================================================
// Raw Input Queries
// ============================================================================

bool InputManager::isInputPressed(InputCode code) const {
    size_t idx = static_cast<size_t>(code);
    if (idx >= inputStates_.size()) return false;
    
    bool current = inputStates_[idx].load(std::memory_order_relaxed);
    bool prev = prevInputStates_[idx].load(std::memory_order_relaxed);
    return current && !prev;
}

bool InputManager::isInputHeld(InputCode code) const {
    size_t idx = static_cast<size_t>(code);
    if (idx >= inputStates_.size()) return false;
    return inputStates_[idx].load(std::memory_order_relaxed);
}

bool InputManager::isInputReleased(InputCode code) const {
    size_t idx = static_cast<size_t>(code);
    if (idx >= inputStates_.size()) return false;
    
    bool current = inputStates_[idx].load(std::memory_order_relaxed);
    bool prev = prevInputStates_[idx].load(std::memory_order_relaxed);
    return !current && prev;
}

float InputManager::getInputAxis(InputCode code) const {
    size_t idx = static_cast<size_t>(code);
    if (idx >= axisValues_.size()) return 0.0f;
    return axisValues_[idx].load(std::memory_order_relaxed) / 1000.0f;
}

// ============================================================================
// Mouse State
// ============================================================================

void InputManager::getMousePosition(float& x, float& y) const {
    x = mouseX_.load(std::memory_order_relaxed) / 1000.0f;
    y = mouseY_.load(std::memory_order_relaxed) / 1000.0f;
}

void InputManager::getMouseDelta(float& dx, float& dy) const {
    dx = mouseDeltaX_.load(std::memory_order_relaxed) / 1000.0f;
    dy = mouseDeltaY_.load(std::memory_order_relaxed) / 1000.0f;
}

void InputManager::getMouseWheel(float& x, float& y) const {
    x = mouseWheelX_.load(std::memory_order_relaxed) / 1000.0f;
    y = mouseWheelY_.load(std::memory_order_relaxed) / 1000.0f;
}

void InputManager::setMouseCaptured(bool captured) {
    if (captured != mouseCaptured_.load(std::memory_order_relaxed)) {
        SDL_Window* window = SDL_GetKeyboardFocus();
        if (window) {
            SDL_SetWindowRelativeMouseMode(window, captured);
        }
        mouseCaptured_.store(captured, std::memory_order_relaxed);
    }
}

bool InputManager::isMouseCaptured() const {
    return mouseCaptured_.load(std::memory_order_relaxed);
}

// ============================================================================
// Runtime Rebinding
// ============================================================================

void InputManager::bindAction(Action action, InputCode code, uint32_t slot) {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS || slot >= MAX_BINDINGS_PER_ACTION) return;

    if (bindings_[idx].action == Action::None) {
        bindings_[idx].action = action;
    }
    bindings_[idx].codes[slot] = code;
}

void InputManager::unbindAction(Action action, uint32_t slot) {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS || slot >= MAX_BINDINGS_PER_ACTION) return;
    bindings_[idx].codes[slot] = InputCode::None;
}

void InputManager::clearActionBindings(Action action) {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS) return;
    for (auto& code : bindings_[idx].codes) {
        code = InputCode::None;
    }
}

const InputBinding* InputManager::getBinding(Action action) const {
    size_t idx = static_cast<size_t>(action);
    if (idx >= MAX_ACTIONS || bindings_[idx].action == Action::None) return nullptr;
    return &bindings_[idx];
}

void InputManager::startRebind(Action action, uint32_t slot, RebindCallback callback) {
    rebindAction_ = action;
    rebindSlot_ = slot;
    rebindCallback_ = callback;
    rebinding_.store(true, std::memory_order_release);
}

void InputManager::cancelRebind() {
    rebinding_.store(false, std::memory_order_release);
    rebindCallback_ = nullptr;
}

bool InputManager::isRebinding() const {
    return rebinding_.load(std::memory_order_acquire);
}

// ============================================================================
// Configuration Persistence
// ============================================================================

bool InputManager::saveBindings(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file) return false;

    file << "# Jupiter Engine Input Bindings\n";
    file << "# Format: ACTION PRIMARY SECONDARY TERTIARY\n\n";

    for (size_t i = 0; i < MAX_ACTIONS; ++i) {
        const auto& binding = bindings_[i];
        if (binding.action == Action::None) continue;

        file << getActionName(binding.action);
        for (const auto& code : binding.codes) {
            file << " " << static_cast<int>(code);
        }
        file << "\n";
    }

    return true;
}

bool InputManager::loadBindings(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) return false;

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string actionName;
        iss >> actionName;

        // Find action by name
        Action action = Action::None;
        for (int i = 0; i < static_cast<int>(Action::Custom) + 50; ++i) {
            if (actionName == getActionName(static_cast<Action>(i))) {
                action = static_cast<Action>(i);
                break;
            }
        }
        if (action == Action::None) continue;

        // Read bindings
        int code;
        uint32_t slot = 0;
        while (iss >> code && slot < MAX_BINDINGS_PER_ACTION) {
            bindAction(action, static_cast<InputCode>(code), slot++);
        }
    }

    return true;
}

void InputManager::resetToDefaults() {
    for (size_t i = 0; i < MAX_ACTIONS; ++i) {
        bindings_[i] = defaultBindings_[i];
    }
}

// ============================================================================
// Callbacks
// ============================================================================

uint32_t InputManager::addActionCallback(ActionCallback callback) {
    uint32_t count = callbackCount_.load(std::memory_order_relaxed);
    if (count >= MAX_CALLBACKS) return UINT32_MAX;

    actionCallbacks_[count] = std::move(callback);
    return callbackCount_.fetch_add(1, std::memory_order_relaxed);
}

void InputManager::removeActionCallback(uint32_t handle) {
    if (handle >= callbackCount_.load(std::memory_order_relaxed)) return;
    actionCallbacks_[handle] = nullptr;
}

// ============================================================================
// Legacy API
// ============================================================================

bool initialize() {
    return InputManager::get().initialize();
}

void shutdown() {
    InputManager::get().shutdown();
}

void update() {
    InputManager::get().update();
}

bool isKeyPressed(int keyCode) {
    return InputManager::get().isInputHeld(static_cast<InputCode>(keyCode));
}

} // namespace input
} // namespace jupiter
