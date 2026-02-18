#include "profiling/profiler.h"
#include <thread>

int main() {
    // With JUPITER_ENABLE_TRACY forced to 0 for this target, all macros should
    // compile to no-ops and run without linking to the viewer.
    JUPITER_PROFILE_SCOPE("noop_scope");
    JUPITER_PROFILE_FRAME("noop_frame");
    JUPITER_PROFILE_MARK_FRAME();
    JUPITER_PROFILE_THREAD("noop_thread");
    JUPITER_PROFILE_PLOT("noop_plot", 0.0);

    jupiter::profiling::setThreadName("noop_thread_fn");
    jupiter::profiling::plotValue("noop_plot_fn", 1.0);
    jupiter::profiling::sendMessage("noop_message", 11);
    jupiter::profiling::markFrame("noop_mark_fn");

    std::thread worker([] {
        JUPITER_PROFILE_SCOPE("noop_worker");
        JUPITER_PROFILE_MARK_FRAME();
    });
    worker.join();

    return 0;
}







