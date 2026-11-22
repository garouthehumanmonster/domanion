#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define RON_EXPORT_GPU_PREFERENCE() \
        extern "C" { \
            __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001; \
            __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1; \
        }
#else
    #define RON_EXPORT_GPU_PREFERENCE()
#endif
