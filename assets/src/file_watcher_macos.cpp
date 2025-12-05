#include "assets/file_watcher.h"
#include "memory/queues.h"
#include "logging/logging.h"

#include <CoreServices/CoreServices.h>
#include <thread>

namespace jupiter {
namespace assets {

// Lock-free queue for file change events
using EventQueue = memory::MPMCQueue<FileChangeEvent, 256>;

struct FileWatcher::Impl {
    FSEventStreamRef stream = nullptr;
    CFRunLoopRef runLoop = nullptr;
    std::thread watchThread;
    EventQueue eventQueue;
    std::string watchPath;
};

// FSEvents callback
void fseventsCallback(
    ConstFSEventStreamRef streamRef,
    void* clientCallBackInfo,
    size_t numEvents,
    void* eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
    (void)streamRef;
    (void)eventIds;

    FileWatcher::Impl* impl = static_cast<FileWatcher::Impl*>(clientCallBackInfo);
    char** paths = static_cast<char**>(eventPaths);

    for (size_t i = 0; i < numEvents; i++) {
        FileChangeEvent event;
        event.path = paths[i];

        // Determine event type
        FSEventStreamEventFlags flags = eventFlags[i];

        if (flags & kFSEventStreamEventFlagItemCreated) {
            event.type = FileChangeType::CREATED;
        } else if (flags & kFSEventStreamEventFlagItemRemoved) {
            event.type = FileChangeType::DELETED;
        } else if (flags & kFSEventStreamEventFlagItemRenamed) {
            event.type = FileChangeType::RENAMED;
        } else if (flags & kFSEventStreamEventFlagItemModified) {
            event.type = FileChangeType::MODIFIED;
        } else {
            // Default to modified for other events
            event.type = FileChangeType::MODIFIED;
        }

        // Push to lock-free queue
        if (!impl->eventQueue.push(event)) {
            LOG_WARN("FileWatcher", "Event queue full, dropping event: %s", event.path.c_str());
        }
    }
}

FileWatcher::FileWatcher()
    : impl_(std::make_unique<Impl>()) {
}

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::watch(const std::string& path, bool recursive, FileChangeCallback callback) {
    if (watching_.load(std::memory_order_acquire)) {
        LOG_WARN("FileWatcher", "Already watching, stop first");
        return false;
    }

    callback_ = callback;
    impl_->watchPath = path;

    // Create FSEventStream
    CFStringRef pathToWatch = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch = CFArrayCreate(nullptr, (const void**)&pathToWatch, 1, nullptr);

    FSEventStreamContext context = {0, impl_.get(), nullptr, nullptr, nullptr};

    impl_->stream = FSEventStreamCreate(
        nullptr,
        &fseventsCallback,
        &context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow,
        0.5,  // Latency in seconds
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagUseCFTypes
    );

    CFRelease(pathsToWatch);
    CFRelease(pathToWatch);

    if (!impl_->stream) {
        LOG_ERROR("FileWatcher", "Failed to create FSEventStream");
        return false;
    }

    // Start watch thread
    watching_.store(true, std::memory_order_release);

    impl_->watchThread = std::thread([this]() {
        impl_->runLoop = CFRunLoopGetCurrent();

        FSEventStreamScheduleWithRunLoop(impl_->stream, impl_->runLoop, kCFRunLoopDefaultMode);
        FSEventStreamStart(impl_->stream);

        LOG_INFO("FileWatcher", "Started watching: %s", impl_->watchPath.c_str());

        while (watching_.load(std::memory_order_acquire)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        }

        FSEventStreamStop(impl_->stream);
        FSEventStreamInvalidate(impl_->stream);
        FSEventStreamRelease(impl_->stream);
        impl_->stream = nullptr;

        LOG_INFO("FileWatcher", "Stopped watching");
    });

    return true;
}

void FileWatcher::stop() {
    if (!watching_.load(std::memory_order_acquire)) {
        return;
    }

    watching_.store(false, std::memory_order_release);

    // Stop the run loop
    if (impl_->runLoop) {
        CFRunLoopStop(impl_->runLoop);
    }

    // Wait for thread to finish
    if (impl_->watchThread.joinable()) {
        impl_->watchThread.join();
    }
}

void FileWatcher::pollEvents() {
    if (!callback_) {
        return;
    }

    FileChangeEvent event;
    while (impl_->eventQueue.pop(event)) {
        callback_(event);
    }
}

} // namespace assets
} // namespace jupiter
