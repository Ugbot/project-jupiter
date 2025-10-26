#include "animation/animation.h"
#include <iostream>

namespace jupiter {
namespace animation {

bool initialize() {
    std::cout << "Animation subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Animation subsystem shutdown" << std::endl;
}

void update(float deltaTime) {
    // Update animations here
    // std::cout << "Animation update, deltaTime: " << deltaTime << std::endl;
}

} // namespace animation
} // namespace jupiter
