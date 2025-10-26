#include "core/core.h"
#include <iostream>

namespace jupiter {
namespace core {

bool initialize() {
    std::cout << "Core subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Core subsystem shutdown" << std::endl;
}

const char* getVersion() {
    return "1.0.0";
}

} // namespace core
} // namespace jupiter
