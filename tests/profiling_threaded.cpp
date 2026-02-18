#include "core/core.h"
#include "profiling/profiler.h"
#include <atomic>
#include <thread>
#include <vector>

int main() {
    JUPITER_PROFILE_THREAD("threaded_main");
    JUPITER_PROFILE_SCOPE("threaded_main_scope");

    std::atomic<int> frame_count{0};

    constexpr int kThreadCount = 4;
    constexpr int kIterations = 4;
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([t, &frame_count]() {
            JUPITER_PROFILE_THREAD("worker");
            JUPITER_PROFILE_SCOPE("worker_scope");
            for (int i = 0; i < kIterations; ++i) {
                JUPITER_PROFILE_SCOPE("worker_iter");
                jupiter::profiling::plotValue("worker_iter", static_cast<double>(i));
                jupiter::profiling::sendMessage("threaded_message", 16);
                jupiter::profiling::markFrame("worker_frame");
                frame_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    {
        JUPITER_PROFILE_SCOPE("core_lifecycle");
        jupiter::core::initialize();
        jupiter::profiling::markFrame("post_init");
        jupiter::core::shutdown();
    }

    // Ensure all frames were executed (sanity check).
    if (frame_count.load(std::memory_order_relaxed) != kThreadCount * kIterations) {
        return 1;
    }

    return 0;
}

