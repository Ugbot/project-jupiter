#include "scripting/scripting.h"
#include <iostream>
#include <sstream>

namespace jupiter {
namespace scripting {

// LuaContext placeholder implementation
class LuaContext : public ScriptContext {
public:
    LuaContext() : m_lastError("") {}
    ~LuaContext() override = default;

    bool executeString(const std::string& code) override {
        std::cout << "Lua: Executing script code: " << code << std::endl;
        // Placeholder: simulate successful execution
        m_lastError.clear();
        return true;
    }

    bool executeFile(const std::string& filename) override {
        std::cout << "Lua: Executing script file: " << filename << std::endl;
        // Placeholder: simulate successful execution
        m_lastError.clear();
        return true;
    }

    Value callFunction(const std::string& functionName,
                      const std::vector<Value>& args) override {
        std::cout << "Lua: Calling function '" << functionName << "' with "
                  << args.size() << " arguments" << std::endl;

        // Placeholder return value
        return Value("function_result");
    }

    void setGlobal(const std::string& name, const Value& value) override {
        std::cout << "Lua: Setting global '" << name << "'" << std::endl;
        m_globals[name] = value;
    }

    Value getGlobal(const std::string& name) override {
        auto it = m_globals.find(name);
        if (it != m_globals.end()) {
            return it->second;
        }
        return Value(); // nil
    }

    bool hasGlobal(const std::string& name) override {
        return m_globals.find(name) != m_globals.end();
    }

    void registerFunction(const std::string& name,
                         std::function<Value(const std::vector<Value>&)> function) override {
        std::cout << "Lua: Registering function '" << name << "'" << std::endl;
        m_registeredFunctions[name] = function;
    }

    std::string getLastError() const override {
        return m_lastError;
    }

    void clearError() override {
        m_lastError.clear();
    }

private:
    std::string m_lastError;
    std::unordered_map<std::string, Value> m_globals;
    std::unordered_map<std::string, std::function<Value(const std::vector<Value>&)>> m_registeredFunctions;
};

// LuaEngine implementation
LuaEngine::LuaEngine() = default;

LuaEngine::~LuaEngine() = default;

std::unique_ptr<ScriptContext> LuaEngine::createContext() {
    return std::make_unique<LuaContext>();
}

bool LuaEngine::isAvailable() const {
    // Placeholder: assume Lua is available
    return true;
}

// ScriptManager implementation
ScriptManager::ScriptManager() = default;

ScriptManager::~ScriptManager() = default;

void ScriptManager::registerEngine(std::unique_ptr<ScriptEngine> engine) {
    if (engine) {
        m_engines.push_back(std::move(engine));
    }
}

ScriptEngine* ScriptManager::getEngine(const std::string& name) {
    for (auto& engine : m_engines) {
        if (engine->getName() == name) {
            return engine.get();
        }
    }
    return nullptr;
}

ScriptEngine* ScriptManager::getDefaultEngine() {
    if (!m_engines.empty()) {
        return m_engines[0].get();
    }
    return nullptr;
}

std::unique_ptr<ScriptContext> ScriptManager::createContext() {
    ScriptEngine* engine = getDefaultEngine();
    if (engine) {
        return engine->createContext();
    }
    return nullptr;
}

std::vector<ScriptEngine*> ScriptManager::getEngines() {
    std::vector<ScriptEngine*> result;
    result.reserve(m_engines.size());
    for (auto& engine : m_engines) {
        result.push_back(engine.get());
    }
    return result;
}

// Global functions
static ScriptManager* s_scriptManager = nullptr;

bool initialize() {
    if (s_scriptManager) {
        return true;
    }

    s_scriptManager = new ScriptManager();

    // Register built-in engines
    s_scriptManager->registerEngine(std::make_unique<LuaEngine>());

    std::cout << "Scripting subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    if (s_scriptManager) {
        delete s_scriptManager;
        s_scriptManager = nullptr;
        std::cout << "Scripting subsystem shutdown" << std::endl;
    }
}

ScriptManager& getScriptManager() {
    if (!s_scriptManager) {
        initialize();
    }
    return *s_scriptManager;
}

} // namespace scripting
} // namespace jupiter
