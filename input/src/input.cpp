#include "input/input.h"
#include <iostream>

namespace jupiter {
namespace input {

bool initialize() {
    std::cout << "Input subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Input subsystem shutdown" << std::endl;
}

void update() {
    // Poll for input events
}

bool isKeyPressed(int keyCode) {
    // Check key state
    return false;
}

} // namespace input
} // namespace jupiter
