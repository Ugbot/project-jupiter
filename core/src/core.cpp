#include "core/core.h"
#include "profiling/profiler.h"
#include <iostream>

namespace jupiter {
namespace core {

bool initialize() {
    JUPITER_PROFILE_SCOPE("core::initialize");
    std::cout << "Core subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    JUPITER_PROFILE_SCOPE("core::shutdown");
    std::cout << "Core subsystem shutdown" << std::endl;
}

const char* getVersion() {
    JUPITER_PROFILE_SCOPE("core::getVersion");
    return "1.0.0";
}

} // namespace core
} // namespace jupiter
