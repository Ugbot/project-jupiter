#include "audio/audio.h"
#include <iostream>

namespace jupiter {
namespace audio {

bool initialize() {
    std::cout << "Audio subsystem initialized" << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Audio subsystem shutdown" << std::endl;
}

void playSound(const char* soundName) {
    std::cout << "Playing sound: " << soundName << std::endl;
}

} // namespace audio
} // namespace jupiter
