#include "assets/assets.h"
#include <iostream>

namespace jupiter {
namespace assets {

bool initialize() {
    std::cout << "Assets subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Assets subsystem shutdown" << std::endl;
}

bool loadAsset(const char* filename) {
    std::cout << "Loading asset: " << filename << std::endl;
    return true;
}

} // namespace assets
} // namespace jupiter
