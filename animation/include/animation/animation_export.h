#pragma once

// Platform-specific DLL export/import macros
#ifdef _WIN32
    #ifdef ANIMATION_EXPORTS
        #define ANIMATION_API __declspec(dllexport)
    #elif defined(ANIMATION_IMPORTS)
        #define ANIMATION_API __declspec(dllimport)
    #else
        #define ANIMATION_API
    #endif
#else
    #define ANIMATION_API
#endif
