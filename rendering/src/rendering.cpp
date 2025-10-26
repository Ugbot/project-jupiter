#include "rendering/rendering.h"
#include <iostream>

namespace jupiter {
namespace rendering {

bool initialize() {
    std::cout << "Rendering subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Rendering subsystem shutdown" << std::endl;
}

void renderFrame() {
    std::cout << "Rendering frame..." << std::endl;
}

void clear() {
    std::cout << "Screen cleared" << std::endl;
}

} // namespace rendering
} // namespace jupiter
