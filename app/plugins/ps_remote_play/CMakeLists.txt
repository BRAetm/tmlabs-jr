set(CHIAKI_DIR "${CMAKE_SOURCE_DIR}/../labs-engine/chiaki-ng" CACHE PATH "chiaki-ng root")
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/third-party/ffmpeg" CACHE PATH "FFmpeg prebuilt root")

add_labs_plugin(PSRemotePlayPlugin
    PSRemotePlayPlugin.h
    PSRemotePlayPlugin.cpp
    PairPSDialog.h
    PairPSDialog.cpp
    PsnLoginDialog.h
    PsnLoginDialog.cpp
)

target_include_directories(PSRemotePlayPlugin PRIVATE
    "${CHIAKI_DIR}/lib/include"
    "${FFMPEG_DIR}/include"
)

target_link_directories(PSRemotePlayPlugin PRIVATE
    "${CHIAKI_DIR}/build/lib"
    "${FFMPEG_DIR}/lib"
)

target_link_libraries(PSRemotePlayPlugin PRIVATE
    Qt6::Network
    labs
    avcodec
    avutil
    swscale
)

# Copy all required runtime DLLs (chiaki + its deps + FFmpeg) next to LabsEngine.exe.
file(GLOB CHIAKI_DLLS "${CHIAKI_DIR}/build/bin/*.dll")
file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")

add_custom_command(TARGET PSRemotePlayPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CHIAKI_DLLS} ${FFMPEG_DLLS}
        "$<TARGET_FILE_DIR:LabsEngine>"
    COMMAND_EXPAND_LISTS
)
