#pragma once

/**
 * @file input.h
 * @brief Jupiter Engine Input System
 * 
 * Action-based input system with runtime key remapping support.
 * 
 * Features:
 * - Action-based input (bind actions to keys, not raw key checks)
 * - Runtime key remapping
 * - Multiple bindings per action (primary/secondary)
 * - Keyboard, mouse, and gamepad support
 * - Input states: Pressed, Released, Held
 * - Axis inputs for analog controls
 * - No runtime allocations - all pre-allocated at init
 * 
 * Usage:
 *   InputManager& input = InputManager::get();
 *   input.initialize();
 *   input.registerAction(Action::MoveForward, InputCode::KeyW);
 *   input.registerAction(Action::Jump, InputCode::KeySpace);
 *   input.update();  // Call once per frame
 *   if (input.isActionPressed(Action::Jump)) { ... }
 *   if (input.isActionHeld(Action::MoveForward)) { ... }
 *   input.bindAction(Action::Jump, InputCode::KeyE);  // Rebind at runtime
 *   input.saveBindings("controls.cfg");
 */

// SDL must be included first on some platforms due to header conflicts
#include <SDL3/SDL.h>

#include <atomic>
#include <array>
#include <string>
#include <functional>

#ifdef _WIN32
    #ifdef INPUT_EXPORTS
        #define INPUT_API __declspec(dllexport)
    #elif defined(INPUT_IMPORTS)
        #define INPUT_API __declspec(dllimport)
    #else
        #define INPUT_API
    #endif
#else
    #define INPUT_API
#endif

namespace jupiter {
namespace input {

// ============================================================================
// Constants
// ============================================================================

constexpr uint32_t MAX_ACTIONS = 128;          // Maximum number of bindable actions
constexpr uint32_t MAX_BINDINGS_PER_ACTION = 3; // Primary, secondary, tertiary
constexpr uint32_t MAX_AXIS_ACTIONS = 32;      // Maximum axis (analog) actions

// ============================================================================
// Input Codes - Platform independent key/button identifiers
// ============================================================================

enum class InputCode : uint16_t {
    None = 0,

    // Keyboard - Letters (matches SDL scancodes for easy mapping)
    KeyA = 4, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI, KeyJ, KeyK, KeyL, KeyM,
    KeyN, KeyO, KeyP, KeyQ, KeyR, KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,

    // Keyboard - Numbers (top row)
    Key1 = 30, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9, Key0,

    // Keyboard - Function keys
    KeyF1 = 58, KeyF2, KeyF3, KeyF4, KeyF5, KeyF6, KeyF7, KeyF8, KeyF9, KeyF10, KeyF11, KeyF12,

    // Keyboard - Special keys
    KeyReturn = 40,
    KeyEscape = 41,
    KeyBackspace = 42,
    KeyTab = 43,
    KeySpace = 44,
    KeyMinus = 45,
    KeyEquals = 46,
    KeyLeftBracket = 47,
    KeyRightBracket = 48,
    KeyBackslash = 49,
    KeySemicolon = 51,
    KeyApostrophe = 52,
    KeyGrave = 53,
    KeyComma = 54,
    KeyPeriod = 55,
    KeySlash = 56,
    KeyCapsLock = 57,

    // Keyboard - Navigation
    KeyPrintScreen = 70,
    KeyScrollLock = 71,
    KeyPause = 72,
    KeyInsert = 73,
    KeyHome = 74,
    KeyPageUp = 75,
    KeyDelete = 76,
    KeyEnd = 77,
    KeyPageDown = 78,
    KeyRight = 79,
    KeyLeft = 80,
    KeyDown = 81,
    KeyUp = 82,

    // Keyboard - Numpad
    KeyNumLock = 83,
    KeyNumDivide = 84,
    KeyNumMultiply = 85,
    KeyNumMinus = 86,
    KeyNumPlus = 87,
    KeyNumEnter = 88,
    KeyNum1 = 89, KeyNum2, KeyNum3, KeyNum4, KeyNum5, KeyNum6, KeyNum7, KeyNum8, KeyNum9, KeyNum0,
    KeyNumPeriod = 99,

    // Keyboard - Modifiers
    KeyLeftCtrl = 224,
    KeyLeftShift = 225,
    KeyLeftAlt = 226,
    KeyLeftGui = 227,   // Windows/Command key
    KeyRightCtrl = 228,
    KeyRightShift = 229,
    KeyRightAlt = 230,
    KeyRightGui = 231,

    // Mouse buttons (offset to avoid collision with keyboard)
    MouseLeft = 300,
    MouseRight = 301,
    MouseMiddle = 302,
    MouseX1 = 303,      // Side button 1
    MouseX2 = 304,      // Side button 2

    // Mouse axes (for axis bindings)
    MouseMoveX = 310,
    MouseMoveY = 311,
    MouseWheelX = 312,
    MouseWheelY = 313,

    // Gamepad buttons (Xbox layout)
    GamepadA = 400,
    GamepadB = 401,
    GamepadX = 402,
    GamepadY = 403,
    GamepadLeftBumper = 404,
    GamepadRightBumper = 405,
    GamepadBack = 406,
    GamepadStart = 407,
    GamepadGuide = 408,
    GamepadLeftStick = 409,
    GamepadRightStick = 410,
    GamepadDPadUp = 411,
    GamepadDPadRight = 412,
    GamepadDPadDown = 413,
    GamepadDPadLeft = 414,

    // Gamepad axes
    GamepadLeftX = 450,
    GamepadLeftY = 451,
    GamepadRightX = 452,
    GamepadRightY = 453,
    GamepadLeftTrigger = 454,
    GamepadRightTrigger = 455,

    InputCodeCount = 500
};

// ============================================================================
// Input Actions - Game-specific actions that can be bound to inputs
// ============================================================================

/**
 * @brief Built-in common actions. Games can use these or define custom actions
 *        using values >= Action::Custom
 */
enum class Action : uint16_t {
    None = 0,

    // Movement
    MoveForward = 1,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Sprint,
    Crouch,
    Jump,

    // Camera
    LookUp = 20,
    LookDown,
    LookLeft,
    LookRight,
    ZoomIn,
    ZoomOut,
    CameraReset,

    // Interaction
    PrimaryAction = 40,     // Fire, attack, use
    SecondaryAction,        // Alt fire, block, aim
    Interact,
    Reload,
    NextWeapon,
    PrevWeapon,

    // UI/Menu
    MenuToggle = 60,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    MenuSelect,
    MenuBack,
    Inventory,
    Map,
    Pause,

    // Debug/Dev
    DebugToggle = 80,
    DebugConsole,
    Screenshot,
    
    // Start of custom actions for game-specific use
    Custom = 100
};

// ============================================================================
// Input State
// ============================================================================

/**
 * @brief State of an input (action or raw input)
 */
enum class InputState : uint8_t {
    Up = 0,         // Not pressed
    Pressed = 1,    // Just pressed this frame
    Held = 2,       // Held down (was pressed, still down)
    Released = 3    // Just released this frame
};

// ============================================================================
// Input Binding
// ============================================================================

/**
 * @brief A binding from an action to input codes
 */
struct InputBinding {
    Action action = Action::None;
    std::array<InputCode, MAX_BINDINGS_PER_ACTION> codes = {};
    float deadzone = 0.15f;     // For analog inputs
    float sensitivity = 1.0f;   // Multiplier for axis inputs
    bool inverted = false;      // Invert axis direction
    
    InputBinding() = default;
    InputBinding(Action a, InputCode primary, InputCode secondary = InputCode::None, 
                 InputCode tertiary = InputCode::None)
        : action(a), codes{primary, secondary, tertiary} {}
};

/**
 * @brief Axis binding for analog inputs (movement, camera, etc.)
 */
struct AxisBinding {
    Action action = Action::None;
    InputCode positiveInput = InputCode::None;  // Key/button for +1
    InputCode negativeInput = InputCode::None;  // Key/button for -1
    InputCode axisInput = InputCode::None;      // Direct axis (gamepad stick, mouse)
    float deadzone = 0.15f;
    float sensitivity = 1.0f;
    bool inverted = false;
};

// ============================================================================
// Callback Types
// ============================================================================

using ActionCallback = std::function<void(Action, InputState)>;
using RebindCallback = std::function<void(Action, InputCode oldCode, InputCode newCode)>;

// ============================================================================
// Input Manager
// ============================================================================

/**
 * @brief Central input management system
 * 
 * Thread-safe for reading state, but updates should only be called from main thread.
 */
class INPUT_API InputManager {
public:
    /**
     * @brief Get the singleton instance
     */
    static InputManager& get();

    /**
     * @brief Initialize the input system
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown the input system
     */
    void shutdown();

    /**
     * @brief Update input state (call once per frame from main thread)
     * 
     * This polls SDL events and updates all input states.
     */
    void update();

    // ========================================================================
    // Action Registration
    // ========================================================================

    /**
     * @brief Register an action with a default binding
     * @param action The action to register
     * @param primary Primary input code
     * @param secondary Optional secondary input code
     * @param tertiary Optional tertiary input code
     */
    void registerAction(Action action, InputCode primary,
                       InputCode secondary = InputCode::None,
                       InputCode tertiary = InputCode::None);

    /**
     * @brief Register an axis action (for analog movement, camera, etc.)
     * @param action The action to register
     * @param positive Input code for positive direction (e.g., KeyD for right)
     * @param negative Input code for negative direction (e.g., KeyA for left)
     * @param axisInput Optional direct axis input (e.g., GamepadLeftX)
     */
    void registerAxis(Action action, InputCode positive, InputCode negative,
                     InputCode axisInput = InputCode::None);

    // ========================================================================
    // Action State Queries
    // ========================================================================

    /**
     * @brief Check if an action was just pressed this frame
     */
    bool isActionPressed(Action action) const;

    /**
     * @brief Check if an action is currently held down
     */
    bool isActionHeld(Action action) const;

    /**
     * @brief Check if an action was just released this frame
     */
    bool isActionReleased(Action action) const;

    /**
     * @brief Get the current state of an action
     */
    InputState getActionState(Action action) const;

    /**
     * @brief Get the axis value for an action (-1.0 to 1.0)
     */
    float getAxisValue(Action action) const;

    // ========================================================================
    // Raw Input Queries
    // ========================================================================

    /**
     * @brief Check if a specific input code is pressed this frame
     */
    bool isInputPressed(InputCode code) const;

    /**
     * @brief Check if a specific input code is held
     */
    bool isInputHeld(InputCode code) const;

    /**
     * @brief Check if a specific input code was released this frame
     */
    bool isInputReleased(InputCode code) const;

    /**
     * @brief Get raw axis value for an input code
     */
    float getInputAxis(InputCode code) const;

    // ========================================================================
    // Mouse State
    // ========================================================================

    /**
     * @brief Get mouse position in window coordinates
     */
    void getMousePosition(float& x, float& y) const;

    /**
     * @brief Get mouse movement delta since last frame
     */
    void getMouseDelta(float& dx, float& dy) const;

    /**
     * @brief Get mouse wheel delta
     */
    void getMouseWheel(float& x, float& y) const;

    /**
     * @brief Set mouse capture mode (hides cursor and captures relative motion)
     */
    void setMouseCaptured(bool captured);

    /**
     * @brief Check if mouse is captured
     */
    bool isMouseCaptured() const;

    // ========================================================================
    // Runtime Rebinding
    // ========================================================================

    /**
     * @brief Bind an input code to an action
     * @param action The action to bind
     * @param code The input code to bind
     * @param slot Binding slot (0=primary, 1=secondary, 2=tertiary)
     */
    void bindAction(Action action, InputCode code, uint32_t slot = 0);

    /**
     * @brief Unbind a specific slot for an action
     */
    void unbindAction(Action action, uint32_t slot);

    /**
     * @brief Clear all bindings for an action
     */
    void clearActionBindings(Action action);

    /**
     * @brief Get the current binding for an action
     */
    const InputBinding* getBinding(Action action) const;

    /**
     * @brief Enter rebind mode - next input will be bound to the specified action
     * @param action Action to rebind
     * @param slot Slot to rebind (0-2)
     * @param callback Called when rebind completes
     */
    void startRebind(Action action, uint32_t slot, RebindCallback callback = nullptr);

    /**
     * @brief Cancel rebind mode
     */
    void cancelRebind();

    /**
     * @brief Check if in rebind mode
     */
    bool isRebinding() const;

    // ========================================================================
    // Configuration Persistence
    // ========================================================================

    /**
     * @brief Save current bindings to a file
     * @param filepath Path to save to
     * @return true if successful
     */
    bool saveBindings(const std::string& filepath) const;

    /**
     * @brief Load bindings from a file
     * @param filepath Path to load from
     * @return true if successful
     */
    bool loadBindings(const std::string& filepath);

    /**
     * @brief Reset all bindings to defaults
     */
    void resetToDefaults();

    // ========================================================================
    // Callbacks
    // ========================================================================

    /**
     * @brief Register a callback for action state changes
     * @param callback Function to call when any action state changes
     * @return Handle to unregister the callback
     */
    uint32_t addActionCallback(ActionCallback callback);

    /**
     * @brief Remove an action callback
     */
    void removeActionCallback(uint32_t handle);

    // ========================================================================
    // Utilities
    // ========================================================================

    /**
     * @brief Get human-readable name for an input code
     */
    static const char* getInputCodeName(InputCode code);

    /**
     * @brief Get human-readable name for an action
     */
    static const char* getActionName(Action action);

    /**
     * @brief Convert SDL scancode to InputCode
     */
    static InputCode fromSDLScancode(int scancode);

    /**
     * @brief Convert SDL mouse button to InputCode
     */
    static InputCode fromSDLMouseButton(int button);

private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void processKeyboardState();
    void processMouseState();
    void updateActionStates();
    InputCode waitForInput();  // For rebind mode

    bool initialized_ = false;

    // Current frame's raw input states
    std::array<std::atomic<uint8_t>, static_cast<size_t>(InputCode::InputCodeCount)> inputStates_{};
    std::array<std::atomic<uint8_t>, static_cast<size_t>(InputCode::InputCodeCount)> prevInputStates_{};
    
    // Axis values for analog inputs
    std::array<std::atomic<int32_t>, static_cast<size_t>(InputCode::InputCodeCount)> axisValues_{};  // Fixed-point: value * 1000

    // Action bindings
    std::array<InputBinding, MAX_ACTIONS> bindings_{};
    std::array<AxisBinding, MAX_AXIS_ACTIONS> axisBindings_{};
    std::array<InputBinding, MAX_ACTIONS> defaultBindings_{};  // For reset

    // Action states (computed from bindings + raw input)
    std::array<std::atomic<uint8_t>, MAX_ACTIONS> actionStates_{};
    std::array<std::atomic<int32_t>, MAX_AXIS_ACTIONS> actionAxisValues_{};  // Fixed-point

    // Mouse state
    std::atomic<int32_t> mouseX_{0}, mouseY_{0};
    std::atomic<int32_t> mouseDeltaX_{0}, mouseDeltaY_{0};
    std::atomic<int32_t> mouseWheelX_{0}, mouseWheelY_{0};
    std::atomic<bool> mouseCaptured_{false};

    // Rebind state
    std::atomic<bool> rebinding_{false};
    Action rebindAction_ = Action::None;
    uint32_t rebindSlot_ = 0;
    RebindCallback rebindCallback_;

    // Callbacks
    static constexpr uint32_t MAX_CALLBACKS = 16;
    std::array<ActionCallback, MAX_CALLBACKS> actionCallbacks_{};
    std::atomic<uint32_t> callbackCount_{0};
};

// ============================================================================
// Legacy API (for backwards compatibility)
// ============================================================================

/**
 * @brief Initialize the input subsystem (legacy)
 * @deprecated Use InputManager::get().initialize() instead
 */
INPUT_API bool initialize();

/**
 * @brief Shutdown the input subsystem (legacy)
 * @deprecated Use InputManager::get().shutdown() instead
 */
INPUT_API void shutdown();

/**
 * @brief Update input state (legacy)
 * @deprecated Use InputManager::get().update() instead
 */
INPUT_API void update();

/**
 * @brief Check if a key is pressed (legacy)
 * @deprecated Use InputManager::get().isInputHeld() instead
 */
INPUT_API bool isKeyPressed(int keyCode);

} // namespace input
} // namespace jupiter
