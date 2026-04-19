cmake_minimum_required(VERSION 3.20)
project(Helios VERSION 0.9.6.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt6 required
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Network OpenGL OpenGLWidgets)

# Optional: CUDA + TensorRT for InferenceCore
find_package(CUDAToolkit QUIET)
find_package(OpenCV QUIET)

# ── HeliosCore.dll ────────────────────────────────────────────────────────────
add_library(HeliosCore SHARED
    helios_core/LoggingService.cpp
    helios_core/SettingsManager.cpp
    helios_core/LicenseService.cpp
    helios_core/SharedMemory.cpp
    helios_core/PluginHost.cpp
    helios_core/UpdateManager.cpp
)
target_link_libraries(HeliosCore PUBLIC
    Qt6::Core Qt6::Network
    bcrypt
)
target_compile_definitions(HeliosCore PRIVATE HELIOSCORE_EXPORTS)

# ── Helios2.exe ───────────────────────────────────────────────────────────────
add_executable(Helios2 WIN32
    main.cpp
    HeliosMainWindow.cpp
    HeliosMainWindow.h
)
target_link_libraries(Helios2 PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Network Qt6::OpenGL Qt6::OpenGLWidgets
    HeliosCore
)
set_target_properties(Helios2 PROPERTIES
    OUTPUT_NAME "Helios2"
    WIN32_EXECUTABLE TRUE
)

# ── Plugin macro ──────────────────────────────────────────────────────────────
macro(add_helios_plugin name)
    add_library(${name} SHARED ${ARGN})
    target_link_libraries(${name} PRIVATE
        Qt6::Core Qt6::Widgets Qt6::Network
        HeliosCore
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endmacro()

# ── Plugins ───────────────────────────────────────────────────────────────────
add_helios_plugin(OutputPanel
    plugins/OutputPanel.cpp)

add_helios_plugin(FileExplorer
    plugins/FileExplorer.cpp)

add_helios_plugin(OnlineResources
    plugins/OnlineResources.cpp)
target_link_libraries(OnlineResources PRIVATE Qt6::Network)

add_helios_plugin(DeviceMonitor
    plugins/DeviceMonitor.cpp)

add_helios_plugin(DS5Input
    plugins/DS5Input.cpp)
target_link_libraries(DS5Input PRIVATE hid setupapi)

add_helios_plugin(DS4Input
    plugins/DS4Input.cpp)
target_link_libraries(DS4Input PRIVATE hid setupapi)

add_helios_plugin(XInputInput
    plugins/XInputInput.cpp)
target_link_libraries(XInputInput PRIVATE xinput)

add_helios_plugin(ViGEmOutput
    plugins/ViGEmOutput.cpp)

add_helios_plugin(TitanBridge
    plugins/TitanBridge.cpp)
target_link_libraries(TitanBridge PRIVATE hid setupapi)

add_helios_plugin(GPC3
    plugins/GPC3.cpp)

add_helios_plugin(DSCapture
    plugins/DSCapture.cpp)
target_link_libraries(DSCapture PRIVATE strmiids quartz)

add_helios_plugin(MFCapture
    plugins/MFCapture.cpp)
target_link_libraries(MFCapture PRIVATE mfplat mf mfreadwrite mfuuid)

if(OpenCV_FOUND)
    add_helios_plugin(OpencvCapture
        plugins/OpencvCapture.cpp)
    target_link_libraries(OpencvCapture PRIVATE ${OpenCV_LIBS})

    add_helios_plugin(OBSCapture
        plugins/OBSCapture.cpp)
    target_link_libraries(OBSCapture PRIVATE ${OpenCV_LIBS})
endif()

add_helios_plugin(MagewellCapture
    plugins/MagewellCapture.cpp)
# target_link_libraries(MagewellCapture PRIVATE LibMWCapture)  # Magewell SDK

add_helios_plugin(OpenGLDisplay
    plugins/OpenGLDisplay.cpp)
target_link_libraries(OpenGLDisplay PRIVATE Qt6::OpenGL Qt6::OpenGLWidgets)

add_helios_plugin(PSRemotePlay
    plugins/PSRemotePlay.cpp)
# target_link_libraries(PSRemotePlay PRIVATE chiaki)  # Chiaki-ng

add_helios_plugin(XboxRemotePlay
    plugins/XboxRemotePlay.cpp)

add_helios_plugin(CvPython
    plugins/CvPython.cpp)

add_helios_plugin(CvCpp
    plugins/CvCpp.cpp)

# ── InferenceCore.dll (optional — requires CUDA) ──────────────────────────────
if(CUDAToolkit_FOUND)
    add_library(InferenceCore SHARED
        inference_core/InferenceCore.cpp
        inference_core/ONNXEngine.cpp
        inference_core/TensorRTEngine.cpp
        inference_core/ModelCrypto.cpp
    )
    target_link_libraries(InferenceCore PRIVATE
        CUDA::cudart
        # nvinfer nvinfer_plugin nvonnxparser  # TensorRT
        Qt6::Core
        bcrypt
    )
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
install(TARGETS Helios2 HeliosCore
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
install(DIRECTORY "${CMAKE_BINARY_DIR}/plugins/"
    DESTINATION bin/plugins
    FILES_MATCHING PATTERN "*.dll"
)
