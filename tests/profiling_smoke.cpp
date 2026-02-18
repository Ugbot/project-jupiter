#include "core/core.h"
#include "profiling/profiler.h"

int main() {
    JUPITER_PROFILE_THREAD("profiling_smoke_thread");
    JUPITER_PROFILE_SCOPE("profiling_smoke_main");

    jupiter::profiling::setThreadName("profiling_smoke_thread");
    jupiter::profiling::plotValue("profiling_smoke_value", 1.0);

    constexpr const char message[] = "profiling smoke message";
    jupiter::profiling::sendMessage(message, sizeof(message) - 1);
    jupiter::profiling::markFrame("profiling_smoke_frame");

    if (!jupiter::core::initialize()) {
        return 1;
    }

    jupiter::profiling::markFrame("profiling_smoke_post_init");
    jupiter::core::shutdown();
    return 0;
}







