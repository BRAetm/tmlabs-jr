cmake_minimum_required(VERSION 3.15)

project(labs C CXX)

# ---------------------------------------------------------------------------
# PS Remote Play feature — lean Windows-only build of the labs engine.
# Produces a single shared library (labs.dll) consumed from C# via P/Invoke.
# All GUI, CLI, tests, and non-Windows platform code has been removed.
# ---------------------------------------------------------------------------

# Like option(), but the value can also be AUTO
macro(tri_option name desc default)
    set("${name}" "${default}" CACHE STRING "${desc}")
    set_property(CACHE "${name}" PROPERTY STRINGS AUTO ON OFF)
endmacro()

# Hard-disabled: we only build the library.
set(LABS_ENABLE_TESTS            OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_CLI              OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_GUI              OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_ANDROID          OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_BOREALIS         OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_SETSU            OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_STEAMDECK_NATIVE OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_SPEEX            OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_PI_DECODER       OFF CACHE BOOL "" FORCE)
set(LABS_ENABLE_STEAM_SHORTCUT   OFF CACHE BOOL "" FORCE)
set(LABS_LIB_ENABLE_MBEDTLS      OFF CACHE BOOL "" FORCE)

# Enabled features for the engine.
option(LABS_LIB_ENABLE_OPUS "Use Opus as part of Labs Lib" ON)
tri_option(LABS_ENABLE_RUDP "Enable Remote Play over Internet" AUTO)
set(LABS_ENABLE_FFMPEG_DECODER OFF CACHE BOOL "" FORCE)  # decode in C# via FFmpeg.AutoGen

# Third-party source layout toggles.
tri_option(LABS_USE_SYSTEM_JERASURE "Use system-provided jerasure instead of submodule" AUTO)
tri_option(LABS_USE_SYSTEM_NANOPB "Use system-provided nanopb instead of submodule" AUTO)
tri_option(LABS_USE_SYSTEM_CURL "Use system-provided curl (must have WebSocket support)" ON)
option(LABS_LIB_JSONC_EXTERNAL_PROJECT "Build json-c from source via FetchContent" OFF)
option(LABS_LIB_MINIUPNPC_EXTERNAL_PROJECT "Build miniupnpc from source via FetchContent" OFF)

set(LABS_VERSION_MAJOR 1)
set(LABS_VERSION_MINOR 10)
set(LABS_VERSION_PATCH 0)
set(LABS_VERSION ${LABS_VERSION_MAJOR}.${LABS_VERSION_MINOR}.${LABS_VERSION_PATCH})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_CXX_STANDARD 11)

# Produce a DLL on Windows so C# can P/Invoke it.
set(BUILD_SHARED_LIBS ON)

if(LABS_USE_SYSTEM_JERASURE)
    if(LABS_USE_SYSTEM_JERASURE STREQUAL AUTO)
        find_package(Jerasure QUIET)
        set(LABS_USE_SYSTEM_JERASURE ${Jerasure_FOUND})
    else()
        find_package(Jerasure REQUIRED)
        set(LABS_USE_SYSTEM_JERASURE ON)
    endif()
endif()

if(LABS_USE_SYSTEM_CURL)
    if(LABS_USE_SYSTEM_CURL STREQUAL AUTO)
        find_package(CURL COMPONENTS HTTP HTTPS WS WSS QUIET)
        set(LABS_USE_SYSTEM_CURL ${CURL_FOUND})
    else()
        find_package(CURL REQUIRED COMPONENTS HTTP HTTPS WS WSS)
        set(LABS_USE_SYSTEM_CURL ON)
    endif()
endif()

find_package(PythonInterp 3 REQUIRED) # nanopb requires Python 3

if(LABS_USE_SYSTEM_NANOPB)
    if(LABS_USE_SYSTEM_NANOPB STREQUAL AUTO)
        find_package(Nanopb QUIET)
        set(LABS_USE_SYSTEM_NANOPB ${Nanopb_FOUND})
    else()
        find_package(Nanopb REQUIRED)
        set(LABS_USE_SYSTEM_NANOPB ON)
    endif()
endif()

add_subdirectory(third-party)

add_definitions(
    -DLABS_VERSION_MAJOR=${LABS_VERSION_MAJOR}
    -DLABS_VERSION_MINOR=${LABS_VERSION_MINOR}
    -DLABS_VERSION_PATCH=${LABS_VERSION_PATCH}
    -DLABS_VERSION="${LABS_VERSION}")

if(LABS_ENABLE_FFMPEG_DECODER)
    find_package(FFMPEG COMPONENTS avcodec avutil avformat)
    if(FFMPEG_FOUND)
        set(LABS_ENABLE_FFMPEG_DECODER ON)
    else()
        if(NOT LABS_ENABLE_FFMPEG_DECODER STREQUAL AUTO)
            message(FATAL_ERROR "LABS_ENABLE_FFMPEG_DECODER is ON but ffmpeg not found.")
        endif()
        set(LABS_ENABLE_FFMPEG_DECODER OFF)
    endif()
endif()

if(LABS_LIB_JSONC_EXTERNAL_PROJECT)
    include(JsoncExternalProject)
endif()

if(LABS_LIB_MINIUPNPC_EXTERNAL_PROJECT)
    include(MiniupnpcExternalProject)
endif()

add_subdirectory(lib)
