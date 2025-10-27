#include "platform/platform.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

#if defined(PLATFORM_WINDOWS)
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <processthreadsapi.h>
#elif defined(PLATFORM_MACOS)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(PLATFORM_LINUX)
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>
#endif

namespace jupiter {
namespace platform {

// ============================================================================
// Timer Implementation
// ============================================================================

Timer::Timer() {
    reset();
}

void Timer::reset() {
    start_time_ = PerformanceCounter::getCurrentValue();
}

double Timer::getElapsedSeconds() const {
    uint64_t current = PerformanceCounter::getCurrentValue();
    return PerformanceCounter::toSeconds(current - start_time_);
}

double Timer::getElapsedMilliseconds() const {
    uint64_t current = PerformanceCounter::getCurrentValue();
    return PerformanceCounter::toMilliseconds(current - start_time_);
}

double Timer::getElapsedMicroseconds() const {
    uint64_t current = PerformanceCounter::getCurrentValue();
    return PerformanceCounter::toMilliseconds(current - start_time_) * 1000.0;
}

double Timer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
}

void Timer::sleep(uint32_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// ============================================================================
// PerformanceCounter Implementation
// ============================================================================

uint64_t PerformanceCounter::getCurrentValue() {
#if defined(PLATFORM_WINDOWS)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
#elif defined(PLATFORM_MACOS)
    return mach_absolute_time();
#else // Linux and others
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

uint64_t PerformanceCounter::getFrequency() {
#if defined(PLATFORM_WINDOWS)
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<uint64_t>(frequency.QuadPart);
#elif defined(PLATFORM_MACOS)
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    return 1000000000ULL * info.denom / info.numer;
#else // Linux and others
    return 1000000000ULL; // nanoseconds
#endif
}

double PerformanceCounter::toSeconds(uint64_t counter_value) {
    return static_cast<double>(counter_value) / static_cast<double>(getFrequency());
}

double PerformanceCounter::toMilliseconds(uint64_t counter_value) {
    return static_cast<double>(counter_value) * 1000.0 / static_cast<double>(getFrequency());
}

// ============================================================================
// Memory Implementation
// ============================================================================

void* Memory::allocateAligned(size_t size, size_t alignment) {
    if (size == 0) return nullptr;

#if defined(PLATFORM_WINDOWS)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void Memory::freeAligned(void* ptr) {
    if (!ptr) return;

#if defined(PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

size_t Memory::getCacheLineSize() {
#if defined(PLATFORM_WINDOWS)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwAllocationGranularity;
#elif defined(PLATFORM_MACOS) || defined(PLATFORM_LINUX)
    // Common cache line sizes are 64 bytes, but let's try to detect it
    size_t line_size = 64; // Default
    #if defined(_SC_LEVEL1_DCACHE_LINESIZE)
        long result = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        if (result > 0) {
            line_size = static_cast<size_t>(result);
        }
    #endif
    return line_size;
#else
    return 64; // Default
#endif
}

void Memory::prefetch(const void* ptr, ptrdiff_t offset) {
    if (!ptr) return;

    const char* addr = static_cast<const char*>(ptr) + offset;

#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr, 0, 3); // Read prefetch, high locality
#elif defined(_MSC_VER)
    _mm_prefetch(addr, _MM_HINT_T0);
#else
    // No prefetch available
    (void)addr;
#endif
}

// ============================================================================
// Atomic Implementation
// ============================================================================

void Atomic::spinWait() {
#if defined(_MSC_VER)
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
    #if defined(__GNUC__) || defined(__clang__)
        __builtin_ia32_pause();
    #endif
#else
    // No-op for other architectures
#endif
}

// ============================================================================
// Thread Implementation
// ============================================================================

uint64_t Thread::getCurrentThreadId() {
#if defined(PLATFORM_WINDOWS)
    return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(PLATFORM_MACOS)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#else // Linux and others
    return static_cast<uint64_t>(pthread_self());
#endif
}

void Thread::setCurrentThreadName(const char* name) {
    if (!name) return;

#if defined(PLATFORM_WINDOWS)
    // Windows thread naming
    #pragma pack(push, 8)
    struct THREADNAME_INFO {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    };
    #pragma pack(pop)

    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = name;
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    __try {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#elif defined(PLATFORM_MACOS) || defined(__APPLE__)
    pthread_setname_np(name);
#else // Linux and others
    pthread_setname_np(pthread_self(), name);
#endif
}

unsigned int Thread::getHardwareConcurrency() {
    return std::thread::hardware_concurrency();
}

void Thread::yield() {
    std::this_thread::yield();
}

// ============================================================================
// FileSystem Implementation
// ============================================================================

bool FileSystem::fileExists(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    DWORD attributes = GetFileAttributesA(path.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES) && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0) && S_ISREG(buffer.st_mode);
#endif
}

bool FileSystem::directoryExists(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    DWORD attributes = GetFileAttributesA(path.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0) && S_ISDIR(buffer.st_mode);
#endif
}

bool FileSystem::createDirectory(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    return CreateDirectoryA(path.c_str(), nullptr) != 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool FileSystem::removeFile(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    return DeleteFileA(path.c_str()) != 0;
#else
    return unlink(path.c_str()) == 0;
#endif
}

bool FileSystem::removeDirectory(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    return RemoveDirectoryA(path.c_str()) != 0;
#else
    return rmdir(path.c_str()) == 0;
#endif
}

int64_t FileSystem::getFileSize(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return -1;
    }
    LARGE_INTEGER size;
    size.HighPart = fileInfo.nFileSizeHigh;
    size.LowPart = fileInfo.nFileSizeLow;
    return size.QuadPart;
#else
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) {
        return -1;
    }
    return buffer.st_size;
#endif
}

std::string FileSystem::getCurrentWorkingDirectory() {
    char buffer[4096];
#if defined(PLATFORM_WINDOWS)
    if (GetCurrentDirectoryA(sizeof(buffer), buffer) == 0) {
        return "";
    }
#else
    if (!getcwd(buffer, sizeof(buffer))) {
        return "";
    }
#endif
    return std::string(buffer);
}

bool FileSystem::setCurrentWorkingDirectory(const std::string& path) {
#if defined(PLATFORM_WINDOWS)
    return SetCurrentDirectoryA(path.c_str()) != 0;
#else
    return chdir(path.c_str()) == 0;
#endif
}

char FileSystem::getDirectorySeparator() {
#if defined(PLATFORM_WINDOWS)
    return '\\';
#else
    return '/';
#endif
}

std::string FileSystem::normalizePath(const std::string& path) {
    std::string result = path;
    char sep = getDirectorySeparator();

    // Replace wrong separators
    char wrong_sep = (sep == '/') ? '\\' : '/';
    std::replace(result.begin(), result.end(), wrong_sep, sep);

    return result;
}

std::string FileSystem::getAbsolutePath(const std::string& path) {
    char buffer[4096];
#if defined(PLATFORM_WINDOWS)
    if (!GetFullPathNameA(path.c_str(), sizeof(buffer), buffer, nullptr)) {
        return path;
    }
#else
    if (!realpath(path.c_str(), buffer)) {
        return path;
    }
#endif
    return std::string(buffer);
}

std::vector<std::string> FileSystem::listFiles(const std::string& path, bool recursive) {
    std::vector<std::string> files;

#if defined(PLATFORM_WINDOWS)
    WIN32_FIND_DATAA findData;
    std::string searchPath = path + "\\*";

    HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return files;
    }

    do {
        std::string filename = findData.cFileName;
        if (filename != "." && filename != "..") {
            std::string fullPath = path + "\\" + filename;
            files.push_back(fullPath);

            if (recursive && (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                auto subFiles = listFiles(fullPath, true);
                files.insert(files.end(), subFiles.begin(), subFiles.end());
            }
        }
    } while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename != "." && filename != "..") {
            std::string fullPath = path + "/" + filename;
            files.push_back(fullPath);

            if (recursive) {
                struct stat statbuf;
                if (stat(fullPath.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                    auto subFiles = listFiles(fullPath, true);
                    files.insert(files.end(), subFiles.begin(), subFiles.end());
                }
            }
        }
    }

    closedir(dir);
#endif

    return files;
}

// ============================================================================
// DynamicLibrary Implementation
// ============================================================================

std::unique_ptr<DynamicLibrary> DynamicLibrary::load(const std::string& path) {
    void* handle = nullptr;

#if defined(PLATFORM_WINDOWS)
    handle = LoadLibraryA(path.c_str());
#else
    handle = dlopen(path.c_str(), RTLD_LAZY);
#endif

    if (!handle) {
        return nullptr;
    }

    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(handle, path));
}

DynamicLibrary::DynamicLibrary(void* handle, const std::string& path)
    : handle_(handle), path_(path) {}

DynamicLibrary::~DynamicLibrary() {
    if (handle_) {
#if defined(PLATFORM_WINDOWS)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
}

void* DynamicLibrary::getFunction(const char* functionName) {
    if (!handle_ || !functionName) {
        return nullptr;
    }

#if defined(PLATFORM_WINDOWS)
    return GetProcAddress(static_cast<HMODULE>(handle_), functionName);
#else
    return dlsym(handle_, functionName);
#endif
}

bool DynamicLibrary::isLoaded() const {
    return handle_ != nullptr;
}

const std::string& DynamicLibrary::getPath() const {
    return path_;
}

// ============================================================================
// SystemInfo Implementation
// ============================================================================

const char* SystemInfo::getPlatformName() {
    return PLATFORM_NAME;
}

uint64_t SystemInfo::getTotalMemoryBytes() {
#if defined(PLATFORM_WINDOWS)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPhys;
    }
    return 0;
#elif defined(PLATFORM_MACOS)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
        return memsize;
    }
    return 0;
#else // Linux
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    }
    return 0;
#endif
}

uint64_t SystemInfo::getAvailableMemoryBytes() {
#if defined(PLATFORM_WINDOWS)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullAvailPhys;
    }
    return 0;
#elif defined(PLATFORM_MACOS)
    vm_size_t page_size;
    mach_port_t mach_port = mach_host_self();
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);

    if (host_page_size(mach_port, &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_port, HOST_VM_INFO, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        return (vm_stats.free_count + vm_stats.inactive_count) * page_size;
    }
    return 0;
#else // Linux
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.freeram * info.mem_unit;
    }
    return 0;
#endif
}

size_t SystemInfo::getPageSize() {
#if defined(PLATFORM_WINDOWS)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwPageSize;
#else
    return static_cast<size_t>(getpagesize());
#endif
}

std::string SystemInfo::getCpuArchitecture() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_ARM) || defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
}

bool SystemInfo::isOnBattery() {
    // Simplified implementation - in a real engine, you'd check power status
    return false;
}

std::string SystemInfo::getSystemLanguage() {
#if defined(PLATFORM_WINDOWS)
    char language[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(language, LOCALE_NAME_MAX_LENGTH)) {
        // Extract language code (first 2 characters)
        return std::string(language, 2);
    }
    return "en";
#elif defined(PLATFORM_MACOS)
    CFLocaleRef locale = CFLocaleCopyCurrent();
    CFStringRef langCode = (CFStringRef)CFLocaleGetValue(locale, kCFLocaleLanguageCode);
    char buffer[32];
    if (CFStringGetCString(langCode, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        CFRelease(locale);
        return std::string(buffer);
    }
    CFRelease(locale);
    return "en";
#else // Linux
    const char* lang = getenv("LANG");
    if (lang) {
        std::string langStr = lang;
        size_t underscore = langStr.find('_');
        if (underscore != std::string::npos) {
            return langStr.substr(0, underscore);
        }
        size_t dot = langStr.find('.');
        if (dot != std::string::npos) {
            return langStr.substr(0, dot);
        }
        return langStr;
    }
    return "en";
#endif
}

// ============================================================================
// Window Implementation (Basic placeholder)
// ============================================================================

std::unique_ptr<Window> Window::create(const CreateParams& params) {
    // Placeholder implementation - would need platform-specific window creation
    std::cout << "Window creation requested: " << params.title
              << " (" << params.width << "x" << params.height << ")" << std::endl;
    return nullptr; // Not implemented yet
}

// ============================================================================
// Global Functions
// ============================================================================

bool initialize() {
    std::cout << "Platform abstraction layer initialized for " << PLATFORM_NAME << std::endl;
    return true;
}

void shutdown() {
    std::cout << "Platform abstraction layer shutdown" << std::endl;
}

} // namespace platform
} // namespace jupiter
