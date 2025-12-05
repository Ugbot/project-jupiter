#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

// Forward declarations for platform-specific types
#ifdef __APPLE__
typedef const struct __FSEventStream* ConstFSEventStreamRef;
typedef unsigned int FSEventStreamEventFlags;
typedef unsigned long long FSEventStreamEventId;
#endif

/**
 * @file file_watcher.h
 * @brief Platform-agnostic file watching for hot-reload
 *
 * Monitors directories for file changes and triggers callbacks.
 * Uses platform-specific implementations:
 * - macOS: FSEvents
 * - Linux: inotify
 * - Windows: ReadDirectoryChangesW
 */

namespace jupiter {
namespace assets {

/**
 * @brief File change event type
 */
enum class FileChangeType {
    CREATED,
    MODIFIED,
    DELETED,
    RENAMED
};

/**
 * @brief File change event
 */
struct FileChangeEvent {
    std::string path;
    FileChangeType type;
};

/**
 * @brief Callback for file change events
 */
using FileChangeCallback = std::function<void(const FileChangeEvent&)>;

/**
 * @brief Cross-platform file watcher
 *
 * Monitors directories for file changes and invokes callbacks.
 * Lock-free event delivery via queue.
 */
class FileWatcher {
    // Friend declaration for platform-specific callbacks
    friend void fseventsCallback(ConstFSEventStreamRef, void*, size_t, void*, const FSEventStreamEventFlags[], const FSEventStreamEventId[]);

public:
    FileWatcher();
    ~FileWatcher();

    // No copy or move
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&&) = delete;
    FileWatcher& operator=(FileWatcher&&) = delete;

    /**
     * @brief Start watching a directory
     * @param path Directory to watch
     * @param recursive Watch subdirectories recursively
     * @param callback Callback to invoke on file changes
     * @return true if successful
     */
    bool watch(const std::string& path, bool recursive, FileChangeCallback callback);

    /**
     * @brief Stop watching
     */
    void stop();

    /**
     * @brief Check if currently watching
     */
    bool isWatching() const { return watching_.load(std::memory_order_acquire); }

    /**
     * @brief Poll for events (call from main thread)
     * Processes queued events and invokes callbacks
     */
    void pollEvents();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> watching_{false};
    FileChangeCallback callback_;
};

} // namespace assets
} // namespace jupiter
