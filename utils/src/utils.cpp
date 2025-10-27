#include "utils/utils.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <random>
#include <ctime>
#include <iostream>

namespace jupiter {
namespace utils {

// StringUtils implementation
std::vector<std::string> StringUtils::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        result.push_back(token);
    }

    return result;
}

std::string StringUtils::join(const std::vector<std::string>& strings, const std::string& delimiter) {
    if (strings.empty()) {
        return "";
    }

    std::string result = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter + strings[i];
    }

    return result;
}

std::string StringUtils::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) {
        return "";
    }

    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, last - first + 1);
}

std::string StringUtils::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string StringUtils::toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool StringUtils::startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtils::endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StringUtils::replace(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;

    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }

    return result;
}

// FileUtils implementation
bool FileUtils::exists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

long long FileUtils::getFileSize(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return -1;
    }
    return file.tellg();
}

std::string FileUtils::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool FileUtils::writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    return file.good();
}

std::string FileUtils::getExtension(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    return path.substr(dotPos + 1);
}

std::string FileUtils::getFilename(const std::string& path) {
    size_t slashPos = path.find_last_of("/\\");
    if (slashPos == std::string::npos) {
        return path;
    }
    return path.substr(slashPos + 1);
}

std::string FileUtils::getDirectory(const std::string& path) {
    size_t slashPos = path.find_last_of("/\\");
    if (slashPos == std::string::npos) {
        return "";
    }
    return path.substr(0, slashPos);
}

// Timer implementation
Timer::Timer() {
    reset();
}

void Timer::reset() {
    m_startTime = std::chrono::high_resolution_clock::now();
}

double Timer::getElapsedSeconds() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_startTime);
    return duration.count() / 1e9;
}

double Timer::getElapsedMilliseconds() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_startTime);
    return duration.count() / 1e6;
}

double Timer::getElapsedMicroseconds() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_startTime);
    return duration.count() / 1e3;
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(const std::string& name) : m_name(name) {
    std::cout << "Starting timer: " << m_name << std::endl;
}

ScopedTimer::~ScopedTimer() {
    double elapsed = m_timer.getElapsedMilliseconds();
    std::cout << "Timer '" << m_name << "' finished in " << elapsed << " ms" << std::endl;
}

// HashUtils implementation
uint32_t HashUtils::fnv1a32(const std::string& str) {
    const uint32_t FNV_PRIME = 16777619u;
    const uint32_t FNV_OFFSET_BASIS = 2166136261u;

    uint32_t hash = FNV_OFFSET_BASIS;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= FNV_PRIME;
    }

    return hash;
}

uint64_t HashUtils::fnv1a64(const std::string& str) {
    const uint64_t FNV_PRIME = 1099511628211ull;
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;

    uint64_t hash = FNV_OFFSET_BASIS;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= FNV_PRIME;
    }

    return hash;
}

// RandomUtils implementation
static std::mt19937 s_randomEngine;

int RandomUtils::randomInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(s_randomEngine);
}

float RandomUtils::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(s_randomEngine);
}

double RandomUtils::randomDouble(double min, double max) {
    std::uniform_real_distribution<double> dist(min, max);
    return dist(s_randomEngine);
}

bool RandomUtils::randomBool() {
    return randomInt(0, 1) == 1;
}

void RandomUtils::setSeed(unsigned int seed) {
    s_randomEngine.seed(seed);
}

// Initialize random engine with current time
static struct RandomInitializer {
    RandomInitializer() {
        RandomUtils::setSeed(static_cast<unsigned int>(std::time(nullptr)));
    }
} s_randomInitializer;

bool initialize() {
    // Nothing to initialize for now
    return true;
}

void shutdown() {
    // Nothing to shutdown for now
}

} // namespace utils
} // namespace jupiter
