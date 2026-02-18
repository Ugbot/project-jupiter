#pragma once

#include <cstddef>

#if defined(JUPITER_ENABLE_TRACY) && JUPITER_ENABLE_TRACY
    #include <tracy/Tracy.hpp>
#endif

namespace jupiter::profiling {

#if defined(JUPITER_ENABLE_TRACY) && JUPITER_ENABLE_TRACY
    #define JUPITER_PROFILE_SCOPE(name_literal) ZoneScopedN(name_literal)
    #define JUPITER_PROFILE_FRAME(name_literal) FrameMarkNamed(name_literal)
    #define JUPITER_PROFILE_MARK_FRAME() FrameMark
    #define JUPITER_PROFILE_THREAD(name_literal) tracy::SetThreadName(name_literal)
    #define JUPITER_PROFILE_PLOT(name_literal, value) TracyPlot(name_literal, value)
    #define JUPITER_PROFILE_MESSAGE(text_ptr, text_len) TracyMessage(text_ptr, text_len)
#else
    #define JUPITER_PROFILE_SCOPE(name_literal) ((void)0)
    #define JUPITER_PROFILE_FRAME(name_literal) ((void)0)
    #define JUPITER_PROFILE_MARK_FRAME() ((void)0)
    #define JUPITER_PROFILE_THREAD(name_literal) ((void)0)
    #define JUPITER_PROFILE_PLOT(name_literal, value) ((void)0)
    #define JUPITER_PROFILE_MESSAGE(text_ptr, text_len) ((void)0)
#endif

inline void setThreadName(const char* name) {
#if defined(JUPITER_ENABLE_TRACY) && JUPITER_ENABLE_TRACY
    tracy::SetThreadName(name);
#else
    (void)name;
#endif
}

inline void markFrame(const char* name = nullptr) {
#if defined(JUPITER_ENABLE_TRACY) && JUPITER_ENABLE_TRACY
    if (name != nullptr) {
        FrameMarkNamed(name);
    } else {
        FrameMark;
    }
#else
    (void)name;
#endif
}

inline void plotValue(const char* name, double value) {
#if defined(JUPITER_ENABLE_TRACY) && JUPITER_ENABLE_TRACY
    TracyPlot(name, value);
#else
    (void)name;
    (void)value;
#endif
}

inline void sendMessage(const char* text, std::size_t length) {
#if defined(JUPITER_ENABLE_TRACY) && JUPITER_ENABLE_TRACY
    TracyMessage(text, length);
#else
    (void)text;
    (void)length;
#endif
}

} // namespace jupiter::profiling

