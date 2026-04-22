add_labs_plugin(WgcCapturePlugin
    WgcSource.h
    WgcSource.cpp
    WgcCapturePlugin.h
    WgcCapturePlugin.cpp
)

target_link_libraries(WgcCapturePlugin PRIVATE
    d3d11
    dxgi
    windowsapp
)

target_compile_definitions(WgcCapturePlugin PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
