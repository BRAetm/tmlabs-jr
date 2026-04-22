#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #if defined(LABSCORE_EXPORTS)
        #define LABSCORE_API __declspec(dllexport)
    #else
        #define LABSCORE_API __declspec(dllimport)
    #endif
#else
    #define LABSCORE_API
#endif
