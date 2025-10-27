#pragma once

#ifdef _WIN32
    #ifdef SCRIPTING_EXPORTS
        #define SCRIPTING_API __declspec(dllexport)
    #elif defined(SCRIPTING_IMPORTS)
        #define SCRIPTING_API __declspec(dllimport)
    #else
        #define SCRIPTING_API
    #endif
#else
    #define SCRIPTING_API
#endif

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <any>

namespace jupiter {
namespace scripting {

/**
 * @brief Script value types
 */
enum class ValueType {
    NIL,
    BOOLEAN,
    NUMBER,
    STRING,
    TABLE,
    FUNCTION,
    USERDATA
};

/**
 * @brief Script value container
 */
class SCRIPTING_API Value {
public:
    Value() : m_type(ValueType::NIL) {}
    Value(bool b) : m_type(ValueType::BOOLEAN), m_boolValue(b) {}
    Value(double n) : m_type(ValueType::NUMBER), m_numberValue(n) {}
    Value(const std::string& s) : m_type(ValueType::STRING), m_stringValue(s) {}
    Value(const char* s) : m_type(ValueType::STRING), m_stringValue(s) {}

    ValueType getType() const { return m_type; }

    bool asBool() const { return m_boolValue; }
    double asNumber() const { return m_numberValue; }
    const std::string& asString() const { return m_stringValue; }

    bool isNil() const { return m_type == ValueType::NIL; }
    bool isBool() const { return m_type == ValueType::BOOLEAN; }
    bool isNumber() const { return m_type == ValueType::NUMBER; }
    bool isString() const { return m_type == ValueType::STRING; }

private:
    ValueType m_type;
    bool m_boolValue = false;
    double m_numberValue = 0.0;
    std::string m_stringValue;
};

/**
 * @brief Script execution context
 */
class SCRIPTING_API ScriptContext {
public:
    virtual ~ScriptContext() = default;

    /**
     * @brief Execute script code
     * @param code Script code to execute
     * @return true if execution was successful
     */
    virtual bool executeString(const std::string& code) = 0;

    /**
     * @brief Execute script file
     * @param filename Script file to execute
     * @return true if execution was successful
     */
    virtual bool executeFile(const std::string& filename) = 0;

    /**
     * @brief Call a global function
     * @param functionName Name of the function to call
     * @param args Arguments to pass to the function
     * @return Function return value
     */
    virtual Value callFunction(const std::string& functionName,
                              const std::vector<Value>& args = {}) = 0;

    /**
     * @brief Set global variable
     * @param name Variable name
     * @param value Variable value
     */
    virtual void setGlobal(const std::string& name, const Value& value) = 0;

    /**
     * @brief Get global variable
     * @param name Variable name
     * @return Variable value
     */
    virtual Value getGlobal(const std::string& name) = 0;

    /**
     * @brief Check if global variable exists
     * @param name Variable name
     * @return true if variable exists
     */
    virtual bool hasGlobal(const std::string& name) = 0;

    /**
     * @brief Register C++ function
     * @param name Function name
     * @param function Function implementation
     */
    virtual void registerFunction(const std::string& name,
                                 std::function<Value(const std::vector<Value>&)> function) = 0;

    /**
     * @brief Get last error message
     * @return Error message
     */
    virtual std::string getLastError() const = 0;

    /**
     * @brief Clear error state
     */
    virtual void clearError() = 0;
};

/**
 * @brief Script engine interface
 */
class SCRIPTING_API ScriptEngine {
public:
    virtual ~ScriptEngine() = default;

    /**
     * @brief Create a new script context
     * @return New script context
     */
    virtual std::unique_ptr<ScriptContext> createContext() = 0;

    /**
     * @brief Get engine name
     * @return Engine name
     */
    virtual const char* getName() const = 0;

    /**
     * @brief Check if engine is available
     * @return true if engine can be used
     */
    virtual bool isAvailable() const = 0;
};

/**
 * @brief Lua script engine implementation
 */
class SCRIPTING_API LuaEngine : public ScriptEngine {
public:
    LuaEngine();
    ~LuaEngine() override;

    std::unique_ptr<ScriptContext> createContext() override;
    const char* getName() const override { return "Lua"; }
    bool isAvailable() const override;
};

/**
 * @brief Script manager for handling multiple script engines
 */
class SCRIPTING_API ScriptManager {
public:
    ScriptManager();
    ~ScriptManager();

    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    /**
     * @brief Register a script engine
     * @param engine Engine to register
     */
    void registerEngine(std::unique_ptr<ScriptEngine> engine);

    /**
     * @brief Get script engine by name
     * @param name Engine name
     * @return Engine pointer, or nullptr if not found
     */
    ScriptEngine* getEngine(const std::string& name);

    /**
     * @brief Get default script engine
     * @return Default engine pointer, or nullptr if no engines available
     */
    ScriptEngine* getDefaultEngine();

    /**
     * @brief Create context with default engine
     * @return New script context
     */
    std::unique_ptr<ScriptContext> createContext();

    /**
     * @brief Get all registered engines
     * @return Vector of engine pointers
     */
    std::vector<ScriptEngine*> getEngines();

private:
    std::vector<std::unique_ptr<ScriptEngine>> m_engines;
};

/**
 * @brief Initialize the scripting subsystem
 * @return true if initialization was successful, false otherwise
 */
SCRIPTING_API bool initialize();

/**
 * @brief Shutdown the scripting subsystem
 */
SCRIPTING_API void shutdown();

/**
 * @brief Get the global script manager
 * @return Reference to the global script manager
 */
SCRIPTING_API ScriptManager& getScriptManager();

} // namespace scripting
} // namespace jupiter
